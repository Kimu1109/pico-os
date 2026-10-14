#!/usr/bin/env python3
"""SpotifyのWeb APIを使うための最初のトークンをPCで取り、SDへ書く(標準ライブラリだけ)。

SpotifyのOAuthには「画面の無い機器向けのフロー(デバイスフロー)」が無く、ログインの後の
戻り先(リダイレクトURI)も HTTPS か 127.0.0.1(ループバック)に限られる。pico-os本体では
受けられないので、**PCのブラウザで1回だけログインして refresh token を取り**、それをSDへ置く。
以後はpico-os(Luaアプリ)が refresh token からアクセストークンを取り直す。

方式は Authorization Code + PKCE(client secret を使わない。本体に秘密を置かずに済む)。

準備(1回だけ):
  1. https://developer.spotify.com/dashboard でアプリを作り、Client ID を控える
  2. そのアプリの Redirect URIs に  http://127.0.0.1:8888/callback  を足す
     (--port を変えたらその番号で。localhost ではなく 127.0.0.1 と書くこと)
  3. 再生の操作(再生/停止/次へ/前へ)は Spotify Premium のアカウントでしか使えない

使い方:
    python3 script/spotify_auth.py --client-id XXXXXXXX
        → ブラウザが開くのでログインして許可する。
          pc/sdcard/lua/apps/Spotify/spotify_auth.json へ書く(PCビルドで試すとき)
    python3 script/spotify_auth.py --client-id XXXXXXXX --sd /media/$USER/PICOSD
        → 本物のSD(USBドライブで繋いだもの等)の lua/apps/Spotify/ へ書く
    python3 script/spotify_auth.py --client-id XXXXXXXX --app-dir lua/apps/音楽リモコン
    python3 script/spotify_auth.py --client-id XXXXXXXX --out ./spotify_auth.json
    python3 script/spotify_auth.py --client-id XXXXXXXX --no-browser
        → ブラウザを開かず、開くURLを表示するだけ(別のPCのブラウザで開いてもよいが、
          戻り先は「このPCの」127.0.0.1 なので、同じPCで開くこと)

書き出すファイル(JSON):
    {"version": 1, "client_id": "...", "refresh_token": "...", "scope": "...",
     "access_token": "...", "expires_at": エポック秒, "obtained_at": エポック秒}

**このファイルは平文で、refresh token はあなたのアカウントを操作できる鍵そのもの。**
Luaアプリは読んだらすぐ pico.store_save(..., {encrypt=true}) で暗号化して持ち直し、
このファイルは pico.sd_remove() で消すこと(SSHの秘密鍵と同じ流儀)。
PKCEの refresh token は使うたびに新しいものが返ることがある(返ったらそれを保存し直す)。
"""
import argparse
import base64
import hashlib
import html
import http.server
import json
import os
import secrets
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
API_URL = "https://api.spotify.com/v1"

# 再生の状態を読む・操作する + プランの確認(/v1/me の product。Premiumかどうか)
DEFAULT_SCOPES = ("user-read-playback-state user-modify-playback-state "
                  "user-read-currently-playing user-read-private")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SD = os.path.join(ROOT, "pc", "sdcard")
DEFAULT_APP_DIR = "lua/apps/Spotify"
FILE_NAME = "spotify_auth.json"


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def make_verifier() -> str:
    """RFC 7636: 43〜128文字の [A-Za-z0-9-._~]。32バイトの乱数で43文字"""
    return b64url(secrets.token_bytes(32))


def make_challenge(verifier: str) -> str:
    """S256: BASE64URL(SHA256(verifier))"""
    return b64url(hashlib.sha256(verifier.encode("ascii")).digest())


def build_authorize_url(auth_url, client_id, redirect_uri, scopes, challenge, state):
    q = {
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": redirect_uri,
        "code_challenge_method": "S256",
        "code_challenge": challenge,
        "scope": scopes,
        "state": state,
    }
    return auth_url + "?" + urllib.parse.urlencode(q)


class CallbackResult:
    def __init__(self):
        self.code = None
        self.error = None
        self.done = threading.Event()


def make_handler(state, path, result):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):  # アクセスログは出さない
            pass

        def _page(self, status, title, body):
            data = ("<!doctype html><meta charset='utf-8'><title>pico-os</title>"
                    "<body style='font-family:sans-serif;margin:2em'><h2>%s</h2><p>%s</p></body>"
                    % (html.escape(title), html.escape(body))).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            u = urllib.parse.urlsplit(self.path)
            if u.path != path:
                self._page(404, "見つかりません", "このページは使いません。")
                return
            q = urllib.parse.parse_qs(u.query)
            # 別のタブ等からの、こちらが始めていない戻りは受けない(CSRF)
            if q.get("state", [""])[0] != state:
                self._page(400, "受け付けられません", "state が一致しません。もう一度やり直してください。")
                return
            if "error" in q:
                result.error = q["error"][0]
                self._page(400, "許可されませんでした", "理由: " + result.error)
            elif "code" in q:
                result.code = q["code"][0]
                self._page(200, "pico-os: ログインできました",
                           "このタブは閉じてかまいません。ターミナルに戻ってください。")
            else:
                result.error = "code がありません"
                self._page(400, "受け付けられません", result.error)
            result.done.set()
    return Handler


def post_form(url, fields, timeout=30):
    data = urllib.parse.urlencode(fields).encode("ascii")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8") or "{}")
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(body)
        except ValueError:
            return e.code, {"error": body[:200]}


