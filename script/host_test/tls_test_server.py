#!/usr/bin/env python3
"""run_net.sh のHTTPS結合テスト(calendar_sync_test)の相手をする使い捨てサーバ。

標準ライブラリだけで動く。証明書は run_net.sh が openssl コマンドで毎回作る。

    python3 tls_test_server.py --port 8443 --cert server.pem --key server.key

わざと「一般のHTTPSサーバらしい」応答を返す(参照実装サーバはPROTOCOL.mdに従う行儀の良い
相手なので、それでは確かめられないものを見るため):
  - /cal.ics  … chunked転送 + ETag。If-None-Match が一致すれば 304
  - /html     … 200 だが中身はHTML(ログイン画面を返された、のような状況)
  - /missing  … 404
  - それ以外  … 404
長いヘッダ(Set-Cookie)も付ける。pico-os側が読まないヘッダで失敗しないことの確認。
"""
import argparse
import http.server
import ssl
import sys

ICS = (
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//pico-os//tls-test//JA\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:tls-test@pico-os\r\n"
    "DTSTART;VALUE=DATE:20260923\r\n"
    "DTEND;VALUE=DATE:20260924\r\n"
    "SUMMARY:HTTPSで取ってきた予定\r\n"
    "END:VEVENT\r\n"
    "END:VCALENDAR\r\n"
).encode("utf-8")

ETAG = '"v1"'


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[tls-test] " + (fmt % args) + "\n")

    def _common_headers(self):
        self.send_header("Set-Cookie", "x=" + "a" * 600 + "; Path=/; Secure")
        self.send_header("Connection", "close")

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/cal.ics":
            if self.headers.get("If-None-Match") == ETAG:
                self.send_response(304)
                self.send_header("ETag", ETAG)
                self._common_headers()
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/calendar; charset=utf-8")
            self.send_header("ETag", ETAG)
            self.send_header("Transfer-Encoding", "chunked")
            self._common_headers()
            self.end_headers()
            # 小さめのチャンクに割って送る
            for i in range(0, len(ICS), 37):
                part = ICS[i:i + 37]
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
            return
        if path == "/html":
            body = b"<!doctype html><html><body>login</body></html>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self._common_headers()
            self.end_headers()
            self.wfile.write(body)
            return
        body = b"not found"
        self.send_response(404)
        self.send_header("Content-Length", str(len(body)))
        self._common_headers()
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    args = parser.parse_args()

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(args.cert, args.key)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print("起動しました", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
