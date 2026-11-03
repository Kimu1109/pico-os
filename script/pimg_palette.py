#!/usr/bin/env python3
"""
pimg_palette.py
---------------
.pimg(4bpp・16色の番号)の番号を色へ当てるパレットを選ぶ・読む。
generate_pimg.py(画像→.pimg の減色)と pimg2png.py(.pimg→PNG)が共通で使う。

.pimg は色そのものを持たず番号だけを持つので、同じファイルでも「どのパレットで描くか」で見え方が変わる
(ダークテーマ・Luaの pico.set_palette() で差し替えたアプリ)。ここで選べるのは:

  組み込みの名前  default(既定の16色。src/gui/core/GFX_Functions.hpp の DEFAULT_COLORS)
                  dark   (ダークテーマ。同じファイルの DARK_COLORS)
  ファイル        .gpl(GIMPのパレット)/ .json(["#RRGGBB", ...] か [[r,g,b], ...]、
                  {"colors": [...]} も可)/ それ以外は1行に1色の16進(#RRGGBB・RRGGBB・0xRRGGBB。
                  「#」で始まり16進6桁でない行と ; // の後ろは注釈)
  その場の並び    "000000,1d2b53,..." のようにカンマか空白で区切った16進

16色より少なければ残りは既定の色のまま(先頭から順に差し替える)。17色以上は断る。
さらに --set-color 3=ff8800 のような「番号=色」で1色ずつ差し替えられる(parse_overrides)。

標準ライブラリだけで動く(Pillow は要らない)。
"""

import json
import re
from pathlib import Path

# ------------------------------------------------------------------
# 既定の16色(RGB888)。src/gui/core/GFX_Functions.hpp の DEFAULT_COLORS(LovyanGFXの TFT_*)と
# 番号の順を合わせること。値は TFT_* の RGB565 を8bitへ広げる前の色(LovyanGFX の定義の元の色)。
# ------------------------------------------------------------------
DEFAULT_PALETTE = [
    (0x00, 0x00, 0x00),  # 0: 透過 / 黒
    (0x00, 0x00, 0x80),  # 1: navy
    (0x00, 0x80, 0x00),  # 2: dark green
    (0x00, 0x80, 0x80),  # 3: dark cyan
    (0x80, 0x00, 0x00),  # 4: maroon
    (0x80, 0x00, 0x80),  # 5: purple
    (0x80, 0x80, 0x00),  # 6: olive
    (0xD3, 0xD3, 0xD3),  # 7: lightgrey
    (0x80, 0x80, 0x80),  # 8: darkgrey
    (0x00, 0x00, 0xFF),  # 9: blue
    (0x00, 0xFF, 0x00),  # 10: green
    (0x00, 0xFF, 0xFF),  # 11: cyan
    (0xFF, 0x00, 0x00),  # 12: red
    (0xFF, 0x00, 0xFF),  # 13: magenta
    (0xFF, 0xFF, 0x00),  # 14: yellow
    (0xFF, 0xFF, 0xFF),  # 15: white
]

# ダークテーマ(GFX_Functions.hpp の DARK_COLORS の Rgb565Const へ渡している値)。向こうを変えたらここも
DARK_PALETTE = [
    (235, 235, 235),  # 0  文字
    (120, 150, 255),  # 1  紺 → 明るい青
    (100, 220, 120),  # 2  深緑 → 明るい緑
    (90, 210, 220),   # 3  暗い水色 → 明るい水色
    (255, 120, 120),  # 4  えんじ → 明るい赤
    (220, 130, 255),  # 5  紫 → 明るい紫
    (220, 210, 90),   # 6  オリーブ → 明るい黄土
    (64, 64, 72),     # 7  薄い灰 → 暗い灰
    (170, 170, 176),  # 8  濃い灰 → 明るい灰
    (70, 110, 255),   # 9  青
    (40, 200, 60),    # 10 緑
    (0, 100, 110),    # 11 水色 → 暗い水色
    (255, 70, 70),    # 12 赤
    (230, 80, 230),   # 13 マゼンタ
    (230, 200, 40),   # 14 黄
    (16, 16, 20),     # 15 背景
]

BUILTIN = {
    "default": (DEFAULT_PALETTE, "既定の16色(明るいテーマ。GFX_Functions.hpp の DEFAULT_COLORS)"),
    "dark": (DARK_PALETTE, "ダークテーマ(GFX_Functions.hpp の DARK_COLORS)"),
}

PALETTE_SIZE = 16

_HEX_RE = re.compile(r"^(?:#|0x|0X)?([0-9A-Fa-f]{6})$")


class PaletteError(ValueError):
    pass


def parse_color(text: str):
    """'#RRGGBB' / 'RRGGBB' / '0xRRGGBB' / 'r,g,b'(0〜255)を (r, g, b) へ。"""
    s = text.strip()
    m = _HEX_RE.match(s)
    if m:
        v = int(m.group(1), 16)
        return ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)
    parts = [p for p in re.split(r"[,\s]+", s) if p]
    if len(parts) == 3 and all(p.isdigit() for p in parts):
        rgb = tuple(int(p) for p in parts)
        if all(0 <= c <= 255 for c in rgb):
            return rgb
    raise PaletteError(f"色として読めません: {text!r}(#RRGGBB か r,g,b)")


