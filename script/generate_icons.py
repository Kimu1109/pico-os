#!/usr/bin/env python3
"""
generate_icons.py

Tabler Icons (SVG) を「完全二値・交互ラン長RLE」形式に変換し、
pico-os のC++ヘッダファイル (include/icons_data.h) を生成する。

依存パッケージ:
    uv pip install -r requirements.txt
    (cairosvg, pillow)

使い方:
    1. tabler_icons/ に必要なSVGファイルを配置する
       (https://tabler.io/icons からダウンロード)
    2. ICONS リストを編集して収録アイコンを追加/削除する
       (サイズは指定不要。ALL_SIZESで定義した全サイズが自動生成される)
    3. python3 tools/generate_icons.py を実行
       -> include/icons_data.h が生成される
    4. platformio.ini の extra_scripts に登録しておくと
       ビルド時に自動実行される (README参照)

出力データフォーマット（完全二値・交互ラン長）:
    パレット制描画のため中間色は一切持たない。各アイコンは
    「off(透明)ランの長さ, on(不透明)ランの長さ, off, on, ...」
    という1byteの長さ値だけが交互に並ぶ形式でRLE符号化される
    (値そのものは持たず、出現位置の偶奇だけで off/on が決まる)。
    先頭は必ずoffランから始まる(onから始まる画像の場合は長さ0のoffランを挿入)。
    1つのランが255を超える場合は、255で区切って反対側の長さ0ランを挟み、
    同じ色のランを継続する。
    画素は行優先(左上から右へ、右端で次の行へ)で並ぶ。
    二値化は単純な閾値判定のみ(ディザリングは撤去済み)。
"""

import io
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    import cairosvg
    from PIL import Image
except ImportError:
    print("必要なパッケージが見つかりません: cairosvg, PIL")
    print("- uv venv : 仮想環境を構築しましたか?")
    print("- uv pip install -r requirements.txt : 依存パッケージをインストールしましたか?")
    sys.exit(1)


# ============================================================
# 収録アイコン定義（ここを編集してアイコンを追加/削除する）
# サイズは指定しない。ALL_SIZES に列挙した全サイズが自動生成される。
# ============================================================

@dataclass
class IconSpec:
    name: str        # C++側 enum 名の元になる (例: "wifi_signal_4" -> WifiSignal4)
    svg_path: str     # SVGファイルへの相対パス


ICON_DIR = Path("tabler_icons")

