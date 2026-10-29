#!/usr/bin/env python3
"""script/build_fw_index.py(Webからのファームの更新の版の一覧を作る)のテスト。

  python3 fw_index_tool_test.py <fw_index_testのパス> <作業ディレクトリ>

- select: gh release list の出力から、新しい順に安定版N本・ナイトリーN本を選ぶ(下書き・他のプレリリースは除く)
- build: 偽の firmware.bin から作った更新ファイルで一覧と置き場を作る(同じ版は安定版に寄せる・壊れたものは飛ばす・
  構成は中身の版の印から・古い印は既定の構成)
- 作った一覧を C++ の fw_index_test(本物の Fw_Index)に読ませ、列の並び・値が両方で同じかを見る
"""
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import build_fw_index as bi  # noqa: E402
import firmware_tool as ft  # noqa: E402
from firmware_tool_test import fake_bin  # noqa: E402

failures = 0


def check(cond, label):
    global failures
    print(("[ OK ] " if cond else "[FAIL] ") + label)
    if not cond:
        failures += 1


def main():
    fw_index_test, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    seed = bytes(range(1, 33))
    quiet = lambda *a, **k: None  # noqa: E731

    # ---- select ----
    releases = [
        {"tagName": "nightly-410-aaaaaaa", "isPrerelease": True, "isDraft": False, "createdAt": "2026-10-20T00:00:00Z"},
        {"tagName": "v405", "isPrerelease": False, "isDraft": False, "createdAt": "2026-10-10T00:00:00Z"},
        {"tagName": "nightly-412-bbbbbbb", "isPrerelease": True, "isDraft": False, "createdAt": "2026-10-22T00:00:00Z"},
        {"tagName": "v412", "isPrerelease": False, "isDraft": False, "createdAt": "2026-10-23T00:00:00Z"},
        {"tagName": "v999", "isPrerelease": False, "isDraft": True, "createdAt": "2026-10-28T00:00:00Z"},
        {"tagName": "beta-1", "isPrerelease": True, "isDraft": False, "createdAt": "2026-10-27T00:00:00Z"},
        {"tagName": "nightly-400-ccccccc", "isPrerelease": True, "isDraft": False, "createdAt": "2026-10-01T00:00:00Z"},
    ]
    picked = bi.select(releases, stable=5, nightly=2)
    check(picked == [("stable", "v412"), ("nightly", "nightly-412-bbbbbbb"), ("nightly", "nightly-410-aaaaaaa"),
                     ("stable", "v405")], f"select: 新しい順・本数・下書きと他のプレリリースは除く({picked})")
    check(bi.select(releases, stable=1, nightly=0) == [("stable", "v412")], "select: 本数を絞る")
    check(bi.select([], 5, 3) == [], "select: リリースが無ければ空")

    # ---- build ----
    def write(name, blob):
        path = os.path.join(work, name)
        with open(path, "wb") as f:
            f.write(blob)
        return path

    s412 = ft.pack(fake_bin(serial=412, git="bbbbbbb", env="rpipico2w"), seed, log=quiet)
    p412_stable = write("v412.pfw", s412)
    p412_nightly = write("n412.pfw", s412)
    p410 = write("n410.pfw", ft.pack(fake_bin(serial=410, git="aaaaaaa+"), None, log=quiet))  # 古い印・署名なし
    p405 = write("v405.pfw", ft.pack(fake_bin(serial=405, git="ddddddd", env="rpipico2w_lite"), seed, log=quiet))
    broken = bytearray(s412)
    broken[-5] ^= 0xFF
    p_broken = write("broken.pfw", bytes(broken))
    p_junk = write("junk.pfw", b"<html>not found</html>")

    out = os.path.join(work, "site", "firmware")
    rows = bi.build([("nightly", p412_nightly), ("stable", p412_stable), ("nightly", p410), ("stable", p405),
                     ("nightly", p_broken), ("nightly", p_junk), ("weird", p410)], out)
    check([(r["serial"], r["channel"]) for r in rows] == [(412, "stable"), (410, "nightly"), (405, "stable")],
          "build: 新しい順・同じ版は安定版の1行・壊れたもの/知らない種類は飛ばす")
    check(rows[0]["env"] == "rpipico2w" and rows[1]["env"] == "rpipico2w" and rows[2]["env"] == "rpipico2w_lite",
          "build: 構成は中身の印から、古い印は既定の構成")
    check(rows[0]["signed"] == 1 and rows[1]["signed"] == 0, "build: 署名の印")
    check(rows[1]["file"] == "pico-os-410-aaaaaaa.pfw" and rows[2]["file"] == "pico-os-405-ddddddd-rpipico2w_lite.pfw",
          f"build: ファイル名は英数字と_だけ・既定でない構成は名前に付ける({rows[1]['file']}, {rows[2]['file']})")
    check(rows[0]["date"] == "2026-10-16 05:22", "build: 日時は中身の印から")
    with open(os.path.join(out, rows[0]["file"]), "rb") as f:
        copied = f.read()
    check(copied == s412 and rows[0]["sha256"] == hashlib.sha256(s412).hexdigest() and rows[0]["bytes"] == len(s412),
          "build: 更新ファイルをそのまま写し、大きさとSHA-256を一覧に書く")
    with open(os.path.join(out, "index.tsv"), "r", encoding="utf-8") as f:
        index_lines = f.read().splitlines()
    check(index_lines[0] == bi.INDEX_MAGIC and len(index_lines) == 4, "build: 1行目は形式の印、続いて3行")
    check(sorted(os.listdir(out)) == sorted(["index.tsv"] + [r["file"] for r in rows]), "build: 置き場には一覧と載せたものだけ")
    empty = os.path.join(work, "empty")
    check(bi.build([], empty) == [] and open(os.path.join(empty, "index.tsv")).read() == bi.INDEX_MAGIC + "\n",
          "build: 何も無くても見出しだけの一覧を作る")

    # ---- C++ の Fw_Index に読ませる ----
    def run_cpp(env, allow_unsigned=False):
        args = [fw_index_test, os.path.join(out, "index.tsv"), env] + (["u"] if allow_unsigned else [])
        r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        return r.returncode, r.stdout.decode("utf-8", "replace").splitlines()

    rc, lines = run_cpp("rpipico2w", allow_unsigned=True)
    check(rc == 0 and lines[:-1] == index_lines[1:3] and lines[-1] == "hidden_env=1 hidden_unsigned=0 bad=0",
          "C++ の Fw_Index が python の一覧を同じ値で読む")
    rc, lines = run_cpp("rpipico2w")
    check(rc == 0 and lines[:-1] == index_lines[1:2] and lines[-1] == "hidden_env=1 hidden_unsigned=1 bad=0",
          "C++: 署名なしは外す")
    rc, lines = run_cpp("rpipico2w_lite")
    check(rc == 0 and lines[:-1] == index_lines[3:4], "C++: 構成が同じものだけ")

    print(f"\n{'FAILED' if failures else 'ALL PASSED'} ({failures} failures)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
