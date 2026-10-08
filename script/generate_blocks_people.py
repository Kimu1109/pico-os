#!/usr/bin/env python3
"""Luaアプリ「ブロック」の人や物の絵(people.pimg)を作る。標準ライブラリだけで動く。

pico.iso の人や物(エンティティ)として箱庭に立てる絵。透過つきの .pimg(0番 = 透過)に、
村人(歩く2コマ、12x22)と羊(16x12)を横に並べる:

    x=0  村人 コマ1(12x22)
    x=12 村人 コマ2(12x22)
    x=24 羊(16x12。下に寄せる)

色は palette.lua(script/generate_blocks_sheet.py が作る、1〜14番を差し替えたパレット)の番号で直接描く:
  1=青 3=濃い灰 4=茶 5=焦げ茶 6=肌(黄土) 8=灰 11=クリーム 12=赤 13=くすんだ茶 15=白
"""
import argparse
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "pc" / "sdcard" / "lua" / "apps" / "ブロック"

# '.' = 透過。それ以外は16進1桁の色番号
VILLAGER_TOP = [
    "....5555....",
    "...555555...",
    "...566665...",
    "...636636...",
    "...666666...",
    "....6666....",
    "..cccccccc..",
    ".cccccccccc.",
    ".6cccccccc6.",
    ".6.cccccc.6.",
    ".6.cccccc.6.",
    "...cccccc...",
    "...cccccc...",
    "...dddddd...",
    "...dd..dd...",
]
VILLAGER_LEGS = [
    [   # コマ1: 揃えた足
        "...dd..dd...",
        "...dd..dd...",
        "...dd..dd...",
        "...dd..dd...",
        "...33..33...",
        "..333..333..",
        "............",
    ],
    [   # コマ2: 開いた足
        "..dd....dd..",
        "..dd....dd..",
        ".dd......dd.",
        ".dd......dd.",
        ".33......33.",
        "333......333",
        "............",
    ],
]
SHEEP = [
    "....ffffff......",
    "..ffffffffff....",
    ".fffffffffff33..",
    ".ffffffffff3333.",
    "ffffffffffff3f3.",
    "ffffffffffff3333",
    "fffffffffff.333.",
    ".ffffffffff.....",
    "..ffffffff......",
    "..33.33..33.33..",
    "..33.33..33.33..",
    "..88.88..88.88..",
]


def pix(ch):
    return 0 if ch == "." else int(ch, 16)


def build():
    w, h = 40, 22
    rows = [[0] * w for _ in range(h)]

    def put(art, ox, oy):
        for y, line in enumerate(art):
            assert len(line) == len(art[0]), line
            for x, ch in enumerate(line):
                rows[oy + y][ox + x] = pix(ch)

    for i, legs in enumerate(VILLAGER_LEGS):
        art = VILLAGER_TOP + legs
        assert len(art) == 22
        put(art, 12 * i, 0)
    put(SHEEP, 24, h - len(SHEEP))
    return rows


def rle(rows):
    out = bytearray()
    run, prev = 0, None
    for row in rows:
        for v in row:
            if run and v == prev and run < 255:
                run += 1
                continue
            if run:
                out += bytes((run, prev))
            prev, run = v, 1
    if run:
        out += bytes((run, prev))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="「ブロック」の人や物の絵(people.pimg)を作る")
    ap.add_argument("--dir", type=Path, default=APP)
    args = ap.parse_args()
    rows = build()
    path = args.dir / "people.pimg"
    with open(path, "wb") as f:
        f.write(struct.pack("<HHB", len(rows[0]), len(rows), 1))
        f.write(rle(rows))
    print(f"{path} ({len(rows[0])}x{len(rows)}) を書きました")


if __name__ == "__main__":
    main()
