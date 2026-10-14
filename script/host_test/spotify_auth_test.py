#!/usr/bin/env python3
"""script/spotify_auth.py のテスト。本物のSpotifyへは行かない。

- PKCE(RFC 7636 付録Bの値)と、認可URLの組み立て
- 偽物のトークン/APIのサーバを 127.0.0.1 に立て、スクリプトを --no-browser で動かし、
  表示されたURLの戻り先へ「ブラウザの代わりに」code を返して、書き出したJSONを確かめる
  (トークンの交換に送った値・state の照合・拒否・トークンの失敗)
"""
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPT = os.path.join(ROOT, "script", "spotify_auth.py")
sys.path.insert(0, os.path.join(ROOT, "script"))
import spotify_auth  # noqa: E402

failures = 0


def check(cond, msg):
    global failures
    print(("[ OK ] " if cond else "[FAIL] ") + msg)
    if not cond:
        failures += 1


# ---- PKCE ----
v = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"
check(spotify_auth.make_challenge(v) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "PKCE: RFC 7636 付録Bの値")
mv = spotify_auth.make_verifier()
check(43 <= len(mv) <= 128 and all(c.isalnum() or c in "-._~" for c in mv), "PKCE: verifierの長さと文字")
check(spotify_auth.make_verifier() != mv, "PKCE: 毎回違うverifier")
u = urllib.parse.urlsplit(spotify_auth.build_authorize_url(
    "https://a.example/authorize", "cid", "http://127.0.0.1:9/callback", "s1 s2", "CH", "ST"))
q = urllib.parse.parse_qs(u.query)
check(q["response_type"] == ["code"] and q["code_challenge_method"] == ["S256"] and q["code_challenge"] == ["CH"]
      and q["scope"] == ["s1 s2"] and q["state"] == ["ST"] and q["redirect_uri"] == ["http://127.0.0.1:9/callback"],
      "認可URL: 必要な値が全部入る")


# ---- 偽物のトークン/APIのサーバ ----
class Fake:
    token_requests = []
    token_status = 200
    product = "premium"


class FakeHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _json(self, status, obj):
        data = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        form = urllib.parse.parse_qs(self.rfile.read(n).decode())
        Fake.token_requests.append(form)
        if Fake.token_status != 200:
            self._json(Fake.token_status, {"error": "invalid_grant", "error_description": "Invalid authorization code"})
            return
        self._json(200, {"access_token": "AT", "token_type": "Bearer", "expires_in": 3600,
                         "refresh_token": "RT", "scope": "user-modify-playback-state"})

    def do_GET(self):
        if self.path == "/v1/me" and self.headers.get("Authorization") == "Bearer AT":
            self._json(200, {"display_name": "テスト", "product": Fake.product})
        else:
            self._json(401, {})


fake = http.server.HTTPServer(("127.0.0.1", 0), FakeHandler)
threading.Thread(target=fake.serve_forever, daemon=True).start()
FAKE = f"http://127.0.0.1:{fake.server_address[1]}"


