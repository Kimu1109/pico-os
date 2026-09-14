#!/usr/bin/env python3
"""
ppm2png.py
----------
picoos_pc の --shot が書き出すPPM(P6)をPNGへ変換する。

LovyanGFXのSDLパネルはPPMで書き出すが、そのままでは見づらいので画像ビューアや
ブラウザで開ける形にする。標準ライブラリだけで動く(Pillowを入れなくてよい)。

    python3 script/ppm2png.py shot.ppm shot.png      # 等倍
    python3 script/ppm2png.py shot.ppm shot.png 2    # 2倍(240x320は小さいので拡大すると見やすい)
"""
import struct, sys, zlib

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # ヘッダ: P6 <w> <h> <maxval> の後に1バイトの空白、以降が画素
    parts, pos = [], 2
    while len(parts) < 3:
        while pos < len(data) and data[pos:pos+1].isspace(): pos += 1
        if data[pos:pos+1] == b"#":
            while data[pos:pos+1] not in (b"\n", b""): pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos:pos+1].isspace(): pos += 1
        parts.append(int(data[start:pos]))
    pos += 1
    w, h, _ = parts
    return w, h, data[pos:pos + w*h*3]

def write_png(path, w, h, rgb, scale=1):
    sw, sh = w*scale, h*scale
    raw = bytearray()
    for y in range(h):
        row = rgb[y*w*3:(y+1)*w*3]
        if scale > 1:
            row = b"".join(row[x*3:x*3+3]*scale for x in range(w))
        for _ in range(scale):
            raw += b"\x00" + row
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)

if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    w, h, rgb = read_ppm(src)
    write_png(dst, w, h, rgb, scale)
    print("%s -> %s (%dx%d, x%d)" % (src, dst, w, h, scale))
