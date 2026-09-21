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
#   calc_eval_test… 電卓アプリの式評価(四則演算/括弧/√/π/エラー)
#   calculator_test… 電卓アプリのGUI配線(キーパッドの当たり判定/履歴/画面切替)
#   dict_test     … 単語辞書(en-ja-and-ja-en.tsv形式)の部分一致検索(前方一致の即時性/全体走査/重複無し/件数上限)
#   dict_scene_test… 辞書アプリ(DictScene)のGUI配線(入力欄→検索→一覧への逐次反映→タップで詳細欄)
#   widget_factory_test… WidgetFactory(WidgetType→new Xxx)とWidgetRegistry::Resolve()
#                         (Lua統合向けの発行側/消費側で、以前は呼び出し元・テストとも無かった)
#   widget_property_test… WidgetProperty(WidgetType非依存のget/set共通口)。
#                          WidgetFactory対応15種それぞれの代表プロパティの読み書きと、
#                          型不一致/非対応id/nullptrがfalseで安全に弾かれることを確認。
#                          Icon::IconOpaque/GridContainer::HAlign・VAlignのget、
#                          NumberInput::Text(setNum/getNum)、ScrollList/DropdownMenuの
#                          ItemCountは元々getterが無く未対応だった項目(2026-09-21解消)
#   step_budget_test… Task::update()内の作業ループを時間で区切るStepBudgetの検証。
#                      他と違いstubs/ではなくpc/compat/を使う(実時間のmicros()が要るため)
#   error_functions_test… ErrorFunctions::ShowFatal()(エラーの見せ方の共通口)。
#                          MsgDialogの生成・登録・自己破棄の配線を確認
#   lua_smoke_test… vendorしたLua本体(lib/lua)が実際にビルド・リンクでき、
#                    lua_newstate/luaL_dostring/lua_closeが動くことの確認
#                    (「ビルドの二重管理」の解消。他と違いsrc/を丸ごとASan付きでコンパイルする)
#   lua_stdlib_test… Lua本体の言語機能・標準ライブラリ(数値/文字列/テーブル/
#                     クロージャ/メタテーブル/pcall/コルーチン/GC)が一通り動くことの確認。
#                     LuaからCの関数(check())を呼べることも兼ねて確認する
#   lua_alloc_budget_test… lua_newstateへカスタムアロケータを渡し、確保量に上限を
#                           課しても安全に動く(予算超過でabortせずLUA_ERRMEM、
#                           lua_close後は必ずused=0)ことの確認。RAM/Flash予算(200KB枠)の
#                           実現方式の裏付け
#   lua_engine_test… LuaEngine(Lua<->C++バインディング本体、src/lua/)を実際の
#                     ウィジェット層と繋げて動かす結合テスト。pico.create/set/get/on/
#                     add_child/destroyの一連の導線、コールバックの発火、コンテナへの
#                     動的追加の重なり順、ScrollContainerの子を個別destroyしても
#                     二重解放しないこと、スクリプトエラー時にErrorFunctions経由で
#                     ダイアログが出ることまでを確認する
#   lua_scene_test… LuaScene(SD上のLuaスクリプトを読んで実行する画面)を実際の
#                    シーン遷移(Scene_Functions.cpp)と組み合わせて動かす結合テスト。
#                    SDからの読み込み・pico.pop()での実際のランチャ復帰・
#                    ファイル不在時のダイアログ表示・大きすぎるスクリプトの
#                    打ち切り警告までを確認する
#   lua_app_scanner_test… LuaAppScanner(SD上の"/lua/apps/<名前>/main.lua"を走査して
#                    ランチャの登録簿へ自動登録する)。ディレクトリの走査自体は
#                    ホストのSdFatスタブでは再現できないため、SD無し/ディレクトリ
#                    が無い場合に安全に0件を返すことまでを確認する
#                    (完全な走査結果はPCビルドの--shotで確認済み。CLAUDE.md
#                    「SDを走査してLuaアプリを見つける処理」参照)
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

