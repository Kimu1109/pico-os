#!/usr/bin/env python3
"""Todoist API v1 の偽物(TODOアプリの結合テスト・PCビルドでの動作確認の相手)。標準ライブラリだけで動く。

本物(https://api.todoist.com)のうち、pico-os の TodoistClient が使うところだけを真似る:

    GET  /api/v1/tasks?limit=&cursor=                 一覧(ページ分け。next_cursor)
    GET  /api/v1/tasks/filter?query=&limit=&cursor=   フィルタ("today | overdue" / "overdue | next 7 days" だけ)
    POST /api/v1/tasks          {"content", "due_string", "due_lang"}  → 作ったタスク
    POST /api/v1/tasks/<id>/close                     → 204(繰り返しは次の日へ進む)

応答のタスクは本物と同じキーを全部持たせる(読み捨てる側の確認のため、1件約800バイト)。
認証は `Authorization: Bearer <--token>`。違えば401。

    python3 todoist_fake_server.py --port 8150 --token 0123abcd [--page-size 10] [--fillers 40]
                                   [--cert server.pem --key server.key]

PCビルドで試すときは /sys/todoist.cfg に `api = http://127.0.0.1:8150` と `token = 0123abcd` を書く。
"""
import argparse
import datetime
import http.server
import json
import ssl
import sys
import threading
import urllib.parse

LOCK = threading.Lock()
TASKS = []
NEXT_ID = [1000]


def today():
    return datetime.date.today()


def make_task(content, due=None, priority=1, parent=None, recurring=False, due_string="", checked=False):
    NEXT_ID[0] += 1
    t = {
        "user_id": "1234567",
        "id": f"T{NEXT_ID[0]}x",
        "project_id": "6XGgm6PHrGgMpCFX",
        "section_id": None,
        "parent_id": parent,
        "added_by_uid": "1234567",
        "assigned_by_uid": None,
        "responsible_uid": None,
        "labels": ["pico"],
        "deadline": None,
        "duration": None,
        "is_collapsed": False,
        "checked": checked,
        "is_deleted": False,
        "added_at": "2026-01-15T10:30:00.000000Z",
        "completed_at": None,
        "completed_by_uid": None,
        "updated_at": "2026-01-17T10:30:00.000000Z",
        "due": None,
        "priority": priority,
        "child_order": NEXT_ID[0],
        "order_key": "a1V",
        "content": content,
        "description": "説明文はpico-osでは読まない。" * 3,
        "note_count": 0,
        "day_order": -1,
        "completed_count": 0,
        "postponed_count": 0,
    }
    if due is not None:
        t["due"] = {"date": due, "is_recurring": recurring, "lang": "ja",
                    "string": due_string or due, "timezone": None}
    return t


def seed(fillers):
    d = today()
    TASKS.clear()
    TASKS.append(make_task("期限切れの書類", (d - datetime.timedelta(days=1)).isoformat(), priority=4))
    TASKS.append(make_task("牛乳を買う", d.isoformat() + "T15:00:00", priority=2))
    TASKS.append(make_task("朝の運動", d.isoformat(), recurring=True, due_string="毎日"))
    # UTCで時刻を持つもの(タイムゾーンつきのタスク)
    TASKS.append(make_task("会議", d.isoformat() + "T01:00:00.000000Z", priority=3))
    TASKS.append(make_task("来週の準備", (d + datetime.timedelta(days=3)).isoformat()))
    parent = TASKS[0]["id"]
    TASKS.append(make_task("サブタスク", None, parent=parent))
    TASKS.append(make_task("いつか読む本 \"引用\" と \\ 記号"))
    TASKS.append(make_task("完了済み(返らないはず)", d.isoformat(), checked=True))
    for i in range(fillers):
        TASKS.append(make_task(f"その他 {i + 1}"))


def due_date(t):
    if not t["due"]:
        return None
    return datetime.date.fromisoformat(t["due"]["date"][:10])


def matches(t, query):
    d = due_date(t)
    if d is None:
        return False
    if query == "today | overdue":
        return d <= today()
    return d <= today() + datetime.timedelta(days=6)  # "overdue | next 7 days"


