#!/usr/bin/env python3
"""Luaアプリ「ゾンビTD」の絵を作る。標準ライブラリだけで動く。

  units.pimg  透過つき(0番 = 透過)。pico.iso の人や物(エンティティ)として立てる絵を横に並べる:
      x=0   ノーマルゾンビ  コマ1, 2(12x22)
      x=24  遠距離ゾンビ    コマ1, 2(12x22。石を持ち上げている)
      x=48  重量級ゾンビ    コマ1, 2(16x26)
      x=80  ベース(44x40)
      x=124 近接兵 コマ1, 2(12x22。短剣)
      x=148 回復兵 コマ1, 2(12x22。白い服に赤十字・杖)
      x=172 弓兵   コマ1, 2(12x22。弓)
    どれも右を向いた絵で、左へ歩くときは左右反転する。高さの違う絵は下に寄せる(足元が y=39)。
    行の並びは main.lua の SPRITES と合わせること。
  icon.pimg   ランチャのアイコン(48x48、既定のパレット)

地面の絵(faces.pimg)と色(palette.lua)は「ブロック」と同じものを使う(scripts/generate_blocks_sheet.py が作る)。
このスクリプトは「ブロック」のフォルダから写す。
人や物の絵は palette.lua の番号で直接描く:
  0=黒 1=青 2=緑 3=濃い灰 4=茶 5=焦げ茶 6=肌 8=灰 9=濃い緑 10=明るい緑 11=クリーム 12=赤 13=くすんだ茶 14=黄 15=白
"""
import argparse
import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "pc" / "sdcard" / "lua" / "apps" / "ゾンビTD"
BLOCKS = ROOT / "pc" / "sdcard" / "lua" / "apps" / "ブロック"

SHEET_H = 40

