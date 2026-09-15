#!/usr/bin/env python3
"""
reference_server.py
-------------------
PROTOCOL.md (pico-os ドキュメントサーバ プロトコル v1) の参照実装。

「文章だけの仕様は必ず解釈が割れる」ので、動く実装を仕様と一緒に置いておくためのもの。
pico-os 側の開発相手(PCビルドから叩く先)も兼ねる。

標準ライブラリのみで動く。外部依存なし。

    python3 script/reference_server.py                      # examples/ を配る
    python3 script/reference_server.py --root ~/docs --port 8080
    python3 script/reference_server.py --check              # 文書がクライアントの上限に
                                                            # 収まっているか検査して終了
    python3 script/reference_server.py --no-search          # 検索非対応サーバの再現
    python3 script/reference_server.py --no-discovery       # 素の静的サーバの再現

実装しているエンドポイント:
    GET /.well-known/pico-os   サーバ情報(TSV)
    GET /<path>                文書・画像などの配信(条件付きGET対応)
    GET /v1/search?q=...       全文検索(TSV)
    GET /v1/manifest           全文書の検証子一覧(TSV, パス昇順)

プロトコル上の約束のうち、実装で落としやすいもの:
    - 応答本文は必ず Content-Length を付けて送る(chunked は禁止)
    - 圧縮しない(クライアントは Accept-Encoding を送らない)
    - TSVの各フィールドからタブ・CR・LFを除去する(エスケープの仕組みが無いため)
    - manifest は path の昇順(UTF-8バイト列の辞書順)で返す
    - 検索結果の version 列は ETag と同じ値にする
"""

import argparse
import hashlib
import http.server
import mimetypes
import os
import posixpath
import re
import socketserver
import sys
import unicodedata
import urllib.parse
from email.utils import formatdate, parsedate_to_datetime

PROTOCOL_VERSION = 1

# PROTOCOL.md 「制限値の一覧」より。クライアント側の実装上の上限。
LIMIT_DOC_BYTES = 8192
LIMIT_BLOCK_BYTES = 512
LIMIT_BLOCKS = 128
LIMIT_TABLE_COLS = 4
LIMIT_PATH_BYTES = 255
LIMIT_SEARCH_RESULTS = 50  # サーバ側の上限。クライアントは20以下しか要求しない

DOC_SUFFIXES = (".md", ".markdown")


# ---------------------------------------------------------------- ユーティリティ

def tsv_field(value):
    """TSVの1フィールドとして安全な文字列にする。

    プロトコルはエスケープの仕組みを持たない(クライアントのパーサを単純に保つため)ので、
    区切りに使う文字はサーバ側で潰しておく必要がある。
    """
    if value is None:
        return ""
    return re.sub(r"[\t\r\n]+", " ", str(value)).strip()


def tsv_line(*fields):
    return "\t".join(tsv_field(f) for f in fields) + "\n"


def compute_etag(data: bytes) -> str:
    """内容が同じなら同じ値、変われば別の値になりさえすればよい(PROTOCOL.md)。"""
    return hashlib.sha1(data).hexdigest()[:16]


def strip_front_matter(text: str):
    """先頭のYAMLフロントマターを取り除き、(本文, メタ辞書) を返す。

    クライアントは skipFrontMatter() で読み飛ばすので、サーバ側でも
    タイトル抽出や検索の対象から外して挙動を揃える。
    """
    lines = text.split("\n")
    if not lines or lines[0].strip() != "---":
        return text, {}

    meta = {}
    for i in range(1, len(lines)):
        stripped = lines[i].strip()
        if stripped in ("---", "..."):
            for raw in lines[1:i]:
                if ":" not in raw:
                    continue
                key, _, value = raw.partition(":")
                meta[key.strip().lower()] = value.strip().strip("\"'")
            return "\n".join(lines[i + 1:]), meta

    # 終端フェンスが無い場合は何も取り除かない(クライアントも同じ安全側の判断をする)
    return text, {}


def extract_title(text: str, fallback: str) -> str:
    """フロントマターの title、無ければ最初の見出し、それも無ければファイル名。"""
    body, meta = strip_front_matter(text)
    if meta.get("title"):
        return meta["title"]
    for line in body.split("\n"):
        stripped = line.strip()
        if stripped.startswith("#"):
            title = stripped.lstrip("#").strip()
            if title:
                return title
    return fallback


