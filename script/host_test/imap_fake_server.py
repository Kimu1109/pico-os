#!/usr/bin/env python3
"""IMAPサーバの偽物(メールアプリの結合テスト・PCビルドでの動作確認の相手)。標準ライブラリだけで動く。

pico-os の ImapClient が使うところだけを真似る(IMAP4rev1、RFC 3501 のごく一部):

    LOGIN "user" "pass"           違えば NO [AUTHENTICATIONFAILED]
    SELECT / EXAMINE "INBOX"      * N EXISTS / * OK [UIDVALIDITY ..]
    NOOP                          spool に増えたメールがあれば * N EXISTS
    FETCH a:b (UID FLAGS INTERNALDATE RFC822.SIZE BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])
    UID FETCH u (UID FLAGS BODY[]<0.n>)       BODY[] なら \\Seen を付ける(BODY.PEEK[] なら付けない)
    LOGOUT / CAPABILITY

メールは --spool のディレクトリの *.eml(名前順。名前が -seen.eml で終わるものは既読)。NOOP のたびに見直すので、テストは .eml を足すだけで
「新着」を作れる。--seed を付けると、spool が空なら見本のメール(ISO-2022-JP・multipart・HTMLだけ・添付・
Shift_JIS 等を合わせて25通)を書き出す。

    python3 imap_fake_server.py --port 8143 --user me@example.jp --password secret --spool DIR [--seed]
                                [--cert server.pem --key server.key]

PCビルドで試すときは /sys/mail.cfg に
    host = 127.0.0.1
    port = 8143
    tls = false
    user = me@example.jp
    password = secret
を書く(password は起動時に暗号化して書き直される)。
"""
import argparse
import base64
import datetime
import os
import re
import socketserver
import ssl
import sys
import threading

LOCK = threading.Lock()
UIDVALIDITY = 1700000000


class Store:
    def __init__(self, spool):
        self.spool = spool
        self.messages = []   # {"name", "uid", "flags", "date", "raw"}
        self.next_uid = 101
        self.initial = None
        self.reload()
        self.initial = len(self.messages)

    def reload(self):
        names = sorted(n for n in os.listdir(self.spool) if n.endswith(".eml"))
        known = {m["name"] for m in self.messages}
        for n in names:
            if n in known:
                continue
            with open(os.path.join(self.spool, n), "rb") as f:
                raw = f.read().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            flags = set()
            if n.endswith("-seen.eml"):
                flags.add("\\Seen")
            # 届いた日時: 起動時からあったものは7時間おきに並べて最後が今、後から足されたものは今
            now = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=9))).replace(microsecond=0)
            if self.initial is None:
                date = now - datetime.timedelta(hours=7 * (len(names) - 1 - names.index(n)))
            else:
                date = now
            self.messages.append({"name": n, "uid": self.next_uid, "flags": flags, "date": date, "raw": raw})
            self.next_uid += 3   # UID は飛び飛び(番号とUIDを取り違えると分かるように)


def internaldate(d):
    return d.strftime("%d-%b-%Y %H:%M:%S %z")


def header_fields(raw, names):
    head = raw.split(b"\r\n\r\n", 1)[0]
    out = []
    keep = False
    for line in head.split(b"\r\n"):
        if line[:1] in (b" ", b"\t"):
            if keep:
                out.append(line)
            continue
        name = line.split(b":", 1)[0].strip().upper()
        keep = name.decode("ascii", "replace") in names
        if keep:
            out.append(line)
    return b"\r\n".join(out) + b"\r\n\r\n"


def parse_quoted_args(s):
    args = []
    i = 0
    while i < len(s):
        if s[i] == " ":
            i += 1
            continue
        if s[i] == '"':
            i += 1
            buf = []
            while i < len(s) and s[i] != '"':
                if s[i] == "\\" and i + 1 < len(s):
                    i += 1
                buf.append(s[i])
                i += 1
            args.append("".join(buf))
            i += 1
        else:
            j = s.find(" ", i)
            if j < 0:
                j = len(s)
            args.append(s[i:j])
            i = j
    return args


def seq_range(spec, count):
    a, _, b = spec.partition(":")
    lo = count if a == "*" else int(a)
    hi = lo if not b else (count if b == "*" else int(b))
    return min(lo, hi), max(lo, hi)