ICONS: list[IconSpec] = [
    # --- ステータス ---
    IconSpec("wifi_signal_1",    str(ICON_DIR / "wifi-0.svg")),
    IconSpec("wifi_signal_2",    str(ICON_DIR / "wifi-1.svg")),
    IconSpec("wifi_signal_3",    str(ICON_DIR / "wifi-2.svg")),
    IconSpec("wifi_signal_4",    str(ICON_DIR / "wifi.svg")),
    IconSpec("wifi_off",         str(ICON_DIR / "wifi-off.svg")),
    IconSpec("battery_0",        str(ICON_DIR / "battery.svg")),
    IconSpec("battery_1",        str(ICON_DIR / "battery-1.svg")),
    IconSpec("battery_2",        str(ICON_DIR / "battery-2.svg")),
    IconSpec("battery_3",        str(ICON_DIR / "battery-3.svg")),
    IconSpec("battery_4",        str(ICON_DIR / "battery-4.svg")),
    IconSpec("battery_charging", str(ICON_DIR / "battery-charging-2.svg")),
    IconSpec("sd_card",          str(ICON_DIR / "device-sd-card.svg")),
    IconSpec("volume_high",      str(ICON_DIR / "volume.svg")),
    IconSpec("volume_low",       str(ICON_DIR / "volume-2.svg")),
    IconSpec("volume_off",       str(ICON_DIR / "volume-3.svg")),
    IconSpec("sun_0",            str(ICON_DIR / "sun-off.svg")),
    IconSpec("sun_1",            str(ICON_DIR / "sun-low.svg")),
    IconSpec("sun_2",            str(ICON_DIR / "sun.svg")),
    IconSpec("sun_3",            str(ICON_DIR / "sun-high.svg")),
    IconSpec("bell",             str(ICON_DIR / "bell.svg")),
    IconSpec("bell_off",         str(ICON_DIR / "bell-off.svg")),
    IconSpec("bell_ringing",     str(ICON_DIR / "bell-ringing.svg")),
    IconSpec("lock",             str(ICON_DIR / "lock.svg")),
    IconSpec("lock_off",         str(ICON_DIR / "lock-off.svg")),
    IconSpec("eye",              str(ICON_DIR / "eye.svg")),
    IconSpec("eye_off",          str(ICON_DIR / "eye-off.svg")),

    # --- 汎用 ---
    IconSpec("home",               str(ICON_DIR / "home.svg")),
    IconSpec("search",             str(ICON_DIR / "search.svg")),
    IconSpec("settings",           str(ICON_DIR / "settings.svg")),
    IconSpec("trash",              str(ICON_DIR / "trash.svg")),
    IconSpec("arrow_up",           str(ICON_DIR / "arrow-narrow-up.svg")),
    IconSpec("arrow_left",         str(ICON_DIR / "arrow-narrow-left.svg")),
    IconSpec("arrow_down",         str(ICON_DIR / "arrow-narrow-down.svg")),
    IconSpec("arrow_right",        str(ICON_DIR / "arrow-narrow-right.svg")),
    IconSpec("x",                  str(ICON_DIR / "x.svg")),
    IconSpec("checkbox_on",        str(ICON_DIR / "checkbox.svg")),
    IconSpec("checkbox_off",       str(ICON_DIR / "square.svg")),
    IconSpec("circle_dashed_plus", str(ICON_DIR / "circle-dashed-plus.svg")),
    IconSpec("refresh",            str(ICON_DIR / "refresh.svg")),
    IconSpec("power",              str(ICON_DIR / "power.svg")),
    IconSpec("menu",               str(ICON_DIR / "menu-2.svg")),
    IconSpec("dots_vertical",      str(ICON_DIR / "dots-vertical.svg")),
    IconSpec("chevron_down",       str(ICON_DIR / "chevron-down.svg")),
    IconSpec("chevron_up",         str(ICON_DIR / "chevron-up.svg")),
    IconSpec("star",               str(ICON_DIR / "star.svg")),
    IconSpec("share",              str(ICON_DIR / "share.svg")),
    IconSpec("user",               str(ICON_DIR / "user.svg")),
    IconSpec("send",                str(ICON_DIR / "send.svg")),
    IconSpec("keyboard",           str(ICON_DIR / "keyboard.svg")),
    IconSpec("language",           str(ICON_DIR / "language.svg")),
    IconSpec("link",               str(ICON_DIR / "link.svg")),

    # --- ダイアログ ---
    IconSpec("alert_triangle", str(ICON_DIR / "alert-triangle.svg")),
    IconSpec("info_circle",    str(ICON_DIR / "info-circle.svg")),
    IconSpec("help",           str(ICON_DIR / "help.svg")),

    # --- ランチャー ---
    IconSpec("app_box",   str(ICON_DIR / "box.svg")),
    IconSpec("app_store", str(ICON_DIR / "apps.svg")),
    IconSpec("calendar",  str(ICON_DIR / "calendar.svg")),
    IconSpec("game",      str(ICON_DIR / "device-gamepad.svg")),

    # --- ファイル ---
    IconSpec("edit",   str(ICON_DIR / "edit.svg")),
    IconSpec("file",   str(ICON_DIR / "file.svg")),
    IconSpec("folder", str(ICON_DIR / "folder.svg")),
    IconSpec("copy",   str(ICON_DIR / "copy.svg")),
]

# 全アイコン共通で生成するサイズ一覧。個別指定はしない。
ALL_SIZES: list[int] = [16, 24, 32, 48, 64]

# 二値化の閾値(0-255)。alpha >= BINARY_THRESHOLD をONピクセルとする。
# ディザリングは撤去した(Bayer行列とalpha量子化のレンジが同じだったため、
# 完全不透明でも行列の最大セルで判定漏れが起き、べた塗り領域に周期的な
# 穴が開くバグがあった。単純な閾値二値化の方が安全かつ十分綺麗)。
BINARY_THRESHOLD = 96


