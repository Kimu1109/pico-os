#!/usr/bin/env python3
"""
chat_server.py
--------------
pico-os のチャットアプリ(ChatScene)とWebクライアントの相手をする、自前のチャットサーバ。
仕様は リポジトリ直下の CHAT_PROTOCOL.md。

標準ライブラリだけで動く(Raspberry Pi OS の python3 そのままでよい)。
発言・部屋・ユーザーは SQLite の1ファイルへ保存する。

使い方(詳しくは server/chat/README.md):

    # ユーザーを作る(パスワードを聞かれる。Web用のログインに使う)
    python3 chat_server.py --db chat.db adduser alice --display "ありす"

    # 部屋を作る(Webからも作れる)
    python3 chat_server.py --db chat.db addroom 雑談

    # 家の中だけで試す(平文HTTP)
    python3 chat_server.py --db chat.db serve --port 8080

    # 外へ公開する(Let's EncryptでHTTPS。80番はACMEの認証と https への転送だけ)
    python3 chat_server.py --db chat.db serve --port 443 \\
        --tls-cert /etc/pico-chat/tls/fullchain.pem --tls-key /etc/pico-chat/tls/privkey.pem \\
        --http-port 80 --acme-root /var/lib/pico-chat/acme

設計の要点:
    - 応答は **1行1件のTSV**(pico-os側にJSONパーサが無いため。PROTOCOL.md と同じ考え方)
    - **常に Content-Length を付け、HTTP/1.1 の keep-alive に対応する**。pico-os はTLSの
      ハンドシェイクで1〜2秒画面が止まるので、接続を使い回せないと数秒ごとに固まる
    - 証明書は **ファイルが更新されたら次の接続から自動で読み直す**(certbot の更新後に
      サーバを再起動しなくてよい)
    - トークン・セッションは **ハッシュだけを保存する**。DBが漏れてもそのまま使えない
"""

import argparse
import getpass
import hashlib
import hmac
import http.server
import os
import re
import secrets
import socket
import socketserver
import sqlite3
import ssl
import sys
import threading
import time
import urllib.parse

PROTOCOL_VERSION = 1

# ---- 上限(CHAT_PROTOCOL.md「制限値」と揃えること。pico-os側の固定長バッファの大きさ) ----
MAX_TEXT_BYTES = 500        # 発言1件の本文(UTF-8)
MAX_NAME_BYTES = 45         # 表示名・部屋名(UTF-8)
MAX_LOGIN_LEN = 32          # ログイン名(英数字と _ -)
MAX_LIMIT = 50              # 1回に返す発言の最大件数
DEFAULT_LIMIT = 20
MAX_WAIT_SEC = 30           # ロングポーリングで待つ最大秒数
MAX_BODY_BYTES = 4096       # 受け付けるリクエスト本文の上限
SESSION_DAYS = 30           # Webのログインが続く日数
IDLE_TIMEOUT_SEC = 120      # keep-alive の接続を何秒で切るか(pico-osは数秒ごとに叩く)

# ログインの総当たり対策。同じIPから LOGIN_WINDOW_SEC 秒に LOGIN_MAX_FAILS 回失敗したら断る
LOGIN_WINDOW_SEC = 600
LOGIN_MAX_FAILS = 10

PBKDF2_ITER = 200_000

LOGIN_RE = re.compile(r"^[A-Za-z0-9_-]{1,%d}$" % MAX_LOGIN_LEN)

WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")


def log(msg):
    sys.stderr.write(time.strftime("[%Y-%m-%d %H:%M:%S] ") + msg + "\n")
    sys.stderr.flush()


# ============================================================ 文字列

def utf8_len(s):
    return len(s.encode("utf-8"))


def clean_name(s):
    """表示名・部屋名。制御文字(タブ・改行を含む)を空白へ潰して前後を詰める。"""
    s = re.sub(r"[\x00-\x1f\x7f]+", " ", s or "").strip()
    return s


def clean_text(s):
    """発言の本文。改行は残し(エスケープして送る)、それ以外の制御文字は落とす。"""
    s = (s or "").replace("\r\n", "\n").replace("\r", "\n")
    s = re.sub(r"[\x00-\x08\x0b-\x1f\x7f]", "", s)
    s = s.replace("\t", "    ")
    return s.strip("\n")