# --- 電卓の式評価 ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/calc_eval_test.cpp" \
    -o "$OUT/calc_eval_test"

echo ""
echo "===== calc_eval_test ====="
"$OUT/calc_eval_test"

# --- 電卓のGUI配線(キーパッドの当たり判定/画面/履歴) ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/calculator_test.cpp" \
    "$ROOT/src/gui/scenes/CalculatorScene.cpp" \
    "$ROOT/src/gui/widgets/apps/CalculatorKeypad.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    -o "$OUT/calculator_test"

echo ""
echo "===== calculator_test ====="
"$OUT/calculator_test"

# --- 単語辞書(部分一致検索) ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/dict_test.cpp" \
    "$ROOT/src/dict/Word_Dict.cpp" \
    -o "$OUT/dict_test"

echo ""
echo "===== dict_test ====="
"$OUT/dict_test"

# --- 辞書アプリ(DictScene)のGUI配線 ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/dict_scene_test.cpp" \
    "$ROOT/src/gui/scenes/DictScene.cpp" \
    "$ROOT/src/dict/Word_Dict.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    -o "$OUT/dict_scene_test"

echo ""
echo "===== dict_scene_test ====="
"$OUT/dict_scene_test"

# --- WidgetFactory / WidgetRegistry::Resolve()(Lua統合の受け皿) ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/widget_factory_test.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/WidgetFactory.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/NumberInput.cpp" \
    "$ROOT/src/gui/widgets/Checkbox.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/Image.cpp" \
    "$ROOT/src/gui/widgets/NumberSlider.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/CanvasRaster.cpp" \
    "$ROOT/src/gui/widgets/LuaCanvas.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/widget_factory_test"

echo ""
echo "===== widget_factory_test ====="
"$OUT/widget_factory_test"

# --- WidgetProperty(プロパティのget/set共通口) ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/widget_property_test.cpp" \
    "$ROOT/src/gui/widgets/WidgetProperty.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/WidgetFactory.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/NumberInput.cpp" \
    "$ROOT/src/gui/widgets/Checkbox.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/Image.cpp" \
    "$ROOT/src/gui/widgets/NumberSlider.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/CanvasRaster.cpp" \
    "$ROOT/src/gui/widgets/LuaCanvas.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSaveDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSelectDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/ColorDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/apps/FileExplorer.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/storage/SD_IO.cpp" \
    -o "$OUT/widget_property_test"

echo ""
echo "===== widget_property_test ====="
"$OUT/widget_property_test"

# --- StepBudget(実時間のmicros()が要るためstubs/ではなくpc/compat/を使う) ---
g++ -std=gnu++17 -g -fsanitize=address,undefined -pthread \
    -I "$ROOT/pc/compat" -I "$ROOT/src" \
    "$ROOT/script/host_test/step_budget_test.cpp" \
    -o "$OUT/step_budget_test"

echo ""
echo "===== step_budget_test ====="
"$OUT/step_budget_test"

# --- ErrorFunctions(エラーの見せ方の共通口) ---
g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/error_functions_test.cpp" \
    "$ROOT/src/functions/Error_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/error_functions_test"

echo ""
echo "===== error_functions_test ====="
"$OUT/error_functions_test"

