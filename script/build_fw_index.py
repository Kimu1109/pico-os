#!/usr/bin/env python3
"""ファームの版の一覧(Webからの更新。docs/FIRMWARE_UPDATE.md「Webから取る」)を作る。
標準ライブラリだけで動く。CI(.github/workflows/web-pages.yml)が GitHub Pages の /firmware/ を作るのに使う。

  # 1. どのリリースを載せるか選ぶ(gh release list --json の出力から。新しい順に安定版N本・ナイトリーN本)
  gh release list --limit 200 --json tagName,isPrerelease,isDraft,createdAt > releases.json
  python3 script/build_fw_index.py select releases.json --stable 5 --nightly 3 > picked.tsv   # 「種類<TAB>タグ」

  # 2. 選んだリリースの firmware.pfw(と firmware.uf2)を落とし、
  #    「種類<TAB>更新ファイル[<TAB>.uf2(無ければ -)[<TAB>タグ]]」を並べて一覧と置き場を作る
  python3 script/build_fw_index.py build got.tsv --out _site/firmware

置き場には index.tsv と、更新ファイルを pico-os-<通し番号>-<git>.pfw の名前で写したものを置く。
.uf2 を渡した版は、同じ名前で拡張子だけ .uf2 にしたものも置く(Webのダウンロードのページ用。本体は読まない)。
Webのダウンロードのページ(pc/web/download.html)が読む downloads.json も書く(形は write_downloads())。
読めない・壊れた更新ファイルは飛ばす(警告だけ出す。古いリリースが1本壊れていても公開を止めない)。
同じ版(通し番号・git・構成が同じ)が安定版とナイトリーの両方にあれば、安定版として1行にする。

一覧の形式(本体は src/apps/firmware/Fw_Index。値を変えるときは両方を揃えること):

  # pico-os-fw-index 1
  <通し番号>\\t<git>\\t<日時(UTC)>\\t<stable|nightly>\\t<構成>\\t<署名 1|0>\\t<バイト数>\\t<SHA-256>\\t<ファイル名>
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import firmware_tool as ft  # noqa: E402

INDEX_MAGIC = "# pico-os-fw-index 1"
INDEX_NAME = "index.tsv"
DOWNLOADS_NAME = "downloads.json"
CHANNELS = ("stable", "nightly")
NIGHTLY_PREFIX = "nightly-"
# env= の無い(2026-10-29より前の)ファームは、ナイトリーが既定の構成しか配っていなかったので既定の構成とみなす
DEFAULT_ENV = "rpipico2w"


def warn(text):
    print(f"warning: {text}", file=sys.stderr)


# ---------------------------------------------------------------------------
# 選ぶ
# ---------------------------------------------------------------------------
def select(releases, stable, nightly):
    """gh release list --json tagName,isPrerelease,isDraft,createdAt の一覧から (種類, タグ) を新しい順に"""
    rows = [r for r in releases if not r.get("isDraft")]
    rows.sort(key=lambda r: r.get("createdAt", ""), reverse=True)
    out = []
    n_stable = n_nightly = 0
    for r in rows:
        tag = r.get("tagName", "")
        if tag.startswith(NIGHTLY_PREFIX):
            if n_nightly < nightly:
                out.append(("nightly", tag))
                n_nightly += 1
        elif not r.get("isPrerelease"):
            if n_stable < stable:
                out.append(("stable", tag))
                n_stable += 1
    return out


def cmd_select(args):
    with open(args.releases, "r", encoding="utf-8") as f:
        releases = json.load(f)
    for channel, tag in select(releases, args.stable, args.nightly):
        print(f"{channel}\t{tag}")


# ---------------------------------------------------------------------------
# 作る
# ---------------------------------------------------------------------------
SAFE_RE = re.compile(r"[^A-Za-z0-9_]")


def file_name(serial, git, env):
    name = f"pico-os-{serial}-{SAFE_RE.sub('', git) or 'nogit'}"
    if env != DEFAULT_ENV:
        name += "-" + SAFE_RE.sub("", env)
    return name + ".pfw"


def read_entry(channel, path, uf2=None, tag=None):
    """更新ファイル1つを一覧の1行ぶんにする。載せられなければ None(理由は警告に出す)。
    uf2 は同じリリースの firmware.uf2(無ければ None)、tag はリリースのタグ(分からなければ None)"""
    try:
        with open(path, "rb") as f:
            blob = f.read()
        p = ft.parse(blob)
    except (OSError, ValueError) as e:
        warn(f"{path}: 読めません({e})")
        return None
    if p["board"] != ft.BOARD:
        warn(f"{path}: 別の基板向けです({p['board']})")
        return None
    if len(p["payload"]) != p["payload_bytes"] or hashlib.sha256(p["payload"]).digest() != p["sha256"]:
        warn(f"{path}: 中身が見出しと合いません")
        return None
    info = ft.payload_build_info(p)
    if info is None or info["serial"] != p["serial"]:
        warn(f"{path}: 中身の版の印が読めないか、見出しと合いません")
        return None
    env = info["env"] or DEFAULT_ENV
    date = info["date"] or time.strftime("%Y-%m-%d %H:%M", time.gmtime(p["build_time"]))
    if uf2 is not None and not os.path.isfile(uf2):
        warn(f"{uf2}: .uf2 がありません(更新ファイルだけ載せます)")
        uf2 = None
    return dict(serial=p["serial"], git=p["git"], date=date, channel=channel, env=env,
                signed=1 if p["flags"] & ft.FLAG_SIGNED else 0, bytes=len(blob),
                sha256=hashlib.sha256(blob).hexdigest(), file=file_name(p["serial"], p["git"], env),
                src=path, uf2_src=uf2, tag=tag)


def build(pairs, out_dir):
    """pairs = [(種類, 更新ファイルのパス[, .uf2 のパス[, タグ]])]。.uf2・タグは None でもよい。
    out_dir に index.tsv・downloads.json・更新ファイル・.uf2 を置き、載せた行を返す"""
    entries = {}
    for pair in pairs:
        channel, path = pair[0], pair[1]
        uf2 = pair[2] if len(pair) > 2 else None
        tag = pair[3] if len(pair) > 3 else None
        if channel not in CHANNELS:
            warn(f"{path}: 種類 {channel} は知りません")
            continue
        e = read_entry(channel, path, uf2, tag)
        if e is None:
            continue
        key = (e["serial"], e["git"], e["env"])
        old = entries.get(key)
        # 同じ版なら安定版を残す(署名の無い方より有る方を残す)
        if old is None or (old["channel"] == "nightly" and channel == "stable") or \
                (old["channel"] == channel and e["signed"] > old["signed"]):
            # 残す方に .uf2 が無ければ、同じ版のもう片方のものを使う(中身は同じビルド)
            if old is not None and e["uf2_src"] is None:
                e["uf2_src"] = old["uf2_src"]
            entries[key] = e
        elif e["uf2_src"] is not None and old["uf2_src"] is None:
            old["uf2_src"] = e["uf2_src"]
    rows = sorted(entries.values(), key=lambda e: (-e["serial"], CHANNELS.index(e["channel"]), e["env"]))

    os.makedirs(out_dir, exist_ok=True)
    # 前に作った置き場へ作り直すときは、載せなくなった更新ファイルを残さない
    for name in os.listdir(out_dir):
        if name.endswith((".pfw", ".uf2")) or name in (INDEX_NAME, DOWNLOADS_NAME):
            os.remove(os.path.join(out_dir, name))
    lines = [INDEX_MAGIC]
    for e in rows:
        shutil.copyfile(e["src"], os.path.join(out_dir, e["file"]))
        e["uf2"] = None
        if e["uf2_src"] is not None:
            e["uf2"] = e["file"][:-len(".pfw")] + ".uf2"
            shutil.copyfile(e["uf2_src"], os.path.join(out_dir, e["uf2"]))
            e["uf2_bytes"] = os.path.getsize(e["uf2_src"])
        lines.append("\t".join(str(e[k]) for k in
                               ("serial", "git", "date", "channel", "env", "signed", "bytes", "sha256", "file")))
    with open(os.path.join(out_dir, INDEX_NAME), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    write_downloads(rows, os.path.join(out_dir, DOWNLOADS_NAME))
    return rows


def write_downloads(rows, path):
    """Webのダウンロードのページ(pc/web/download.html)が読む一覧。本体は読まない(本体は index.tsv)。

      {"format": 1, "versions": [{"serial", "git", "date", "channel", "env", "signed", "tag",
                                  "pfw": {"file", "bytes", "sha256"}, "uf2": {"file", "bytes"} か null}, ...]}

    並びは index.tsv と同じ(新しい順)。tag は分からなければ null。ファイル名は downloads.json からの相対"""
    versions = []
    for e in rows:
        versions.append({
            "serial": e["serial"], "git": e["git"], "date": e["date"], "channel": e["channel"],
            "env": e["env"], "signed": bool(e["signed"]), "tag": e["tag"],
            "pfw": {"file": e["file"], "bytes": e["bytes"], "sha256": e["sha256"]},
            "uf2": {"file": e["uf2"], "bytes": e["uf2_bytes"]} if e["uf2"] else None,
        })
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"format": 1, "versions": versions}, f, ensure_ascii=False, indent=1)
        f.write("\n")


def cmd_build(args):
    pairs = []
    with open(args.list, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                warn(f"{args.list}: 「種類<TAB>ファイル」ではない行: {line}")
                continue
            # 3列目(.uf2)・4列目(タグ)は無くてもよい。「-」も無しの意味
            extra = [None if (x == "" or x == "-") else x for x in parts[2:4]]
            pairs.append(tuple(parts[:2] + extra))
    rows = build(pairs, args.out)
    for e in rows:
        print(f"#{e['serial']} ({e['git']}) {e['channel']} {e['env']} {'署名あり' if e['signed'] else '署名なし'}"
              f" {e['bytes']}バイト → {e['file']}" + (f" + {e['uf2']}" if e["uf2"] else ""))
    print(f"{os.path.join(args.out, INDEX_NAME)}: {len(rows)}本")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("select", help="載せるリリースを選ぶ")
    p.add_argument("releases", help="gh release list --json tagName,isPrerelease,isDraft,createdAt の出力")
    p.add_argument("--stable", type=int, default=5)
    p.add_argument("--nightly", type=int, default=3)
    p.set_defaults(func=cmd_select)

    p = sub.add_parser("build", help="一覧と置き場を作る")
    p.add_argument("list", help="「種類<TAB>更新ファイルのパス[<TAB>.uf2のパス[<TAB>タグ]]」の行を並べたファイル")
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_build)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