def parse_due_string(s):
    """本物の自然言語の日付の、ごく一部だけ"""
    d = today()
    s = s.strip()
    table = {"今日": d, "today": d, "明日": d + datetime.timedelta(days=1), "tomorrow": d + datetime.timedelta(days=1)}
    if s in table:
        return table[s].isoformat(), False
    if s == "毎日":
        return d.isoformat(), True
    if s.startswith("明日 ") and s.endswith("時"):
        try:
            h = int(s[3:-1])
            return (d + datetime.timedelta(days=1)).isoformat() + f"T{h:02d}:00:00", False
        except ValueError:
            pass
    return None, False


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    token = ""
    page_size = 50

    def log_message(self, fmt, *args):
        sys.stderr.write("[todoist] " + (fmt % args) + "\n")

    def send_json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, code, message):
        self.send_json(code, {"error": message, "error_code": code, "error_extra": {"error": "入れ子は見ない"},
                              "error_tag": "ERR", "http_code": code})

    def authorized(self):
        if self.headers.get("Authorization", "") == "Bearer " + self.token:
            return True
        self.send_error_json(401, "Invalid token")
        return False

    def list_page(self, items, qs):
        try:
            limit = int(qs.get("limit", ["50"])[0])
        except ValueError:
            limit = 50
        limit = max(1, min(limit, 200, self.page_size))
        start = 0
        cursor = qs.get("cursor", [""])[0]
        if cursor:
            if not cursor.startswith("c."):
                self.send_error_json(400, "Invalid cursor")
                return
            start = int(cursor[2:])
        page = items[start:start + limit]
        nxt = f"c.{start + limit}" if start + limit < len(items) else None
        self.send_json(200, {"results": page, "next_cursor": nxt})

    def do_GET(self):
        if not self.authorized():
            return
        url = urllib.parse.urlsplit(self.path)
        qs = urllib.parse.parse_qs(url.query)
        with LOCK:
            if url.path == "/api/v1/tasks":
                # 本物は完了済みを返さないが、読む側が飛ばすことの確認のため混ぜる
                return self.list_page(list(TASKS), qs)
            if url.path == "/api/v1/tasks/filter":
                query = qs.get("query", [""])[0]
                if query not in ("today | overdue", "overdue | next 7 days"):
                    return self.send_error_json(400, "Invalid filter query")
                return self.list_page([t for t in TASKS if matches(t, query) and not t["checked"]], qs)
        self.send_error_json(404, "Not found")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        if not self.authorized():
            return
        url = urllib.parse.urlsplit(self.path)
        parts = url.path.strip("/").split("/")
        with LOCK:
            if url.path == "/api/v1/tasks":
                try:
                    req = json.loads(body.decode("utf-8"))
                except (ValueError, UnicodeDecodeError):
                    return self.send_error_json(400, "Invalid JSON")
                content = req.get("content", "")
                if not content:
                    return self.send_error_json(400, "Content is required")
                due, rec = None, False
                if req.get("due_string"):
                    if req.get("due_lang") != "ja":
                        return self.send_error_json(400, "due_lang must be ja in this fake")
                    due, rec = parse_due_string(req["due_string"])
                    if due is None:
                        return self.send_error_json(400, "Date is invalid")
                t = make_task(content, due, recurring=rec, due_string=req.get("due_string", ""))
                TASKS.append(t)
                return self.send_json(200, t)
            if len(parts) == 5 and parts[:3] == ["api", "v1", "tasks"] and parts[4] == "close":
                for t in TASKS:
                    if t["id"] == parts[3] and not t["checked"]:
                        if t["due"] and t["due"]["is_recurring"]:
                            nd = due_date(t) + datetime.timedelta(days=1)
                            t["due"]["date"] = nd.isoformat() + t["due"]["date"][10:]
                        else:
                            TASKS.remove(t)
                        self.send_response(204)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        return
                return self.send_error_json(404, "Task not found")
        self.send_error_json(404, "Not found")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8150)
    ap.add_argument("--token", default="0123456789abcdef0123456789abcdef01234567")
    ap.add_argument("--page-size", type=int, default=50)
    ap.add_argument("--fillers", type=int, default=0)
    ap.add_argument("--cert")
    ap.add_argument("--key")
    args = ap.parse_args()

    seed(args.fillers)
    Handler.token = args.token
    Handler.page_size = args.page_size
    server = http.server.ThreadingHTTPServer((args.host, args.port), Handler)
    if args.cert:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.cert, args.key)
        server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print(f"Todoistの偽物を起動しました: {'https' if args.cert else 'http'}://{args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
