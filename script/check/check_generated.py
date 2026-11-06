#!/usr/bin/env python3
"""src/ 等へ入れてある生成物が、材料から作り直したものと合っているかをまとめて見る(docs/design/devtools.md「生成物の検査」)。

    python3 script/check/check_generated.py              … 全部見る。合わない・確かめられないものがあれば終了コード1
    python3 script/check/check_generated.py --allow-skip … 依存(cairosvg・LovyanGFX)が無くて確かめられないものは飛ばす
    python3 script/check/check_generated.py --skip fonts … 名前で指定したものだけ飛ばす(CIの「層の決まり」。フォントは実機ビルドの job が見る)
    python3 script/check/check_generated.py --list       … 見るものの一覧

中身は各生成器の --check を順に呼ぶだけ(判定は生成器が持つ)。新しい生成器を足したら --check を付けて下の CHECKS へ1行足す。
CI(host-tests.yml の「層の決まり」)は cairosvg を入れてから --skip fonts で回す(--allow-skip だと依存の入れ損ないを見逃す)。
フォントの分割(split_font)は LovyanGFX の本体が要るので、PCビルドの configure か PlatformIO の取得の後でないと確かめられない
(CIでは firmware-build.yml が PlatformIO の取得先を渡して別に回す)。
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CODEGEN = os.path.join(ROOT, "script", "codegen")

# (名前, 生成物, 呼ぶもの)。呼んだものが終了コード2を返したら「依存が無くて確かめられない」
CHECKS = [
    ("lua_builtins", "src/lua/LuaBuiltinModules.hpp", ["gen_lua_builtins.py", "--check"]),
    ("file_share_page", "src/net/File_Share_Page.cpp", ["gen_file_share_page.py", "--check"]),
    ("i18n", "i18n/*.po・src/system/I18n_Table_*.cpp・同梱アプリの lang/en.lua", ["i18n.py", "--check"]),
    ("jis_table", "src/apps/mail/Mail_Jis_Data.hpp", ["generate_jis_table.py", "--check"]),
    ("tls_roots", "src/net/Tls_Roots_Data.hpp(ROOTS との並び・枚数だけ)", ["generate_tls_roots.py", "--check"]),
    ("icons", "src/gui/icons/icons_data.h(要 cairosvg・pillow)", ["generate_icons.py", "--check"]),
    ("fonts", "src/gui/fonts/・pc/sdcard/sys/fonts/(要 LovyanGFX の本体)", None),
]

LGFX_FONT = [
    os.path.join(ROOT, "pc", "build*", "_deps", "lovyangfx-src", "src", "lgfx", "Fonts", "IPA", "lgfx_font_japan.c"),
    os.path.join(ROOT, ".pio", "libdeps", "*", "LovyanGFX", "src", "lgfx", "Fonts", "IPA", "lgfx_font_japan.c"),
]


def command(name, args):
    if name == "fonts":
        found = sorted(p for pat in LGFX_FONT for p in glob.glob(pat))
        if not found:
            return None  # 依存が無い
        return [sys.executable, os.path.join(CODEGEN, "font", "split_font.py"), "--check", "--lgfx", found[0]]
    return [sys.executable, os.path.join(CODEGEN, args[0])] + args[1:]


def main(argv):
    if "--list" in argv:
        for name, what, _ in CHECKS:
            print(f"{name:16} {what}")
        return 0
    allow_skip = "--allow-skip" in argv
    skip = set()
    for i, a in enumerate(argv):
        if a == "--skip" and i + 1 < len(argv):
            skip |= set(argv[i + 1].split(","))
    unknown = skip - {name for name, _, _ in CHECKS}
    if unknown:
        print(f"--skip に知らない名前があります: {' '.join(sorted(unknown))}(--list で一覧)")
        return 2
    failed, skipped = [], []
    for name, what, args in CHECKS:
        if name in skip:
            print(f"-     {name}: --skip で飛ばしました")
            continue
        cmd = command(name, args)
        if cmd is None:
            rc, out = 2, "LovyanGFX の lgfx_font_japan.c が見つかりません(PCビルドの configure か pio run の後で回す)"
        else:
            r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
            rc, out = r.returncode, (r.stdout + r.stderr).strip()
        last = out.splitlines()[-1] if out else ""
        if rc == 0:
            print(f"ok    {name}: {last}")
        elif rc == 2:
            skipped.append(name)
            print(f"skip  {name}: 依存が無くて確かめられません — {last}")
        else:
            failed.append(name)
            print(f"NG    {name}:\n" + "\n".join("      " + line for line in out.splitlines()))
    if failed:
        print(f"\n合っていない生成物があります: {' '.join(failed)}")
        return 1
    if skipped and not allow_skip:
        print(f"\n確かめられなかったもの: {' '.join(skipped)}(依存を入れるか --allow-skip)")
        return 1
    print("\n生成物は全部材料と合っています" + (f"(飛ばしたもの: {' '.join(skipped)})" if skipped else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
