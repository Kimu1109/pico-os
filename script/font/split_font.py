#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
split_font.py — 日本語フォントを「フラッシュに残す字」と「SDの追加フォント」に分ける。
docs/design/performance.md「漢字の一部をSDへ」。

LovyanGFX の lgfx_font_japan.c(IPAexゴシックを u8g2 形式にしたもの)から、16px と 24px のプロポーショナル
(lgfx_font_japan_gothic_p_16 / _24)を読み、

  1. フラッシュ用: かな・記号・英数字の全部と、script/font/base_kanji.txt の漢字だけを持つ u8g2 のフォント
     → src/gui/fonts/JpFont_Base16.cpp / JpFont_Base24.cpp(配列)
  2. SD用: 残りの漢字だけを持つ追加フォント(下の「.pkf の形」)
     → pc/sdcard/sys/fonts/jpgothic16.pkf / jpgothic24.pkf(PCビルドのSDの中身。Webの配布元へもここから写す)
  3. 両方をつなぐ定数(追加フォントの印・大きさ・SHA-256・配布元の名前)
     → src/gui/fonts/JpFont_Data.hpp

を書き出す。字のデータ(u8g2 の1字ぶんの記録)は1バイトも変えずに移すだけなので、描いた絵は元と同じ。

使い方:
    python3 script/font/split_font.py                 # 書き出す(LovyanGFX はPCビルドかPlatformIOの取得先から探す)
    python3 script/font/split_font.py --check         # 書き出したものが今の入力と合っているかだけ見る(違えば終了コード1)
    python3 script/font/split_font.py --lgfx <lgfx_font_japan.c>

---- .pkf の形(数値はリトルエンディアン) ----
   0  "PKF1"
   4  u8  版(1)
   5  u8  字の高さ(16/24)
   6  u16 索引の数(n_index。kMaxIndex 以下)
   8  u32 印(ext_id。元のフォントの中身とフラッシュに残す字から作る。ファームの JpFont_Data.hpp と合わなければ使わない)
  12  u32 字の数
  16  u16 1字の記録の最大の大きさ(見出し3Bを含む)
  18  u16 0
  20  u32 字の並びの位置(ファイルの先頭から)
  24  u32 字の並びのバイト数(終わりの0x0000の2Bを含む)
  28  索引 n_index 個: [u16 その区切りの最初の文字コード][u32 その字の記録の位置(字の並びの先頭から)]
      (stride 字おき。stride は 字の数/kMaxIndex を切り上げたもの)
  …   字の並び: u8g2 の0x100以上の部と同じ [文字コード2B(ビッグエンディアン)][記録の大きさ1B][データ…] を文字コード順に。
      最後に 0x0000

