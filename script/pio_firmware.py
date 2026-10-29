# PlatformIO の追加スクリプト(platformio.ini の extra_scripts = pre:script/pio_firmware.py)。
# 2. のターゲットは PROGNAME が決まった後に足す必要があるので post:script/pio_firmware_targets.py に分けてある。
#
# 1. ファームの版を $BUILD_DIR/pico_build_info/Pico_Build_Info.h へ書く(src/apps/firmware/Fw_Version.cpp だけが読む)。
#    通し番号 = git rev-list --count HEAD、git = 短いコミットのハッシュ(手元に変更があれば末尾に'+')、
#    日時 = HEADのコミットの日時(UTC)、構成 = env の名前(PIOENV。Webの版の一覧が同じ構成のものを選ぶのに使う)。
#    中身が変わったときだけ書き直すので、変更が無ければビルドは「最新」のまま。
#    gitが無い/リポジトリでない場合は 0 / "nogit"。
# 2. ファームの更新ファイルを作る・送るターゲット(docs/FIRMWARE_UPDATE.md):
#      pio run -e rpipico2w -t fwpack                       … .pio/build/rpipico2w/firmware.pfw を作る
#      pio run -e rpipico2w -t fwpush --upload-port <IP>    … Wi-Fiで本体へ送る(本体で[受け取る]を押しておく)
#    鍵は環境変数 PICOOS_FW_KEY か ~/.pico-os/firmware.key(無ければ署名なし)。
import os
import subprocess

Import("env")  # noqa: F821  (PlatformIOが用意する)

PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
BUILD_DIR = env.subst("$BUILD_DIR")  # noqa: F821


def git(*args):
    try:
        out = subprocess.run(["git"] + list(args), cwd=PROJECT_DIR, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, check=True, env=dict(os.environ, TZ="UTC"))
        return out.stdout.decode("utf-8", "replace").strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def build_info():
    serial = git("rev-list", "--count", "HEAD")
    short = git("rev-parse", "--short=7", "HEAD")
    if serial is None or short is None:
        return 0, "nogit", ""
    # 手元の変更(追跡しているファイルだけ。.pio 等の未追跡は見ない)
    dirty = git("status", "--porcelain", "--untracked-files=no")
    date = git("log", "-1", "--format=%cd", "--date=format-local:%Y-%m-%d %H:%M") or ""
    return int(serial), short + ("+" if dirty else ""), date


def write_header():
    serial, short, date = build_info()
    pioenv = env.subst("$PIOENV")  # noqa: F821
    text = (
        "// script/pio_firmware.py が書く(手で直さない)\n"
        "#pragma once\n"
        f"#define PICOOS_BUILD_SERIAL {serial}\n"
        f"#define PICOOS_BUILD_GIT \"{short}\"\n"
        f"#define PICOOS_BUILD_DATE \"{date}\"\n"
        f"#define PICOOS_BUILD_ENV \"{pioenv}\"\n"
    )
    out_dir = os.path.join(BUILD_DIR, "pico_build_info")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "Pico_Build_Info.h")
    old = None
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            old = f.read()
    if old != text:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    print(f"pico-os ファームの版: #{serial} ({short}) {date} [{pioenv}]")
    return out_dir


env.Append(CPPPATH=[write_header()])  # noqa: F821