def get_json(url, token, timeout=30):
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + token})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, json.loads(r.read().decode("utf-8") or "{}")
    except urllib.error.HTTPError as e:
        return e.code, {}


def output_path(args):
    if args.out:
        return os.path.abspath(args.out)
    rel = args.app_dir.strip("/").replace("/", os.sep)
    return os.path.join(os.path.abspath(args.sd), rel, FILE_NAME)


def write_secret_json(path, obj):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    # 自分だけが読めるように作る(PC側に残る控え。FATのSDでは効かない)
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=1)
        f.write("\n")
    os.replace(tmp, path)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="SpotifyのWeb API用のrefresh tokenをPCで取り、pico-osのSDへ書く(PKCE)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="準備: DashboardのRedirect URIsに http://127.0.0.1:<port>/callback を足すこと。")
    ap.add_argument("--client-id", default=os.environ.get("SPOTIFY_CLIENT_ID"),
                    help="Spotify DashboardのClient ID(環境変数 SPOTIFY_CLIENT_ID でも可)")
    ap.add_argument("--port", type=int, default=8888, help="戻り先 http://127.0.0.1:<port>/callback の番号(既定8888)")
    ap.add_argument("--scopes", default=DEFAULT_SCOPES, help="要求するスコープ(空白区切り)")
    ap.add_argument("--sd", default=DEFAULT_SD, help="SDのルート(既定 pc/sdcard)")
    ap.add_argument("--app-dir", default=DEFAULT_APP_DIR, help="SDのルートからのアプリのフォルダ(既定 lua/apps/Spotify)")
    ap.add_argument("--out", help="書き出すファイルを直接指定する(--sd/--app-dirより優先)")
    ap.add_argument("--no-browser", action="store_true", help="ブラウザを開かず、URLを表示するだけ")
    ap.add_argument("--timeout", type=int, default=300, help="ログインを待つ秒数(既定300)")
    ap.add_argument("--no-check", action="store_true", help="取った後の /v1/me での確認をしない")
    # テスト用(偽物のサーバへ向ける)。ヘルプには出さない
    ap.add_argument("--auth-url", default=AUTH_URL, help=argparse.SUPPRESS)
    ap.add_argument("--token-url", default=TOKEN_URL, help=argparse.SUPPRESS)
    ap.add_argument("--api-url", default=API_URL, help=argparse.SUPPRESS)
    args = ap.parse_args(argv)

    if not args.client_id:
        ap.error("--client-id(または環境変数 SPOTIFY_CLIENT_ID)が要ります")

    out = output_path(args)
    redirect_path = "/callback"
    verifier = make_verifier()
    state = b64url(secrets.token_bytes(16))
    result = CallbackResult()

    try:
        httpd = http.server.HTTPServer(("127.0.0.1", args.port), make_handler(state, redirect_path, result))
    except OSError as e:
        print(f"127.0.0.1:{args.port} で待ち受けられません: {e}", file=sys.stderr)
        print("--port で別の番号にし、DashboardのRedirect URIsも合わせてください", file=sys.stderr)
        return 2
    port = httpd.server_address[1]
    redirect_uri = f"http://127.0.0.1:{port}{redirect_path}"
    url = build_authorize_url(args.auth_url, args.client_id, redirect_uri, args.scopes,
                              make_challenge(verifier), state)

    th = threading.Thread(target=httpd.serve_forever, daemon=True)
    th.start()
    print(f"戻り先: {redirect_uri}  (DashboardのRedirect URIsに同じものが必要です)")
    print("このURLをブラウザで開いてログインし、許可してください:")
    print(url, flush=True)
    if not args.no_browser:
        webbrowser.open(url)

    got = result.done.wait(args.timeout)
    httpd.shutdown()
    httpd.server_close()
    if not got:
        print(f"{args.timeout}秒待ちましたがログインが終わりませんでした", file=sys.stderr)
        return 1
    if result.error:
        print(f"許可されませんでした: {result.error}", file=sys.stderr)
        return 1

    status, tok = post_form(args.token_url, {
        "grant_type": "authorization_code",
        "code": result.code,
        "redirect_uri": redirect_uri,
        "client_id": args.client_id,
        "code_verifier": verifier,
    })
    if status != 200 or "refresh_token" not in tok:
        msg = tok.get("error_description") or tok.get("error") or ""
        print(f"トークンを取れませんでした (HTTP {status}) {msg}", file=sys.stderr)
        return 1

    now = int(time.time())
    obj = {
        "version": 1,
        "client_id": args.client_id,
        "refresh_token": tok["refresh_token"],
        "scope": tok.get("scope", args.scopes),
        "access_token": tok.get("access_token", ""),
        "expires_at": now + int(tok.get("expires_in", 0)),
        "obtained_at": now,
    }

    if not args.no_check and obj["access_token"]:
        st, me = get_json(args.api_url.rstrip("/") + "/me", obj["access_token"])
        if st == 200:
            name = me.get("display_name") or me.get("id") or "?"
            product = me.get("product")
            print(f"ログインしたアカウント: {name}" + (f"(プラン: {product})" if product else ""))
            if product and product != "premium":
                print("注意: 再生の操作(再生/停止/次へ/前へ)は Premium のアカウントでしか使えません", file=sys.stderr)
        else:
            print(f"注意: /v1/me で確かめられませんでした (HTTP {st})", file=sys.stderr)

    write_secret_json(out, obj)
    print(f"書きました: {out}")
    print("このファイルは平文です。SDへ写した後、PC側の控えが要らなければ消してください。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
