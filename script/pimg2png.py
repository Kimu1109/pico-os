#!/usr/bin/env python3
"""
pimg2png.py
-----------
pico-os 独自フォーマット .pimg (4bpp indexed RLE) をPNGへ変換する
(generate_pimg.py の逆変換)。手元の.pimgファイルを、元のPNG/JPGが
無くても目視確認できるようにするための道具。

.pimg は色ではなく番号(0〜15)を持つので、どの色で描くかはパレットで選べる
(pimg_palette.py。既定は本体の既定の16色、dark でダークテーマ、ファイルや16進の並びも可)。
Luaの pico.set_palette() で色を差し替えるアプリの絵は、そのパレットを渡すと本体と同じ見え方になる。
フォーマット仕様は generate_pimg.py のヘッダコメント参照。

使い方:
    python3 pimg2png.py input.pimg output.png
    python3 pimg2png.py input.pimg output.png --scale 4
    python3 pimg2png.py input.pimg output.png --palette dark            # ダークテーマで見る
    python3 pimg2png.py input.pimg output.png --palette my.gpl --set-color 3=ff8800
    python3 pimg2png.py input.pimg output.png --bg ffffff               # 透過を白で塗る
    python3 pimg2png.py input.pimg output.png --rgb565                  # 液晶と同じRGB565へ丸める
    python3 pimg2png.py input.pimg --info                               # 大きさと使っている番号だけ出す
    python3 pimg2png.py --list-palettes
"""

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

import pimg_palette


def read_pimg(path: Path):
    data = path.read_bytes()
    if len(data) < 5:
        print(f"error: file too small to be a valid .pimg: {path}", file=sys.stderr)
        sys.exit(1)

    w, h, flags = struct.unpack_from("<HHB", data, 0)
    has_transparent = bool(flags & 0x01)

    indices = []
    pos = 5
    n = len(data)
    while pos + 1 < n:
        run = data[pos]
        idx = data[pos + 1]
        indices.extend([idx] * run)
        pos += 2

    expected = w * h
    if len(indices) != expected:
        print(f"warning: デコードした画素数({len(indices)})が w*h({expected})と一致しません"
              f" (ファイルが壊れている可能性があります)", file=sys.stderr)
    bad = sorted({i for i in indices if i >= pimg_palette.PALETTE_SIZE})
    if bad:
        print(f"warning: 16色の外の番号があります: {bad}(0として描きます)", file=sys.stderr)

    return w, h, has_transparent, indices


def write_png(w, h, has_transparent, indices, palette, out_path: Path, scale: int, bg=None):
    from PIL import Image  # --info / --list-palettes だけなら Pillow は要らない

    keep_alpha = has_transparent and bg is None
    mode = "RGBA" if keep_alpha else "RGB"
    img = Image.new(mode, (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            i = y * w + x
            idx = indices[i] if i < len(indices) else 0
            if idx >= len(palette):
                idx = 0
            r, g, b = palette[idx]
            if has_transparent and idx == 0:
                px[x, y] = (r, g, b, 0) if keep_alpha else bg
            elif keep_alpha:
                px[x, y] = (r, g, b, 255)
            else:
                px[x, y] = (r, g, b)

    if scale != 1:
        img = img.resize((w * scale, h * scale), Image.NEAREST)
    img.save(out_path)


def print_info(path: Path, w, h, has_transparent, indices, palette):
    print(f"file  : {path}")
    print(f"size  : {w}x{h}  transparent={has_transparent}  bytes={path.stat().st_size}")
    counts = Counter(indices)
    total = max(1, len(indices))
    print("colors:")
    for idx in sorted(counts):
        rgb = palette[idx] if idx < len(palette) else (0, 0, 0)
        note = "  (透過)" if has_transparent and idx == 0 else ""
        print(f"  {idx:2d}  #{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}  {counts[idx]:7d}px"
              f"  {100.0 * counts[idx] / total:5.1f}%{note}")


def main():
    parser = argparse.ArgumentParser(description="Convert a pico-os .pimg file back to PNG (for visual inspection)")
    parser.add_argument("input", type=Path, nargs="?", help="入力 .pimg ファイル")
    parser.add_argument("output", type=Path, nargs="?", help="出力先 PNG ファイル(--info だけなら省略可)")
    parser.add_argument("--scale", type=int, default=1, help="拡大率(最近傍補間、既定1)")
    pimg_palette.add_arguments(parser)
    parser.add_argument("--bg", metavar="COLOR", default=None,
                        help="透過(番号0)をこの色で塗って不透明なPNGにする(例 ffffff。既定は透過のまま)")
    parser.add_argument("--rgb565", action="store_true",
                        help="色を液晶と同じRGB565へ丸めてから書く(本体の画面の見え方に近くなる)")
    parser.add_argument("--info", action="store_true", help="大きさと使っている番号・色の内訳を出す")
    args = parser.parse_args()

    if args.list_palettes:
        pimg_palette.print_builtin()
        return

    if args.input is None or (args.output is None and not args.info):
        parser.error("input と output が要ります(--info だけなら output は省略できます)")
    if args.scale < 1:
        parser.error("--scale は1以上")

    if not args.input.exists():
        print(f"error: input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    try:
        palette = pimg_palette.resolve(args.palette, args.set_color, rgb565=args.rgb565)
        bg = pimg_palette.parse_color(args.bg) if args.bg is not None else None
    except pimg_palette.PaletteError as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

    w, h, has_transparent, indices = read_pimg(args.input)

    if args.info:
        print_info(args.input, w, h, has_transparent, indices, palette)
    if args.output is None:
        return

    write_png(w, h, has_transparent, indices, palette, args.output, args.scale, bg)

    pal_name = args.palette or "default"
    extra = f", palette={pal_name}" + (f", +{len(args.set_color)}色差し替え" if args.set_color else "")
    print(f"input : {args.input}  ({w}x{h}, transparent={has_transparent}{extra})")
    print(f"output: {args.output}")


if __name__ == "__main__":
    main()