def fold(text: str) -> str:
    """検索用の正規化。全角/半角と大文字/小文字の違いを吸収する。

    日本語は単語分割せず部分一致で引く(形態素解析を入れると標準ライブラリで
    収まらなくなるうえ、クライアントが求めているのは素朴な一致で足りる)。
    """
    return unicodedata.normalize("NFKC", text).casefold()


def make_snippet(body: str, needle_folded: str, width: int = 80) -> str:
    """一致箇所の周辺を切り出す。列4(任意)用。"""
    folded = fold(body)
    pos = folded.find(needle_folded)
    if pos < 0:
        return tsv_field(body[:width])

    start = max(0, pos - width // 3)
    end = min(len(body), pos + width)
    snippet = body[start:end]
    snippet = re.sub(r"\s+", " ", snippet).strip()
    if start > 0:
        snippet = "…" + snippet
    if end < len(body):
        snippet = snippet + "…"
    return snippet


# ---------------------------------------------------------------- 文書の索引

class Document:
    __slots__ = ("url_path", "fs_path", "mtime", "size", "etag", "title", "body", "raw")

    def __init__(self, url_path, fs_path, mtime, size, etag, title, body, raw):
        self.url_path = url_path   # 必ず "/" 始まりのサーバ絶対パス
        self.fs_path = fs_path
        self.mtime = mtime
        self.size = size
        self.etag = etag
        self.title = title
        self.body = body           # フロントマターを除いた本文(検索対象)
        self.raw = raw             # 配信する生バイト列


class DocIndex:
    """doc root 以下の Markdown を索引する。

    ファイルの (mtime, size) が変わったものだけ読み直す。参照実装なので
    inotify のような凝ったことはせず、リクエストのたびにディレクトリを歩く。
    """

    def __init__(self, root):
        self.root = os.path.abspath(root)
        self._docs = {}   # url_path -> Document
        self._stamp = {}  # url_path -> (mtime, size)

    def refresh(self):
        seen = set()
        for dirpath, dirnames, filenames in os.walk(self.root):
            dirnames[:] = [d for d in dirnames if not d.startswith(".")]
            for name in sorted(filenames):
                if not name.lower().endswith(DOC_SUFFIXES):
                    continue
                fs_path = os.path.join(dirpath, name)
                rel = os.path.relpath(fs_path, self.root).replace(os.sep, "/")
                url_path = "/" + rel
                seen.add(url_path)

                try:
                    st = os.stat(fs_path)
                except OSError:
                    continue
                stamp = (st.st_mtime, st.st_size)
                if self._stamp.get(url_path) == stamp:
                    continue  # 変わっていないので読み直さない

                try:
                    with open(fs_path, "rb") as f:
                        raw = f.read()
                except OSError:
                    continue

                text = raw.decode("utf-8", errors="replace")
                body, _ = strip_front_matter(text)
                self._docs[url_path] = Document(
                    url_path=url_path,
                    fs_path=fs_path,
                    mtime=st.st_mtime,
                    size=st.st_size,
                    etag=compute_etag(raw),
                    title=extract_title(text, os.path.splitext(name)[0]),
                    body=body,
                    raw=raw,
                )
                self._stamp[url_path] = stamp

        for gone in set(self._docs) - seen:
            del self._docs[gone]
            self._stamp.pop(gone, None)

        return self._docs

    def all(self):
        return self.refresh()

    def search(self, query, limit, offset):
        docs = self.refresh()
        needle = fold(query)
        if not needle:
            return []

        hits = []
        for doc in docs.values():
            title_hit = needle in fold(doc.title)
            body_count = fold(doc.body).count(needle)
            if not title_hit and body_count == 0:
                continue
            # タイトル一致を強く、本文は出現回数で。素朴だが順位付けはサーバの責務
            score = (100 if title_hit else 0) + body_count
            hits.append((score, doc))

        # 同点はパス順にして、同じ問い合わせが常に同じ順序になるようにする
        hits.sort(key=lambda pair: (-pair[0], pair[1].url_path.encode("utf-8")))
        return [doc for _, doc in hits[offset:offset + limit]]


# ---------------------------------------------------------------- 文書の検査

def check_documents(index, out=sys.stdout):
    """文書がクライアントの上限に収まっているかを調べる。--check 用。

    上限を超えても配信はできる(クライアントが切り捨てる)が、
    書いた人が気付けないと「後半が消えた」という形で表面化するため。
    """
    docs = index.all()
    if not docs:
        print("文書が1件も見つかりません: %s" % index.root, file=out)
        return 1

    problems = 0
    for url_path in sorted(docs, key=lambda p: p.encode("utf-8")):
        doc = docs[url_path]
        notes = []

        if len(doc.raw) > LIMIT_DOC_BYTES:
            notes.append("サイズ %dB > %dB (後半が切り捨てられます)"
                         % (len(doc.raw), LIMIT_DOC_BYTES))

        if len(url_path.encode("utf-8")) > LIMIT_PATH_BYTES:
            notes.append("パス長 %dB > %dB (クライアントが取得しません)"
                         % (len(url_path.encode("utf-8")), LIMIT_PATH_BYTES))

        # ブロック数と1ブロックの長さ。空行区切りでの概算(クライアントの
        # parseBlocks() とは厳密には一致しないが、上限に近いことは分かる)
        chunks = [c for c in re.split(r"\n\s*\n", doc.body) if c.strip()]
        if len(chunks) > LIMIT_BLOCKS:
            notes.append("ブロック数 約%d > %d (以降が表示されません)"
                         % (len(chunks), LIMIT_BLOCKS))
        for chunk in chunks:
            n = len(chunk.encode("utf-8"))
            if n > LIMIT_BLOCK_BYTES:
                notes.append("%dB の段落があります > %dB (途中で切れます)"
                             % (n, LIMIT_BLOCK_BYTES))
                break

        for line in doc.body.split("\n"):
            if line.strip().startswith("|"):
                cols = len([c for c in line.strip().strip("|").split("|")])
                if cols > LIMIT_TABLE_COLS:
                    notes.append("%d列のテーブルがあります > %d列 (右が切り捨てられます)"
                                 % (cols, LIMIT_TABLE_COLS))
                    break

        for match in re.finditer(r"!\[[^\]]*\]\(([^)]+)\)", doc.body):
            ref = match.group(1)
            if not ref.lower().endswith(".pimg"):
                notes.append("画像 %s は .pimg ではありません (表示されません)" % ref)
                break

        if notes:
            problems += 1
            print("[NG] %s" % url_path, file=out)
            for note in notes:
                print("       - %s" % note, file=out)
        else:
            print("[OK] %s  (%dB, %s)" % (url_path, len(doc.raw), doc.title), file=out)

    print("", file=out)
    print("%d件中 %d件に指摘があります。" % (len(docs), problems), file=out)
    return 1 if problems else 0


# ---------------------------------------------------------------- HTTPハンドラ

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "picoos-reference/1"

    # --- 応答の送出 ---------------------------------------------------

    def _send(self, status, body=b"", content_type="text/plain; charset=utf-8",
              extra_headers=None):
        """本文を必ず Content-Length 付きで送る。

        chunked も圧縮も使わない(プロトコルで禁止している)。
        """
        if isinstance(body, str):
            body = body.encode("utf-8")

        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for key, value in (extra_headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD" and body:
            self.wfile.write(body)
        self.close_connection = True

    def _send_tsv(self, status, text, extra_headers=None):
        self._send(status, text, "text/plain; charset=utf-8", extra_headers)

    def _send_error_text(self, status, message):
        self._send(status, message + "\n")

    # --- 条件付きGET ---------------------------------------------------

    def _not_modified(self, etag, mtime):
        """If-None-Match / If-Modified-Since を見て 304 にできるか判定する。"""
        inm = self.headers.get("If-None-Match")
        if inm:
            for candidate in inm.split(","):
                candidate = candidate.strip()
                if candidate.startswith("W/"):
                    candidate = candidate[2:]
                candidate = candidate.strip('"')
                if candidate == etag or candidate == "*":
                    return True
            # ETagが提示されたなら日付は見ない(ETagのほうが強い)
            return False

        ims = self.headers.get("If-Modified-Since")
        if ims:
            try:
                since = parsedate_to_datetime(ims).timestamp()
            except (TypeError, ValueError):
                return False
            # HTTP-dateは秒精度なので切り捨てて比較する
            return int(mtime) <= int(since)

        return False

    # --- ルーティング ---------------------------------------------------

    def do_HEAD(self):
        self.do_GET()

    def _parse_request_target(self):
        """リクエストラインをバイト列に戻してから解釈する。

        http.server は受信したバイト列を latin-1 で文字列化するため、
        パーセントエンコードされていない生のUTF-8(日本語のパスや検索語)が
        そのままでは文字化けする。仕様上クライアントはURLエンコードして送るが、
        参照実装としては素の状態でも取り違えないようにしておく。
        """
        # 分解は文字列のまま行う(urlsplitはバイト列を渡すと内部でASCIIデコードし、
        # 非ASCIIバイトで例外になる)。各成分をlatin-1でバイト列へ戻してから
        # パーセントデコードし、UTF-8として解釈する。
        parts = urllib.parse.urlsplit(self.path)

        raw_path = parts.path.encode("latin-1", "replace")
        path = urllib.parse.unquote_to_bytes(raw_path).decode("utf-8", "replace")

        query = {}
        raw_query = parts.query.encode("latin-1", "replace")
        for pair in raw_query.split(b"&"):
            if not pair:
                continue
            raw_key, _, raw_value = pair.partition(b"=")
            key = self._decode_component(raw_key)
            value = self._decode_component(raw_value)
            query.setdefault(key, []).append(value)

        return path, query

    @staticmethod
    def _decode_component(raw):
        return urllib.parse.unquote_to_bytes(raw.replace(b"+", b" ")).decode("utf-8", "replace")

    def do_GET(self):
        try:
            self._route()
        except Exception as exc:  # 壊れた要求でサーバごと落とさない
            self.log_message("error: %s", exc)
            try:
                self._send_error_text(500, "internal server error")
            except Exception:
                pass

    def _route(self):
        path, query = self._parse_request_target()

        if path == "/.well-known/pico-os":
            return self.handle_discovery()
        if self.server.opts.search_path and path == self.server.opts.search_path:
            return self.handle_search(query)
        if self.server.opts.manifest_path and path == self.server.opts.manifest_path:
            return self.handle_manifest()
        if path == "/":
            return self.handle_root()
        return self.handle_file(path)

    def do_POST(self):
        self._send_error_text(405, "GET のみ対応しています")

    # --- 各エンドポイント -------------------------------------------------

    def handle_discovery(self):
        opts = self.server.opts
        if not opts.discovery:
            # 素の静的ファイルサーバの再現。クライアントは検索を無効化して続行する
            return self._send_error_text(404, "not found")

        lines = [tsv_line("version", PROTOCOL_VERSION)]
        if opts.name:
            lines.append(tsv_line("name", opts.name))
        if opts.home:
            lines.append(tsv_line("home", opts.home))
        if opts.search_path:
            lines.append(tsv_line("search", opts.search_path))
        if opts.manifest_path:
            lines.append(tsv_line("manifest", opts.manifest_path))

        body = "".join(lines).encode("utf-8")
        etag = compute_etag(body)
        if self._not_modified(etag, 0):
            return self._send(304, b"", extra_headers={"ETag": '"%s"' % etag})
        self._send_tsv(200, body, {"ETag": '"%s"' % etag})

    def handle_root(self):
        home = self.server.opts.home
        if home:
            # リダイレクトの追従もここで試せるようにしておく
            return self._send(302, b"", extra_headers={"Location": home})
        self._send_error_text(404, "home が設定されていません")

    def handle_search(self, query):
        values = query.get("q") or []
        if not values:
            return self._send_error_text(400, "q が指定されていません")

        q = values[0]
        limit = self._int_param(query, "limit", 10, 1, LIMIT_SEARCH_RESULTS)
        offset = self._int_param(query, "offset", 0, 0, 100000)

        hits = self.server.index.search(q, limit, offset)
        # 一致0件は404ではなく200+空本文(プロトコルで規定)
        body = "".join(
            tsv_line(doc.url_path, doc.title, doc.etag,
                     make_snippet(doc.body, fold(q)))
            for doc in hits
        )
        self._send_tsv(200, body)

    def handle_manifest(self):
        docs = self.server.index.all()
        # パスの昇順(UTF-8バイト列の辞書順)。クライアントは自分の目録と
        # 先頭から突き合わせるため、順序が崩れると比較できない
        ordered = sorted(docs.values(), key=lambda d: d.url_path.encode("utf-8"))
        body = "".join(tsv_line(doc.url_path, doc.etag) for doc in ordered).encode("utf-8")

        etag = compute_etag(body)
        if self._not_modified(etag, 0):
            return self._send(304, b"", extra_headers={"ETag": '"%s"' % etag})
        self._send_tsv(200, body, {"ETag": '"%s"' % etag})

    def handle_file(self, path):
        fs_path = self._resolve(path)
        if fs_path is None:
            return self._send_error_text(404, "not found")
        if not os.path.isfile(fs_path):
            return self._send_error_text(404, "not found")

        try:
            st = os.stat(fs_path)
            with open(fs_path, "rb") as f:
                data = f.read()
        except OSError:
            return self._send_error_text(404, "not found")

        etag = compute_etag(data)
        headers = {
            "ETag": '"%s"' % etag,
            "Last-Modified": formatdate(st.st_mtime, usegmt=True),
        }
        if self._not_modified(etag, st.st_mtime):
            return self._send(304, b"", extra_headers=headers)

        lower = fs_path.lower()
        if lower.endswith(DOC_SUFFIXES):
            content_type = "text/markdown; charset=utf-8"
            if len(data) > LIMIT_DOC_BYTES:
                self.log_message("warning: %s は %dB で上限(%dB)を超えています",
                                 path, len(data), LIMIT_DOC_BYTES)
        elif lower.endswith(".pimg"):
            content_type = "application/octet-stream"
        else:
            content_type = mimetypes.guess_type(fs_path)[0] or "application/octet-stream"

        self._send(200, data, content_type, headers)

    # --- 補助 -----------------------------------------------------------

    def _int_param(self, query, key, default, lo, hi):
        values = query.get(key) or []
        if not values:
            return default
        try:
            value = int(values[0])
        except ValueError:
            return default
        return max(lo, min(hi, value))

    def _resolve(self, path):
        """URLパスをdoc root配下の実パスへ。root外へ出る指定はNoneを返す。"""
        normalized = posixpath.normpath(path)
        if not normalized.startswith("/"):
            return None
        parts = [p for p in normalized.split("/") if p and p != "."]
        if any(p == ".." for p in parts):
            return None

        root = self.server.index.root
        fs_path = os.path.join(root, *parts) if parts else root
        real = os.path.realpath(fs_path)
        if real != os.path.realpath(root) and not real.startswith(os.path.realpath(root) + os.sep):
            return None
        return real

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, addr, handler, index, opts):
        super().__init__(addr, handler)
        self.index = index
        self.opts = opts


# ---------------------------------------------------------------- エントリポイント

def main(argv=None):
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    parser = argparse.ArgumentParser(
        description="pico-os ドキュメントサーバ プロトコル v1 の参照実装 (PROTOCOL.md)")
    parser.add_argument("--root", default=os.path.join(repo_root, "examples"),
                        help="配る文書のディレクトリ (既定: リポジトリの examples/)")
    parser.add_argument("--host", default="0.0.0.0", help="待ち受けアドレス")
    parser.add_argument("--port", type=int, default=8080, help="待ち受けポート")
    parser.add_argument("--name", default="pico-os reference server",
                        help="discoveryで返すサーバ名")
    parser.add_argument("--home", default="", help="discoveryで返すホーム文書のパス")
    parser.add_argument("--no-discovery", action="store_true",
                        help="/.well-known/pico-os を404にする(素の静的サーバの再現)")
    parser.add_argument("--no-search", action="store_true",
                        help="検索を提供しない(検索非対応サーバの再現)")
    parser.add_argument("--no-manifest", action="store_true",
                        help="マニフェストを提供しない")
    parser.add_argument("--check", action="store_true",
                        help="文書がクライアントの上限に収まっているか検査して終了する")
    opts = parser.parse_args(argv)

    if not os.path.isdir(opts.root):
        print("ディレクトリがありません: %s" % opts.root, file=sys.stderr)
        return 2

    index = DocIndex(opts.root)

    if opts.check:
        return check_documents(index)

    opts.discovery = not opts.no_discovery
    opts.search_path = None if opts.no_search else "/v1/search"
    opts.manifest_path = None if opts.no_manifest else "/v1/manifest"

    # 起動時に一度索引を作る(件数と上限超過をその場で知らせる)
    docs = index.all()
    oversized = [p for p, d in docs.items() if len(d.raw) > LIMIT_DOC_BYTES]

    print("pico-os reference server (protocol v%d)" % PROTOCOL_VERSION)
    print("  文書ルート : %s (%d件)" % (index.root, len(docs)))
    print("  待ち受け   : http://%s:%d/" % (opts.host, opts.port))
    print("  discovery  : %s" % ("/.well-known/pico-os" if opts.discovery else "無効(404)"))
    print("  検索       : %s" % (opts.search_path or "非対応"))
    print("  マニフェスト: %s" % (opts.manifest_path or "非対応"))
    if opts.home:
        print("  ホーム     : %s" % opts.home)
    if oversized:
        print("  警告       : %d件が%dBの上限を超えています(--check で詳細)"
              % (len(oversized), LIMIT_DOC_BYTES))
    print("  終了       : Ctrl-C")

    try:
        with Server((opts.host, opts.port), Handler, index, opts) as httpd:
            httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n終了します。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