ライセンス: 元のフォントは IPAフォントライセンスv1.0。分けたもの(フラッシュ用・SD用とも)はその「派生プログラム」で、
同じライセンスで配る(第3条1項)。ファイル名・フォント名に元の名前(IPA…)を使わないこと(同(4))。
pc/sdcard/sys/fonts/README.md に元へ戻す方法を書いてある(同(2))。
"""

import argparse
import glob
import hashlib
import os
import struct
import sys
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

SIZES = (16, 24)
SRC_NAME = "lgfx_font_japan_gothic_p_{}"
OUT_SRC_DIR = os.path.join(ROOT, "src", "gui", "fonts")
OUT_SD_DIR = os.path.join(ROOT, "pc", "sdcard", "sys", "fonts")
BASE_LIST = os.path.join(ROOT, "script", "font", "base_kanji.txt")

PKF_VERSION = 1
PKF_HEADER = 28
K_MAX_INDEX = 128      # src/gui/core/ExtGlyphStore.hpp の kMaxIndex と同じ
HEADER_SIZE = 23       # u8g2 のフォントの見出し


def is_kanji(code):
    return 0x3400 <= code <= 0x9FFF or 0xF900 <= code <= 0xFAFF


# ---------------------------------------------------------------------------
# 入力
# ---------------------------------------------------------------------------

def find_lgfx_font_c():
    pats = [
        os.path.join(ROOT, "pc", "build*", "_deps", "lovyangfx-src", "src", "lgfx", "Fonts", "IPA", "lgfx_font_japan.c"),
        os.path.join(ROOT, ".pio", "libdeps", "*", "LovyanGFX", "src", "lgfx", "Fonts", "IPA", "lgfx_font_japan.c"),
    ]
    for p in pats:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[0]
    return None


def decode_c_string(body, i):
    """body[i] は '"' の次。閉じる '"' までを読んでバイト列と次の位置を返す(C の文字列の書き方)"""
    out = bytearray()
    n = len(body)
    simple = {"n": 10, "t": 9, "r": 13, "0": 0, "a": 7, "b": 8, "f": 12, "v": 11,
              "\\": 92, "'": 39, '"': 34, "?": 63}
    while i < n:
        c = body[i]
        if c == '"':
            return bytes(out), i + 1
        if c != "\\":
            out.append(ord(c))
            i += 1
            continue
        i += 1
        c = body[i]
        if c in "01234567":
            j = i
            while j < n and j - i < 3 and body[j] in "01234567":
                j += 1
            out.append(int(body[i:j], 8) & 0xFF)
            i = j
        elif c == "x":
            j = i + 1
            while j < n and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(body[i + 1:j], 16) & 0xFF)
            i = j
        elif c in simple:
            out.append(simple[c])
            i += 1
        else:
            raise ValueError("知らないエスケープ \\" + c)
    raise ValueError("文字列が閉じていない")


def read_font_array(text, name):
    key = "uint8_t " + name + "["
    at = text.find(key)
    if at < 0:
        raise SystemExit(f"{name} が見つかりません")
    lb = text.index("[", at)
    rb = text.index("]", lb)
    size = int(text[lb + 1:rb])
    i = text.index("=", rb) + 1
    data = bytearray()
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            chunk, i = decode_c_string(text, i + 1)
            data += chunk
        elif c == ";":
            break
        else:
            i += 1
    if len(data) == size - 1:
        data.append(0)  # 文字列の終わりの0も配列に入る(宣言の大きさが中身+1のとき)
    if len(data) != size:
        raise SystemExit(f"{name}: 読めたのが {len(data)}B で、宣言の {size}B と違います")
    return bytes(data)


def read_base_chars(path):
    chars = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                continue
            for ch in line.strip():
                if not ch.isspace():
                    chars.add(ord(ch))
    return chars


# ---------------------------------------------------------------------------
# u8g2 の形
# ---------------------------------------------------------------------------

def be16(d, p):
    return (d[p] << 8) | d[p + 1]


def parse_u8g2(font):
    """(見出し23B, 0〜255の部のバイト列(終わりの0を含む), [(文字コード, 記録)…]) を返す"""
    header = font[:HEADER_SIZE]
    uni = HEADER_SIZE + be16(font, 21)
    ascii_part = font[HEADER_SIZE:uni]
    # 表を飛ばす
    p = uni
    first = uni + be16(font, uni)
    glyphs = []
    p = first
    while True:
        code = be16(font, p)
        if code == 0:
            break
        size = font[p + 2]
        glyphs.append((code, font[p:p + size]))
        p += size
    codes = [c for c, _ in glyphs]
    if codes != sorted(codes) or len(set(codes)) != len(codes):
        raise SystemExit("0x100以上の字が文字コード順に並んでいません")
    return header, ascii_part, glyphs


def build_u8g2(header, ascii_part, glyphs, block=100):
    """0x100以上の部を作り直す。表の項目は [前の区画の先頭からの距離][区画の最後の文字コード](最後は 0xFFFF)"""
    blocks = [glyphs[i:i + block] for i in range(0, len(glyphs), block)]
    n_entries = len(blocks)  # 最後の区画の文字コードを 0xFFFF にする(元の u8g2 の作りと同じ)
    table_size = 4 * n_entries
    table = bytearray()
    body = bytearray()
    prev_start = 0  # 表の先頭からの位置
    for bi, b in enumerate(blocks):
        start = table_size + len(body)
        dist = start - prev_start
        last = 0xFFFF if bi == len(blocks) - 1 else b[-1][0]
        if dist > 0xFFFF:
            raise SystemExit("区画が大きすぎます(block を小さくすること)")
        table += struct.pack(">HH", dist, last)
        prev_start = start
        for _, rec in b:
            body += rec
    body += b"\x00\x00"
    uni = bytes(table) + bytes(body)
    hdr = bytearray(header)
    off = len(ascii_part)
    hdr[21] = (off >> 8) & 0xFF
    hdr[22] = off & 0xFF
    return bytes(hdr) + ascii_part + uni


def build_pkf(px, ext_id, glyphs):
    n = len(glyphs)
    stride = max(1, (n + K_MAX_INDEX - 1) // K_MAX_INDEX)
    data = bytearray()
    index = []
    for i, (code, rec) in enumerate(glyphs):
        if i % stride == 0:
            index.append((code, len(data)))
        data += rec
    data += b"\x00\x00"
    max_rec = max((len(r) for _, r in glyphs), default=0)
    data_off = PKF_HEADER + 6 * len(index)
    out = bytearray()
    out += b"PKF1"
    out += struct.pack("<BBH", PKF_VERSION, px, len(index))
    out += struct.pack("<II", ext_id, n)
    out += struct.pack("<HH", max_rec, 0)
    out += struct.pack("<II", data_off, len(data))
    for code, pos in index:
        out += struct.pack("<HI", code, pos)
    assert len(out) == data_off
    out += data
    return bytes(out), max_rec


# ---------------------------------------------------------------------------
# 書き出し
# ---------------------------------------------------------------------------

def c_bytes(name, data, comment):
    """C++ の文字列リテラルで配列を書く(8進は必ず3桁にして、続く数字とつながらないようにする)"""
    lines = [comment, f"extern const uint8_t {name}[{len(data) + 1}];",
             f"const uint8_t {name}[{len(data) + 1}] ="]
    cur = '  "'
    for b in data:
        if 0x20 <= b < 0x7F and b not in (0x22, 0x5C, 0x3F):
            s = chr(b)
        else:
            s = "\\%03o" % b
        cur += s
        if len(cur) >= 110:
            lines.append(cur + '"')
            cur = '  "'
    lines.append(cur + '";')
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lgfx", help="LovyanGFX の src/lgfx/Fonts/IPA/lgfx_font_japan.c")
    ap.add_argument("--check", action="store_true", help="書き出さず、今あるものと合っているかだけ見る")
    args = ap.parse_args()

    src = args.lgfx or find_lgfx_font_c()
    if not src or not os.path.exists(src):
        raise SystemExit("lgfx_font_japan.c が見つかりません(先にPCビルドの configure をするか --lgfx で指定)")
    with open(src, encoding="latin-1") as f:
        text = f.read()
    base = read_base_chars(BASE_LIST)
    base_sorted = "".join(chr(c) for c in sorted(base)).encode("utf-8")

    outputs = {}  # パス -> 中身(bytes)
    consts = []
    for px in SIZES:
        name = SRC_NAME.format(px)
        font = read_font_array(text, name)
        header, ascii_part, glyphs = parse_u8g2(font)
        keep = [(c, r) for c, r in glyphs if not is_kanji(c) or c in base]
        ext = [(c, r) for c, r in glyphs if is_kanji(c) and c not in base]
        ext_id = zlib.crc32(base_sorted, zlib.crc32(font)) & 0xFFFFFFFF
        flash = build_u8g2(header, ascii_part, keep)
        pkf, max_rec = build_pkf(px, ext_id, ext)
        sha = hashlib.sha256(pkf).digest()
        web_name = f"jpgothic{px}-{ext_id:08x}.pkf"

        arr = f"kJpGothicBase{px}"
        outputs[os.path.join(OUT_SRC_DIR, f"JpFont_Base{px}.cpp")] = (
            "// 生成物。手で直さず script/font/split_font.py で作り直すこと。\n"
            "// 日本語フォントのうちフラッシュに残す字(かな・記号・英数字・script/font/base_kanji.txt の漢字)。u8g2 の形。\n"
            "// 元は LovyanGFX の lgfx_font_japan_gothic_p_%d(IPAexゴシックを変換したもの)。\n"
            "// IPAフォントライセンスv1.0 の派生プログラム(THIRD_PARTY_LICENSES.md、pc/sdcard/sys/fonts/README.md)。\n"
            "#include \"gui/fonts/JpFont_Data.hpp\"\n\n" % px
            + c_bytes(arr, flash, f"// {len(keep)}字(元の {len(glyphs)}字のうち。漢字 {sum(1 for c, _ in keep if is_kanji(c))}字)")
        ).encode("utf-8")
        outputs[os.path.join(OUT_SD_DIR, f"jpgothic{px}.pkf")] = pkf
        consts.append((px, arr, len(flash), len(font), len(keep), len(ext), ext_id, len(pkf), sha, max_rec, web_name))

    h = []
    h.append("#pragma once")
    h.append("// 生成物。手で直さず script/font/split_font.py で作り直すこと(docs/design/performance.md「漢字の一部をSDへ」)。")
    h.append("// フラッシュに残した日本語フォントと、SDに置く追加フォント(/sys/fonts/jpgothic<px>.pkf)をつなぐ定数。")
    h.append("#include <cstdint>")
    h.append("")
    for px, arr, flash_len, orig_len, nkeep, next_, ext_id, pkf_len, sha, max_rec, web_name in consts:
        h.append(f"extern const uint8_t {arr}[{flash_len + 1}];")
    h.append("")
    h.append("namespace JpFontData {")
    h.append("    struct Ext {")
    h.append("        uint8_t px;                 // 字の高さ")
    h.append("        uint32_t id;                // .pkf の見出しの印(合わなければ使わない)")
    h.append("        uint32_t bytes;             // .pkf の大きさ")
    h.append("        uint16_t glyphs;            // .pkf の字の数")
    h.append("        uint16_t max_record;        // .pkf の1字の記録の最大")
    h.append("        uint8_t sha256[32];         // .pkf のSHA-256(Webから落としたものを確かめる)")
    h.append("        const char* sd_path;        // SDの置き場所")
    h.append("        const char* web_name;       // 配布元(web-pages.yml の /fonts/)での名前")
    h.append("    };")
    for px, arr, flash_len, orig_len, nkeep, next_, ext_id, pkf_len, sha, max_rec, web_name in consts:
        h.append(f"    // {px}px: フラッシュ {flash_len}B({nkeep}字。元は {orig_len}B)、SD {pkf_len}B({next_}字)")
        sha_s = ", ".join(f"0x{b:02x}" for b in sha)
        h.append(f"    constexpr Ext kExt{px} = {{ {px}, 0x{ext_id:08x}u, {pkf_len}u, {next_}, {max_rec},")
        h.append(f"        {{ {sha_s} }},")
        h.append(f"        \"/sys/fonts/jpgothic{px}.pkf\", \"{web_name}\" }};")
    h.append("}")
    outputs[os.path.join(OUT_SRC_DIR, "JpFont_Data.hpp")] = ("\n".join(h) + "\n").encode("utf-8")

    bad = []
    for path, data in outputs.items():
        old = open(path, "rb").read() if os.path.exists(path) else None
        if args.check:
            if old != data:
                bad.append(os.path.relpath(path, ROOT))
            continue
        if old != data:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            print("書いた:", os.path.relpath(path, ROOT), len(data), "B")
    for px, arr, flash_len, orig_len, nkeep, next_, ext_id, pkf_len, sha, max_rec, web_name in consts:
        print(f"{px}px: フラッシュ {orig_len}B -> {flash_len}B(-{orig_len - flash_len}B)、"
              f"SD {pkf_len}B({next_}字、1字の最大 {max_rec}B)、印 {ext_id:08x}")
    if bad:
        print("今の入力と合わないもの(split_font.py で作り直すこと):", *bad, sep="\n  ")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
