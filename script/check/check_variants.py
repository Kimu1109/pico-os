#!/usr/bin/env python3
"""実機の版(buildcfg/*.ini の env)の旗を、PCビルドの読み取り器(pc/cmake/PicoVariant.cmake)が
PlatformIO と同じに読めているかを確かめる(docs/design/devtools.md「ビルドの構成の置き場所」)。

    python3 script/check/check_variants.py      … 食い違いがあれば一覧を出して終了コード1

PlatformIO の解釈は `pio project config --json-output` から取り、土台(env:rpipico2w)の build_flags を
引いた残りを版の旗とする。読み取り器は buildcfg/ の書き方の決まりの範囲しか読まないので、決まりの外の
書き方(${…} の入れ子・-D 以外の旗 等)を足すとここか CMake のどちらかで止まる。
CI(firmware-build.yml)では PlatformIO を用意したあとに回す。pio と cmake が要る。
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE = "env:rpipico2w"
DEFINE = re.compile(r"^-D([A-Za-z_][A-Za-z0-9_]*)(?:=(.*))?$")


def pio_envs():
    out = subprocess.run(["pio", "project", "config", "--json-output"], cwd=ROOT,
                         check=True, capture_output=True, text=True).stdout
    envs = {}
    for section, opts in json.loads(out):
        if section.startswith("env:"):
            flags = dict(opts).get("build_flags", [])
            envs[section] = flags if isinstance(flags, list) else flags.split()
    return envs


def to_defines(flags, where):
    result = []
    for f in flags:
        m = DEFINE.match(f)
        if not m:
            raise SystemExit(f"{where}: -DNAME[=VAL] 以外の旗があります(PCビルドでは読めない): {f}")
        result.append(f"{m.group(1)}={m.group(2) if m.group(2) is not None else '1'}")
    return result


def cmake_defines(env):
    r = subprocess.run(["cmake", f"-DPICOOS_VARIANT={env}", "-P", "pc/cmake/PicoVariant.cmake"],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"{env}: pc/cmake/PicoVariant.cmake が失敗しました\n{r.stderr}")
    return [line for line in r.stdout.splitlines() if line]


def main():
    envs = pio_envs()
    base = envs.get(BASE)
    if base is None:
        raise SystemExit(f"{BASE} が見つかりません")
    bad = 0
    for section, flags in sorted(envs.items()):
        env = section[len("env:"):]
        if flags[:len(base)] != base:
            print(f"NG {env}: build_flags の先頭が ${{{BASE}.build_flags}} ではありません")
            bad += 1
            continue
        want = to_defines(flags[len(base):], section)
        got = cmake_defines(env)
        if sorted(want) != sorted(got):
            print(f"NG {env}\n  PlatformIO: {' '.join(sorted(want))}\n  CMake:      {' '.join(sorted(got))}")
            bad += 1
        else:
            print(f"ok {env}: {' '.join(want) or '(土台のまま)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
