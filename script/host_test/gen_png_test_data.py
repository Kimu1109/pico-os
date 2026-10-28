#!/usr/bin/env python3
"""image_codec_test / lua_ext_test が使う小さなPNGを png_test_data.hpp へ書き出す(標準ライブラリだけ)。

    python3 script/host_test/gen_png_test_data.py > script/host_test/png_test_data.hpp

4分割(左上=赤 右上=緑 左下=青 右下=白)の絵を、色の種類・ビット数・インターレースを変えて作る。
"""
import struct
import zlib

RED, GREEN, BLUE, WHITE = (255, 0, 0), (0, 170, 0), (0, 0, 255), (255, 255, 255)


def quad(x, y, w, h):
    top, left = y < h // 2, x < w // 2
    if top:
        return RED if left else GREEN
    return BLUE if left else WHITE


def chunk(kind, data):
    c = kind + data
    return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)


# Adam7 の各段: (x0, y0, dx, dy)
ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


def png(w, h, color_type, depth, pixel_bytes, interlace=False, plte=None, trns=None):
    """pixel_bytes(x, y) は1画素ぶんのバイト列(depth<8 のときは番号の整数)"""
    def row(xs, y):
        if depth >= 8:
            return b"\x00" + b"".join(pixel_bytes(x, y) for x in xs)
        bits, n = 0, 0
        out = bytearray()
        for x in xs:
            bits = (bits << depth) | pixel_bytes(x, y)
            n += depth
            if n == 8:
                out.append(bits); bits, n = 0, 0
        if n:
            out.append(bits << (8 - n))
        return b"\x00" + bytes(out)

    raw = b""
    if interlace:
        for x0, y0, dx, dy in ADAM7:
            xs = list(range(x0, w, dx))
            if not xs:
                continue
            for y in range(y0, h, dy):
                raw += row(xs, y)
    else:
        for y in range(h):
            raw += row(range(w), y)
    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, color_type, 0, 0, 1 if interlace else 0))
    if plte:
        out += chunk(b"PLTE", b"".join(bytes(c) for c in plte))
    if trns is not None:
        out += chunk(b"tRNS", trns)
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    out += chunk(b"IEND", b"")
    return out


def rgb(x, y, w, h):
    return bytes(quad(x, y, w, h))


IMAGES = [
    ("kPngQuad32", "32x32 RGB 8bit", png(32, 32, 2, 8, lambda x, y: rgb(x, y, 32, 32))),
    ("kPngQuad64x48", "64x48 RGB 8bit", png(64, 48, 2, 8, lambda x, y: rgb(x, y, 64, 48))),
    ("kPngQuadInterlaced", "32x32 RGB 8bit インターレース", png(32, 32, 2, 8, lambda x, y: rgb(x, y, 32, 32), interlace=True)),
    ("kPngQuad16bit", "16x16 RGB 16bit", png(16, 16, 2, 16, lambda x, y: b"".join(bytes([c, c]) for c in quad(x, y, 16, 16)))),
    # 左上を透明(α=0)、右上を半透明(α=128)にしたRGBA
    ("kPngQuadAlpha", "32x32 RGBA 8bit(左上α=0・右上α=128)",
     png(32, 32, 6, 8, lambda x, y: rgb(x, y, 32, 32) + bytes([0 if (x < 16 and y < 16) else (128 if y < 16 else 255)]))),
    # パレット4色(2bit)+tRNSで0番(赤)を透明
    ("kPngQuadPalette", "16x16 パレット2bit tRNSで赤が透明",
     png(16, 16, 3, 2, lambda x, y: [RED, GREEN, BLUE, WHITE].index(quad(x, y, 16, 16)),
         plte=[RED, GREEN, BLUE, WHITE], trns=b"\x00")),
    # グレー8bit: 左半分が黒、右半分が白
    ("kPngGray", "8x8 グレー8bit(左=黒 右=白)", png(8, 8, 0, 8, lambda x, y: bytes([0 if x < 4 else 255]))),
]


def main():
    print("// image_codec_test / lua_ext_test の PNG。script/host_test/gen_png_test_data.py が作る(手で直さない)")
    print("//   4分割(左上=赤 右上=緑 左下=青 右下=白)を、色の種類・ビット数・インターレースを変えたもの")
    print("#pragma once")
    for name, desc, data in IMAGES:
        print()
        print(f"// {desc}({len(data)}バイト)")
        print(f"static const unsigned char {name}[] = {{")
        for i in range(0, len(data), 20):
            print("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 20]) + ",")
        print("};")


if __name__ == "__main__":
    main()
