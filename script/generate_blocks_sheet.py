#!/usr/bin/env python3
"""
generate_blocks_sheet.py
------------------------
Luaアプリ「ブロック」(pc/sdcard/lua/apps/ブロック/。TheScienceElf氏の Blocks-TI-84 を
pico-os へ移したもの)の絵を作る。標準ライブラリのみ(PNGも自前で読む)。

元の絵は script/blocks_assets/ にある Blocks-TI-84 の TextureMap.png(16x16のテクスチャを
上面/左面/右面の3段で23種類並べたもの)と player.png(カーソル)。どちらも MIT ライセンス
(script/blocks_assets/LICENSE-Blocks-TI-84.txt)。

作るもの:
    faces.pimg   ブロックの面の絵(透過つき)。1種類につき高さ23pxの1段で、横に
                     x=0   上面(日なた)   32x15
                     x=32  上面(影)       32x15
                     x=64  左面(日なた)   16x23
                     x=80  左面(影)       16x23
                     x=96  右面(いつも影) 16x23
                 を並べる。段の番号 = ブロックの番号 - 1(1=水, 2=石 … 24=金, 25=カーソル)。
                 面の形は元の convert_textures.py と同じ(上面は2:1のひし形、横の面は2pxごとに1段ずらす)。
    palette.lua  アプリが pico.set_palette で入れる色(1〜14番)と、空・水・UIに使う番号。
                 テクスチャの色(日なたと、明るさ半分の影)と空・水の色を k-means で14色にまとめる。
    icon.pimg    ランチャのアイコン(48x48、既定のパレット)。

使い方:
    python3 script/generate_blocks_sheet.py                   # 既定の場所へ書く
    python3 script/generate_blocks_sheet.py --preview p.ppm   # 確認用のPPM(2倍)も書く
"""

import argparse
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "script" / "blocks_assets"
APP = ROOT / "pc" / "sdcard" / "lua" / "apps" / "ブロック"

# 既定のパレット(src/functions/GFX_Functions.hpp の DEFAULT_COLORS)
DEFAULT_PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0x80), (0x00, 0x80, 0x00), (0x00, 0x80, 0x80),
    (0x80, 0x00, 0x00), (0x80, 0x00, 0x80), (0x80, 0x80, 0x00), (0xD3, 0xD3, 0xD3),
    (0x80, 0x80, 0x80), (0x00, 0x00, 0xFF), (0x00, 0xFF, 0x00), (0x00, 0xFF, 0xFF),
    (0xFF, 0x00, 0x00), (0xFF, 0x00, 0xFF), (0xFF, 0xFF, 0x00), (0xFF, 0xFF, 0xFF),
]

SKY = (192, 240, 255)     # 元のゲームの空の色
WATER = (56, 96, 232)     # 水(ディザで半透明に見せる)
TEX_COUNT = 23            # 石(2)〜金(24)
B_GRASS = 3
ROW_H = 23                # 1種類ぶんの段の高さ
SHEET_W = 112


# ---------------------------------------------------------------- PNG(8bit、パレット/RGBA)

def read_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, plte, trns = 8, b"", None, None
    w = h = ctype = 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and interlace == 0 and ctype in (3, 6), (path, depth, ctype)
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    bpp = 1 if ctype == 3 else 4
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    px = []
    for line in rows:
        out = []
        for x in range(w):
            if ctype == 3:
                i = line[x]
                alpha = trns[i] if trns and i < len(trns) else 255
                out.append(None if alpha < 128 else plte[i])
            else:
                r, g, b, a = line[x * 4:x * 4 + 4]
                out.append(None if a < 128 else (r, g, b))
        px.append(out)
    return w, h, px


# ---------------------------------------------------------------- 面の形(元の convert_textures.py と同じ)

def top_rows():
    """上面のひし形: 行r(0〜14)の左半分の幅"""
    return [2 * (r + 1) if r <= 7 else 2 * (15 - r) for r in range(15)]