def run_flow(out, answer, extra=()):
    """スクリプトを動かし、表示された認可URLに answer(state, redirect) -> 戻り先のクエリ を返す"""
    p = subprocess.Popen([sys.executable, SCRIPT, "--client-id", "CID", "--port", "0", "--no-browser",
                          "--timeout", "20", "--out", out,
                          "--auth-url", FAKE + "/authorize", "--token-url", FAKE + "/api/token",
                          "--api-url", FAKE + "/v1", *extra],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    url = None
    for line in p.stdout:
        if line.startswith(FAKE + "/authorize?"):
            url = line.strip()
            break
    if not url:
        p.kill()
        return p.wait(), "", "URLが表示されない", {}
    q = urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
    redirect = q["redirect_uri"][0]
    for query in answer(q["state"][0], redirect):
        try:
            urllib.request.urlopen(redirect + "?" + urllib.parse.urlencode(query), timeout=10).read()
        except urllib.error.HTTPError:
            pass
    out_s, err_s = p.communicate(timeout=30)
    return p.returncode, out_s, err_s, q


with tempfile.TemporaryDirectory() as d:
    # 正常: 違うstateの戻りは無視され、正しいstateのcodeで交換する
    out = os.path.join(d, "sd", "lua", "apps", "Spotify", "spotify_auth.json")
    rc, so, se, q = run_flow(out, lambda st, r: [{"state": "wrong", "code": "BAD"}, {"state": st, "code": "C1"}])
    check(rc == 0, "流れ: 成功する " + se.strip())
    check(os.path.exists(out), "流れ: 指定した場所へ書く(フォルダも作る)")
    obj = json.load(open(out, encoding="utf-8")) if os.path.exists(out) else {}
    check(obj.get("refresh_token") == "RT" and obj.get("client_id") == "CID" and obj.get("access_token") == "AT",
          "流れ: refresh_token/client_id/access_tokenが入る")
    check(obj.get("version") == 1 and obj.get("expires_at", 0) - obj.get("obtained_at", 0) == 3600, "流れ: 期限")
    check((os.stat(out).st_mode & 0o077) == 0, "流れ: 自分だけが読めるファイル")
    check(len(Fake.token_requests) == 1, "流れ: 違うstateの戻りではトークンを取りに行かない")
    tr = Fake.token_requests[0] if Fake.token_requests else {}
    check(tr.get("grant_type") == ["authorization_code"] and tr.get("code") == ["C1"] and tr.get("client_id") == ["CID"],
          "交換: grant_type/code/client_id")
    check(tr.get("redirect_uri") == q.get("redirect_uri"), "交換: 認可URLと同じredirect_uri")
    check("code_verifier" in tr and spotify_auth.make_challenge(tr["code_verifier"][0]) == q["code_challenge"][0],
          "交換: verifierがchallengeと対になる")
    check("client_secret" not in tr, "交換: client secretは送らない")
    check("テスト" in so and "premium" in so, "確認: /v1/meのアカウント名とプランを出す")

    # Premiumでなければ注意を出す(失敗にはしない)
    Fake.product = "free"
    out2 = os.path.join(d, "free.json")
    rc, so, se, _ = run_flow(out2, lambda st, r: [{"state": st, "code": "C2"}])
    check(rc == 0 and "Premium" in se, "確認: Premiumでなければ注意を出す")
    Fake.product = "premium"

    # 拒否された
    out3 = os.path.join(d, "denied.json")
    n = len(Fake.token_requests)
    rc, so, se, _ = run_flow(out3, lambda st, r: [{"state": st, "error": "access_denied"}])
    check(rc != 0 and "access_denied" in se and not os.path.exists(out3), "拒否: 失敗して何も書かない")
    check(len(Fake.token_requests) == n, "拒否: トークンを取りに行かない")

    # トークンの交換に失敗
    Fake.token_status = 400
    out4 = os.path.join(d, "bad.json")
    rc, so, se, _ = run_flow(out4, lambda st, r: [{"state": st, "code": "C4"}])
    check(rc != 0 and "Invalid authorization code" in se and not os.path.exists(out4), "交換の失敗: 理由を出して何も書かない")
    Fake.token_status = 200

    # 既定の書き出し先は <sd>/lua/apps/Spotify/spotify_auth.json
    args = spotify_auth.argparse.Namespace(out=None, sd=os.path.join(d, "card"), app_dir="/lua/apps/Spotify/")
    check(spotify_auth.output_path(args) == os.path.join(d, "card", "lua", "apps", "Spotify", "spotify_auth.json"),
          "書き出し先: --sd と --app-dir から組み立てる")

    # client id が無ければ使い方の誤り
    r = subprocess.run([sys.executable, SCRIPT, "--no-browser"], capture_output=True, text=True,
                       env={k: v for k, v in os.environ.items() if k != "SPOTIFY_CLIENT_ID"})
    check(r.returncode == 2 and "client-id" in r.stderr, "引数: --client-idが無ければ断る")

fake.shutdown()
print("")
if failures:
    print(f"FAILED (failures={failures})")
    sys.exit(1)
print("ALL PASSED (failures=0)")
