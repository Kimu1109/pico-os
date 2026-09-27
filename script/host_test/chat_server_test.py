#!/usr/bin/env python3
"""
chat_server_test.py
-------------------
チャットサーバ(server/chat/chat_server.py)の、部屋の種類・参加コード・役割と権限のテスト。
run_net.sh から呼ぶ(サーバを同じプロセスの中で 127.0.0.1 の空きポートに立て、本物のHTTPで叩く)。

見ること:
  - オープンチャットは検索に出て、誰でも参加できる。プライベートチャットは検索に出ず、
    id を知っていても参加も閲覧もできない(有ることすら 404 で隠す)
  - 参加コード: 参加者なら誰でも発行でき、小文字・区切り無しでも通る。期限切れ・回数切れ・
    無効化・追放された人には通らない。外し続けると 429
  - 役割: 管理者は自分より弱い人だけを追い出せる。オーナーだけが役割を変え、部屋を消せる。
    オーナーが抜けると次の人へ譲られ、誰もいなくなった部屋は消える
  - サーバ管理者: ユーザーの作成/パスワードの決め直し/無効化/管理者の任命、全部屋の一覧、部屋を作れる人の設定。
    参加していない部屋でも管理の操作(メンバーを見る・追い出す・オーナーを決める・消す)はできるが、
    プライベートチャットの発言は参加しない限り読めない。管理者でない人は /api/v1/admin/ を一切使えない
  - v1 のDB(rooms.name が UNIQUE、参加者の表が無い)を開くと、部屋を残したまま v3 へ移る

使い方: python3 chat_server_test.py
"""

import http.client
import importlib.util
import os
import sqlite3
import sys
import tempfile
import threading
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_PY = os.path.join(HERE, "..", "..", "server", "chat", "chat_server.py")

spec = importlib.util.spec_from_file_location("chat_server", SERVER_PY)
cs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cs)

failures = 0


def check(cond, label):
    global failures
    print("%s %s" % ("[ OK ]" if cond else "[FAIL]", label))
    if not cond:
        failures += 1


def eq(actual, expected, label):
    global failures
    ok = actual == expected
    print("%s %-44s 実測=%r%s" % ("[ OK ]" if ok else "[FAIL]", label, actual, "" if ok else " 期待=%r" % (expected,)))
    if not ok:
        failures += 1


class Client:
    def __init__(self, port, token):
        self.port = port
        self.token = token

    def req(self, method, path, body=None, query=None):
        if query:
            path += "?" + urllib.parse.urlencode(query)
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        headers = {"Authorization": "Bearer " + self.token}
        data = None
        if body is not None:
            data = body.encode("utf-8")
            headers["Content-Type"] = "text/plain; charset=utf-8"
        conn.request(method, path, body=data, headers=headers)
        res = conn.getresponse()
        text = res.read().decode("utf-8")
        conn.close()
        return res.status, text

    def rows(self, method, path, body=None, query=None):
        status, text = self.req(method, path, body, query)
        return status, [line.split("\t") for line in text.split("\n") if line]

    def rooms(self):
        return {int(r[0]): r for r in self.rows("GET", "/api/v1/rooms")[1]}


def add_user(store, login, display, admin=False):
    return store.add_user(login, display, "password1", admin)