def escape_text(s):
    """TSVの本文欄へ入れるためのエスケープ(CHAT_PROTOCOL.md「本文のエスケープ」)。"""
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")


def tsv_line(*fields):
    return "\t".join(str(f) for f in fields) + "\n"


# ============================================================ 秘密の値

def new_token():
    # 32バイトの乱数(base64urlで43文字)
    return secrets.token_urlsafe(32)


def token_hash(token):
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def hash_password(password, salt=None):
    salt = salt or secrets.token_bytes(16)
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, PBKDF2_ITER)
    return salt.hex(), digest.hex()


def check_password(password, salt_hex, digest_hex):
    if not salt_hex or not digest_hex:
        return False
    _, digest = hash_password(password, bytes.fromhex(salt_hex))
    return hmac.compare_digest(digest, digest_hex)


# ============================================================ 保存(SQLite)

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    id          INTEGER PRIMARY KEY,
    login       TEXT NOT NULL UNIQUE,
    display     TEXT NOT NULL,
    pw_salt     TEXT,
    pw_hash     TEXT,
    token_hash  TEXT UNIQUE
);
CREATE TABLE IF NOT EXISTS rooms (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    created     INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS messages (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id     INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    user_id     INTEGER NOT NULL REFERENCES users(id),
    epoch       INTEGER NOT NULL,
    text        TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS messages_room ON messages(room_id, id);
CREATE TABLE IF NOT EXISTS reads (
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    room_id     INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    last_read   INTEGER NOT NULL,
    PRIMARY KEY (user_id, room_id)
);
CREATE TABLE IF NOT EXISTS sessions (
    hash        TEXT PRIMARY KEY,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires     INTEGER NOT NULL
);
"""


class Store:
    """SQLiteへの読み書き。スレッドごとに接続を持ち、書き込みは1本のロックで直列にする。

    友達数人の規模なので、これで十分(SQLiteのWALで読みは並行して進む)。
    """

    def __init__(self, path):
        self.path = path
        self.local = threading.local()
        self.write_lock = threading.Lock()
        # 新しい発言を待っている(ロングポーリング中の)接続を起こすための合図
        self.new_message = threading.Condition()
        db = self.db()
        db.executescript(SCHEMA)
        db.commit()

    def db(self):
        conn = getattr(self.local, "conn", None)
        if conn is None:
            conn = sqlite3.connect(self.path, timeout=10)
            conn.row_factory = sqlite3.Row
            conn.execute("PRAGMA journal_mode=WAL")
            conn.execute("PRAGMA foreign_keys=ON")
            self.local.conn = conn
        return conn

    def write(self, sql, params=()):
        with self.write_lock:
            db = self.db()
            cur = db.execute(sql, params)
            db.commit()
            return cur

    # ---- ユーザー ----
    def user_by_token(self, token):
        return self.db().execute("SELECT * FROM users WHERE token_hash=?", (token_hash(token),)).fetchone()

    def user_by_login(self, login):
        return self.db().execute("SELECT * FROM users WHERE login=?", (login,)).fetchone()

    def user_by_session(self, sid):
        row = self.db().execute(
            "SELECT users.* FROM sessions JOIN users ON users.id=sessions.user_id "
            "WHERE sessions.hash=? AND sessions.expires>?", (token_hash(sid), int(time.time()))).fetchone()
        return row

    def new_session(self, user_id):
        sid = new_token()
        self.write("DELETE FROM sessions WHERE expires<=?", (int(time.time()),))
        self.write("INSERT INTO sessions(hash,user_id,expires) VALUES(?,?,?)",
                   (token_hash(sid), user_id, int(time.time()) + SESSION_DAYS * 86400))
        return sid

    def drop_session(self, sid):
        self.write("DELETE FROM sessions WHERE hash=?", (token_hash(sid),))

    def reset_token(self, user_id):
        token = new_token()
        self.write("UPDATE users SET token_hash=? WHERE id=?", (token_hash(token), user_id))
        return token

    # ---- 部屋 ----
    def rooms_for(self, user_id):
        return self.db().execute(
            "SELECT rooms.id, rooms.name, "
            "  COALESCE((SELECT MAX(id) FROM messages WHERE room_id=rooms.id), 0) AS last_id, "
            "  COALESCE((SELECT last_read FROM reads WHERE user_id=? AND room_id=rooms.id), 0) AS last_read "
            "FROM rooms ORDER BY rooms.id", (user_id,)).fetchall()

    def room(self, room_id):
        return self.db().execute("SELECT * FROM rooms WHERE id=?", (room_id,)).fetchone()

    def add_room(self, name):
        cur = self.write("INSERT INTO rooms(name,created) VALUES(?,?)", (name, int(time.time())))
        return cur.lastrowid

    def unread_count(self, room_id, last_read):
        return self.db().execute(
            "SELECT COUNT(*) FROM messages WHERE room_id=? AND id>?", (room_id, last_read)).fetchone()[0]

    # ---- 発言 ----
    def messages(self, room_id, after=None, before=None, limit=DEFAULT_LIMIT):
        db = self.db()
        base = ("SELECT messages.id, messages.epoch, messages.text, users.display "
                "FROM messages JOIN users ON users.id=messages.user_id WHERE room_id=? ")
        if after is not None:
            rows = db.execute(base + "AND messages.id>? ORDER BY messages.id ASC LIMIT ?",
                              (room_id, after, limit)).fetchall()
        else:
            # 最新(または before より前)の limit 件を、古い順に並べて返す
            if before is not None:
                rows = db.execute(base + "AND messages.id<? ORDER BY messages.id DESC LIMIT ?",
                                  (room_id, before, limit)).fetchall()
            else:
                rows = db.execute(base + "ORDER BY messages.id DESC LIMIT ?", (room_id, limit)).fetchall()
            rows = list(reversed(rows))
        return rows

    def post(self, room_id, user_id, text):
        now = int(time.time())
        cur = self.write("INSERT INTO messages(room_id,user_id,epoch,text) VALUES(?,?,?,?)",
                         (room_id, user_id, now, text))
        msg_id = cur.lastrowid
        # 自分の発言は既読にしておく(自分の発言で未読が増えるのは変なので)
        self.mark_read(user_id, room_id, msg_id)
        with self.new_message:
            self.new_message.notify_all()
        return msg_id, now

    def mark_read(self, user_id, room_id, msg_id):
        self.write(
            "INSERT INTO reads(user_id,room_id,last_read) VALUES(?,?,?) "
            "ON CONFLICT(user_id,room_id) DO UPDATE SET last_read=MAX(last_read, excluded.last_read)",
            (user_id, room_id, msg_id))


# ============================================================ HTTP

class HttpError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status
        self.message = message


class LoginThrottle:
    def __init__(self):
        self.lock = threading.Lock()
        self.fails = {}  # ip -> [時刻, ...]

    def blocked(self, ip):
        now = time.time()
        with self.lock:
            lst = [t for t in self.fails.get(ip, []) if now - t < LOGIN_WINDOW_SEC]
            self.fails[ip] = lst
            return len(lst) >= LOGIN_MAX_FAILS

    def fail(self, ip):
        with self.lock:
            self.fails.setdefault(ip, []).append(time.time())

    def clear(self, ip):
        with self.lock:
            self.fails.pop(ip, None)


class ChatHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "pico-chat/%d" % PROTOCOL_VERSION
    timeout = IDLE_TIMEOUT_SEC

    # server 側から差し込まれる
    store: Store = None
    throttle: LoginThrottle = None
    secure_cookie = False

    def log_message(self, fmt, *args):
        log("%s %s" % (self.client_address[0], fmt % args))

    # ---- 送信 ----
    def send_body(self, status, body, ctype="text/plain; charset=utf-8", headers=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (headers or []):
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_error_text(self, status, message):
        # 本文の1行目が理由(pico-osはそれをそのまま画面へ出す)
        self.send_body(status, message + "\n")

    # ---- 受信 ----
    def read_body(self):
        if self.body_consumed:
            return b""
        self.body_consumed = True
        if self.headers.get("Transfer-Encoding"):
            self.close_connection = True
            raise HttpError(411, "Content-Length を付けてください")
        n = self.headers.get("Content-Length")
        if n is None:
            return b""
        try:
            n = int(n)
        except ValueError:
            raise HttpError(400, "Content-Length が不正です")
        if n < 0 or n > MAX_BODY_BYTES:
            # 読まずに返すと残りがkeep-aliveの次の要求として解釈されるので、接続ごと閉じる
            self.close_connection = True
            raise HttpError(413, "本文が大きすぎます")
        data = self.rfile.read(n)
        if len(data) != n:
            self.close_connection = True
            raise HttpError(400, "本文が途中で切れました")
        return data

    def read_text_body(self):
        try:
            return self.read_body().decode("utf-8")
        except UnicodeDecodeError:
            raise HttpError(400, "本文はUTF-8で送ってください")

    # ---- 認証 ----
    def cookie(self, name):
        raw = self.headers.get("Cookie", "")
        for part in raw.split(";"):
            k, _, v = part.strip().partition("=")
            if k == name:
                return v
        return None

    def current_user(self, for_write=False):
        auth = self.headers.get("Authorization", "")
        if auth.startswith("Bearer "):
            user = self.store.user_by_token(auth[7:].strip())
            if user is None:
                raise HttpError(401, "トークンが違います")
            return user
        sid = self.cookie("pc_session")
        if sid:
            user = self.store.user_by_session(sid)
            if user is not None:
                # Cookieで書き込む要求は、別のサイトから送らせられないよう独自ヘッダを必須にする
                # (独自ヘッダはCORSの事前確認なしには付けられない)
                if for_write and self.headers.get("X-Pico-Chat") != "1":
                    raise HttpError(403, "X-Pico-Chat ヘッダがありません")
                return user
        raise HttpError(401, "ログインしてください")

    # ---- 振り分け ----
    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        self.dispatch("GET")

    def do_POST(self):
        self.dispatch("POST")

    def dispatch(self, method):
        self.body_consumed = False
        try:
            url = urllib.parse.urlsplit(self.path)
            path = url.path
            query = urllib.parse.parse_qs(url.query)

            if method == "GET" and path in ("/", "/index.html"):
                return self.serve_web()
            if method == "GET" and path == "/favicon.ico":
                return self.send_body(204, b"")
            if method == "GET" and path == "/.well-known/pico-chat":
                return self.send_body(200, tsv_line("version", PROTOCOL_VERSION)
                                      + tsv_line("max_text_bytes", MAX_TEXT_BYTES))

            if path == "/api/v1/login" and method == "POST":
                return self.api_login()
            if path == "/api/v1/logout" and method == "POST":
                return self.api_logout()
            if path == "/api/v1/me" and method == "GET":
                return self.api_me()
            if path == "/api/v1/token" and method == "POST":
                return self.api_token()
            if path == "/api/v1/rooms":
                if method == "GET":
                    return self.api_rooms()
                if method == "POST":
                    return self.api_add_room()

            m = re.fullmatch(r"/api/v1/rooms/(\d+)/messages", path)
            if m:
                room_id = int(m.group(1))
                if method == "GET":
                    return self.api_messages(room_id, query)
                if method == "POST":
                    return self.api_post(room_id)

            raise HttpError(404, "そのようなAPIはありません")
        except HttpError as e:
            # 未読の本文があれば読み捨てる(keep-aliveで次の要求と混ざらないように)
            if method == "POST" and not self.body_consumed and not self.close_connection:
                try:
                    self.read_body()
                except HttpError:
                    pass
            self.send_error_text(e.status, e.message)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            self.close_connection = True
        except Exception as e:  # 想定外。サーバは落とさない
            log("内部エラー: %r" % (e,))
            self.close_connection = True
            try:
                self.send_error_text(500, "サーバ内部のエラーです")
            except Exception:
                pass

    # ---- Web画面 ----
    def serve_web(self):
        try:
            with open(os.path.join(WEB_DIR, "index.html"), "rb") as f:
                body = f.read()
        except OSError:
            raise HttpError(404, "web/index.html がありません")
        self.send_body(200, body, "text/html; charset=utf-8", [
            ("Content-Security-Policy",
             "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'"),
            ("X-Frame-Options", "DENY"),
            ("Referrer-Policy", "no-referrer"),
        ])

    # ---- API ----
    def api_login(self):
        ip = self.client_address[0]
        if self.throttle.blocked(ip):
            raise HttpError(429, "失敗が多すぎます。しばらく待ってください")
        form = urllib.parse.parse_qs(self.read_text_body())
        login = (form.get("login") or [""])[0]
        password = (form.get("password") or [""])[0]
        user = self.store.user_by_login(login) if LOGIN_RE.match(login) else None
        if user is None or not check_password(password, user["pw_salt"], user["pw_hash"]):
            self.throttle.fail(ip)
            raise HttpError(401, "ログイン名かパスワードが違います")
        self.throttle.clear(ip)
        sid = self.store.new_session(user["id"])
        cookie = "pc_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d" % (sid, SESSION_DAYS * 86400)
        if self.secure_cookie:
            cookie += "; Secure"
        self.send_body(200, tsv_line(user["login"], user["display"]), headers=[("Set-Cookie", cookie)])

    def api_logout(self):
        sid = self.cookie("pc_session")
        if sid:
            self.store.drop_session(sid)
        self.read_body()
        self.send_body(200, "ok\n", headers=[("Set-Cookie", "pc_session=; Path=/; Max-Age=0")])

    def api_me(self):
        user = self.current_user()
        self.send_body(200, tsv_line(user["login"], user["display"]))

    def api_token(self):
        # pico-os用のトークンを発行し直す(古いトークンは使えなくなる)。
        # 平文のトークンはこの応答でしか見られない(サーバはハッシュしか持たない)
        user = self.current_user(for_write=True)
        self.read_body()
        token = self.store.reset_token(user["id"])
        log("%s がトークンを発行し直しました" % user["login"])
        self.send_body(200, token + "\n")

    def api_rooms(self):
        user = self.current_user()
        out = []
        for r in self.store.rooms_for(user["id"]):
            unread = self.store.unread_count(r["id"], r["last_read"]) if r["last_id"] > r["last_read"] else 0
            out.append(tsv_line(r["id"], clean_name(r["name"]), r["last_id"], unread))
        self.send_body(200, "".join(out))

    def api_add_room(self):
        self.current_user(for_write=True)
        name = clean_name(self.read_text_body())
        if not name:
            raise HttpError(400, "部屋の名前が空です")
        if utf8_len(name) > MAX_NAME_BYTES:
            raise HttpError(400, "部屋の名前が長すぎます(%dバイトまで)" % MAX_NAME_BYTES)
        try:
            room_id = self.store.add_room(name)
        except sqlite3.IntegrityError:
            raise HttpError(409, "同じ名前の部屋があります")
        with self.store.new_message:
            self.store.new_message.notify_all()
        self.send_body(201, tsv_line(room_id, name))

    def int_param(self, query, name, default=None, lo=0, hi=2**53):
        vals = query.get(name)
        if not vals:
            return default
        try:
            v = int(vals[0])
        except ValueError:
            raise HttpError(400, "%s が数値ではありません" % name)
        return max(lo, min(hi, v))

    def api_messages(self, room_id, query):
        user = self.current_user()
        if self.store.room(room_id) is None:
            raise HttpError(404, "部屋がありません")
        after = self.int_param(query, "after")
        before = self.int_param(query, "before")
        limit = self.int_param(query, "limit", DEFAULT_LIMIT, 1, MAX_LIMIT)
        wait = self.int_param(query, "wait", 0, 0, MAX_WAIT_SEC)

        rows = self.store.messages(room_id, after=after, before=before, limit=limit)
        if not rows and after is not None and wait > 0:
            # ロングポーリング: 新しい発言が来るまで(最大wait秒)応答を保留する
            deadline = time.time() + wait
            while not rows:
                left = deadline - time.time()
                if left <= 0:
                    break
                with self.store.new_message:
                    self.store.new_message.wait(timeout=min(left, 5))
                rows = self.store.messages(room_id, after=after, limit=limit)

        out = []
        for r in rows:
            out.append(tsv_line(r["id"], r["epoch"], clean_name(r["display"]), escape_text(r["text"])))
        if rows and before is None:
            # 取っていった分を既読にする(遡って古い発言を読むときは動かさない)
            self.store.mark_read(user["id"], room_id, rows[-1]["id"])
        self.send_body(200, "".join(out))

    def api_post(self, room_id):
        user = self.current_user(for_write=True)
        if self.store.room(room_id) is None:
            raise HttpError(404, "部屋がありません")
        text = clean_text(self.read_text_body())
        if not text.strip():
            raise HttpError(400, "本文が空です")
        if utf8_len(text) > MAX_TEXT_BYTES:
            raise HttpError(413, "本文が長すぎます(%dバイトまで)" % MAX_TEXT_BYTES)
        msg_id, epoch = self.store.post(room_id, user["id"], text)
        self.send_body(201, tsv_line(msg_id, epoch))


class AcmeHandler(http.server.BaseHTTPRequestHandler):
    """80番の係。Let's Encrypt(ACME http-01)の確認ファイルを配り、それ以外は https へ転送する。"""
    protocol_version = "HTTP/1.1"
    server_version = "pico-chat-acme"
    timeout = 30
    acme_root = None
    https_port = 443

    def log_message(self, fmt, *args):
        log("[http] %s %s" % (self.client_address[0], fmt % args))

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        prefix = "/.well-known/acme-challenge/"
        if path.startswith(prefix) and self.acme_root:
            name = path[len(prefix):]
            # トークンは base64url の文字だけ。それ以外(../ 等)は受け付けない
            if re.fullmatch(r"[A-Za-z0-9_-]+", name):
                try:
                    with open(os.path.join(self.acme_root, ".well-known", "acme-challenge", name), "rb") as f:
                        body = f.read()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                except OSError:
                    pass
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        host = (self.headers.get("Host") or "").split(":")[0]
        if not re.fullmatch(r"[A-Za-z0-9.-]{1,253}", host):
            self.send_response(400)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        port = "" if self.https_port == 443 else ":%d" % self.https_port
        self.send_response(301)
        self.send_header("Location", "https://%s%s%s" % (host, port, self.path))
        self.send_header("Content-Length", "0")
        self.end_headers()

    do_HEAD = do_GET


class ReloadingTlsContext:
    """証明書と鍵を、ファイルが更新されていたら読み直すSSLContextの入れ物。

    certbot は期限の30日前に証明書を差し替えるので、サーバを再起動しなくても
    次の接続から新しい証明書が使われるようにしておく。
    """

    def __init__(self, cert, key):
        self.cert = cert
        self.key = key
        self.lock = threading.Lock()
        self.stamp = None
        self.ctx = None
        self.get()  # 起動時に1回読んで、読めなければここで落とす

    def _stamp(self):
        return (os.stat(self.cert).st_mtime_ns, os.stat(self.key).st_mtime_ns)

    def get(self):
        with self.lock:
            try:
                stamp = self._stamp()
            except OSError as e:
                if self.ctx is None:
                    raise
                log("証明書を確認できません(前のものを使い続けます): %s" % e)
                return self.ctx
            if stamp != self.stamp:
                ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                # pico-os(BearSSL)は TLS 1.2 まで。ブラウザは 1.3 を使う
                ctx.minimum_version = ssl.TLSVersion.TLSv1_2
                try:
                    ctx.load_cert_chain(self.cert, self.key)
                except (OSError, ssl.SSLError) as e:
                    if self.ctx is None:
                        raise
                    log("証明書を読み直せません(前のものを使い続けます): %s" % e)
                    return self.ctx
                if self.ctx is not None:
                    log("証明書を読み直しました")
                self.ctx = ctx
                self.stamp = stamp
            return self.ctx


class ChatServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True
    tls = None  # ReloadingTlsContext

    def server_bind(self):
        # IPv6が使えるなら v4/v6 両方で待つ
        if self.address_family == socket.AF_INET6:
            try:
                self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
            except OSError:
                pass
        super().server_bind()

    def finish_request(self, request, client_address):
        # TLSのハンドシェイクは受け付けのスレッドではなく、接続ごとのスレッドで行う
        # (遅い相手が1人いると他の人が繋がらなくなるため)
        if self.tls is not None:
            request.settimeout(30)
            try:
                request = self.tls.get().wrap_socket(request, server_side=True)
            except (ssl.SSLError, OSError) as e:
                log("%s TLSのハンドシェイクに失敗: %s" % (client_address[0], e))
                try:
                    request.close()
                except OSError:
                    pass
                return
        super().finish_request(request, client_address)


def make_server(host, port, handler, tls=None):
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    cls = type("Server", (ChatServer,), {"address_family": family, "tls": tls})
    return cls((host, port), handler)


# ============================================================ コマンド

def cmd_serve(args, store):
    ChatHandler.store = store
    ChatHandler.throttle = LoginThrottle()
    ChatHandler.timeout = args.idle_timeout

    use_tls = bool(args.tls_cert or args.tls_key)
    if use_tls and not (args.tls_cert and args.tls_key):
        sys.exit("--tls-cert と --tls-key は両方指定してください")
    port = args.port if args.port else (443 if use_tls else 8080)

    try:
        # 80番(ACMEの確認と https への転送)は先に立てる。初回は証明書がまだ無く、
        # certbot --webroot がこの80番を通して証明書を取るため
        if args.http_port:
            AcmeHandler.acme_root = args.acme_root
            AcmeHandler.https_port = port
            acme = make_server(args.host, args.http_port, AcmeHandler)
            threading.Thread(target=acme.serve_forever, daemon=True).start()
            log("80番の係を起動しました (port %d, ACME=%s)" % (args.http_port, args.acme_root or "なし"))

        tls = None
        if use_tls:
            # 証明書がまだ無ければ(初回の certbot の前)、置かれるまで待つ
            waited = False
            while not (os.path.exists(args.tls_cert) and os.path.exists(args.tls_key)):
                if not waited:
                    log("証明書がまだありません。置かれるまで待ちます: %s" % args.tls_cert)
                    waited = True
                time.sleep(10)
            tls = ReloadingTlsContext(args.tls_cert, args.tls_key)
            ChatHandler.secure_cookie = True

        main = make_server(args.host, port, ChatHandler, tls)
        threading.Thread(target=main.serve_forever, daemon=True).start()
        log("チャットサーバを起動しました (%s://%s:%d, DB=%s)" % ("https" if tls else "http", args.host, port, args.db))

        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        log("終了します")


def ask_password():
    while True:
        p1 = getpass.getpass("パスワード(Webでのログイン用): ")
        if len(p1) < 8:
            print("8文字以上にしてください")
            continue
        p2 = getpass.getpass("もう一度: ")
        if p1 != p2:
            print("一致しません")
            continue
        return p1


def cmd_adduser(args, store):
    if not LOGIN_RE.match(args.login):
        sys.exit("ログイン名は英数字と _ - だけ、%d文字までです" % MAX_LOGIN_LEN)
    display = clean_name(args.display or args.login)
    if utf8_len(display) > MAX_NAME_BYTES:
        sys.exit("表示名が長すぎます(%dバイトまで)" % MAX_NAME_BYTES)
    if store.user_by_login(args.login):
        sys.exit("%s は既にいます" % args.login)
    password = args.password if args.password is not None else ask_password()
    salt, digest = hash_password(password)
    token = new_token()
    store.write("INSERT INTO users(login,display,pw_salt,pw_hash,token_hash) VALUES(?,?,?,?,?)",
                (args.login, display, salt, digest, token_hash(token)))
    print("作成しました: %s (%s)" % (args.login, display))
    print("pico-os用のトークン(このときしか表示しません。Webからいつでも発行し直せます):")
    print("  " + token)


def cmd_passwd(args, store):
    user = store.user_by_login(args.login)
    if not user:
        sys.exit("%s はいません" % args.login)
    password = args.password if args.password is not None else ask_password()
    salt, digest = hash_password(password)
    store.write("UPDATE users SET pw_salt=?, pw_hash=? WHERE id=?", (salt, digest, user["id"]))
    store.write("DELETE FROM sessions WHERE user_id=?", (user["id"],))
    print("パスワードを変更しました(ログイン中のWebは全てログアウトされます)")


def cmd_token(args, store):
    user = store.user_by_login(args.login)
    if not user:
        sys.exit("%s はいません" % args.login)
    print(store.reset_token(user["id"]))


def cmd_deluser(args, store):
    user = store.user_by_login(args.login)
    if not user:
        sys.exit("%s はいません" % args.login)
    # 発言は残す(会話の流れが壊れるので)。ログインとトークンだけ使えなくする
    store.write("UPDATE users SET pw_salt=NULL, pw_hash=NULL, token_hash=NULL WHERE id=?", (user["id"],))
    store.write("DELETE FROM sessions WHERE user_id=?", (user["id"],))
    print("%s を使えなくしました(過去の発言は残ります)" % args.login)


def cmd_users(args, store):
    for u in store.db().execute("SELECT * FROM users ORDER BY id"):
        state = "" if u["token_hash"] or u["pw_hash"] else " (無効)"
        print("%s\t%s%s" % (u["login"], u["display"], state))


def cmd_addroom(args, store):
    name = clean_name(args.name)
    if not name or utf8_len(name) > MAX_NAME_BYTES:
        sys.exit("部屋の名前は1〜%dバイトにしてください" % MAX_NAME_BYTES)
    try:
        print("作成しました: #%d %s" % (store.add_room(name), name))
    except sqlite3.IntegrityError:
        sys.exit("同じ名前の部屋があります")


def cmd_rooms(args, store):
    for r in store.db().execute("SELECT * FROM rooms ORDER BY id"):
        print("%d\t%s" % (r["id"], r["name"]))


def cmd_delroom(args, store):
    row = store.db().execute("SELECT * FROM rooms WHERE name=?", (args.name,)).fetchone()
    if not row:
        sys.exit("その部屋はありません")
    store.write("DELETE FROM messages WHERE room_id=?", (row["id"],))
    store.write("DELETE FROM reads WHERE room_id=?", (row["id"],))
    store.write("DELETE FROM rooms WHERE id=?", (row["id"],))
    print("削除しました(発言も全て消えました)")


def main():
    p = argparse.ArgumentParser(description="pico-os チャットサーバ (CHAT_PROTOCOL.md)")
    p.add_argument("--db", default=os.environ.get("PICO_CHAT_DB", "chat.db"), help="SQLiteのファイル")
    sub = p.add_subparsers(dest="cmd")

    s = sub.add_parser("serve", help="サーバを起動する(省略時もこれ)")
    s.add_argument("--host", default="0.0.0.0", help="待ち受けるアドレス(IPv6なら ::)")
    s.add_argument("--port", type=int, default=0, help="待ち受けるポート(既定: HTTPSなら443、平文なら8080)")
    s.add_argument("--tls-cert", help="証明書(Let's Encryptなら fullchain.pem)")
    s.add_argument("--tls-key", help="秘密鍵(Let's Encryptなら privkey.pem)")
    s.add_argument("--http-port", type=int, default=0,
                   help="この番号(通常80)でACMEの確認ファイルを配り、それ以外は https へ転送する")
    s.add_argument("--acme-root", help="certbot --webroot -w に渡すディレクトリ")
    s.add_argument("--idle-timeout", type=int, default=IDLE_TIMEOUT_SEC,
                   help="keep-aliveの接続を何秒で切るか(既定%d)" % IDLE_TIMEOUT_SEC)

    a = sub.add_parser("adduser", help="ユーザーを作る")
    a.add_argument("login")
    a.add_argument("--display", help="発言に出る名前(既定: ログイン名)")
    a.add_argument("--password", help="省略すると対話で聞く")

    a = sub.add_parser("passwd", help="パスワードを変える")
    a.add_argument("login")
    a.add_argument("--password")

    a = sub.add_parser("token", help="pico-os用のトークンを発行し直して表示する")
    a.add_argument("login")

    a = sub.add_parser("deluser", help="ユーザーを使えなくする(発言は残る)")
    a.add_argument("login")

    sub.add_parser("users", help="ユーザーの一覧")

    a = sub.add_parser("addroom", help="部屋を作る")
    a.add_argument("name")

    sub.add_parser("rooms", help="部屋の一覧")

    a = sub.add_parser("delroom", help="部屋を発言ごと消す")
    a.add_argument("name")

    args = p.parse_args()
    if args.cmd is None:
        args = p.parse_args(sys.argv[1:] + ["serve"])

    store = Store(args.db)
    {
        "serve": cmd_serve, "adduser": cmd_adduser, "passwd": cmd_passwd, "token": cmd_token,
        "deluser": cmd_deluser, "users": cmd_users, "addroom": cmd_addroom, "rooms": cmd_rooms,
        "delroom": cmd_delroom,
    }[args.cmd](args, store)


if __name__ == "__main__":
    main()