# '.' = 透過。それ以外は16進1桁の色番号
ZOMBIE_TOP = [
    "...9999.....",
    "..922229....",
    "..922c2c....",
    "..9222229...",
    "...92229....",
    "....999.....",
    "...11111....",
    "..1111122222",
    "..1111199999",
    "..111111....",
    "..11.111....",
    "..111111....",
    "...11111....",
    "...ddddd....",
    "...dd.dd....",
]
RANGED_TOP = [
    "......888...",
    ".....8888...",
    "...999888...",
    "..922229.2..",
    "..922c2c.2..",
    "..9222229...",
    "...92229....",
    "...44444....",
    "..4444442...",
    "..4444442...",
    "..444444....",
    "..44.444....",
    "...44444....",
    "...ddddd....",
    "...dd.dd....",
]
ZOMBIE_LEGS = [
    [
        "...dd.dd....",
        "...dd.dd....",
        "...dd.dd....",
        "...dd.dd....",
        "...33.33....",
        "..333.333...",
        "............",
    ],
    [
        "..dd...dd...",
        "..dd...dd...",
        ".dd.....dd..",
        ".dd.....dd..",
        ".33.....33..",
        "333.....333.",
        "............",
    ],
]
HEAVY_TOP = [
    "....999999......",
    "...99999999.....",
    "...9999c9c9.....",
    "...99999999.....",
    "....999999......",
    "..3333333333....",
    ".333888888333...",
    ".338888888839999",
    ".338888888839999",
    ".33888888883....",
    ".33888888883....",
    ".3338888883.....",
    "..333333333.....",
    "..333333333.....",
    "..555555555.....",
    "..555....555....",
    "..555....555....",
    "..555....555....",
]
HEAVY_LEGS = [
    [
        "..555....555....",
        "..555....555....",
        "..555....555....",
        "..333....333....",
        ".3333...3333....",
        ".3333...3333....",
        "................",
        "................",
    ],
    [
        ".555......555...",
        ".555......555...",
        "555........555..",
        "333........333..",
        "3333.......3333.",
        "3333.......3333.",
        "................",
        "................",
    ],
]
BASE = [
    "..................1.........................",
    "..................11........................",
    "..................111.......................",
    "..................1.........................",
    "..................5.........................",
    "...............cccccccc.....................",
    ".............cccccccccccc...................",
    "...........cccccccccccccccc.................",
    ".........cccccccccccccccccccc...............",
    ".......cccccccccccccccccccccccc.............",
    ".....cccccccccccccccccccccccccccc...........",
    "...cccccccccccccccccccccccccccccccc.........",
    ".cccccccccccccccccccccccccccccccccccc.......",
    "cccccccccccccccccccccccccccccccccccccc......",
    "..bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb44......",
    "..bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb444.....",
    "..bb1111bbbbbbbbbbbbbbbbbbbbb1111bbb4444....",
    "..bb1771bbbbbbbbbbbbbbbbbbbbb1771bbb44444...",
    "..bb1771bbbbbbbbbbbbbbbbbbbbb1771bbb444444..",
    "..bb1111bbbbbbbbbbbbbbbbbbbbb1111bbb444444..",
    "..bbbbbbbbbbbbbbb55555bbbbbbbbbbbbbb444444..",
    "..bbbbbbbbbbbbbb5555555bbbbbbbbbbbbb444444..",
    "..bbbbbbbbbbbbbb5555555bbbbbbbbbbbbb444444..",
    "..bbbbbbbbbbbbbb5555555bbbbbbbbbbbbb444444..",
    "..bbbbbbbbbbbbbb55555e5bbbbbbbbbbbbb444444..",
    "..bbbbbbbbbbbbbb5555555bbbbbbbbbbbbb444444..",
    "..bbbbbbbbbbbbbb5555555bbbbbbbbbbbbb44444...",
    "..bbbbbbbbbbbbbb5555555bbbbbbbbbbbbb4444....",
    "..88888888888888888888888888888888884.......",
    "..8888888888888888888888888888888888........",
]
# 兵士(人間側)。上の15行 + 足の7行
MELEE_TOP = [
    "....888.....",
    "...88888....",
    "...8666.....",
    "...66606....",
    "...66666....",
    "....666.....",
    "...11111....",
    "..1111116...",
    "..11111.6888",
    "..111111....",
    "..111111....",
    "..444444....",
    "..111111....",
    "...11111....",
    "...55.55....",
]
HEALER_TOP = [
    "....222.....",
    "...22222....",
    "...2666.....",
    "...26606....",
    "...26666....",
    "....666.....",
    "...fffff..a.",
    "..ffcfff6aaa",
    "..fcccff.4a.",
    "..ffcfff.4..",
    "..ffffff.4..",
    "..ffffff.4..",
    "..ffffff.4..",
    "...fffff.4..",
    "...55.55.4..",
]
RANGED_TOP = [
    "....999.....",
    "...99999....",
    "...9666.....",
    "...96606.4..",
    "...96666..4.",
    "....666...4.",
    "...99999..4.",
    "..9999996.4.",
    "..99999..64.",
    "..999999..4.",
    "..999999..4.",
    "..444444.4..",
    "..999999....",
    "...99999....",
    "...55.55....",
]
SOLDIER_LEGS = [
    [
        "...55.55....",
        "...55.55....",
        "...55.55....",
        "...55.55....",
        "...00.00....",
        "..000.000...",
        "............",
    ],
    [
        "..55...55...",
        "..55...55...",
        ".55.....55..",
        ".55.....55..",
        ".00.....00..",
        "000.....000.",
        "............",
    ],
]
# アイコン(既定のパレット: 0=黒 2=暗い緑 7=明るい灰 8=暗い灰 10=緑 12=赤 14=黄 15=白)
ICON = [
    "................................................",
    "................................................",
    "................................................",
    "................22222222222222..................",
    "..............222aaaaaaaaaaa222.................",
    ".............22aaaaaaaaaaaaaaaa22...............",
    "............2aaaaaaaaaaaaaaaaaaaa2..............",
    "...........2aaaaaaaaaaaaaaaaaaaaaa2.............",
    "...........2aaaaaaaaaaaaaaaaaaaaaa2.............",
    "..........2aaaaaaaaaaaaaaaaaaaaaaaa2............",
    "..........2aaa0000aaaaaaaa0000aaaaa2............",
    "..........2aa0cccc0aaaaaa0cccc0aaaa2............",
    "..........2aa0cccc0aaaaaa0cccc0aaaa2............",
    "..........2aaa0000aaaaaaaa0000aaaaa2............",
    "..........2aaaaaaaaaaaaaaaaaaaaaaaa2............",
    "..........2aaaaaaaaaa22aaaaaaaaaaaa2............",
    "...........2aaaaaaaaaaaaaaaaaaaaaa2.............",
    "...........2aaa00000000000000aaaaa2.............",
    "............2aa0f0f0f0f0f0f00aaaa2..............",
    ".............2aa00000000000000aa2...............",
    "..............22aaaaaaaaaaaaaa22................",
    "................22222222222222..................",
    "................................................",
    "................................................",
    "........8888888888888888888888888888............",
    "........8777777777777777777777777778............",
    "........87788887777888877778888777788...........",
    "........8777777777777777777777777778............",
    "........8888888888888888888888888888............",
    "..........8c8..........8c8..........8c8.........",
    "..........8c8..........8c8..........8c8.........",
    "..........888..........888..........888.........",
    "................................................",
    "................................................",
    "............................e...................",
    "...........................eee..................",
    "..........................eeeee.................",
    ".........................e00e00e................",
    "........................eeeeeeeee...............",
    "................................................",
]