# --- lib/lua(vendorしたLua本体)が実際にビルド・リンクできること ---
# pc/CMakeLists.txtと同じくLUA_USE_LINUX等は定義しない(実機は
# dlopen等のPOSIX機能を持たないため、ANSI構成で確認する。-ldlも不要になる)。
# 3本のLuaテストで同じオブジェクトを使い回す
mkdir -p "$OUT/lua_obj"
for f in "$ROOT"/lib/lua/src/*.c; do
    gcc -std=gnu99 -g -fsanitize=address,undefined \
        -I "$ROOT/lib/lua/src" -c "$f" -o "$OUT/lua_obj/$(basename "$f" .c).o"
done

g++ $CXXFLAGS -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_smoke_test.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_smoke_test"

echo ""
echo "===== lua_smoke_test ====="
"$OUT/lua_smoke_test"

g++ $CXXFLAGS -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_stdlib_test.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_stdlib_test"

echo ""
echo "===== lua_stdlib_test ====="
"$OUT/lua_stdlib_test"

g++ $CXXFLAGS -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_alloc_budget_test.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_alloc_budget_test"

echo ""
echo "===== lua_alloc_budget_test ====="
"$OUT/lua_alloc_budget_test"

# --- LuaEngine(Lua<->C++バインディング本体)をウィジェット層と繋げた結合テスト ---
g++ $CXXFLAGS $INCLUDES -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_engine_test.cpp" \
    "$ROOT/src/lua/LuaEngine.cpp" \
    "$ROOT/src/storage/SD_IO.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/WidgetFactory.cpp" \
    "$ROOT/src/gui/widgets/WidgetProperty.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/NumberInput.cpp" \
    "$ROOT/src/gui/widgets/Checkbox.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/Image.cpp" \
    "$ROOT/src/gui/widgets/NumberSlider.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/CanvasRaster.cpp" \
    "$ROOT/src/gui/widgets/LuaCanvas.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSaveDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSelectDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/ColorDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/apps/FileExplorer.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/functions/Error_Functions.cpp" \
    "$ROOT/src/functions/Scene_Functions.cpp" \
    "$ROOT/src/functions/App_Functions.cpp" \
    "$ROOT/src/gui/scenes/LuaScene.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_engine_test"

echo ""
echo "===== lua_engine_test ====="
"$OUT/lua_engine_test"

# --- LuaScene(SD上のLuaスクリプトを読んで実行する画面)をシーン遷移と組み合わせた結合テスト ---
g++ $CXXFLAGS $INCLUDES -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_scene_test.cpp" \
    "$ROOT/src/gui/scenes/LuaScene.cpp" \
    "$ROOT/src/lua/LuaEngine.cpp" \
    "$ROOT/src/storage/SD_IO.cpp" \
    "$ROOT/src/functions/Scene_Functions.cpp" \
    "$ROOT/src/functions/App_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Error_Functions.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/WidgetFactory.cpp" \
    "$ROOT/src/gui/widgets/WidgetProperty.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/NumberInput.cpp" \
    "$ROOT/src/gui/widgets/Checkbox.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/Image.cpp" \
    "$ROOT/src/gui/widgets/NumberSlider.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/CanvasRaster.cpp" \
    "$ROOT/src/gui/widgets/LuaCanvas.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSaveDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSelectDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/ColorDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/apps/FileExplorer.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_scene_test"

echo ""
echo "===== lua_scene_test ====="
"$OUT/lua_scene_test"

# --- LuaAppScanner(SD走査によるLuaアプリの自動登録) ---
g++ $CXXFLAGS $INCLUDES -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_app_scanner_test.cpp" \
    "$ROOT/src/lua/LuaAppScanner.cpp" \
    "$ROOT/src/gui/scenes/LuaScene.cpp" \
    "$ROOT/src/lua/LuaEngine.cpp" \
    "$ROOT/src/storage/SD_IO.cpp" \
    "$ROOT/src/functions/Scene_Functions.cpp" \
    "$ROOT/src/functions/App_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Error_Functions.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/WidgetFactory.cpp" \
    "$ROOT/src/gui/widgets/WidgetProperty.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/NumberInput.cpp" \
    "$ROOT/src/gui/widgets/Checkbox.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/Image.cpp" \
    "$ROOT/src/gui/widgets/NumberSlider.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/CanvasRaster.cpp" \
    "$ROOT/src/gui/widgets/LuaCanvas.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSaveDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSelectDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/ColorDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/apps/FileExplorer.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_app_scanner_test"

echo ""
echo "===== lua_app_scanner_test ====="
"$OUT/lua_app_scanner_test"
