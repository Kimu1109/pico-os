#!/usr/bin/env python3
"""文字起こしのAPI(OpenAI互換)の偽物(録音アプリの文字起こしの結合テスト・PCビルドでの動作確認の相手)。標準ライブラリだけで動く。

本物(https://api.openai.com/v1/audio/transcriptions)のうち、pico-os の TranscribeClient が使うところだけを真似る:

    POST /v1/audio/transcriptions   multipart/form-data(model / language / prompt / response_format / file)

- 認証は `Authorization: Bearer <--key>`。違えば401(OpenAIと同じ形のJSON: {"error":{"message":…}})
- response_format=text なら本文そのもの(text/plain)を返す。中身は受け取ったものの要約と、決まった日本語の文:
      model=<model> language=<language または (none)> prompt=<prompt または (none)> name=<filename> bytes=<file の長さ> sha256=<file のSHA-256>
      今日は晴れです。録音のテストをしています。
- file が --max-bytes を超えると、本文を読み終える前に413を返す(送り終える前に返事が来る場合の確認)
- prompt が "fail500" なら500({"error":"…"}。whisper.cpp のサーバの形)

    python3 transcribe_fake_server.py --port 8160 --key sk-test [--max-bytes 1000000]
                                      [--cert server.pem --key-file server.key]

PCビルドで試すときは /sys/transcribe.cfg に `api = http://127.0.0.1:8160/v1`、/sys/transcribe_key.cfg に `sk-test` の1行を置く。
"""
import argparse
import hashlib
import http.server
import json
import socket
import ssl

SENTENCE = "今日は晴れです。録音のテストをしています。"


def parse_multipart(body, boundary):
    """multipart/form-data を {name: (filename, bytes)} にする(email/cgi に頼らない素朴な実装)"""
    delim = b"--" + boundary
    parts = body.split(delim)
    out = {}
    # 先頭は空、最後は "--\r\n"
    if not parts or parts[0] not in (b"", b"\r\n"):
        raise ValueError("先頭が区切りではない")
    if not parts[-1].startswith(b"--"):
        raise ValueError("終わりの区切りが無い")
    for p in parts[1:-1]:
        if not p.startswith(b"\r\n") or not p.endswith(b"\r\n"):
            raise ValueError("部分の前後が CRLF ではない")
        p = p[2:-2]
        head, sep, data = p.partition(b"\r\n\r\n")
        if not sep:
            raise ValueError("部分の見出しの終わりが無い")
        name = filename = None
        for line in head.split(b"\r\n"):
            k, _, v = line.decode("utf-8").partition(":")
            if k.strip().lower() == "content-disposition":
                for item in v.split(";"):
                    item = item.strip()
                    if item.startswith('name="'):
                        name = item[6:-1]
                    elif item.startswith('filename="'):
                        filename = item[10:-1]
        if name is None:
            raise ValueError("name が無い")
        out[name] = (filename, data)
    return out


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "fake-transcribe/1"

    def log_message(self, fmt, *args):
        print("  " + fmt % args, flush=True)

    def reply(self, status, body, ctype):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)
        self.wfile.flush()
        self.close_connection = True

    def drain(self, length):
        # 返事を送った後、残りの本文を短い時間だけ読み捨ててから閉じる(相手の送信がRSTで途切れないように)
        try:
            self.connection.settimeout(2.0)
            while length > 0:
                chunk = self.rfile.read(min(65536, length))
                if not chunk:
                    break
                length -= len(chunk)
        except (socket.timeout, OSError):
            pass

    def do_POST(self):
        cfg = self.server.cfg
        length = int(self.headers.get("Content-Length", "0"))
        if self.path != "/v1/audio/transcriptions":
            self.drain(length)
            return self.reply(404, json.dumps({"error": {"message": "Not found"}}), "application/json")
        if self.headers.get("Authorization", "") != "Bearer " + cfg.key:
            self.reply(401, json.dumps({"error": {"message": "Incorrect API key provided", "type": "invalid_request_error"}}),
                       "application/json")
            return self.drain(length)
        if cfg.max_bytes and length > cfg.max_bytes:
            # 本文を読む前に断る
            self.reply(413, json.dumps({"error": {"message": "Maximum content size limit exceeded"}}), "application/json")
            return self.drain(length)
        ctype = self.headers.get("Content-Type", "")
        if not ctype.startswith("multipart/form-data; boundary="):
            self.drain(length)
            return self.reply(400, json.dumps({"error": {"message": "Content-Type: " + ctype}}), "application/json")
        boundary = ctype.split("boundary=", 1)[1].encode()
        body = self.rfile.read(length)
        try:
            form = parse_multipart(body, boundary)
        except ValueError as e:
            return self.reply(400, json.dumps({"error": {"message": "multipart: " + str(e)}}), "application/json")

        def field(name):
            return form[name][1].decode("utf-8") if name in form else None

        if "file" not in form or field("model") is None:
            return self.reply(400, json.dumps({"error": {"message": "file と model が要ります"}}), "application/json")
        if field("prompt") == "fail500":
            return self.reply(500, json.dumps({"error": "failed to process audio"}), "application/json")
        filename, data = form["file"]
        summary = "model=%s language=%s prompt=%s name=%s bytes=%d sha256=%s" % (
            field("model"), field("language") or "(none)", field("prompt") or "(none)", filename,
            len(data), hashlib.sha256(data).hexdigest())
        if field("response_format") == "text":
            return self.reply(200, summary + "\n" + SENTENCE + "\n", "text/plain; charset=utf-8")
        return self.reply(200, json.dumps({"text": summary + "\n" + SENTENCE}), "application/json")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8160)
    ap.add_argument("--key", default="sk-test")
    ap.add_argument("--max-bytes", type=int, default=0)
    ap.add_argument("--cert")
    ap.add_argument("--key-file")
    args = ap.parse_args()

    srv = http.server.ThreadingHTTPServer((args.host, args.port), Handler)
    srv.cfg = args
    if args.cert:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.cert, args.key_file)
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print(f"文字起こしの偽物を起動しました: {'https' if args.cert else 'http'}://{args.host}:{args.port}/v1", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