def pix(ch):
    return 0 if ch == "." else int(ch, 16)


def build_units():
    w = 12 * 4 + 16 * 2 + 44 + 12 * 6
    rows = [[0] * w for _ in range(SHEET_H)]

    def put(art, ox):
        oy = SHEET_H - len(art)   # 下に寄せる
        for y, line in enumerate(art):
            assert len(line) == len(art[0]), (line, len(line), len(art[0]))
            for x, ch in enumerate(line):
                rows[oy + y][ox + x] = pix(ch)

    for i, legs in enumerate(ZOMBIE_LEGS):
        put(ZOMBIE_TOP + legs, 12 * i)
        put(RANGED_TOP + legs, 24 + 12 * i)
    for i, legs in enumerate(HEAVY_LEGS):
        put(HEAVY_TOP + legs, 48 + 16 * i)
    put(BASE, 80)
    for i, legs in enumerate(SOLDIER_LEGS):
        put(MELEE_TOP + legs, 124 + 12 * i)
        put(HEALER_TOP + legs, 148 + 12 * i)
        put(RANGED_TOP + legs, 172 + 12 * i)
    return rows


def build_icon():
    rows = []
    for y in range(48):
        line = ICON[y] if y < len(ICON) else "." * 48
        assert len(line) == 48, (y, len(line))
        rows.append([15 if ch == "." else int(ch, 16) for ch in line])
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


def write(path, rows, transparent):
    with open(path, "wb") as f:
        f.write(struct.pack("<HHB", len(rows[0]), len(rows), 1 if transparent else 0))
        f.write(rle(rows))
    print(f"{path} ({len(rows[0])}x{len(rows)}) を書きました")


def main():
    ap = argparse.ArgumentParser(description="「ゾンビTD」の絵を作る")
    ap.add_argument("--dir", type=Path, default=APP)
    args = ap.parse_args()
    args.dir.mkdir(parents=True, exist_ok=True)
    write(args.dir / "units.pimg", build_units(), True)
    write(args.dir / "icon.pimg", build_icon(), False)
    for name in ("faces.pimg", "palette.lua"):
        shutil.copyfile(BLOCKS / name, args.dir / name)
        print(f"{args.dir / name} を「ブロック」から写しました")


if __name__ == "__main__":
    main()