# ============================================================
# SVG -> アルファ濃淡 -> 二値化
# ============================================================

def rasterize_to_alpha(svg_path: str, size: int) -> list:
    """SVGをsize x sizeでラスタライズし、0-15のアルファ量子化値の2次元配列を返す。
    ※この時点ではまだ二値化していない「生の」濃淡データ。
    """
    if not Path(svg_path).exists():
        raise FileNotFoundError(f"SVGファイルが見つかりません: {svg_path}")

    png_bytes = cairosvg.svg2png(
        url=svg_path,
        output_width=size,
        output_height=size,
        background_color=None,  # 透過のまま
    )
    if png_bytes is None:
        # write_to引数を渡さない呼び出しでは通常Noneにはならないが、
        # 型スタブ上はbytes | Noneなのでここで明示的に絞り込む
        raise RuntimeError(f"{svg_path}: SVGのPNGラスタライズに失敗しました (svg2pngがNoneを返却)")

    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")

    if img.width != size or img.height != size:
        raise ValueError(
            f"{svg_path}: ラスタライズサイズ不一致 "
            f"({img.width}x{img.height} != {size}x{size})"
        )

    alpha = [[0] * size for _ in range(size)]
    pixels = img.load()
    if pixels is None:
        # convert("RGBA")直後のImageでload()がNoneになることは通常ないが、
        # 型スタブ上はPixelAccess | Noneなのでここで明示的に絞り込む
        raise RuntimeError(f"{svg_path}: ピクセルデータの取得に失敗しました (img.load()がNoneを返却)")

    for y in range(size):
        for x in range(size):
            _, _, _, a = pixels[x, y]
            alpha[y][x] = a  # 0-255の生アルファ値のまま保持
    return alpha