def _from_json(text: str, where: str):
    try:
        data = json.loads(text)
    except json.JSONDecodeError as e:
        raise PaletteError(f"{where}: JSONとして読めません: {e}") from None
    if isinstance(data, dict):
        data = data.get("colors", data.get("palette"))
    if not isinstance(data, list):
        raise PaletteError(f"{where}: 色の配列(または {{\"colors\": [...]}})が要ります")
    colors = []
    for item in data:
        if isinstance(item, str):
            colors.append(parse_color(item))
        elif isinstance(item, (list, tuple)) and len(item) == 3 and all(isinstance(c, int) for c in item):
            if not all(0 <= c <= 255 for c in item):
                raise PaletteError(f"{where}: 0〜255の外の値があります: {item}")
            colors.append(tuple(item))
        else:
            raise PaletteError(f"{where}: 色として読めません: {item!r}")
    return colors


def _from_gpl(text: str, where: str):
    lines = text.splitlines()
    if not lines or not lines[0].strip().startswith("GIMP Palette"):
        raise PaletteError(f"{where}: 先頭が 'GIMP Palette' ではありません")
    colors = []
    for line in lines[1:]:
        s = line.strip()
        if not s or s.startswith("#") or ":" in s.split()[0]:
            continue  # 注釈・Name:/Columns: の行
        parts = s.split()
        if len(parts) >= 3 and all(p.isdigit() for p in parts[:3]):
            colors.append(tuple(int(p) for p in parts[:3]))
    return colors


def _from_hex_lines(text: str, where: str):
    colors = []
    for n, line in enumerate(text.splitlines(), 1):
        s = re.split(r";|//", line, maxsplit=1)[0].strip()
        if not s:
            continue
        # 1行にカンマ区切りで並べてあってもよい
        for tok in [t for t in re.split(r"[,\s]+", s) if t]:
            if tok.startswith("#") and not _HEX_RE.match(tok):
                break  # 「# 注釈」
            try:
                colors.append(parse_color(tok))
            except PaletteError:
                raise PaletteError(f"{where}:{n}: 色として読めません: {tok!r}") from None
    return colors


def _fill(colors, where: str):
    if not colors:
        raise PaletteError(f"{where}: 色が1つもありません")
    if len(colors) > PALETTE_SIZE:
        raise PaletteError(f"{where}: 色が{len(colors)}個あります(.pimg は{PALETTE_SIZE}色まで)")
    return list(colors) + DEFAULT_PALETTE[len(colors):]


def load_palette(spec):
    """名前・ファイル・その場の並びから16色のパレットを作る。spec が None なら既定。"""
    if spec is None or spec == "":
        return list(DEFAULT_PALETTE)
    if spec in BUILTIN:
        return list(BUILTIN[spec][0])
    path = Path(spec).expanduser()
    if path.is_file():
        text = path.read_text(encoding="utf-8", errors="replace")
        where = str(path)
        suffix = path.suffix.lower()
        if suffix == ".json":
            return _fill(_from_json(text, where), where)
        if suffix == ".gpl" or text.lstrip().startswith("GIMP Palette"):
            return _fill(_from_gpl(text, where), where)
        return _fill(_from_hex_lines(text, where), where)
    # ファイルでも名前でもなければ、その場の並び("000000,1d2b53,...")として読む
    if re.fullmatch(r"[#0-9A-Fa-fxX,\s]+", spec) and re.search(r"[0-9A-Fa-f]{6}", spec):
        return _fill(_from_hex_lines(spec, "--palette"), "--palette")
    names = ", ".join(BUILTIN)
    raise PaletteError(f"パレットが見つかりません: {spec!r}(組み込みは {names}、またはファイル/16進の並び)")


def parse_overrides(items):
    """['3=ff8800', '12=#00ff00'] を {3: (255,136,0), ...} へ。"""
    out = {}
    for item in items or []:
        idx, sep, color = item.partition("=")
        if not sep or not idx.strip().isdigit():
            raise PaletteError(f"--set-color は 番号=色 の形です: {item!r}")
        i = int(idx)
        if not 0 <= i < PALETTE_SIZE:
            raise PaletteError(f"--set-color の番号は0〜{PALETTE_SIZE - 1}: {item!r}")
        out[i] = parse_color(color)
    return out


def resolve(spec=None, overrides=None, rgb565=False):
    """load_palette + parse_overrides。rgb565=True なら本体の液晶と同じく RGB565 へ丸めた色にする。"""
    pal = load_palette(spec)
    for i, rgb in parse_overrides(overrides).items():
        pal[i] = rgb
    if rgb565:
        pal = [to_rgb565_888(c) for c in pal]
    return pal


def to_rgb565_888(rgb):
    """RGB565 へ丸めてから8bitへ戻した色(本体の画面で実際に出る色に近い)。"""
    r, g, b = rgb
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def add_arguments(parser):
    """argparse へ --palette / --set-color / --list-palettes を足す(2つのスクリプトで同じ書き方にする)。"""
    parser.add_argument(
        "--palette", metavar="NAME|FILE|HEX,...", default=None,
        help="番号に当てるパレット。default(既定)/ dark / .gpl・.json・16進を並べたファイル / "
             "'000000,1d2b53,...' のような16進の並び(16色未満なら残りは既定)",
    )
    parser.add_argument(
        "--set-color", metavar="N=COLOR", action="append", default=[],
        help="番号Nの色だけ差し替える(例 --set-color 3=ff8800。何度でも指定できる)",
    )
    parser.add_argument("--list-palettes", action="store_true", help="組み込みのパレットを並べて終わる")


def print_builtin():
    for name, (pal, desc) in BUILTIN.items():
        print(f"{name}: {desc}")
        for i, (r, g, b) in enumerate(pal):
            print(f"  {i:2d}  #{r:02x}{g:02x}{b:02x}")


def format_palette(pal):
    return ",".join(f"{r:02x}{g:02x}{b:02x}" for r, g, b in pal)