class Handler(socketserver.StreamRequestHandler):
    def send(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.wfile.write(data)
        self.wfile.flush()

    def handle(self):
        cfg = self.server.cfg
        store = self.server.store
        authed = False
        selected = False
        exists = 0
        self.send("* OK [CAPABILITY IMAP4rev1] pico-os fake IMAP ready\r\n")
        while True:
            line = self.rfile.readline()
            if not line:
                return
            line = line.decode("utf-8", "replace").rstrip("\r\n")
            tag, _, rest = line.partition(" ")
            cmd, _, args = rest.partition(" ")
            cmd = cmd.upper()
            uid_mode = False
            if cmd == "UID":
                uid_mode = True
                cmd, _, args = args.partition(" ")
                cmd = cmd.upper()
            log = line if cmd != "LOGIN" else tag + " LOGIN ***"
            print("<<", log, flush=True)

            if cmd == "CAPABILITY":
                self.send("* CAPABILITY IMAP4rev1\r\n%s OK done\r\n" % tag)
            elif cmd == "LOGOUT":
                self.send("* BYE bye\r\n%s OK done\r\n" % tag)
                return
            elif cmd == "LOGIN":
                a = parse_quoted_args(args)
                if len(a) == 2 and a[0] == cfg.user and a[1] == cfg.password:
                    authed = True
                    self.send("%s OK [CAPABILITY IMAP4rev1] Logged in\r\n" % tag)
                else:
                    self.send("%s NO [AUTHENTICATIONFAILED] Invalid credentials\r\n" % tag)
            elif not authed:
                self.send("%s BAD not authenticated\r\n" % tag)
            elif cmd in ("SELECT", "EXAMINE"):
                box = parse_quoted_args(args)
                if not box or box[0].upper() != "INBOX":
                    self.send("%s NO [NONEXISTENT] Unknown Mailbox\r\n" % tag)
                    continue
                with LOCK:
                    store.reload()
                    exists = len(store.messages)
                    unseen = [i + 1 for i, m in enumerate(store.messages) if "\\Seen" not in m["flags"]]
                selected = True
                self.send("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n")
                self.send("* %d EXISTS\r\n* 0 RECENT\r\n" % exists)
                if unseen:
                    self.send("* OK [UNSEEN %d] first unseen\r\n" % unseen[0])
                self.send("* OK [UIDVALIDITY %d] UIDs valid\r\n* OK [UIDNEXT %d] next\r\n" % (UIDVALIDITY, store.next_uid))
                self.send("%s OK [READ-WRITE] Select completed\r\n" % tag)
            elif not selected:
                self.send("%s BAD no mailbox selected\r\n" % tag)
            elif cmd == "NOOP":
                with LOCK:
                    store.reload()
                    n = len(store.messages)
                if n != exists:
                    exists = n
                    self.send("* %d EXISTS\r\n" % exists)
                self.send("%s OK NOOP completed\r\n" % tag)
            elif cmd == "FETCH":
                spec, _, items = args.partition(" ")
                with LOCK:
                    msgs = list(enumerate(store.messages[:exists], start=1))
                if uid_mode:
                    lo, hi = seq_range(spec, 1 << 30)
                    picked = [(s, m) for s, m in msgs if lo <= m["uid"] <= hi]
                else:
                    lo, hi = seq_range(spec, exists)
                    picked = [(s, m) for s, m in msgs if lo <= s <= hi]
                for s, m in picked:
                    self.send(self.fetch_one(s, m, items.upper(), uid_mode))
                self.send("%s OK FETCH completed\r\n" % tag)
            else:
                self.send("%s BAD unknown command\r\n" % tag)

    def fetch_one(self, seq, m, items, uid_mode):
        parts = []
        literal = None
        if "UID" in items or uid_mode:
            parts.append("UID %d" % m["uid"])
        body_match = re.search(r"BODY(\.PEEK)?\[\]<0\.(\d+)>", items)
        if body_match and not body_match.group(1):
            with LOCK:
                m["flags"].add("\\Seen")
        if "FLAGS" in items:
            parts.append("FLAGS (%s)" % " ".join(sorted(m["flags"])))
        if "INTERNALDATE" in items:
            parts.append('INTERNALDATE "%s"' % internaldate(m["date"]))
        if "RFC822.SIZE" in items:
            parts.append("RFC822.SIZE %d" % len(m["raw"]))
        hm = re.search(r"BODY\.PEEK\[HEADER\.FIELDS \(([^)]*)\)\]", items)
        if hm:
            names = hm.group(1).split()
            data = header_fields(m["raw"], names)
            literal = ("BODY[HEADER.FIELDS (%s)]" % " ".join(names), data)
        if body_match:
            n = int(body_match.group(2))
            literal = ("BODY[]<0>", m["raw"][:n])
        out = ("* %d FETCH (" % seq + " ".join(parts)).encode()
        if literal:
            out += (" %s {%d}\r\n" % (literal[0], len(literal[1]))).encode() + literal[1]
        return out + b")\r\n"


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


# ---------------------------------------------------------------------------
# 見本のメール
# ---------------------------------------------------------------------------

def b64(b):
    return base64.b64encode(b).decode()


def mime_word(text, charset="UTF-8"):
    enc = "iso2022_jp" if charset == "ISO-2022-JP" else "utf-8"
    return "=?%s?B?%s?=" % (charset, b64(text.encode(enc)))


def seed(spool):
    mails = []
    # 1. ISO-2022-JP の text/plain(昔ながらの日本語のメール)
    body = "山田です。\n\n来週の打ち合わせは火曜日の10時からでお願いします。\n場所は第2会議室です。\n\nよろしくお願いします。\n"
    mails.append(
        b"From: " + mime_word("山田 太郎", "ISO-2022-JP").encode() + b" <taro@example.jp>\n"
        b"To: me@example.jp\n"
        b"Subject: " + mime_word("打ち合わせの件", "ISO-2022-JP").encode() + b"\n"
        b"Date: Thu, 01 Oct 2026 09:00:00 +0900\n"
        b"MIME-Version: 1.0\n"
        b"Content-Type: text/plain; charset=ISO-2022-JP\n"
        b"Content-Transfer-Encoding: 7bit\n\n" + body.encode("iso2022_jp"))
    # 2. multipart/mixed(alternative + 添付PDF)
    plain = "ご注文ありがとうございます。\n\n注文番号: 123-4567\n合計: ¥3,980\n\n領収書を添付します。\n"
    html = "<html><body><p>ご注文ありがとうございます。</p></body></html>"
    mails.append((
        "From: =?UTF-8?B?" + b64("ショップ".encode()) + "?= <shop@example.com>\n"
        "To: me@example.jp\n"
        "Subject: " + mime_word("【ご注文の確認】注文番号 123-4567") + "\n"
        "Date: Fri, 02 Oct 2026 12:30:00 +0900\n"
        "MIME-Version: 1.0\n"
        "Content-Type: multipart/mixed; boundary=\"MIX\"\n\n"
        "--MIX\n"
        "Content-Type: multipart/alternative; boundary=\"ALT\"\n\n"
        "--ALT\nContent-Type: text/plain; charset=UTF-8\nContent-Transfer-Encoding: base64\n\n"
        + b64(plain.encode()) + "\n"
        "--ALT\nContent-Type: text/html; charset=UTF-8\nContent-Transfer-Encoding: quoted-printable\n\n"
        + html + "\n--ALT--\n"
        "--MIX\nContent-Type: application/pdf; name=\"receipt.pdf\"\n"
        "Content-Disposition: attachment; filename*=UTF-8''%E9%A0%98%E5%8F%8E%E6%9B%B8.pdf\n"
        "Content-Transfer-Encoding: base64\n\n" + b64(b"%PDF-1.4 fake") + "\n--MIX--\n").encode())
    # 3. HTMLだけ(ニュースレター)
    mails.append((
        "From: Weekly News <news@example.org>\n"
        "To: me@example.jp\n"
        "Subject: =?UTF-8?Q?=E9=80=B1=E5=88=8A=E3=83=8B=E3=83=A5=E3=83=BC=E3=82=B9?= #42\n"
        "Date: Sat, 03 Oct 2026 07:00:00 +0000\n"
        "MIME-Version: 1.0\n"
        "Content-Type: text/html; charset=utf-8\n\n"
        "<html><head><style>h1{color:red}</style></head><body>"
        "<h1>今週の話題</h1><ul><li>ラズパイの新製品</li><li>RP2350 &amp; Wi-Fi</li></ul>"
        "<p>配信停止は<a href=\"https://example.org/u\">こちら</a></p></body></html>\n").encode())
    # 4. Shift_JIS(8bit)
    mails.append(
        b"From: sjis@example.jp\n"
        b"To: me@example.jp\n"
        b"Subject: " + mime_word("シフトJISのメール", "ISO-2022-JP").encode() + b"\n"
        b"Date: Sun, 04 Oct 2026 20:15:00 +0900\n"
        b"Content-Type: text/plain; charset=Shift_JIS\n"
        b"Content-Transfer-Encoding: 8bit\n\n" + "表示のテスト①～ソ\n".encode("cp932"))
    # 数合わせ(一覧が kMaxMessages 通で切れることの確認用)。上の4通より古く、半分は既読
    fillers = []
    for i in range(1, 22):
        fillers.append((
            "From: \"Sender %d\" <s%d@example.com>\n"
            "Subject: Message number %d\n"
            "Date: Wed, 30 Sep 2026 10:%02d:00 +0900\n"
            "Content-Type: text/plain; charset=us-ascii\n\n"
            "Hello %d.\n" % (i, i, i, i, i)).encode())
    mails = fillers + mails
    for i, raw in enumerate(mails, start=1):
        suffix = "-seen" if i <= 11 else ""
        with open(os.path.join(spool, "%03d%s.eml" % (i, suffix)), "wb") as f:
            f.write(raw)
    return len(mails)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--user", default="me@example.jp")
    ap.add_argument("--password", default="secret")
    ap.add_argument("--spool", required=True)
    ap.add_argument("--seed", action="store_true")
    ap.add_argument("--cert")
    ap.add_argument("--key")
    cfg = ap.parse_args()

    os.makedirs(cfg.spool, exist_ok=True)
    if cfg.seed and not any(n.endswith(".eml") for n in os.listdir(cfg.spool)):
        seed(cfg.spool)

    server = Server(("127.0.0.1", cfg.port), Handler)
    server.cfg = cfg
    server.store = Store(cfg.spool)
    if cfg.cert:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(cfg.cert, cfg.key)
        server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print("IMAPの偽物を起動しました port=%d tls=%s 通数=%d" % (cfg.port, bool(cfg.cert), len(server.store.messages)),
          flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
