#!/usr/bin/env python3
"""app_store_net_test の配布物を作る(run_net.sh から呼ぶ)。

  python3 app_store_fixture.py <OUT> → <OUT>/variants/<名前>/ に配布物、<OUT>/pubkey.txt に信頼する公開鍵

variants:
  good      serial 100 … tetris 2.0.0 / reversi 2.0.0
  untrusted serial 150 … good と同じ中身を別の鍵で署名
  badsig    serial 150 … good の署名を1ビット壊したもの
  v2        serial 200 … tetris 2.1.0(bgm.mml を消し extra/data.txt を足した)
  old       serial 50  … good と同じ中身(古い番号)
  tampered  serial 300 … v2 と同じ一覧で、配った main.lua だけ後から書き換えたもの
  perms     serial 400 … tetris 2.2.0 の app.cfg は permission_network=true なのに一覧は "-"
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import build_app_repo as b  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "pc", "sdcard", "lua", "apps")


def set_cfg(path, key, value):
    lines = [l for l in open(path, encoding="utf-8").read().splitlines() if not l.startswith(key + "=")]
    lines.append(f"{key}={value}")
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def build(apps_dir, out, seed, serial):
    apps = b.collect_apps(apps_dir, None)
    b.write_index(out, apps, seed, serial)
    for a in apps:
        for rel, _, _ in a.files:
            dst = os.path.join(out, a.id, a.version, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(a.path, rel), dst)


def main():
    out = sys.argv[1]
    seed = bytes(range(1, 33))
    other = bytes(range(101, 133))
    with open(os.path.join(out, "pubkey.txt"), "w") as f:
        f.write(b.ed25519_public(seed).hex())

    src = os.path.join(out, "src")
    shutil.rmtree(src, ignore_errors=True)
    os.makedirs(src)
    for d in ("テトリス", "リバーシ"):
        shutil.copytree(os.path.join(SRC, d), os.path.join(src, d),
                        ignore=shutil.ignore_patterns("store.json*", "settings.cfg", "hiscore.txt"))
    var = os.path.join(out, "variants")
    shutil.rmtree(var, ignore_errors=True)

    build(src, os.path.join(var, "good"), seed, 100)
    build(src, os.path.join(var, "old"), seed, 50)
    build(src, os.path.join(var, "untrusted"), other, 150)
    build(src, os.path.join(var, "badsig"), seed, 150)
    sig_path = os.path.join(var, "badsig", "catalog.sig")
    parts = open(sig_path).read().split()
    s = bytearray(bytes.fromhex(parts[2]))
    s[5] ^= 0x10
    open(sig_path, "w").write(f"{parts[0]} {parts[1]} {bytes(s).hex()}\n")

    tet = os.path.join(src, "テトリス")
    set_cfg(os.path.join(tet, "app.cfg"), "version", "2.1.0")
    os.remove(os.path.join(tet, "bgm.mml"))
    os.makedirs(os.path.join(tet, "extra"))
    open(os.path.join(tet, "extra", "data.txt"), "w").write("v2 data\n")
    build(src, os.path.join(var, "v2"), seed, 200)

    build(src, os.path.join(var, "tampered"), seed, 300)
    main_lua = os.path.join(var, "tampered", "tetris", "2.1.0", "main.lua")
    open(main_lua, "a").write("\n-- tampered\n")

    set_cfg(os.path.join(tet, "app.cfg"), "version", "2.2.0")
    set_cfg(os.path.join(tet, "app.cfg"), "permission_network", "true")
    apps = b.collect_apps(src, None)
    for a in apps:
        if a.id == "tetris":
            a.perms = "-"  # 一覧では権限を隠す(本体は app.cfg と突き合わせて断るはず)
    pout = os.path.join(var, "perms")
    b.write_index(pout, apps, seed, 400)
    for a in apps:
        for rel, _, _ in a.files:
            dst = os.path.join(pout, a.id, a.version, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(a.path, rel), dst)
    print("配布物を作りました:", var)


if __name__ == "__main__":
    main()