def start_server(db_path, invite_ttl=cs.INVITE_TTL_SEC):
    store = cs.Store(db_path)
    cs.ChatHandler.store = store
    cs.ChatHandler.throttle = cs.LoginThrottle()
    cs.ChatHandler.join_throttle = cs.LoginThrottle(cs.JOIN_WINDOW_SEC, cs.JOIN_MAX_FAILS)
    cs.ChatHandler.invite_ttl = invite_ttl
    cs.log = lambda msg: None  # テストの出力を汚さない
    srv = cs.make_server("127.0.0.1", 0, cs.ChatHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return store, srv, srv.server_address[1]


def test_rooms_and_invites(tmp):
    print("\n---- 部屋の種類と参加コード ----")
    store, srv, port = start_server(os.path.join(tmp, "a.db"))
    alice = Client(port, add_user(store, "alice", "ありす"))
    bob = Client(port, add_user(store, "bob", "ぼぶ"))
    carol = Client(port, add_user(store, "carol", "きゃろる"))

    # ---- オープンチャット ----
    status, rows = alice.rows("POST", "/api/v1/rooms", "雑談")
    eq(status, 201, "オープンチャットを作れる(kind 省略時)")
    open_id = int(rows[0][0])
    eq(rows[0][2:], ["open", "owner"], "作った人がオーナー")
    eq(alice.req("POST", "/api/v1/rooms", "雑談")[0], 409, "オープンチャットの名前は重ならない")

    eq(open_id in bob.rooms(), False, "参加していない部屋は一覧に出ない")
    status, rows = bob.rows("GET", "/api/v1/rooms/search", query={"q": "雑"})
    eq([(int(r[0]), r[1], r[2], r[3]) for r in rows], [(open_id, "雑談", "1", "0")], "検索で見つかる(人数・未参加)")
    eq(bob.req("GET", "/api/v1/rooms/%d/messages" % open_id)[0], 403, "参加前は読めない(オープンは403)")
    status, rows = bob.rows("POST", "/api/v1/rooms/%d/join" % open_id, "")
    eq(status, 200, "検索した部屋に参加できる")
    eq(rows[0], [str(open_id), "雑談", "open", "member"], "参加の応答は id/名前/種類/役割")
    eq(bob.rooms()[open_id][4:6], ["open", "member"], "一覧の5・6列目は種類と役割")
    eq(bob.req("POST", "/api/v1/rooms/%d/messages" % open_id, "こんにちは")[0], 201, "参加すれば発言できる")
    eq(alice.req("POST", "/api/v1/rooms/%d/invites" % open_id, "")[0], 400, "オープンチャットに参加コードは出さない")

    # 検索語の % や _ はそのままの文字として扱う
    eq(bob.rows("GET", "/api/v1/rooms/search", query={"q": "%"})[1], [], "検索語の % は文字として扱う")

    # ---- プライベートチャット ----
    status, rows = alice.rows("POST", "/api/v1/rooms", "秘密基地", query={"kind": "private"})
    eq(status, 201, "プライベートチャットを作れる")
    priv_id = int(rows[0][0])
    eq(alice.req("POST", "/api/v1/rooms", "秘密基地", query={"kind": "private"})[0], 201,
       "プライベートチャットの名前は重なってよい(有ることを漏らさない)")
    eq(bob.rows("GET", "/api/v1/rooms/search", query={"q": "秘密"})[1], [], "プライベートチャットは検索に出ない")
    eq(bob.req("POST", "/api/v1/rooms/%d/join" % priv_id, "")[0], 404, "id を知っていても検索からは入れない")
    eq(bob.req("GET", "/api/v1/rooms/%d/messages" % priv_id)[0], 404, "参加していなければ有ることも隠す")
    eq(bob.req("POST", "/api/v1/rooms/%d/invites" % priv_id, "")[0], 404, "参加していなければコードも出せない")

    status, rows = alice.rows("POST", "/api/v1/rooms/%d/invites" % priv_id, "")
    eq(status, 201, "参加者は参加コードを発行できる")
    code, expires = rows[0][0], int(rows[0][1])
    check(len(code) == 9 and code[4] == "-", "コードは XXXX-XXXX の形")
    check(abs(expires - (time.time() + cs.INVITE_TTL_SEC)) < 5, "期限は30分後")

    status, rows = bob.rows("POST", "/api/v1/join", code.replace("-", "").lower())
    eq(status, 200, "小文字・区切り無しでも参加できる")
    eq(rows[0], [str(priv_id), "秘密基地", "private", "member"], "参加した部屋が返る")
    eq(bob.req("GET", "/api/v1/rooms/%d/messages" % priv_id)[0], 200, "参加すれば読める")

    # 参加者(bob)も招待できる。1回きりのコード
    status, rows = bob.rows("POST", "/api/v1/rooms/%d/invites" % priv_id, "", query={"uses": 1})
    eq(status, 201, "参加者もコードを出せる(回数指定)")
    once = rows[0][0]
    eq(rows[0][2], "1", "回数が返る")
    eq(carol.req("POST", "/api/v1/join", once)[0], 200, "1回きりのコードで入れる")
    dave = Client(port, add_user(store, "dave", "でいぶ"))
    eq(dave.req("POST", "/api/v1/join", once)[0], 404, "2人目は入れない")
    eq(dave.req("POST", "/api/v1/join", "ABCD")[0], 404, "形の違うコードは通らない")

    # ---- 無効化(管理者から) ----
    eq(bob.req("POST", "/api/v1/rooms/%d/invites/revoke" % priv_id, "")[0], 403, "参加者は無効化できない")
    status, text = alice.req("GET", "/api/v1/rooms/%d/invites" % priv_id)
    eq((status, len(text.splitlines())), (200, 1), "管理者は有効なコードの一覧を見られる(使い切った分は出ない)")
    eq(alice.req("POST", "/api/v1/rooms/%d/invites/revoke" % priv_id, "")[0], 200, "オーナーは無効化できる")
    eq(dave.req("POST", "/api/v1/join", code)[0], 404, "無効化したコードは通らない")

    # ---- 期限切れ ----
    cs.ChatHandler.invite_ttl = 1
    code2 = alice.rows("POST", "/api/v1/rooms/%d/invites" % priv_id, "")[1][0][0]
    time.sleep(1.2)
    eq(dave.req("POST", "/api/v1/join", code2)[0], 404, "期限の切れたコードは通らない")
    cs.ChatHandler.invite_ttl = cs.INVITE_TTL_SEC

    # ---- 総当たり ----
    for _ in range(cs.JOIN_MAX_FAILS):
        dave.req("POST", "/api/v1/join", "ZZZZ-ZZZZ")
    code3 = alice.rows("POST", "/api/v1/rooms/%d/invites" % priv_id, "")[1][0][0]
    eq(dave.req("POST", "/api/v1/join", code3)[0], 429, "外し続けると正しいコードでも断る")

    srv.shutdown()
    srv.server_close()
    return store


def test_roles(tmp):
    print("\n---- 役割と権限 ----")
    store, srv, port = start_server(os.path.join(tmp, "b.db"))
    owner = Client(port, add_user(store, "owner", "おーなー"))
    admin = Client(port, add_user(store, "admin", "かんり"))
    m1 = Client(port, add_user(store, "m1", "めんばー1"))
    m2 = Client(port, add_user(store, "m2", "めんばー2"))

    room = int(owner.rows("POST", "/api/v1/rooms", "広場")[1][0][0])
    for c in (admin, m1, m2):
        c.req("POST", "/api/v1/rooms/%d/join" % room, "")

    eq(m1.req("POST", "/api/v1/rooms/%d/role" % room, "m2\tadmin")[0], 403, "参加者は役割を変えられない")
    eq(owner.req("POST", "/api/v1/rooms/%d/role" % room, "admin\tadmin")[0], 200, "オーナーは管理者を任命できる")
    eq(owner.req("POST", "/api/v1/rooms/%d/role" % room, "admin\tking")[0], 400, "知らない役割は断る")

    status, rows = m1.rows("GET", "/api/v1/rooms/%d/members" % room)
    eq([(r[0], r[2]) for r in rows], [("owner", "owner"), ("admin", "admin"), ("m1", "member"), ("m2", "member")],
       "参加者の一覧(強い順)")

    eq(m1.req("POST", "/api/v1/rooms/%d/kick" % room, "m2")[0], 403, "参加者は追い出せない")
    eq(admin.req("POST", "/api/v1/rooms/%d/kick" % room, "owner")[0], 403, "管理者はオーナーを追い出せない")
    eq(admin.req("POST", "/api/v1/rooms/%d/kick" % room, "m1")[0], 200, "管理者は参加者を追い出せる")
    eq(m1.req("GET", "/api/v1/rooms/%d/messages" % room)[0], 403, "追い出された人は読めない")
    eq(m1.req("POST", "/api/v1/rooms/%d/join" % room, "")[0], 200, "追い出しただけなら検索から戻れる")

    eq(admin.req("POST", "/api/v1/rooms/%d/kick" % room, "m1", query={"ban": 1})[0], 200, "追放できる")
    eq(m1.req("POST", "/api/v1/rooms/%d/join" % room, "")[0], 403, "追放された人は戻れない")
    status, rows = admin.rows("GET", "/api/v1/rooms/%d/members" % room)
    check(["m1", "めんばー1", "banned"] in rows, "管理者には追放した人も見える")
    status, rows = m2.rows("GET", "/api/v1/rooms/%d/members" % room)
    check(all(r[2] != "banned" for r in rows), "参加者には追放した人は見えない")
    eq(admin.req("POST", "/api/v1/rooms/%d/unban" % room, "m1")[0], 200, "追放を解ける")
    eq(m1.req("POST", "/api/v1/rooms/%d/join" % room, "")[0], 200, "解かれたら戻れる")

    # 追放はプライベートチャットの参加コードにも効く
    priv = int(owner.rows("POST", "/api/v1/rooms", "内緒", query={"kind": "private"})[1][0][0])
    owner.req("POST", "/api/v1/rooms/%d/kick" % priv, "m2", query={"ban": 1})
    code = owner.rows("POST", "/api/v1/rooms/%d/invites" % priv, "")[1][0][0]
    eq(m2.req("POST", "/api/v1/join", code)[0], 404, "追放された人はコードでも入れない")

    eq(admin.req("POST", "/api/v1/rooms/%d/delete" % room, "")[0], 403, "管理者は部屋を消せない")

    # ---- 譲渡とオーナーの退出 ----
    eq(owner.req("POST", "/api/v1/rooms/%d/role" % room, "m2\towner")[0], 200, "オーナーを譲れる")
    roles = {r[0]: r[2] for r in owner.rows("GET", "/api/v1/rooms/%d/members" % room)[1]}
    eq((roles["m2"], roles["owner"]), ("owner", "admin"), "譲った人は管理者になる")
    eq(m2.req("POST", "/api/v1/rooms/%d/leave" % room, "")[0], 200, "オーナーも抜けられる")
    roles = {r[0]: r[2] for r in owner.rows("GET", "/api/v1/rooms/%d/members" % room)[1]}
    eq(list(roles.values()).count("owner"), 1, "抜けたらオーナーは誰か1人へ移る")
    check(roles.get("owner") == "owner" or roles.get("admin") == "owner", "管理者が優先して引き継ぐ")

    for c in (owner, admin, m1):
        c.req("POST", "/api/v1/rooms/%d/leave" % room, "")
    eq(store.room(room), None, "誰もいなくなった部屋は消える")

    eq(owner.req("POST", "/api/v1/rooms/%d/delete" % priv, "")[0], 200, "オーナーは部屋を消せる")
    eq(store.room(priv), None, "消した部屋は無い")
    eq(store.db().execute("SELECT COUNT(*) FROM invites WHERE room_id=?", (priv,)).fetchone()[0], 0,
       "部屋を消すと参加コードも消える")

    srv.shutdown()
    srv.server_close()


def test_server_admin(tmp):
    print("\n---- サーバ管理者 ----")
    store, srv, port = start_server(os.path.join(tmp, "c.db"))
    root = Client(port, add_user(store, "root", "かんりにん", admin=True))
    alice = Client(port, add_user(store, "alice", "ありす"))
    bob = Client(port, add_user(store, "bob", "ぼぶ"))

    eq(root.rows("GET", "/api/v1/me")[1][0][2:], ["admin", "1"], "me: 管理者で、部屋を作れる")
    eq(alice.rows("GET", "/api/v1/me")[1][0][2:], ["user", "1"], "me: 一般の人")
    for method, path in (("GET", "/api/v1/admin/users"), ("GET", "/api/v1/admin/rooms"),
                         ("GET", "/api/v1/admin/settings"), ("POST", "/api/v1/admin/users/bob/disable")):
        eq(alice.req(method, path, "" if method == "POST" else None)[0], 403, "一般の人は使えない: " + path)

    # ---- ユーザーの管理 ----
    form = urllib.parse.urlencode({"login": "carol", "display": "きゃろる", "password": "carolpass"})
    eq(root.req("POST", "/api/v1/admin/users", form)[0], 201, "ユーザーを作れる")
    eq(root.req("POST", "/api/v1/admin/users", form)[0], 409, "同じログイン名は断る")
    short = urllib.parse.urlencode({"login": "dave", "password": "short"})
    eq(root.req("POST", "/api/v1/admin/users", short)[0], 400, "短いパスワードは断る")
    users = {r[0]: r for r in root.rows("GET", "/api/v1/admin/users")[1]}
    eq(users["carol"][1:], ["きゃろる", "0", "1"], "一覧: 表示名・管理者でない・使える")
    eq(users["root"][2], "1", "一覧: 管理者の印")

    eq(root.req("POST", "/api/v1/admin/users/bob/disable", "")[0], 200, "無効にできる")
    eq(bob.req("GET", "/api/v1/rooms")[0], 401, "無効にした人のトークンは通らない")
    eq(root.req("POST", "/api/v1/admin/users/root/disable", "")[0], 400, "自分は無効にできない")
    eq(root.req("POST", "/api/v1/admin/users/bob/password", "newpassword")[0], 200, "パスワードを決め直せる")
    b = store.user_by_login("bob")
    check(cs.check_password("newpassword", b["pw_salt"], b["pw_hash"]), "決め直したパスワードでログインできる")
    eq(root.req("POST", "/api/v1/admin/users/nobody/password", "newpassword")[0], 404, "いない人は404")

    eq(root.req("POST", "/api/v1/admin/users/alice/admin", "1")[0], 200, "管理者にできる")
    eq(alice.rows("GET", "/api/v1/me")[1][0][2], "admin", "管理者になった")
    eq(root.req("POST", "/api/v1/admin/users/root/admin", "0")[0], 400, "自分の管理者は外せない")
    eq(root.req("POST", "/api/v1/admin/users/alice/admin", "0")[0], 200, "他の管理者は外せる")
    eq(alice.req("GET", "/api/v1/admin/users")[0], 403, "外された人は使えなくなる")

    # ---- 部屋を作れる人 ----
    eq(root.req("POST", "/api/v1/admin/settings", "room_create\tadmin")[0], 200, "設定を変えられる")
    eq(root.req("POST", "/api/v1/admin/settings", "room_create\teveryone")[0], 400, "知らない値は断る")
    eq(root.req("POST", "/api/v1/admin/settings", "color\tred")[0], 400, "知らない設定は断る")
    eq(alice.rows("GET", "/api/v1/me")[1][0][3], "0", "me: 部屋を作れない")
    eq(alice.req("POST", "/api/v1/rooms", "作れない部屋")[0], 403, "管理者でなければ部屋を作れない")
    eq(root.req("POST", "/api/v1/rooms", "お知らせ")[0], 201, "管理者は作れる")
    root.req("POST", "/api/v1/admin/settings", "room_create\tall")
    eq(alice.req("POST", "/api/v1/rooms", "作れる部屋")[0], 201, "全員に戻すと作れる")

    # ---- 参加していない部屋の管理 ----
    priv = int(alice.rows("POST", "/api/v1/rooms", "ないしょ", query={"kind": "private"})[1][0][0])
    alice.req("POST", "/api/v1/rooms/%d/messages" % priv, "ひみつ")
    rooms = {int(r[0]): r for r in root.rows("GET", "/api/v1/admin/rooms")[1]}
    eq(rooms[priv][1:], ["ないしょ", "private", "1", "alice"], "全部屋の一覧(プライベートも、オーナーも分かる)")
    eq(root.req("GET", "/api/v1/rooms/%d/messages" % priv)[0], 404, "参加していないプライベートチャットは読めない")
    eq(root.req("POST", "/api/v1/rooms/%d/invites" % priv, "")[0], 404, "参加コードも出せない")
    eq(root.req("GET", "/api/v1/rooms/%d/members" % priv)[0], 200, "メンバーは見られる")

    code = alice.rows("POST", "/api/v1/rooms/%d/invites" % priv, "")[1][0][0]
    carol_token = store.reset_token(store.user_by_login("carol")["id"])
    carol = Client(port, carol_token)
    carol.req("POST", "/api/v1/join", code)
    eq(root.req("POST", "/api/v1/rooms/%d/role" % priv, "carol\towner")[0], 200, "オーナーを付け替えられる")
    roles = {r[0]: r[2] for r in root.rows("GET", "/api/v1/rooms/%d/members" % priv)[1]}
    eq((roles["carol"], roles["alice"]), ("owner", "admin"), "前のオーナーは管理者になる(オーナーは1人)")
    eq(root.req("POST", "/api/v1/rooms/%d/kick" % priv, "carol", query={"ban": 1})[0], 200,
       "オーナーでも追放できる")
    eq(root.req("POST", "/api/v1/rooms/%d/role" % priv, "alice\towner")[0], 200, "オーナーを置き直せる")
    eq(root.req("POST", "/api/v1/rooms/%d/delete" % priv, "")[0], 200, "参加していない部屋を消せる")
    eq(store.room(priv), None, "消えた")

    # オーナーのいない部屋(v1 から移行した部屋や、管理コマンドで作った部屋)を引き取る
    orphan = store.add_room("みんなの部屋")
    root.req("POST", "/api/v1/rooms/%d/join" % orphan, "")
    eq(root.req("POST", "/api/v1/rooms/%d/role" % orphan, "root\towner")[0], 200, "自分をオーナーにできる")
    eq(root.rooms()[orphan][5], "owner", "オーナーになった")
    alice.req("POST", "/api/v1/rooms/%d/join" % orphan, "")
    eq(alice.req("POST", "/api/v1/rooms/%d/role" % orphan, "alice\towner")[0], 403, "一般の人には同じことはできない")

    srv.shutdown()
    srv.server_close()


def test_migration(tmp):
    print("\n---- v1 のDBからの移行 ----")
    path = os.path.join(tmp, "v1.db")
    db = sqlite3.connect(path)
    db.executescript("""
        CREATE TABLE users (id INTEGER PRIMARY KEY, login TEXT NOT NULL UNIQUE, display TEXT NOT NULL,
                            pw_salt TEXT, pw_hash TEXT, token_hash TEXT UNIQUE);
        CREATE TABLE rooms (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, created INTEGER NOT NULL);
        CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT,
                               room_id INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
                               user_id INTEGER NOT NULL REFERENCES users(id), epoch INTEGER NOT NULL, text TEXT NOT NULL);
        CREATE TABLE reads (user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                            room_id INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
                            last_read INTEGER NOT NULL, PRIMARY KEY (user_id, room_id));
        CREATE TABLE sessions (hash TEXT PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                               expires INTEGER NOT NULL);
        INSERT INTO users VALUES (1, 'alice', 'ありす', 's', 'h', 't1');
        INSERT INTO users VALUES (2, 'gone', '消えた人', NULL, NULL, NULL);
        INSERT INTO rooms VALUES (1, '雑談', 0), (2, '連絡', 0);
        INSERT INTO messages(room_id, user_id, epoch, text) VALUES (1, 1, 0, 'やあ'), (2, 2, 0, '昔の発言');
        INSERT INTO reads VALUES (1, 1, 1);
    """)
    db.commit()
    db.close()

    store = cs.Store(path)
    db = store.db()
    eq(db.execute("PRAGMA user_version").fetchone()[0], cs.SCHEMA_VERSION, "版が上がる")
    eq([(r["id"], r["name"], r["kind"]) for r in db.execute("SELECT * FROM rooms ORDER BY id")],
       [(1, "雑談", "open"), (2, "連絡", "open")], "部屋はオープンチャットとして残る")
    eq(db.execute("SELECT COUNT(*) FROM messages").fetchone()[0], 2, "発言は消えない")
    eq(db.execute("SELECT COUNT(*) FROM reads").fetchone()[0], 1, "既読も消えない")
    eq([(r["room_id"], r["user_id"]) for r in db.execute("SELECT * FROM members ORDER BY room_id, user_id")],
       [(1, 1), (2, 1)], "使える人は全部の部屋の参加者になる(無効にした人は除く)")
    eq(db.execute("PRAGMA foreign_key_check").fetchall(), [], "外部キーが壊れていない")
    eq(db.execute("SELECT is_admin FROM users WHERE login='alice'").fetchone()[0], 0,
       "サーバ管理者の列が足され、誰も管理者にはならない")
    eq(store.admin_count(), 0, "管理者は0人(管理コマンドで決める)")
    try:
        store.add_room("雑談")
        check(False, "オープンチャットの名前の重なりは移行後も断る")
    except sqlite3.IntegrityError:
        check(True, "オープンチャットの名前の重なりは移行後も断る")
    store.add_room("雑談", "private", 1)
    check(True, "移行後はプライベートチャットなら同名を作れる")
    # 2回目に開いても何もしない
    cs.Store(path)
    eq(db.execute("SELECT COUNT(*) FROM rooms").fetchone()[0], 3, "開き直しても部屋は変わらない")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        test_rooms_and_invites(tmp)
        test_roles(tmp)
        test_server_admin(tmp)
        test_migration(tmp)
    print("\n%s (失敗 %d件)" % ("ALL PASSED" if failures == 0 else "FAILED", failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