def make_faces(top, left, right):
    """16x16の上面/左面/右面 → (上面32x15, 左面16x23, 右面16x23)。None は透過"""
    t = [[None] * 32 for _ in range(15)]
    for y, half in enumerate(top_rows()):
        for x in range(16 - half, 16 + half):
            # 画面(x-16, y) = M・テクスチャ(u, v)、M = [[1, -1], [0.5, 0.5]] の逆写像
            dx = x - 16 + 0.5
            dy = y + 0.5
            u = int(dy + dx / 2)
            v = int(dy - dx / 2)
            t[y][x] = top[min(max(v, 0), 15)][min(max(u, 0), 15)]
    lf = [[None] * 16 for _ in range(23)]
    rf = [[None] * 16 for _ in range(23)]
    for i in range(8):
        for c in (2 * i, 2 * i + 1):
            for k in range(16):
                lf[i + k][c] = left[k][c]           # 元: out[8+i : 24+i, 2i:2i+2]
                rf[7 - i + k][c] = right[k][c]      # 元: out[15-i : 31-i, 16+2i:18+2i]
    return t, lf, rf


def half(c):
    return None if c is None else (c[0] // 2, c[1] // 2, c[2] // 2)


def dither(face, color):
    """水: 市松模様の半分だけ色を置く(残りは透過で、奥が透けて見える)"""
    return [[color if c is not None and (x + y) % 2 == 0 else None for x, c in enumerate(row)]
            for y, row in enumerate(face)]


# ---------------------------------------------------------------- 減色

def dist(a, b):
    dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
    return 2 * dr * dr + 4 * dg * dg + 3 * db * db


def kmeans(samples, fixed, k, iters=40):
    """samples: {color: 重み}。fixed は動かさない中心。全部で len(fixed)+k 色"""
    colors = sorted(samples, key=lambda c: -samples[c])
    centers = list(fixed)
    # 決定的な k-means++ 風の初期値: いちばん遠い(重み付き)色を順に足す
    while len(centers) < len(fixed) + k:
        best, best_d = None, -1
        for c in colors:
            d = min(dist(c, z) for z in centers) * samples[c]
            if d > best_d:
                best, best_d = c, d
        centers.append(best)
    for _ in range(iters):
        acc = [[0, 0, 0, 0] for _ in centers]
        for c in colors:
            j = min(range(len(centers)), key=lambda i: dist(c, centers[i]))
            w = samples[c]
            acc[j][0] += c[0] * w
            acc[j][1] += c[1] * w
            acc[j][2] += c[2] * w
            acc[j][3] += w
        moved = False
        for j in range(len(fixed), len(centers)):
            if acc[j][3]:
                n = tuple(round(acc[j][i] / acc[j][3]) for i in range(3))
                if n != centers[j]:
                    centers[j], moved = n, True
        if not moved:
            break
    return centers


def nearest(color, palette, skip=()):
    return min((i for i in range(len(palette)) if i not in skip), key=lambda i: dist(color, palette[i]))


# ---------------------------------------------------------------- 本体

def build():
    w, _, tex = read_png(ASSETS / "TextureMap.png")
    assert w // 16 >= TEX_COUNT
    _, _, cur = read_png(ASSETS / "player.png")

    def cut(x0, y0):
        return [row[x0:x0 + 16] for row in tex[y0:y0 + 16]]

    # 段ごとの面: [上(日なた), 上(影), 左(日なた), 左(影), 右(影)]
    rows = []
    blank = make_faces([[None] * 16] * 16, [[None] * 16] * 16, [[None] * 16] * 16)
    tw, lw, rw = make_faces([[WATER] * 16] * 16, [[WATER] * 16] * 16, [[WATER] * 16] * 16)
    rows.append([dither(tw, WATER), dither(tw, WATER), dither(lw, WATER), dither(lw, WATER),
                 dither(rw, WATER)])
    for i in range(TEX_COUNT):
        t, lf, rf = make_faces(cut(i * 16, 0), cut(i * 16, 16), cut(i * 16, 32))
        sh = lambda f: [[half(c) for c in row] for row in f]
        rows.append([t, sh(t), lf, sh(lf), sh(rf)])
    # カーソル: player.png の上半分が上面、下半分が横の面
    ct, cl, cr = make_faces([r[:16] for r in cur[:16]], [r[:16] for r in cur[16:32]],
                            [r[:16] for r in cur[16:32]])
    rows.append([ct, ct, cl, cl, cr])
    del blank

    # 減色: テクスチャ(日なた+影)と空・水
    samples = {}
    for faces in rows[1:-1]:
        for f in faces:
            for row in f:
                for c in row:
                    if c is not None:
                        samples[c] = samples.get(c, 0) + 1
    samples[SKY] = samples.get(SKY, 0) + 500
    fixed = [(0, 0, 0), (255, 255, 255), SKY, WATER]
    centers = kmeans(samples, fixed, 16 - len(fixed))

    # 1〜14番へ並べる: 既定のパレットの近い番号に置く(ステータスバー等の色がなるべく変わらないように)
    free = centers[2:]
    pal = [None] * 16
    pal[0], pal[15] = (0, 0, 0), (255, 255, 255)
    left = list(range(1, 15))
    pairs = sorted(((dist(c, DEFAULT_PALETTE[i]), ci, i) for ci, c in enumerate(free) for i in left))
    used_c, used_i = set(), set()
    for _, ci, i in pairs:
        if ci in used_c or i in used_i:
            continue
        pal[i] = free[ci]
        used_c.add(ci)
        used_i.add(i)

    # 面の絵を番号にする(テクスチャの黒は透過の0番にしない)
    def idx(c):
        if c is None:
            return 0
        if c == (255, 255, 255):
            return 15
        return nearest(c, pal, skip=(0,))

    sheet_h = ROW_H * len(rows)
    sheet = [[0] * SHEET_W for _ in range(sheet_h)]
    for r, faces in enumerate(rows):
        y0 = r * ROW_H
        for f, x0 in zip(faces, (0, 32, 64, 80, 96)):
            for y, row in enumerate(f):
                for x, c in enumerate(row):
                    sheet[y0 + y][x0 + x] = idx(c)

    info = {
        "sky": pal.index(SKY), "water": pal.index(WATER),
        "dark": nearest((48, 48, 48), pal), "mid": nearest((128, 128, 128), pal, skip=(0, 15)),
        "light": nearest((200, 200, 200), pal, skip=(0, 15)),
        "accent": nearest((220, 40, 20), pal, skip=(0, 15)),
        "green": nearest((60, 170, 60), pal, skip=(0, 15)),
    }

    # アイコン(既定のパレット): 空色の地に、草のブロックを1.5倍で
    block = [[None] * 32 for _ in range(31)]
    faces = rows[B_GRASS - 1]
    for f, (dx, dy) in ((faces[0], (0, 0)), (faces[2], (0, 8)), (faces[4], (16, 8))):
        for y, line in enumerate(f):
            for x, c in enumerate(line):
                if c is not None:
                    block[dy + y][dx + x] = c
    icon = [[11] * 48 for _ in range(48)]
    for y in range(46):
        for x in range(48):
            c = block[y * 2 // 3][x * 2 // 3]
            if c is not None:
                # 既定のパレットは原色ばかりなので、彩度を上げてから一番近い色を選ぶ(草が灰色にならないように)
                m = sum(c) / 3
                v = tuple(min(255, max(0, round(m + (k - m) * 2))) for k in c)
                icon[y + 1][x] = nearest(v, DEFAULT_PALETTE, skip=(11,))
    return sheet, pal, info, icon


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


def write_pimg(path, rows, transparent):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<HHB", len(rows[0]), len(rows), 1 if transparent else 0))
        f.write(rle(rows))
    print(f"{path} ({len(rows[0])}x{len(rows)}) を書きました")


def write_palette(path, pal, info):
    lines = [
        "-- script/generate_blocks_sheet.py が作る。手で直さないこと。",
        "-- 1〜14番の色(pico.set_palette で入れる)と、空・水・UIに使う番号。",
        "return {",
        "    colors = {",
    ]
    for i in range(1, 15):
        lines.append("        { %d, %d, %d }," % pal[i])
    lines.append("    },")
    for k in ("sky", "water", "dark", "mid", "light", "accent", "green"):
        lines.append(f"    {k} = {info[k]},")
    lines.append("}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{path} を書きました")


def main():
    ap = argparse.ArgumentParser(description="「ブロック」の絵(faces.pimg / palette.lua / icon.pimg)を作る")
    ap.add_argument("--dir", type=Path, default=APP)
    ap.add_argument("--preview", type=Path, help="確認用のPPM(2倍、透過は空色)")
    args = ap.parse_args()
    sheet, pal, info, icon = build()
    write_pimg(args.dir / "faces.pimg", sheet, True)
    write_pimg(args.dir / "icon.pimg", icon, False)
    write_palette(args.dir / "palette.lua", pal, info)
    if args.preview:
        h, w = len(sheet), len(sheet[0])
        with open(args.preview, "wb") as f:
            f.write(f"P6\n{w * 2} {h * 2}\n255\n".encode())
            for row in sheet:
                line = b"".join(bytes(pal[v] if v else pal[info["sky"]]) * 2 for v in row)
                f.write(line * 2)


if __name__ == "__main__":
    main()
