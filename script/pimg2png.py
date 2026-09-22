#!/usr/bin/env python3
"""
pimg2png.py
-----------
pico-os 独自フォーマット .pimg (4bpp indexed RLE) をPNGへ変換する
(generate_pimg.py の逆変換)。手元の.pimgファイルを、元のPNG/JPGが
無くても目視確認できるようにするための道具。

パレットは generate_pimg.py の PALETTE をそのまま import して使う
(二重管理すると片方だけ更新されてズレる)。フォーマット仕様も
generate_pimg.py のヘッダコメント参照。

使い方:
    python3 pimg2png.py input.pimg output.png
    python3 pimg2png.py input.pimg output.png --scale 4
"""

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

from generate_pimg import PALETTE


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

    return w, h, has_transparent, indices


def write_png(w, h, has_transparent, indices, palette, out_path: Path, scale: int):
    mode = "RGBA" if has_transparent else "RGB"
    img = Image.new(mode, (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            i = y * w + x
            idx = indices[i] if i < len(indices) else 0
            r, g, b = palette[idx]
            if has_transparent:
                px[x, y] = (r, g, b, 0) if idx == 0 else (r, g, b, 255)
            else:
                px[x, y] = (r, g, b)

    if scale != 1:
        img = img.resize((w * scale, h * scale), Image.NEAREST)
    img.save(out_path)


def main():
    parser = argparse.ArgumentParser(description="Convert a pico-os .pimg file back to PNG (for visual inspection)")
    parser.add_argument("input", type=Path, help="入力 .pimg ファイル")
    parser.add_argument("output", type=Path, help="出力先 PNG ファイル")
    parser.add_argument("--scale", type=int, default=1, help="拡大率(最近傍補間、既定1)")
    args = parser.parse_args()

    if not args.input.exists():
        print(f"error: input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    w, h, has_transparent, indices = read_pimg(args.input)
    write_png(w, h, has_transparent, indices, PALETTE, args.output, args.scale)

    print(f"input : {args.input}  ({w}x{h}, transparent={has_transparent})")
    print(f"output: {args.output}")


if __name__ == "__main__":
    main()
