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
import unicodedata
import urllib.parse

PROTOCOL_VERSION = 2

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

# 参加コード(プライベートチャット)
INVITE_TTL_SEC = 30 * 60    # 発行から30分で無効
MAX_INVITE_USES = 50        # 回数を指定するときの上限(指定しなければ期限内は何回でも)
MAX_ACTIVE_INVITES = 20     # 1つの部屋で同時に有効なコードの数
# 参加コードの総当たり対策。同じ人/同じIPから JOIN_WINDOW_SEC 秒に JOIN_MAX_FAILS 回外したら断る
JOIN_WINDOW_SEC = 600
JOIN_MAX_FAILS = 10

MIN_PASSWORD_LEN = 8

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

SCHEMA_VERSION = 3

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    id          INTEGER PRIMARY KEY,
    login       TEXT NOT NULL UNIQUE,
    display     TEXT NOT NULL,
    pw_salt     TEXT,
    pw_hash     TEXT,
    token_hash  TEXT UNIQUE,
    is_admin    INTEGER NOT NULL DEFAULT 0   -- サーバ管理者(v3)
);
CREATE TABLE IF NOT EXISTS rooms (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    kind        TEXT NOT NULL DEFAULT 'open' CHECK (kind IN ('open', 'private')),
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
-- 部屋の参加者と役割(v2)。役割の意味は ROLE_RANK / PERMS
CREATE TABLE IF NOT EXISTS members (
    room_id     INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role        TEXT NOT NULL CHECK (role IN ('owner', 'admin', 'member')),
    joined      INTEGER NOT NULL,
    PRIMARY KEY (room_id, user_id)
);
CREATE INDEX IF NOT EXISTS members_user ON members(user_id);
-- 追い出したうえで、検索からも参加コードからも戻れなくした人(v2)
CREATE TABLE IF NOT EXISTS bans (
    room_id     INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created     INTEGER NOT NULL,
    PRIMARY KEY (room_id, user_id)
);
-- プライベートチャットの参加コード(v2)。コードそのものは持たずハッシュだけ
CREATE TABLE IF NOT EXISTS invites (
    id          INTEGER PRIMARY KEY,
    hash        TEXT NOT NULL UNIQUE,
    room_id     INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    created_by  INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created     INTEGER NOT NULL,
    expires     INTEGER NOT NULL,
    uses_left   INTEGER            -- NULL = 期限内なら何回でも
);
CREATE INDEX IF NOT EXISTS invites_room ON invites(room_id);
-- サーバ全体の設定(v3)。値は SERVER_SETTINGS の既定値から選ぶ
CREATE TABLE IF NOT EXISTS settings (
    key         TEXT PRIMARY KEY,
    value       TEXT NOT NULL
);
"""

# v1 の rooms は name が UNIQUE で kind も無い。作り直す(外部キーの付け替えを避けるため
# SQLite の推奨手順「新しい表へ写す → 古い表を消す → 名前を変える」を外部キー無効で行う)
MIGRATE_V1_ROOMS = """
BEGIN;
CREATE TABLE rooms_v2 (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    kind        TEXT NOT NULL DEFAULT 'open' CHECK (kind IN ('open', 'private')),
    created     INTEGER NOT NULL
);
INSERT INTO rooms_v2(id, name, kind, created) SELECT id, name, 'open', created FROM rooms;
DROP TABLE rooms;
ALTER TABLE rooms_v2 RENAME TO rooms;
COMMIT;
"""

# 役割の強さ。数が大きいほど強い
ROLE_RANK = {"member": 1, "admin": 2, "owner": 3}

# 操作ごとに要る役割(CHAT_PROTOCOL.md「役割と権限」と揃えること)
PERMS = {
    "read": "member",            # 発言を読む
    "post": "member",            # 発言する
    "invite": "member",          # 参加コードを発行する(プライベートのみ)
    "members": "member",         # 参加者の一覧を見る
    "kick": "admin",             # 自分より弱い人を追い出す/追放する/追放を解く
    "revoke_invites": "admin",   # 有効な参加コードを全部無効にする
    "set_role": "owner",         # 管理者の任命・解任、オーナーの譲渡
    "delete_room": "owner",      # 部屋を発言ごと消す
}


KIND_LABEL = {"open": "オープンチャット", "private": "プライベートチャット"}
ROLE_LABEL = {"member": "参加者", "admin": "管理者", "owner": "オーナー"}


def can(role, action):
    return role_rank(role) >= ROLE_RANK[PERMS[action]]


# サーバ管理者は、部屋に参加していなくても「管理の操作」だけはオーナーより強い立場でできる。
# **発言を読む・書く・参加コードを出すには、管理者でも参加が要る**(プライベートチャットを黙って読めないように)
SERVER_ADMIN_ROLE = "server"
SERVER_ADMIN_RANK = ROLE_RANK["owner"] + 1
MODERATION_ACTIONS = {"members", "kick", "revoke_invites", "set_role", "delete_room"}


def role_rank(role):
    if role == SERVER_ADMIN_ROLE:
        return SERVER_ADMIN_RANK
    return ROLE_RANK.get(role or "", 0)


# サーバ全体の設定と、取りうる値(先頭が既定値)
SERVER_SETTINGS = {
    "room_create": ("all", "admin"),   # 部屋を作れるのは 全員 / サーバ管理者だけ
}


# 参加コード。読み違えやすい 0/O 1/I を除いた32文字 × 8文字(40bit)。見せるときは4文字ずつ - で区切る
INVITE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
INVITE_LEN = 8


def new_invite_code():
    return "".join(secrets.choice(INVITE_ALPHABET) for _ in range(INVITE_LEN))


def normalize_invite_code(s):
    """利用者が打ったコードを揃える(小文字・区切りの - や空白を許す)。形が違えば None。"""
    # 全角で打たれた英数字(日本語キーボードのまま等)も受け付ける
    s = unicodedata.normalize("NFKC", s or "")
    s = re.sub(r"[\s\-\u2010-\u2015\u2212\u30fc]+", "", s).upper()
    if len(s) != INVITE_LEN or any(c not in INVITE_ALPHABET for c in s):
        return None
    return s


def format_invite_code(code):
    return code[:4] + "-" + code[4:]


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
        self.migrate()

    def migrate(self):
        db = self.db()
        version = db.execute("PRAGMA user_version").fetchone()[0]
        if version >= SCHEMA_VERSION:
            return
        cols = [r["name"] for r in db.execute("PRAGMA table_info(rooms)")]
        if "kind" not in cols:
            # v1 → v2: 既存の部屋は全てオープンチャットにし、今いる人を全員参加者にする
            # (v1 では全員が全部の部屋を見ていたので、アップデートで部屋が消えたように見せない)
            with self.write_lock:
                db.execute("PRAGMA foreign_keys=OFF")
                db.executescript(MIGRATE_V1_ROOMS)
                db.execute("PRAGMA foreign_keys=ON")
                bad = db.execute("PRAGMA foreign_key_check").fetchall()
                if bad:
                    raise RuntimeError("移行後の外部キーの検査に失敗しました: %r" % (bad,))
                db.execute(
                    "INSERT OR IGNORE INTO members(room_id, user_id, role, joined) "
                    "SELECT rooms.id, users.id, 'member', ? FROM rooms, users "
                    "WHERE users.pw_hash IS NOT NULL OR users.token_hash IS NOT NULL",
                    (int(time.time()),))
                db.commit()
            log("DBを v2 へ移行しました(既存の部屋はオープンチャット、今いる人は全員参加者)")
        user_cols = [r["name"] for r in db.execute("PRAGMA table_info(users)")]
        if "is_admin" not in user_cols:
            # v2 → v3: サーバ管理者の印。誰も管理者にはしない(管理コマンド admin で決める)
            with self.write_lock:
                db.execute("ALTER TABLE users ADD COLUMN is_admin INTEGER NOT NULL DEFAULT 0")
                db.commit()
            log("DBを v3 へ移行しました(サーバ管理者は「admin <名前>」で決めてください)")
        with self.write_lock:
            db.executescript("CREATE UNIQUE INDEX IF NOT EXISTS rooms_open_name ON rooms(name) WHERE kind='open';")
            db.execute("PRAGMA user_version=%d" % SCHEMA_VERSION)
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
            try:
                cur = db.execute(sql, params)
                db.commit()
            except sqlite3.Error:
                # 失敗した文(同じ名前の重複など)の暗黙のトランザクションを残すと、DBの書き込みの鍵を
                # 持ったままになり、他のスレッドの書き込みが全部待たされる
                db.rollback()
                raise
            return cur

    def wake(self):
        """ロングポーリング中の接続を起こす(新しい発言・追い出し・部屋の削除)。"""
        with self.new_message:
            self.new_message.notify_all()

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

    # ---- サーバ管理 ----
    def users(self):
        return self.db().execute("SELECT * FROM users ORDER BY id").fetchall()

    def admin_count(self):
        return self.db().execute(
            "SELECT COUNT(*) FROM users WHERE is_admin=1 AND (pw_hash IS NOT NULL OR token_hash IS NOT NULL)"
        ).fetchone()[0]

    def add_user(self, login, display, password, admin=False):
        """ユーザーを作って pico-os 用のトークンを返す。ログイン名が重なれば sqlite3.IntegrityError。"""
        salt, digest = hash_password(password)
        token = new_token()
        self.write("INSERT INTO users(login,display,pw_salt,pw_hash,token_hash,is_admin) VALUES(?,?,?,?,?,?)",
                   (login, display, salt, digest, token_hash(token), 1 if admin else 0))
        return token

    def set_password(self, user_id, password):
        salt, digest = hash_password(password)
        self.write("UPDATE users SET pw_salt=?, pw_hash=? WHERE id=?", (salt, digest, user_id))
        self.write("DELETE FROM sessions WHERE user_id=?", (user_id,))

    def disable_user(self, user_id):
        # 発言は残す(会話の流れが壊れるので)。ログインとトークンだけ使えなくする
        self.write("UPDATE users SET pw_salt=NULL, pw_hash=NULL, token_hash=NULL WHERE id=?", (user_id,))
        self.write("DELETE FROM sessions WHERE user_id=?", (user_id,))
        self.wake()

    def set_admin(self, user_id, on):
        self.write("UPDATE users SET is_admin=? WHERE id=?", (1 if on else 0, user_id))

    def setting(self, key):
        row = self.db().execute("SELECT value FROM settings WHERE key=?", (key,)).fetchone()
        return row["value"] if row else SERVER_SETTINGS[key][0]

    def set_setting(self, key, value):
        self.write("INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                   (key, value))

    def all_rooms(self):
        return self.db().execute(
            "SELECT rooms.*, (SELECT COUNT(*) FROM members WHERE room_id=rooms.id) AS n, "
            "  (SELECT users.login FROM members JOIN users ON users.id=members.user_id "
            "   WHERE members.room_id=rooms.id AND members.role='owner' LIMIT 1) AS owner "
            "FROM rooms ORDER BY rooms.id").fetchall()

    # ---- 部屋 ----
    def rooms_for(self, user_id):
        """参加している部屋だけ。"""
        return self.db().execute(
            "SELECT rooms.id, rooms.name, rooms.kind, members.role, "
            "  COALESCE((SELECT MAX(id) FROM messages WHERE room_id=rooms.id), 0) AS last_id, "
            "  COALESCE((SELECT last_read FROM reads WHERE user_id=? AND room_id=rooms.id), 0) AS last_read "
            "FROM rooms JOIN members ON members.room_id=rooms.id AND members.user_id=? "
            "ORDER BY rooms.id", (user_id, user_id)).fetchall()

    def room(self, room_id):
        return self.db().execute("SELECT * FROM rooms WHERE id=?", (room_id,)).fetchone()

    def role_of(self, room_id, user_id):
        row = self.db().execute("SELECT role FROM members WHERE room_id=? AND user_id=?",
                                (room_id, user_id)).fetchone()
        return row["role"] if row else None

    def is_banned(self, room_id, user_id):
        return self.db().execute("SELECT 1 FROM bans WHERE room_id=? AND user_id=?",
                                 (room_id, user_id)).fetchone() is not None

    def add_room(self, name, kind="open", owner_id=None):
        """部屋を作る。オープンチャットの名前が重なれば sqlite3.IntegrityError。"""
        now = int(time.time())
        with self.write_lock:
            db = self.db()
            try:
                cur = db.execute("INSERT INTO rooms(name,kind,created) VALUES(?,?,?)", (name, kind, now))
                room_id = cur.lastrowid
                if owner_id is not None:
                    db.execute("INSERT INTO members(room_id,user_id,role,joined) VALUES(?,?,'owner',?)",
                               (room_id, owner_id, now))
                db.commit()
            except sqlite3.Error:
                db.rollback()
                raise
        return room_id

    def delete_room(self, room_id):
        # 外部キーの ON DELETE CASCADE で発言・既読・参加者・追放・参加コードも消える
        self.write("DELETE FROM rooms WHERE id=?", (room_id,))
        self.wake()

    def join(self, room_id, user_id):
        """参加者にする(既に参加していれば何もしない)。"""
        self.write("INSERT OR IGNORE INTO members(room_id,user_id,role,joined) VALUES(?,?,'member',?)",
                   (room_id, user_id, int(time.time())))

    def leave(self, room_id, user_id):
        """抜ける。オーナーが抜けたら一番強く古い人へ譲り、誰もいなくなったら部屋ごと消す。

        戻り値: "left" / "deleted"
        """
        with self.write_lock:
            db = self.db()
            role = db.execute("SELECT role FROM members WHERE room_id=? AND user_id=?",
                              (room_id, user_id)).fetchone()
            db.execute("DELETE FROM members WHERE room_id=? AND user_id=?", (room_id, user_id))
            db.execute("DELETE FROM reads WHERE room_id=? AND user_id=?", (room_id, user_id))
            rest = db.execute(
                "SELECT user_id FROM members WHERE room_id=? "
                "ORDER BY CASE role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END, joined, user_id",
                (room_id,)).fetchall()
            result = "left"
            if not rest:
                db.execute("DELETE FROM rooms WHERE id=?", (room_id,))
                result = "deleted"
            elif role is not None and role["role"] == "owner":
                db.execute("UPDATE members SET role='owner' WHERE room_id=? AND user_id=?",
                           (room_id, rest[0]["user_id"]))
            db.commit()
        self.wake()
        return result

    def kick(self, room_id, user_id, ban=False):
        with self.write_lock:
            db = self.db()
            db.execute("DELETE FROM members WHERE room_id=? AND user_id=?", (room_id, user_id))
            db.execute("DELETE FROM reads WHERE room_id=? AND user_id=?", (room_id, user_id))
            if ban:
                db.execute("INSERT OR IGNORE INTO bans(room_id,user_id,created) VALUES(?,?,?)",
                           (room_id, user_id, int(time.time())))
            db.commit()
        self.wake()

    def unban(self, room_id, user_id):
        return self.write("DELETE FROM bans WHERE room_id=? AND user_id=?", (room_id, user_id)).rowcount > 0

    def set_role(self, room_id, user_id, role):
        """役割を変える。role='owner' は譲渡(今のオーナーは管理者になる。オーナーは常に1人)。"""
        with self.write_lock:
            db = self.db()
            if role == "owner":
                db.execute("UPDATE members SET role='admin' WHERE room_id=? AND role='owner'", (room_id,))
            db.execute("UPDATE members SET role=? WHERE room_id=? AND user_id=?", (role, room_id, user_id))
            db.commit()

    def members(self, room_id):
        return self.db().execute(
            "SELECT users.login, users.display, members.role FROM members JOIN users ON users.id=members.user_id "
            "WHERE members.room_id=? "
            "ORDER BY CASE members.role WHEN 'owner' THEN 0 WHEN 'admin' THEN 1 ELSE 2 END, members.joined, users.id",
            (room_id,)).fetchall()

    def banned(self, room_id):
        return self.db().execute(
            "SELECT users.login, users.display FROM bans JOIN users ON users.id=bans.user_id "
            "WHERE bans.room_id=? ORDER BY bans.created", (room_id,)).fetchall()

    def search_open(self, query, user_id, limit, offset):
        """オープンチャットを名前の部分一致で探す。参加者の多い順。"""
        like = "%" + query.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%"
        return self.db().execute(
            "SELECT rooms.id, rooms.name, "
            "  (SELECT COUNT(*) FROM members WHERE room_id=rooms.id) AS n, "
            "  EXISTS(SELECT 1 FROM members WHERE room_id=rooms.id AND user_id=?) AS joined "
            "FROM rooms WHERE kind='open' AND name LIKE ? ESCAPE '\\' "
            "ORDER BY n DESC, rooms.id LIMIT ? OFFSET ?", (user_id, like, limit, offset)).fetchall()

    def unread_count(self, room_id, last_read):
        return self.db().execute(
            "SELECT COUNT(*) FROM messages WHERE room_id=? AND id>?", (room_id, last_read)).fetchone()[0]

    # ---- 参加コード ----
    def new_invite(self, room_id, user_id, ttl_sec, uses):
        """参加コードを発行して (コード, 期限) を返す。有効なコードが多すぎれば None。"""
        now = int(time.time())
        with self.write_lock:
            db = self.db()
            db.execute("DELETE FROM invites WHERE expires<=? OR uses_left<=0", (now,))
            n = db.execute("SELECT COUNT(*) FROM invites WHERE room_id=?", (room_id,)).fetchone()[0]
            if n >= MAX_ACTIVE_INVITES:
                db.commit()
                return None
            for _ in range(5):
                code = new_invite_code()
                try:
                    db.execute("INSERT INTO invites(hash,room_id,created_by,created,expires,uses_left) "
                               "VALUES(?,?,?,?,?,?)",
                               (token_hash(code), room_id, user_id, now, now + ttl_sec, uses or None))
                    db.commit()
                    return code, now + ttl_sec
                except sqlite3.IntegrityError:
                    continue  # 有効なコードと重なった(ほぼ起きない)。引き直す
            db.commit()
            return None

    def use_invite(self, code, user_id):
        """参加コードで参加する。成功なら部屋の行、コードが無い/期限切れ/追放されていれば None。

        1回きりのコードを2人が同時に使っても1人しか通らないよう、確認と消費を1つのロックの中で行う。
        """
        now = int(time.time())
        with self.write_lock:
            db = self.db()
            inv = db.execute("SELECT * FROM invites WHERE hash=? AND expires>? AND (uses_left IS NULL OR uses_left>0)",
                             (token_hash(code), now)).fetchone()
            if inv is None:
                return None
            room_id = inv["room_id"]
            if db.execute("SELECT 1 FROM bans WHERE room_id=? AND user_id=?", (room_id, user_id)).fetchone():
                return None
            already = db.execute("SELECT 1 FROM members WHERE room_id=? AND user_id=?",
                                 (room_id, user_id)).fetchone()
            if not already:
                db.execute("INSERT INTO members(room_id,user_id,role,joined) VALUES(?,?,'member',?)",
                           (room_id, user_id, now))
                if inv["uses_left"] is not None:
                    db.execute("UPDATE invites SET uses_left=uses_left-1 WHERE id=?", (inv["id"],))
            db.commit()
            return db.execute("SELECT * FROM rooms WHERE id=?", (room_id,)).fetchone()

    def active_invites(self, room_id):
        return self.db().execute(
            "SELECT invites.expires, invites.uses_left, users.display FROM invites "
            "JOIN users ON users.id=invites.created_by "
            "WHERE room_id=? AND expires>? AND (uses_left IS NULL OR uses_left>0) ORDER BY invites.id",
            (room_id, int(time.time()))).fetchall()

    def revoke_invites(self, room_id):
        return self.write("DELETE FROM invites WHERE room_id=?", (room_id,)).rowcount

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
        self.wake()
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
    """一定時間の失敗の回数を数える(ログインと参加コードの総当たり対策)。キーはIPや利用者。"""

    def __init__(self, window=LOGIN_WINDOW_SEC, max_fails=LOGIN_MAX_FAILS):
        self.lock = threading.Lock()
        self.window = window
        self.max_fails = max_fails
        self.fails = {}  # キー -> [時刻, ...]

    def blocked(self, ip):
        now = time.time()
        with self.lock:
            lst = [t for t in self.fails.get(ip, []) if now - t < self.window]
            if lst:
                self.fails[ip] = lst
            else:
                self.fails.pop(ip, None)
            return len(lst) >= self.max_fails

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
    join_throttle: LoginThrottle = None
    invite_ttl = INVITE_TTL_SEC
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
                    return self.api_add_room(query)
            if path == "/api/v1/rooms/search" and method == "GET":
                return self.api_search(query)
            if path == "/api/v1/join" and method == "POST":
                return self.api_join_code()

            if path == "/api/v1/admin/users":
                if method == "GET":
                    return self.api_admin_users()
                if method == "POST":
                    return self.api_admin_add_user()
            m = re.fullmatch(r"/api/v1/admin/users/([^/]+)/(password|disable|admin)", path)
            if m and method == "POST":
                return self.api_admin_user_action(urllib.parse.unquote(m.group(1)), m.group(2))
            if path == "/api/v1/admin/rooms" and method == "GET":
                return self.api_admin_rooms()
            if path == "/api/v1/admin/settings":
                return self.api_admin_settings(method)

            m = re.fullmatch(r"/api/v1/rooms/(\d+)/([a-z/]+)", path)
            if m:
                room_id = int(m.group(1))
                action = m.group(2)
                route = {
                    ("GET", "messages"): lambda: self.api_messages(room_id, query),
                    ("POST", "messages"): lambda: self.api_post(room_id),
                    ("POST", "join"): lambda: self.api_join_open(room_id),
                    ("POST", "leave"): lambda: self.api_leave(room_id),
                    ("GET", "members"): lambda: self.api_members(room_id),
                    ("GET", "invites"): lambda: self.api_invites(room_id),
                    ("POST", "invites"): lambda: self.api_new_invite(room_id, query),
                    ("POST", "invites/revoke"): lambda: self.api_revoke_invites(room_id),
                    ("POST", "kick"): lambda: self.api_kick(room_id, query),
                    ("POST", "unban"): lambda: self.api_unban(room_id),
                    ("POST", "role"): lambda: self.api_set_role(room_id),
                    ("POST", "delete"): lambda: self.api_delete_room(room_id),
                }.get((method, action))
                if route:
                    return route()

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

    def is_server_admin(self, user):
        return bool(user["is_admin"])

    def can_create_rooms(self, user):
        return self.store.setting("room_create") == "all" or self.is_server_admin(user)

    def api_me(self):
        # 3列目はサーバでの立場(admin / user)、4列目は部屋を作れるか(1/0)。v1/v2 のクライアントは読まない
        user = self.current_user()
        self.send_body(200, tsv_line(user["login"], user["display"],
                                     "admin" if self.is_server_admin(user) else "user",
                                     1 if self.can_create_rooms(user) else 0))

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
            out.append(tsv_line(r["id"], clean_name(r["name"]), r["last_id"], unread, r["kind"], r["role"]))
        self.send_body(200, "".join(out))

    def room_name_param(self):
        name = clean_name(self.read_text_body())
        if not name:
            raise HttpError(400, "部屋の名前が空です")
        if utf8_len(name) > MAX_NAME_BYTES:
            raise HttpError(400, "部屋の名前が長すぎます(%dバイトまで)" % MAX_NAME_BYTES)
        return name

    def api_add_room(self, query):
        user = self.current_user(for_write=True)
        kind = (query.get("kind") or ["open"])[0]
        if kind not in ("open", "private"):
            raise HttpError(400, "kind は open か private です")
        if not self.can_create_rooms(user):
            raise HttpError(403, "部屋を作れるのはサーバ管理者だけです")
        name = self.room_name_param()
        try:
            room_id = self.store.add_room(name, kind, user["id"])
        except sqlite3.IntegrityError:
            raise HttpError(409, "同じ名前のオープンチャットがあります")
        log("%s が%s「%s」(#%d)を作りました" % (user["login"], KIND_LABEL[kind], name, room_id))
        self.send_body(201, tsv_line(room_id, name, kind, "owner"))

    def member_room(self, user, room_id, action="read"):
        """部屋と自分の役割を返す。参加していなければ、プライベートは 404(有るかどうかも教えない)、
        オープンは 403。役割が足りなければ 403。

        サーバ管理者は、管理の操作(MODERATION_ACTIONS)に限り参加していなくても、
        どの部屋でもオーナーより強い立場(SERVER_ADMIN_ROLE)で行える。"""
        room = self.store.room(room_id)
        if room is not None and action in MODERATION_ACTIONS and self.is_server_admin(user):
            return room, SERVER_ADMIN_ROLE
        role = self.store.role_of(room_id, user["id"]) if room is not None else None
        if room is None or (role is None and room["kind"] == "private"):
            raise HttpError(404, "部屋がありません")
        if role is None:
            raise HttpError(403, "この部屋に参加していません")
        if not can(role, action):
            raise HttpError(403, "%s以上でないとできません" % ROLE_LABEL[PERMS[action]])
        return room, role

    def target_user(self, room_id, login):
        login = (login or "").strip()
        target = self.store.user_by_login(login) if LOGIN_RE.match(login) else None
        if target is None:
            raise HttpError(404, "その人はいません")
        return target

    def api_search(self, query):
        user = self.current_user()
        q = clean_name((query.get("q") or [""])[0])
        if utf8_len(q) > MAX_NAME_BYTES:
            raise HttpError(400, "検索語が長すぎます")
        limit = self.int_param(query, "limit", DEFAULT_LIMIT, 1, MAX_LIMIT)
        offset = self.int_param(query, "offset", 0, 0, 10**6)
        out = []
        for r in self.store.search_open(q, user["id"], limit, offset):
            out.append(tsv_line(r["id"], clean_name(r["name"]), r["n"], 1 if r["joined"] else 0))
        self.send_body(200, "".join(out))

    def joined_line(self, room, user):
        return tsv_line(room["id"], clean_name(room["name"]), room["kind"],
                        self.store.role_of(room["id"], user["id"]) or "member")

    def api_join_open(self, room_id):
        user = self.current_user(for_write=True)
        self.read_body()
        room = self.store.room(room_id)
        # プライベートチャットは参加コードでしか入れない。有るかどうかも教えない
        if room is None or room["kind"] != "open":
            raise HttpError(404, "部屋がありません")
        if self.store.is_banned(room_id, user["id"]):
            raise HttpError(403, "この部屋には参加できません")
        self.store.join(room_id, user["id"])
        self.send_body(200, self.joined_line(room, user))

    def api_join_code(self):
        user = self.current_user(for_write=True)
        ip = self.client_address[0]
        keys = ("ip:" + ip, "user:%d" % user["id"])
        if any(self.join_throttle.blocked(k) for k in keys):
            raise HttpError(429, "失敗が多すぎます。しばらく待ってください")
        code = normalize_invite_code(self.read_text_body())
        room = self.store.use_invite(code, user["id"]) if code else None
        if room is None:
            # 「コードが無い」「期限切れ」「追放されている」を区別しない(総当たりの手がかりを与えない)
            for k in keys:
                self.join_throttle.fail(k)
            raise HttpError(404, "参加コードが違うか、期限が切れています")
        log("%s が参加コードで「%s」(#%d)に参加しました" % (user["login"], room["name"], room["id"]))
        self.send_body(200, self.joined_line(room, user))

    def api_leave(self, room_id):
        user = self.current_user(for_write=True)
        self.read_body()
        room, _ = self.member_room(user, room_id)
        result = self.store.leave(room_id, user["id"])
        if result == "deleted":
            log("「%s」(#%d)は誰もいなくなったので消しました" % (room["name"], room_id))
        self.send_body(200, result + "\n")

    def api_members(self, room_id):
        user = self.current_user()
        _, role = self.member_room(user, room_id, "members")
        out = [tsv_line(m["login"], clean_name(m["display"]), m["role"]) for m in self.store.members(room_id)]
        if can(role, "kick"):
            # 追放した人は、解く権限のある人にだけ見せる
            out += [tsv_line(b["login"], clean_name(b["display"]), "banned") for b in self.store.banned(room_id)]
        self.send_body(200, "".join(out))

    def api_invites(self, room_id):
        user = self.current_user()
        self.member_room(user, room_id, "revoke_invites")
        out = [tsv_line(i["expires"], i["uses_left"] or 0, clean_name(i["display"]))
               for i in self.store.active_invites(room_id)]
        self.send_body(200, "".join(out))

    def api_new_invite(self, room_id, query):
        user = self.current_user(for_write=True)
        self.read_body()
        room, _ = self.member_room(user, room_id, "invite")
        if room["kind"] != "private":
            raise HttpError(400, "オープンチャットは検索から参加できます(参加コードはプライベートチャット用です)")
        uses = self.int_param(query, "uses", 0, 0, MAX_INVITE_USES)
        made = self.store.new_invite(room_id, user["id"], self.invite_ttl, uses)
        if made is None:
            raise HttpError(429, "有効な参加コードが多すぎます(%d個まで)。管理者に無効化してもらってください"
                            % MAX_ACTIVE_INVITES)
        code, expires = made
        log("%s が「%s」(#%d)の参加コードを発行しました" % (user["login"], room["name"], room_id))
        self.send_body(201, tsv_line(format_invite_code(code), expires, uses))

    def api_revoke_invites(self, room_id):
        user = self.current_user(for_write=True)
        self.read_body()
        self.member_room(user, room_id, "revoke_invites")
        n = self.store.revoke_invites(room_id)
        self.send_body(200, "%d\n" % n)

    def api_kick(self, room_id, query):
        user = self.current_user(for_write=True)
        _, role = self.member_room(user, room_id, "kick")
        target = self.target_user(room_id, self.read_text_body())
        target_role = self.store.role_of(room_id, target["id"])
        ban = self.int_param(query, "ban", 0, 0, 1) == 1
        if target["id"] == user["id"]:
            raise HttpError(400, "自分は追い出せません(抜けるときは leave)")
        if target_role is None and not ban:
            raise HttpError(404, "その人は参加していません")
        if target_role is not None and role_rank(target_role) >= role_rank(role):
            raise HttpError(403, "自分と同じか上の役割の人は追い出せません")
        self.store.kick(room_id, target["id"], ban)
        log("%s が %s を「#%d」から%s" % (user["login"], target["login"], room_id, "追放しました" if ban else "追い出しました"))
        self.send_body(200, "ok\n")

    def api_unban(self, room_id):
        user = self.current_user(for_write=True)
        self.member_room(user, room_id, "kick")
        target = self.target_user(room_id, self.read_text_body())
        if not self.store.unban(room_id, target["id"]):
            raise HttpError(404, "その人は追放されていません")
        self.send_body(200, "ok\n")

    def api_set_role(self, room_id):
        user = self.current_user(for_write=True)
        _, role = self.member_room(user, room_id, "set_role")
        login, _, new_role = self.read_text_body().strip().partition("\t")
        if new_role not in ROLE_RANK:
            raise HttpError(400, "役割は owner / admin / member のどれかです")
        target = self.target_user(room_id, login)
        # オーナーが自分を変えると部屋にオーナーがいなくなる。サーバ管理者は自分をオーナーにしてよい
        # (オーナーのいない部屋を引き取るため)
        if target["id"] == user["id"] and role != SERVER_ADMIN_ROLE:
            raise HttpError(400, "自分の役割は変えられません(譲るときは相手を owner に)")
        if self.store.role_of(room_id, target["id"]) is None:
            raise HttpError(404, "その人は参加していません")
        self.store.set_role(room_id, target["id"], new_role)
        log("%s が %s を「#%d」の %s にしました" % (user["login"], target["login"], room_id, new_role))
        self.send_body(200, "ok\n")

    # ---- サーバ管理(サーバ管理者だけ) ----
    def admin_user(self, for_write=False):
        user = self.current_user(for_write=for_write)
        if not self.is_server_admin(user):
            raise HttpError(403, "サーバ管理者だけが使えます")
        return user

    def admin_target(self, login):
        target = self.store.user_by_login(login) if LOGIN_RE.match(login or "") else None
        if target is None:
            raise HttpError(404, "その人はいません")
        return target

    def api_admin_users(self):
        self.admin_user()
        out = []
        for u in self.store.users():
            active = 1 if (u["pw_hash"] or u["token_hash"]) else 0
            out.append(tsv_line(u["login"], clean_name(u["display"]), 1 if u["is_admin"] else 0, active))
        self.send_body(200, "".join(out))

    def api_admin_add_user(self):
        user = self.admin_user(for_write=True)
        form = urllib.parse.parse_qs(self.read_text_body())
        login = (form.get("login") or [""])[0]
        display = clean_name((form.get("display") or [""])[0]) or login
        password = (form.get("password") or [""])[0]
        if not LOGIN_RE.match(login):
            raise HttpError(400, "ログイン名は英数字と _ - だけ、%d文字までです" % MAX_LOGIN_LEN)
        if utf8_len(display) > MAX_NAME_BYTES:
            raise HttpError(400, "表示名が長すぎます(%dバイトまで)" % MAX_NAME_BYTES)
        if len(password) < MIN_PASSWORD_LEN:
            raise HttpError(400, "パスワードは%d文字以上にしてください" % MIN_PASSWORD_LEN)
        try:
            self.store.add_user(login, display, password)
        except sqlite3.IntegrityError:
            raise HttpError(409, "%s は既にいます" % login)
        log("%s がユーザー %s を作りました" % (user["login"], login))
        self.send_body(201, tsv_line(login, display))

    def api_admin_user_action(self, login, action):
        user = self.admin_user(for_write=True)
        body = self.read_text_body()
        target = self.admin_target(login)
        if action == "password":
            if len(body) < MIN_PASSWORD_LEN:
                raise HttpError(400, "パスワードは%d文字以上にしてください" % MIN_PASSWORD_LEN)
            # 使えなくしていた人も、パスワードを決め直せばWebでログインできるようになる
            # (pico-os のトークンは本人が Web から発行し直す)
            self.store.set_password(target["id"], body)
            log("%s が %s のパスワードを決め直しました" % (user["login"], login))
        elif action == "disable":
            if target["id"] == user["id"]:
                raise HttpError(400, "自分は使えなくできません")
            self.store.disable_user(target["id"])
            log("%s が %s を使えなくしました" % (user["login"], login))
        elif action == "admin":
            on = body.strip() == "1"
            if target["id"] == user["id"] and not on:
                # 最後の管理者がいなくなるのを防ぐ(外すなら別の管理者から)
                raise HttpError(400, "自分の管理者は外せません(別の管理者に外してもらってください)")
            self.store.set_admin(target["id"], on)
            log("%s が %s を%s" % (user["login"], login, "サーバ管理者にしました" if on else "サーバ管理者から外しました"))
        self.send_body(200, "ok\n")

    def api_admin_rooms(self):
        self.admin_user()
        out = [tsv_line(r["id"], clean_name(r["name"]), r["kind"], r["n"], r["owner"] or "-")
               for r in self.store.all_rooms()]
        self.send_body(200, "".join(out))

    def api_admin_settings(self, method):
        user = self.admin_user(for_write=(method == "POST"))
        if method == "POST":
            key, _, value = self.read_text_body().strip().partition("\t")
            if key not in SERVER_SETTINGS:
                raise HttpError(400, "そのような設定はありません")
            if value not in SERVER_SETTINGS[key]:
                raise HttpError(400, "%s は %s のどれかです" % (key, " / ".join(SERVER_SETTINGS[key])))
            self.store.set_setting(key, value)
            log("%s が設定 %s を %s にしました" % (user["login"], key, value))
        out = [tsv_line(k, self.store.setting(k)) for k in SERVER_SETTINGS]
        self.send_body(200, "".join(out))

    def api_delete_room(self, room_id):
        user = self.current_user(for_write=True)
        self.read_body()
        room, _ = self.member_room(user, room_id, "delete_room")
        self.store.delete_room(room_id)
        log("%s が「%s」(#%d)を消しました" % (user["login"], room["name"], room_id))
        self.send_body(200, "ok\n")

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
        self.member_room(user, room_id)
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
                # 待っている間に追い出された/部屋が消えたなら、そこで打ち切る
                self.member_room(user, room_id)
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
        self.member_room(user, room_id, "post")
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
    ChatHandler.join_throttle = LoginThrottle(JOIN_WINDOW_SEC, JOIN_MAX_FAILS)
    ChatHandler.invite_ttl = args.invite_ttl
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

        if store.admin_count() == 0:
            log("サーバ管理者がいません。「admin <名前>」で決めると Web から管理できます")
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
        if len(p1) < MIN_PASSWORD_LEN:
            print("%d文字以上にしてください" % MIN_PASSWORD_LEN)
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
    # 最初のユーザーはサーバ管理者にする(誰も管理者がいないと Web から管理できないため)
    admin = args.admin or store.db().execute("SELECT COUNT(*) FROM users").fetchone()[0] == 0
    token = store.add_user(args.login, display, password, admin)
    print("作成しました: %s (%s)%s" % (args.login, display, " [サーバ管理者]" if admin else ""))
    print("pico-os用のトークン(このときしか表示しません。Webからいつでも発行し直せます):")
    print("  " + token)


def cmd_passwd(args, store):
    user = store.user_by_login(args.login)
    if not user:
        sys.exit("%s はいません" % args.login)
    password = args.password if args.password is not None else ask_password()
    store.set_password(user["id"], password)
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
    store.disable_user(user["id"])
    print("%s を使えなくしました(過去の発言は残ります)" % args.login)


def cmd_users(args, store):
    for u in store.users():
        state = "" if u["token_hash"] or u["pw_hash"] else " (無効)"
        admin = " [サーバ管理者]" if u["is_admin"] else ""
        print("%s\t%s%s%s" % (u["login"], u["display"], admin, state))


def cmd_admin(args, store):
    user = store.user_by_login(args.login)
    if not user:
        sys.exit("%s はいません" % args.login)
    store.set_admin(user["id"], not args.off)
    print("%s を%s" % (args.login, "サーバ管理者から外しました" if args.off else "サーバ管理者にしました"))


def cmd_setting(args, store):
    if args.key is None:
        for k in SERVER_SETTINGS:
            print("%s\t%s\t(%s)" % (k, store.setting(k), " / ".join(SERVER_SETTINGS[k])))
        return
    if args.key not in SERVER_SETTINGS:
        sys.exit("設定は %s のどれかです" % " / ".join(SERVER_SETTINGS))
    if args.value not in SERVER_SETTINGS[args.key]:
        sys.exit("%s は %s のどれかです" % (args.key, " / ".join(SERVER_SETTINGS[args.key])))
    store.set_setting(args.key, args.value)
    print("%s = %s" % (args.key, args.value))


def find_room(store, spec):
    """部屋を id(#3 や 3)か、オープンチャットの名前で探す。プライベートチャットは名前が重なり得るので id で。"""
    m = re.fullmatch(r"#?(\d+)", spec)
    if m:
        row = store.room(int(m.group(1)))
        if row:
            return row
    rows = store.db().execute("SELECT * FROM rooms WHERE name=? ORDER BY kind, id", (spec,)).fetchall()
    if len(rows) > 1:
        sys.exit("同じ名前の部屋が複数あります。id(rooms で確認)で指定してください")
    if not rows:
        sys.exit("その部屋はありません")
    return rows[0]


def cmd_addroom(args, store):
    name = clean_name(args.name)
    if not name or utf8_len(name) > MAX_NAME_BYTES:
        sys.exit("部屋の名前は1〜%dバイトにしてください" % MAX_NAME_BYTES)
    kind = "private" if args.private else "open"
    owner = None
    if args.owner:
        owner = store.user_by_login(args.owner)
        if not owner:
            sys.exit("%s はいません" % args.owner)
    elif kind == "private":
        sys.exit("プライベートチャットは --owner を指定してください(誰も参加コードを出せなくなるため)")
    try:
        room_id = store.add_room(name, kind, owner["id"] if owner else None)
    except sqlite3.IntegrityError:
        sys.exit("同じ名前のオープンチャットがあります")
    print("作成しました: #%d %s (%s%s)" % (room_id, name, KIND_LABEL[kind],
                                       ", オーナー " + args.owner if owner else ""))


def cmd_rooms(args, store):
    for r in store.db().execute(
            "SELECT rooms.*, (SELECT COUNT(*) FROM members WHERE room_id=rooms.id) AS n FROM rooms ORDER BY id"):
        print("#%d\t%s\t%s\t%d人" % (r["id"], r["name"], KIND_LABEL[r["kind"]], r["n"]))


def cmd_members(args, store):
    room = find_room(store, args.room)
    for m in store.members(room["id"]):
        print("%s\t%s\t%s" % (m["login"], m["display"], ROLE_LABEL[m["role"]]))
    for b in store.banned(room["id"]):
        print("%s\t%s\t追放" % (b["login"], b["display"]))


def cmd_setrole(args, store):
    """サーバの管理者として役割を直接決める(オーナーのいない部屋にオーナーを置く、など)。"""
    room = find_room(store, args.room)
    user = store.user_by_login(args.login)
    if not user:
        sys.exit("%s はいません" % args.login)
    if args.role == "owner":
        # オーナーは1人。今のオーナーは管理者にする
        store.write("UPDATE members SET role='admin' WHERE room_id=? AND role='owner'", (room["id"],))
    store.write("DELETE FROM bans WHERE room_id=? AND user_id=?", (room["id"], user["id"]))
    store.write("INSERT INTO members(room_id,user_id,role,joined) VALUES(?,?,?,?) "
                "ON CONFLICT(room_id,user_id) DO UPDATE SET role=excluded.role",
                (room["id"], user["id"], args.role, int(time.time())))
    print("#%d %s: %s を%sにしました" % (room["id"], room["name"], args.login, ROLE_LABEL[args.role]))


def cmd_delroom(args, store):
    row = find_room(store, args.name)
    store.delete_room(row["id"])
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
    s.add_argument("--invite-ttl", type=int, default=INVITE_TTL_SEC,
                   help="参加コードが使える秒数(既定%d=30分。テスト用に縮められる)" % INVITE_TTL_SEC)

    a = sub.add_parser("adduser", help="ユーザーを作る")
    a.add_argument("login")
    a.add_argument("--display", help="発言に出る名前(既定: ログイン名)")
    a.add_argument("--password", help="省略すると対話で聞く")
    a.add_argument("--admin", action="store_true", help="サーバ管理者にする(最初のユーザーは指定しなくても管理者)")

    a = sub.add_parser("passwd", help="パスワードを変える")
    a.add_argument("login")
    a.add_argument("--password")

    a = sub.add_parser("token", help="pico-os用のトークンを発行し直して表示する")
    a.add_argument("login")

    a = sub.add_parser("deluser", help="ユーザーを使えなくする(発言は残る)")
    a.add_argument("login")

    sub.add_parser("users", help="ユーザーの一覧")

    a = sub.add_parser("admin", help="サーバ管理者にする(--off で外す)")
    a.add_argument("login")
    a.add_argument("--off", action="store_true")

    a = sub.add_parser("setting", help="サーバ全体の設定を見る/変える(例: setting room_create admin)")
    a.add_argument("key", nargs="?")
    a.add_argument("value", nargs="?")

    a = sub.add_parser("addroom", help="部屋を作る(既定はオープンチャット)")
    a.add_argument("name")
    a.add_argument("--private", action="store_true", help="プライベートチャット(参加コードでだけ入れる)にする")
    a.add_argument("--owner", help="オーナーにする人(プライベートチャットでは必須)")

    sub.add_parser("rooms", help="部屋の一覧")

    a = sub.add_parser("members", help="部屋の参加者と役割")
    a.add_argument("room", help="部屋のid(#3)またはオープンチャットの名前")

    a = sub.add_parser("setrole", help="部屋での役割を決める(参加していなければ参加させる)")
    a.add_argument("room")
    a.add_argument("login")
    a.add_argument("role", choices=["owner", "admin", "member"])

    a = sub.add_parser("delroom", help="部屋を発言ごと消す")
    a.add_argument("name", help="部屋のid(#3)またはオープンチャットの名前")

    args = p.parse_args()
    if args.cmd is None:
        args = p.parse_args(sys.argv[1:] + ["serve"])

    store = Store(args.db)
    {
        "serve": cmd_serve, "adduser": cmd_adduser, "passwd": cmd_passwd, "token": cmd_token,
        "deluser": cmd_deluser, "users": cmd_users, "addroom": cmd_addroom, "rooms": cmd_rooms,
        "members": cmd_members, "setrole": cmd_setrole, "delroom": cmd_delroom,
        "admin": cmd_admin, "setting": cmd_setting,
    }[args.cmd](args, store)


if __name__ == "__main__":
    main()
