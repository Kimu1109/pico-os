#!/usr/bin/env python3
"""
reference_server_test.py
------------------------
ドキュメントサーバの参照実装(script/tools/reference_server.py)を、ベースパスの下に置いたときのテスト。
run_net.sh から呼ぶ(サーバを同じプロセスの中で 127.0.0.1 の空きポートに立て、本物のHTTPで叩く)。

見ること(Apache の後ろで https://example.com/docs/ に置く形):
  - discovery は /.well-known/pico-os(Apache がホストの頭から転送する)でも /docs/.well-known/pico-os でも返り、
    home・search・manifest の行はベースパス付き
  - 文書はベースパス付きでも、頭を外されて来ても(Apache が外したとき)取れる。/docs は /docs/ へ、/docs/ は home へ回す
  - 検索結果・マニフェストの path はベースパス付きで、そのまま GET できる。マニフェストは昇順のまま
  - ベースパスなしなら今までと同じ(パスに何も足さない)

使い方: python3 reference_server_test.py
"""

import argparse
import http.client
import importlib.util
import os
import sys
import threading
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
SERVER_PY = os.path.join(ROOT, "script", "tools", "reference_server.py")

spec = importlib.util.spec_from_file_location("reference_server", SERVER_PY)
rs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rs)

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


def start(base_path):
    opts = argparse.Namespace(
        name="test", home="/doc.md", discovery=True, search_path="/v1/search", manifest_path="/v1/manifest",
        base_path=base_path, trust_proxy=True)
    index = rs.DocIndex(os.path.join(ROOT, "examples"))
    rs.Handler.log_message = lambda self, fmt, *args: None  # テストの出力を汚さない
    srv = rs.Server(("127.0.0.1", 0), rs.Handler, index, opts)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]


def get(port, path, headers=None):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    conn.request("GET", path, headers=headers or {})
    res = conn.getresponse()
    body = res.read()
    conn.close()
    return res, body


def tsv(body):
    return [line.split("\t") for line in body.decode("utf-8").split("\n") if line and not line.startswith("#")]


def test_base_path():
    print("\n---- ベースパス(/docs)の下 ----")
    eq(rs.normalize_base_path("docs/"), "/docs", "ベースパスは /docs の形へ揃える")
    eq(rs.normalize_base_path("/"), "", "/ だけならルート")
    for bad in ("/../x", "/a b"):
        try:
            rs.normalize_base_path(bad)
            check(False, "使えないベースパスは断る: %r" % bad)
        except ValueError:
            check(True, "使えないベースパスは断る: %r" % bad)

    srv, port = start("/docs")
    for path in ("/.well-known/pico-os", "/docs/.well-known/pico-os"):
        res, body = get(port, path)
        info = {r[0]: r[1] for r in tsv(body)}
        eq((res.status, info.get("home"), info.get("search"), info.get("manifest")),
           (200, "/docs/doc.md", "/docs/v1/search", "/docs/v1/manifest"), "discovery(%s)はベースパス付き" % path)

    res, body = get(port, "/docs/doc.md")
    eq(res.status, 200, "文書をベースパス付きで取れる")
    etag = res.getheader("ETag")
    eq(get(port, "/doc.md")[0].status, 200, "頭を外されて来ても(Apache が外したとき)取れる")
    eq(get(port, "/docs/doc.md", {"If-None-Match": etag})[0].status, 304, "条件付きGETも効く")
    res, _ = get(port, "/docs")
    eq((res.status, res.getheader("Location")), (301, "/docs/"), "/docs は /docs/ へ回す")
    res, _ = get(port, "/docs/")
    eq((res.status, res.getheader("Location")), (302, "/docs/doc.md"), "/docs/ は home(ベースパス付き)へ回す")

    res, body = get(port, "/docs/v1/manifest")
    rows = tsv(body)
    paths = [r[0] for r in rows]
    check(res.status == 200 and paths and all(p.startswith("/docs/") for p in paths), "マニフェストの path はベースパス付き")
    eq(paths, sorted(paths, key=lambda p: p.encode("utf-8")), "マニフェストは昇順のまま")
    first_path, first_ver = rows[0][0], rows[0][1]
    res, _ = get(port, urllib.parse.quote(first_path))
    eq((res.status, res.getheader("ETag").strip('"')), (200, first_ver), "マニフェストの path をそのまま GET でき、ETag が一致する")

    res, body = get(port, "/docs/v1/search?q=" + urllib.parse.quote("pico"))
    rows = tsv(body)
    check(res.status == 200 and rows and all(r[0].startswith("/docs/") for r in rows), "検索結果の path はベースパス付き")
    eq(get(port, urllib.parse.quote(rows[0][0]))[0].status, 200, "検索結果の path をそのまま GET できる")
    eq(get(port, "/docs/../README.md")[0].status, 404, "文書ルートの外へは出られない")
    srv.shutdown()

    print("\n---- ベースパスなし ----")
    srv, port = start("")
    info = {r[0]: r[1] for r in tsv(get(port, "/.well-known/pico-os")[1])}
    eq((info.get("home"), info.get("search")), ("/doc.md", "/v1/search"), "ベースパスなしなら今までと同じ")
    check(all(not r[0].startswith("/docs/") for r in tsv(get(port, "/v1/manifest")[1])), "マニフェストにも何も足さない")
    srv.shutdown()


def main():
    test_base_path()
    print("\n%s (失敗 %d件)" % ("ALL PASSED" if failures == 0 else "FAILED", failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
