#!/bin/sh
# 実コードをPC上で動かす検証スクリプト。
#
# 実機(RP2350)のビルドとは無関係で、LovyanGFX/SdFat等はstubs/以下の
# ダミーヘッダに差し替えてホストのg++でリンクする。
# AddressSanitizerで解放漏れ・二重解放・バッファ越えを検出するのが主目的。
#
# 収録テスト:
#   scene_test … シーン遷移(SceneFunctions / WidgetFunctions)とメモリ計測フックの配線
#   label_test … Labelのテキストレイアウト結果(幅/高さ/文字数/カーソル座標)の固定
#   markdown_test … MarkdownViewのブロック高さとLabelの実高さの整合(重なり検出)
#   config_test   … 設定ファイル(key=value)の読み書き
#   app_test      … アプリ登録簿とランチャのタイル配置/当たり判定
#   path_test     … パスの正規化と相対解決(Markdownブラウザのリンク追従の土台)
#   cache_test    … 文書キャッシュ(半端なファイルを残さないこと/目録の書き換え)とマニフェストの引き当て
#   http_test     … URLの分解/解決と、HTTPレスポンスの解釈(ソケット抜きで検証)
#   discovery_test… サーバ情報(/.well-known/pico-os)の解釈と前方互換
#
# 確保回数やピーク使用量の計測は run_mem.sh の担当(ASanはmallocごと差し替えるため両立しない)。
#
# 使い方: sh script/host_test/run.sh
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d)

CXXFLAGS="-std=gnu++17 -g -fsanitize=address,undefined"
INCLUDES="-I$ROOT/script/host_test/stubs -I$ROOT/src"

# --- シーン遷移 ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/scene_test.cpp" \
    "$ROOT/src/functions/Scene_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    -o "$OUT/scene_test"

echo "===== scene_test ====="
"$OUT/scene_test"

# --- Labelのレイアウト ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/label_test.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/label_test"

echo ""
echo "===== label_test ====="
"$OUT/label_test"

# --- MarkdownViewのブロックレイアウト ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/markdown_test.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/Image.cpp" \
    "$ROOT/src/gui/widgets/apps/MarkdownView.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/markdown_test"

echo ""
echo "===== markdown_test ====="
"$OUT/markdown_test"

# --- 設定ファイルの読み書き ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/config_test.cpp" \
    -o "$OUT/config_test"

echo ""
echo "===== config_test ====="
"$OUT/config_test"

# --- アプリ登録簿とランチャ ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/app_test.cpp" \
    "$ROOT/src/functions/App_Functions.cpp" \
    "$ROOT/src/gui/widgets/systems/AppGrid.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/app_test"

echo ""
echo "===== app_test ====="
"$OUT/app_test"

# --- パスの正規化と相対解決 ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/path_test.cpp" \
    -o "$OUT/path_test"

echo ""
echo "===== path_test ====="
"$OUT/path_test"

# --- 文書キャッシュ ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/cache_test.cpp" \
    "$ROOT/src/storage/Doc_Cache.cpp" \
    "$ROOT/src/storage/SD_IO.cpp" \
    "$ROOT/src/net/Manifest.cpp" \
    -o "$OUT/cache_test"

echo ""
echo "===== cache_test ====="
"$OUT/cache_test"

# --- URLとHTTPレスポンスの解釈 ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/http_test.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    -o "$OUT/http_test"

echo ""
echo "===== http_test ====="
"$OUT/http_test"

# --- サーバ情報(discovery) ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/discovery_test.cpp" \
    "$ROOT/src/net/Discovery.cpp" \
    -o "$OUT/discovery_test"

echo ""
echo "===== discovery_test ====="
"$OUT/discovery_test"
