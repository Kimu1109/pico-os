#!/bin/sh
# シーン遷移(SceneFunctions / WidgetFunctions)の実コードをPC上で動かす検証スクリプト。
#
# 実機(RP2350)のビルドとは無関係で、LovyanGFX/SdFat等はstubs/以下の
# ダミーヘッダに差し替えてホストのg++でリンクする。
# ウィジェットの解放漏れ・二重解放をAddressSanitizerで検出するのが主目的。
#
# 使い方: sh script/host_test/run.sh
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d)

g++ -std=gnu++17 -g -fsanitize=address,undefined \
    -I"$ROOT/script/host_test/stubs" -I"$ROOT/src" \
    "$ROOT/script/host_test/scene_test.cpp" \
    "$ROOT/src/functions/Scene_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    -o "$OUT/scene_test"

"$OUT/scene_test"