def threshold_to_binary(alpha: list, threshold: int = 96) -> list:
    """0-255のアルファ値を、パレット制の描画に載せられる完全な二値(bool)に変換する。

    単純な閾値二値化のみ。ディザリングは撤去した。
    (Bayerディザは alpha の量子化レンジ(0-15)と行列の値レンジ(0-15)が
     同じだったため、完全不透明(alpha最大値)でも行列の最大セルでは
     判定が「不透明未満」になり、べた塗り領域に周期的な穴が開くバグが
     あった。ベクター由来の線画アイコンは単純な閾値二値化で十分綺麗に
     出るため、ディザによる恩恵よりバグの温床になるリスクの方が大きいと
     判断し撤去した。)
    """
    size = len(alpha)
    out = [[False] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            out[y][x] = alpha[y][x] >= threshold
    return out


# ============================================================
# 完全二値・交互ラン長RLE
# ============================================================

def encode_binary_rle(binary: list) -> bytes:
    """二値(bool)ビットマップを「交互ラン長のみ」の形式でRLE符号化する。

    出力バイト列は off, on, off, on, ... と交互に並ぶ長さ値のみで構成される
    (値そのものは持たない。位置の偶奇だけでoff/onが決まる)。
    先頭は必ずoffランから始める(onから始まる画像は長さ0のoffランを先頭に挿入)。
    255を超えるランは、255で区切って反対色の長さ0ランを挟み、同じ色を継続する。
    """
    flat = [1 if v else 0 for row in binary for v in row]
    n = len(flat)

    # 通常のRLEでまず (color, length) のリストを作る
    runs: list[tuple[int, int]] = []
    i = 0
    while i < n:
        color = flat[i]
        length = 1
        while i + length < n and flat[i + length] == color:
            length += 1
        runs.append((color, length))
        i += length

    # 先頭がonから始まる場合、offの0-runを挿入して交互を保証する
    if runs and runs[0][0] == 1:
        runs.insert(0, (0, 0))

    # 各runを255以下のチャンクに分割する。255ちょうどで割り切れて
    # まだ続きがある場合は、間に反対色の0-runを挟んで交互を維持する。
    out = bytearray()
    for _color, length in runs:
        remaining = length
        first_chunk = True
        while remaining > 0 or first_chunk:
            chunk = min(remaining, 255)
            out.append(chunk)
            remaining -= chunk
            first_chunk = False
            if remaining > 0:
                out.append(0)  # 反対色の0-run(継続用スペーサ)
    return bytes(out)


# ============================================================
# C++ ヘッダ生成
# ============================================================

def to_enum_name(name: str) -> str:
    """snake_case -> PascalCase (例: wifi_signal_4 -> WifiSignal4)"""
    return "".join(part.capitalize() for part in name.split("_"))


def size_enum_name(size: int) -> str:
    return f"Px{size}"


def generate_header(icons: list, sizes: list, out_path: Path):
    lines = []
    lines.append("// このファイルは tools/generate_icons.py により自動生成されています。")
    lines.append("// 手動で編集しないでください。再生成すると上書きされます。")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("#include <cstddef>")
    lines.append("")

    entry_map: dict[tuple[str, int], str] = {}  # (icon.name, size) -> C配列変数名
    total_bytes = 0

    for icon in icons:
        for size in sizes:
            print(f"  変換中: {icon.name} @ {size}px  ({icon.svg_path})")

            alpha = rasterize_to_alpha(icon.svg_path, size)
            binary = threshold_to_binary(alpha, BINARY_THRESHOLD)
            rle = encode_binary_rle(binary)
            total_bytes += len(rle)

            var_name = f"icon_{icon.name}_{size}"
            entry_map[(icon.name, size)] = var_name

            hex_bytes = ", ".join(f"0x{b:02X}" for b in rle)
            lines.append(f"static const uint8_t {var_name}[] = {{{hex_bytes}}};")

    lines.append("")

    # --- IconID enum ---
    lines.append("enum class IconID : uint8_t {")
    for icon in icons:
        lines.append(f"    {to_enum_name(icon.name)},")
    lines.append("    IconCount")
    lines.append("};")
    lines.append("")

    # --- IconSize enum ---
    lines.append("enum class IconSize : uint8_t {")
    for size in sizes:
        lines.append(f"    {size_enum_name(size)},")
    lines.append("    SizeCount")
    lines.append("};")
    lines.append("")

    # --- IconAsset ---
    lines.append("struct IconAsset {")
    lines.append("    const uint8_t* data;")
    lines.append("    size_t data_len;")
    lines.append("    uint8_t width;")
    lines.append("    uint8_t height;")
    lines.append("};")
    lines.append("")

    # --- テーブル本体 [IconSize][IconID] ---
    # 全アイコン x 全サイズが必ず生成されるため、nullptrフォールバックは不要。
    lines.append(
        "static const IconAsset kIconTable"
        "[static_cast<size_t>(IconSize::SizeCount)]"
        "[static_cast<size_t>(IconID::IconCount)] = {"
    )
    for size in sizes:
        lines.append(f"    // {size_enum_name(size)}")
        lines.append("    {")
        for icon in icons:
            var_name = entry_map[(icon.name, size)]
            lines.append(
                f"        {{ {var_name}, sizeof({var_name}), {size}, {size} }}, "
                f"// {icon.name}"
            )
        lines.append("    },")
    lines.append("};")
    lines.append("")

    lines.append(
        "static_assert(sizeof(kIconTable) / sizeof(kIconTable[0]) "
        "== static_cast<size_t>(IconSize::SizeCount), "
        "\"kIconTable row count mismatch\");"
    )
    lines.append(
        "static_assert(sizeof(kIconTable[0]) / sizeof(kIconTable[0][0]) "
        "== static_cast<size_t>(IconID::IconCount), "
        "\"kIconTable column count mismatch\");"
    )
    lines.append("")

    lines.append("inline const IconAsset& GetIcon(IconID id, IconSize size) {")
    lines.append(
        "    return kIconTable[static_cast<size_t>(size)]"
        "[static_cast<size_t>(id)];"
    )
    lines.append("}")
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"\n生成完了: {out_path}  ({total_bytes} bytes のアイコンデータ, "
          f"{len(icons)}アイコン x {len(sizes)}サイズ = {len(icons) * len(sizes)}枚)")


def main():
    out_path = Path("../src/icons/icons_data.h")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    generate_header(ICONS, ALL_SIZES, out_path)


if __name__ == "__main__":
    main()
