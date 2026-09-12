#!/bin/sh
# ウィジェットのヒープ確保パターンをPC上で実測するスクリプト(script/host_test/mem_probe.cpp)。
#
# 実機(RP2350)のビルドとは無関係で、LovyanGFX/SdFat等はstubs/以下のダミーヘッダに
# 差し替えてホストのg++でリンクする。グローバルなoperator new/deleteを差し替えて
# 「ウィジェット1個あたり何回ヒープを叩くか」「1シーンの同時生存ピークは何バイトか」を数える。
#
# run.sh と違いAddressSanitizerは使わない(ASanが自前のoperator newを差し込むため
# 確保カウンタと干渉する)。解放漏れの検出は run.sh の担当。
#
# -Os -fno-rtti が必要な理由:
# IFontImplementation / IBorderColor / ITextColor は仮想関数の宣言だけを持ち定義を持たない
# (全ての具象クラスがoverrideするので実機では呼ばれない)。そのため
#   - RTTI有効だと、派生クラスのtypeinfoが基底クラスのtypeinfoを参照して未定義になる
#   - -O0だと基底クラスのコンストラクタが実体化され、基底のvtableを参照して未定義になる
# の2点でリンクが通らない。-Os は基底コンストラクタをインライン化して前者を、
# -fno-rtti は後者を回避する。
#
# 使い方: sh script/host_test/run_mem.sh
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d)

g++ -std=gnu++17 -g -Os -fno-rtti \
    -I"$ROOT/script/host_test/stubs" -I"$ROOT/src" \
    "$ROOT/script/host_test/mem_probe.cpp" \
    "$ROOT/src/functions/Scene_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/Image.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/CanvasRaster.cpp" \
    "$ROOT/src/gui/widgets/MarkdownView.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    -o "$OUT/mem_probe"

"$OUT/mem_probe"
