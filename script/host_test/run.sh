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
#   secret_cipher_test… Wi-Fi SSID/パスワードの暗号化保存(util/Secret_Cipher.hpp)。
#                   往復・用途(purpose)ごとに鍵ストリームが変わること・enc1:接頭辞の
#                   無い値は平文として読める後方互換・壊れたデータへの安全な失敗
#   wifi_profiles_test… 保存済みのWi-Fiネットワーク(net/Wifi_Profiles、/sys/wifi.cfg)。network.cfgの
#                   旧形式からの取り込み・追加/更新/上限/削除・接続できたものを先頭へ(自動接続の順番)・
#                   ON/OFFの保存・番号ごとの暗号化
#   app_test      … アプリ登録簿とランチャのタイル配置/当たり判定
#   path_test     … パスの正規化と相対解決(Markdownブラウザのリンク追従の土台)
#   touch_filter_test… タッチ座標のmedian-of-3ノイズ抑制(util/TouchFilter.hpp)。
#                   単発のスパイクを無視すること・滑らかな動きへの追従・タッチ開始ごとの
#                   履歴リセット
#   cache_test    … 文書キャッシュ(半端なファイルを残さないこと/目録の書き換え)とマニフェストの引き当て
#   http_test     … URLの分解/解決と、HTTPレスポンスの解釈(ソケット抜きで検証)
#   discovery_test… サーバ情報(/.well-known/pico-os)の解釈と前方互換
#   ical_test     … iCalendar(.ics)の読み取り(折り返し/エスケープ/UTC→現地時刻)と
#                   繰り返し(RRULE/EXDATE/上書き予定)がどの日に出るか
#   calendar_scene_test… カレンダーアプリのGUI配線(MonthGridのタップ位置→日付、
#                   CalendarSceneの生成/解放、SDや.icsが無いときの案内)
#   calendar_sync_secret_test… /calendar/sources.cfgの暗号化保存(CalendarSync::WriteSource/
#                   ReadSourceがutil/Secret_Cipher.hppを通すこと)。長いURLの往復・複数件・
#                   同名の上書き・後方互換(平文の既存行がそのまま読める)
#   chat_proto_test… チャットの応答(1行1件のTSV)の読み取りと本文のエスケープ
#   chat_scene_test… チャットアプリのGUI配線(ChatLogViewの折り返し/スクロール、
#                   ChatSceneの生成/解放、chat.cfgが無い/足りないときの案内)
#   todoist_proto_test… TODOアプリ(Todoist)の応答の読み取り(todo/Todoist_Proto)と、その下のJSONの
#                   読み取り(util/Json_Reader)。エスケープ/サロゲート/切り詰め/1バイトずつ/誤り、
#                   一覧から必要なキーだけ拾う・完了済みを飛ばす・next_cursor、期限の読み取りと表記・並べ替え
#   todo_test     … TODOアプリのリマインダー(todo/Todo_Reminders。本物のNotificationFunctionsへ予約する)と
#                   TodoSceneの生成/解放・トークンが無いときの案内
#   gb_emu_test   … Game Boyエミュ(GbEmu/GameBoyPad/GameBoyView)。テスト内で組み立てたROMで、
#                   読み込みの断り方・セーブ(.sav)の往復・ボタン・不正な命令で落ちないこと・
#                   変わった行だけdirtyにすること・操作パッドのタップ位置→ボタン・
#                   音源チップへの書き込みが時刻付きで渡ること/読み出しの答え方を確認する
#   gb_apu_test   … ゲームボーイの音源チップ(GbApu)と時刻付きの列(GbAudioLink)。矩形波の高さ/デューティ・
#                   長さ・エンベロープ・スイープ・波形メモリ・ノイズ・電源・振り分け、フレームの中の位置どおりに
#                   効くこと・エミュが止まったら無音・溜まりすぎたら追いつく
#   sound_test    … 音声出力(SoundFunctions)。アンプの検出(ばたつきを採らない)、刺さっている間だけ
#                   I2Sを動かすこと、未接続の間も音が時間どおりに進み刺し直すと続きから鳴ること、
#                   矩形波の中身、sound.cfg(output=off/volume)、I2Sを開始できなかったとき
#   wav_test      … WAVの再生。WavDecoder(形式ごとの変換・チャンネルの平均・周波数の変換・チャンクの読み飛ばし・
#                   ループ・断り方)とSoundFunctionsの配線(音量・止める/切り替えで先読みを捨てる・途切れの数え方・
#                   アンプが無い間も時間どおりに進む)
#   music_test    … 曲データ(pico-os MML、MUSIC_FORMAT.md)。読み取り(音の高さ/長さ/繰り返し/マクロ/
#                   誤りの行・列/警告)、シーケンサー(サンプル単位の音の位置・テンポ・繰り返し・ループ・
#                   効果音への貸し出し)、SoundFunctionsの配線(置き場の入れ替え・効果音との同居)
#   midi2mml_test … MIDI→MMLの変換(script/midi2mml.py、Python)。テストの中で組み立てたMIDIを変換し、
#                   出てきたMMLを本物の読み取り(mml_dump.cpp)へ通して音の位置/高さ/長さ/テンポを確かめる
#   key_input_test … 物理キーボードの窓口(KeyInputFunctions)。"key ..."の行の読み取り、padの行との同居、
#                   画面のonKey()→開いているキー盤の順に配ること、キー盤の編集(挿入/削除/移動/決定)、数字のキー盤の制限
#   pad_test      … 外部コントローラーの窓口(PadFunctions)。USBシリアルの行("pad XXXX")の読み取り、
#                   押した/離したのはそのフレームだけ、行が途切れたら外れて押しっぱなしにならないこと、
#                   Game Boyのボタンへの対応
#   power_test    … スリープ(PowerFunctions)。操作が無いまま sleep-timeout が過ぎると入り、画面/Wi-Fi/音の
#                   省電力が掛かること、タッチで起きて起こした指は離すまで画面へ渡さないこと、KeepAwake()・
#                   音・Wi-Fi接続中は入らないこと
#   notification_test … 通知(NotificationFunctions)の中身。予約の種類ごとの発火、置き換え・上限、履歴、
#                   控えめ、保存と読み込み、起動理由
#   alarm_test    … アラーム(AlarmFunctions)。alarm.cfgの読み書き、時刻での発火と確認ダイアログ、
#                   繰り返し(1回/毎日/平日/土日)、止める/5分後/放置での停止、ダイアログが消えたときの出し直し
#   calc_eval_test… 電卓アプリの式評価(四則演算/括弧/√/π/エラー)
#   calculator_test… 電卓アプリのGUI配線(キーパッドの当たり判定/履歴/画面切替)
#   dict_test     … 単語辞書(en-ja-and-ja-en.tsv形式)の部分一致検索(前方一致の即時性/全体走査/重複無し/件数上限)
#   dict_scene_test… 辞書アプリ(DictScene)のGUI配線(入力欄→検索→一覧への逐次反映→タップで詳細欄)
#   widget_factory_test… WidgetFactory(WidgetType→new Xxx)とWidgetRegistry::Resolve()
#                         (Lua統合向けの発行側/消費側で、以前は呼び出し元・テストとも無かった)
#   widget_property_test… WidgetProperty(WidgetType非依存のget/set共通口)。
#                          WidgetFactory対応20種それぞれの代表プロパティの読み書きと、
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
#   tetris_test… Luaアプリ「テトリス」(pc/sdcard/lua/apps/テトリス/)のゲームの規則。
#                 pico.*をLuaの偽物に差し替え、lua_script_test(vendorしたLuaで.luaを
#                 動かすだけの下請け)で tetris_test.lua を実行する。ライン消し・壁蹴り・
#                 HOLD・長押しの連続移動・ゲームオーバーとハイスコア保存・タッチの操作ボタン
#   vt_terminal_test… SSHアプリの端末エミュレータ(src/ssh/Vt_Terminal)。折り返し・カーソル移動・
#                 消去・色(256色/RGB→16色)・全角(2セル)・スクロールバック・範囲スクロール・
#                 代替画面・問い合わせ(6n等)への返事・大きさの変更
#   ssh_util_test… SSHクライアントの小道具(Ssh_Sha256/Ssh_Util)。SHA-256の既知の値・Base64・
#                 OpenSSH形式の秘密鍵の読み取り(パスフレーズ付き/RSAは断る)・ホスト鍵の指紋・
#                 known_hostsの照合と追記(本物のsshd相手の往復は run_net.sh の ssh_net_test)
#
# 確保回数やピーク使用量の計測は run_mem.sh の担当(ASanはmallocごと差し替えるため両立しない)。
#
# 使い方: sh script/host_test/run.sh
set -e

# 「run.shが一生終わらない」事故の対策(2026-09-21追加)。
# コンパイル・テスト実行のどちらも、正常なら数秒〜数十秒で終わる軽量なものばかりだが、
# 実行環境側の要因(共有サンドボックスの資源競合等)や、将来ここへ足すテストが万一
# 無限ループを埋め込んでしまった場合に、run.sh自体は必ず有限時間で終わるようにする。
# timeout(1)で区切り、124(タイムアウト)の場合だけ明示的なメッセージを出してから
# 終了する(通常のコンパイルエラー/テスト失敗は今まで通りset -eにそのまま任せる)。
# 秒数はこの環境での実測(最も重いテストでも数秒〜十数秒)に対して十分な余裕を持たせた値。
COMPILE_TIMEOUT_SEC=180
RUN_TIMEOUT_SEC=60

# -k/--kill-after: SIGTERMで終了しない(I/O待ち等でD-stateに入っている等)プロセスに
# 備え、猶予時間後にSIGKILLで強制終了する。これが無いとtimeout自体がSIGTERM無視に
# よって固まってしまい、対策の意味が無くなる。
#
# 「cmd || rc=$?」の形で終了コードを取る(if cmd; then/elseや if ! cmd; thenは
# 使わない): POSIXでは「ifの条件が偽で、実行された分岐が無い場合、if文自体の
# 終了コードは0になる」「! cmdの終了コードはcmdの実際の値ではなく0/1への論理反転」
# と定義されているため、どちらの書き方でも$?でtimeoutの実際の終了コード(124等)を
# 取り出せない(このバグを一度実際に踏んで学んだ)。「cmd || rc=$?」なら、cmdが
# 失敗した場合のみ右辺が実行されその時点の$?(=cmdの終了コードそのもの)を拾える。
# `||`の左側なので、この行自体はset -eの即終了対象にもならない
compile_or_die() {
    rc=0
    timeout -k 10 "$COMPILE_TIMEOUT_SEC" "$@" || rc=$?
    if [ "$rc" -ne 0 ]; then
        if [ "$rc" -eq 124 ]; then
            echo "[FATAL] コンパイルが${COMPILE_TIMEOUT_SEC}秒を超えて応答しませんでした: $*" >&2
        fi
        exit "$rc"
    fi
}

run_or_die() {
    rc=0
    timeout -k 10 "$RUN_TIMEOUT_SEC" "$@" || rc=$?
    if [ "$rc" -ne 0 ]; then
        if [ "$rc" -eq 124 ]; then
            echo "[FATAL] $1 が${RUN_TIMEOUT_SEC}秒を超えて応答しませんでした(無限ループの疑いあり)" >&2
        fi
        exit "$rc"
    fi
}

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d)


# -DPICOOS_PC: Battery_Functions.cppだけが見るフラグ(他のstubs/はどれも参照していない、
# 2026-09-28時点でgrep済み)。実ADC/実CYW43が無いホストテスト環境では、GFX_Functions/
# Touch_Functionsと同じくBattery_Functions自体をASan対象外にする手もあったが、
# Sound_FunctionsがBatteryFunctions::IsExternallyPowered()を読むようになった(音割れ対策の
# バッテリー駆動時音量キャップ)ため、Sound_Functions.cppをリンクする全テストで
# Battery_Functions.cppも一緒にリンクする必要が生じた。PCビルドと同じ「環境変数で
# 疑似値を返す」簡易実装のほうを使う(ASanもここは無害に通る)
CXXFLAGS="-std=gnu++17 -g -fsanitize=address,undefined -DPICOOS_PC"
INCLUDES="-I$ROOT/script/host_test/stubs -I$ROOT/src"

# --- シーン遷移 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/scene_test.cpp" \
    "$ROOT/src/functions/Scene_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    -o "$OUT/scene_test"

echo "===== scene_test ====="
run_or_die "$OUT/scene_test"

# --- Labelのレイアウト ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
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
run_or_die "$OUT/label_test"

# --- MarkdownViewのブロックレイアウト ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
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
run_or_die "$OUT/markdown_test"

# --- 設定ファイルの読み書き ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/config_test.cpp" \
    -o "$OUT/config_test"

echo ""
echo "===== config_test ====="
run_or_die "$OUT/config_test"

# --- Wi-Fi SSID/パスワードの暗号化保存 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/secret_cipher_test.cpp" \
    -o "$OUT/secret_cipher_test"

echo ""
echo "===== secret_cipher_test ====="
run_or_die "$OUT/secret_cipher_test"

# --- 保存済みのWi-Fiネットワーク(/sys/wifi.cfg) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/wifi_profiles_test.cpp" \
    "$ROOT/src/net/Wifi_Profiles.cpp" \
    -o "$OUT/wifi_profiles_test"

echo ""
echo "===== wifi_profiles_test ====="
run_or_die "$OUT/wifi_profiles_test"

# --- アプリ登録簿とランチャ ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
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
run_or_die "$OUT/app_test"

# --- パスの正規化と相対解決 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/path_test.cpp" \
    -o "$OUT/path_test"

echo ""
echo "===== path_test ====="
run_or_die "$OUT/path_test"

# --- タッチ座標のノイズ抑制(median-of-3フィルタ) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/touch_filter_test.cpp" \
    -o "$OUT/touch_filter_test"

echo ""
echo "===== touch_filter_test ====="
run_or_die "$OUT/touch_filter_test"

# --- 文書キャッシュ ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/cache_test.cpp" \
    "$ROOT/src/storage/Doc_Cache.cpp" \
    "$ROOT/src/storage/SD_IO.cpp" \
    "$ROOT/src/net/Manifest.cpp" \
    -o "$OUT/cache_test"

echo ""
echo "===== cache_test ====="
run_or_die "$OUT/cache_test"

# --- URLとHTTPレスポンスの解釈 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/http_test.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    -o "$OUT/http_test"

echo ""
echo "===== http_test ====="
run_or_die "$OUT/http_test"

# --- サーバ情報(discovery) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/discovery_test.cpp" \
    "$ROOT/src/net/Discovery.cpp" \
    -o "$OUT/discovery_test"

echo ""
echo "===== discovery_test ====="
run_or_die "$OUT/discovery_test"

# --- iCalendar(.ics)の読み取り ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/ical_test.cpp" \
    "$ROOT/src/calendar/Ical.cpp" \
    -o "$OUT/ical_test"

echo ""
echo "===== ical_test ====="
run_or_die "$OUT/ical_test"

# --- カレンダーアプリのGUI配線 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/calendar_scene_test.cpp" \
    "$ROOT/src/gui/scenes/CalendarScene.cpp" \
    "$ROOT/src/gui/widgets/apps/MonthGrid.cpp" \
    "$ROOT/src/gui/widgets/dialogs/EventDetailDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/calendar/Ical.cpp" \
    "$ROOT/src/calendar/Calendar_Sync.cpp" \
    "$ROOT/src/task/Http_Get.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/functions/Error_Functions.cpp" \
    -o "$OUT/calendar_scene_test" -lssl -lcrypto

echo ""
echo "===== calendar_scene_test ====="
run_or_die "$OUT/calendar_scene_test"

# --- カレンダー取得元の暗号化保存 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/calendar_sync_secret_test.cpp" \
    "$ROOT/src/calendar/Calendar_Sync.cpp" \
    "$ROOT/src/task/Http_Get.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    -o "$OUT/calendar_sync_secret_test" -lssl -lcrypto

echo ""
echo "===== calendar_sync_secret_test ====="
run_or_die "$OUT/calendar_sync_secret_test"

# --- チャット: 応答の読み取り ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/chat_proto_test.cpp" \
    "$ROOT/src/chat/Chat_Proto.cpp" \
    -o "$OUT/chat_proto_test"

echo ""
echo "===== chat_proto_test ====="
run_or_die "$OUT/chat_proto_test"

# --- チャットアプリのGUI配線 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/chat_scene_test.cpp" \
    "$ROOT/src/gui/scenes/ChatScene.cpp" \
    "$ROOT/src/gui/widgets/apps/ChatLogView.cpp" \
    "$ROOT/src/chat/Chat_Proto.cpp" \
    "$ROOT/src/chat/Chat_Client.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    -o "$OUT/chat_scene_test" -lssl -lcrypto

echo ""
echo "===== chat_scene_test ====="
run_or_die "$OUT/chat_scene_test"

# --- TODO(Todoist): 応答の読み取り ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/todoist_proto_test.cpp" \
    "$ROOT/src/todo/Todoist_Proto.cpp" \
    "$ROOT/src/util/Json_Reader.cpp" \
    -o "$OUT/todoist_proto_test"

echo ""
echo "===== todoist_proto_test ====="
run_or_die "$OUT/todoist_proto_test"

# --- TODO: リマインダーと画面の配線 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/todo_test.cpp" \
    "$ROOT/src/gui/scenes/TodoScene.cpp" \
    "$ROOT/src/todo/Todo_Reminders.cpp" \
    "$ROOT/src/todo/Todoist_Client.cpp" \
    "$ROOT/src/todo/Todoist_Proto.cpp" \
    "$ROOT/src/util/Json_Reader.cpp" \
    "$ROOT/src/functions/Notification_Functions.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Textbox.cpp" \
    "$ROOT/src/gui/widgets/ScrollList.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    -o "$OUT/todo_test" -lssl -lcrypto

echo ""
echo "===== todo_test ====="
run_or_die "$OUT/todo_test"

# --- Game Boyエミュ ---
compile_or_die g++ $CXXFLAGS $INCLUDES -I"$ROOT/lib/peanut_gb/src" \
    "$ROOT/script/host_test/gb_emu_test.cpp" \
    "$ROOT/src/gb/Gb_Emu.cpp" \
    "$ROOT/src/gui/widgets/apps/GameBoyPad.cpp" \
    "$ROOT/src/gui/widgets/apps/GameBoyView.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/gb_emu_test"

echo ""
echo "===== gb_emu_test ====="
run_or_die "$OUT/gb_emu_test"

# --- ゲームボーイの音源チップ ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/gb_apu_test.cpp" \
    "$ROOT/src/sound/Gb_Apu.cpp" \
    "$ROOT/src/sound/Gb_Audio_Link.cpp" \
    "$ROOT/src/sound/Wav_Decoder.cpp" \
    "$ROOT/src/sound/Wav_Stream.cpp" \
    -o "$OUT/gb_apu_test"

echo ""
echo "===== gb_apu_test ====="
run_or_die "$OUT/gb_apu_test"

# --- 音声出力 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/sound_test.cpp" \
    "$ROOT/src/functions/Sound_Functions.cpp" \
    "$ROOT/src/functions/Battery_Functions.cpp" \
    "$ROOT/src/sound/Chip_Synth.cpp" \
    "$ROOT/src/sound/Mml_Compiler.cpp" \
    "$ROOT/src/sound/Music_Player.cpp" \
    "$ROOT/src/sound/Gb_Apu.cpp" \
    "$ROOT/src/sound/Gb_Audio_Link.cpp" \
    "$ROOT/src/sound/Wav_Decoder.cpp" \
    "$ROOT/src/sound/Wav_Stream.cpp" \
    -o "$OUT/sound_test"

echo ""
echo "===== sound_test ====="
run_or_die "$OUT/sound_test"

compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/wav_test.cpp" \
    "$ROOT/src/functions/Sound_Functions.cpp" \
    "$ROOT/src/functions/Battery_Functions.cpp" \
    "$ROOT/src/sound/Chip_Synth.cpp" \
    "$ROOT/src/sound/Mml_Compiler.cpp" \
    "$ROOT/src/sound/Music_Player.cpp" \
    "$ROOT/src/sound/Gb_Apu.cpp" \
    "$ROOT/src/sound/Gb_Audio_Link.cpp" \
    "$ROOT/src/sound/Wav_Decoder.cpp" \
    "$ROOT/src/sound/Wav_Stream.cpp" \
    -o "$OUT/wav_test"

echo ""
echo "===== wav_test ====="
run_or_die "$OUT/wav_test"

# --- 曲データ(MML)の読み取りとシーケンサー ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/music_test.cpp" \
    "$ROOT/src/functions/Sound_Functions.cpp" \
    "$ROOT/src/functions/Battery_Functions.cpp" \
    "$ROOT/src/sound/Chip_Synth.cpp" \
    "$ROOT/src/sound/Mml_Compiler.cpp" \
    "$ROOT/src/sound/Music_Player.cpp" \
    "$ROOT/src/sound/Gb_Apu.cpp" \
    "$ROOT/src/sound/Gb_Audio_Link.cpp" \
    "$ROOT/src/sound/Wav_Decoder.cpp" \
    "$ROOT/src/sound/Wav_Stream.cpp" \
    -o "$OUT/music_test"

echo ""
echo "===== music_test ====="
run_or_die "$OUT/music_test"

# --- MIDI→MMLの変換(Python。出てきたMMLを本物の読み取りで確かめる) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/mml_dump.cpp" \
    "$ROOT/src/sound/Mml_Compiler.cpp" \
    -o "$OUT/mml_dump"

echo ""
echo "===== midi2mml_test ====="
rc=0
timeout -k 10 "$RUN_TIMEOUT_SEC" python3 "$ROOT/script/host_test/midi2mml_test.py" "$OUT/mml_dump" || rc=$?
if [ "$rc" -ne 0 ]; then
    if [ "$rc" -eq 124 ]; then
        echo "[FATAL] midi2mml_test が${RUN_TIMEOUT_SEC}秒を超えて応答しませんでした" >&2
    fi
    exit "$rc"
fi

# --- 外部コントローラー ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/pad_test.cpp" \
    "$ROOT/src/functions/Pad_Functions.cpp" \
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    -o "$OUT/pad_test"

echo ""
echo "===== pad_test ====="
run_or_die "$OUT/pad_test"

# --- 物理キーボード(KeyInputFunctions) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/key_input_test.cpp" \
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    "$ROOT/src/functions/KeyInput_Dispatch.cpp" \
    "$ROOT/src/functions/Pad_Functions.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardPanel.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardEng.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/key_input_test"

echo ""
echo "===== key_input_test ====="
run_or_die "$OUT/key_input_test"

# --- スリープ(省電力) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/power_test.cpp" \
    "$ROOT/src/functions/Power_Functions.cpp" \
    "$ROOT/src/functions/Pad_Functions.cpp" \
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    -o "$OUT/power_test"

echo ""
echo "===== power_test ====="
run_or_die "$OUT/power_test"

# --- 電卓の式評価 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/calc_eval_test.cpp" \
    -o "$OUT/calc_eval_test"

echo ""
echo "===== calc_eval_test ====="
run_or_die "$OUT/calc_eval_test"

# --- 電卓のGUI配線(キーパッドの当たり判定/画面/履歴) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
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
run_or_die "$OUT/calculator_test"

# --- 単語辞書(部分一致検索) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/dict_test.cpp" \
    "$ROOT/src/dict/Word_Dict.cpp" \
    -o "$OUT/dict_test"

echo ""
echo "===== dict_test ====="
run_or_die "$OUT/dict_test"

# --- 辞書アプリ(DictScene)のGUI配線 ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
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
run_or_die "$OUT/dict_scene_test"

# --- WidgetFactory / WidgetRegistry::Resolve()(Lua統合の受け皿) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
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
    "$ROOT/src/gui/widgets/RectShape.cpp" \
    "$ROOT/src/gui/widgets/EllipseShape.cpp" \
    "$ROOT/src/gui/widgets/LineShape.cpp" \
    "$ROOT/src/gui/widgets/TriangleShape.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardPanel.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    -o "$OUT/widget_factory_test"

echo ""
echo "===== widget_factory_test ====="
run_or_die "$OUT/widget_factory_test"

# --- WidgetProperty(プロパティのget/set共通口) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
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
    "$ROOT/src/gui/widgets/RectShape.cpp" \
    "$ROOT/src/gui/widgets/EllipseShape.cpp" \
    "$ROOT/src/gui/widgets/LineShape.cpp" \
    "$ROOT/src/gui/widgets/TriangleShape.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardPanel.cpp" \
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
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    -o "$OUT/widget_property_test"

echo ""
echo "===== widget_property_test ====="
run_or_die "$OUT/widget_property_test"

# --- StepBudget(実時間のmicros()が要るためstubs/ではなくpc/compat/を使う) ---
compile_or_die g++ -std=gnu++17 -g -fsanitize=address,undefined -pthread \
    -I "$ROOT/pc/compat" -I "$ROOT/src" \
    "$ROOT/script/host_test/step_budget_test.cpp" \
    -o "$OUT/step_budget_test"

echo ""
echo "===== step_budget_test ====="
run_or_die "$OUT/step_budget_test"

# --- アラーム ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/alarm_test.cpp" \
    "$ROOT/src/functions/Alarm_Functions.cpp" \
    "$ROOT/src/functions/Widget_Functions.cpp" \
    "$ROOT/src/gui/widgets/Widget.cpp" \
    "$ROOT/src/gui/widgets/WidgetRegistry.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
    "$ROOT/src/gui/widgets/Button.cpp" \
    "$ROOT/src/gui/widgets/Label.cpp" \
    "$ROOT/src/gui/widgets/Icon.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/functions/Font_Functions.cpp" \
    "$ROOT/src/functions/Mem_Functions.cpp" \
    -o "$OUT/alarm_test"

echo ""
echo "===== alarm_test ====="
run_or_die "$OUT/alarm_test"

# --- 通知(中身だけ。トースト/音/起動は Notification_Sources.cpp でPCビルドで確かめる) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/notification_test.cpp" \
    "$ROOT/src/functions/Notification_Functions.cpp" \
    -o "$OUT/notification_test"

echo ""
echo "===== notification_test ====="
run_or_die "$OUT/notification_test"

# --- ErrorFunctions(エラーの見せ方の共通口) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/error_functions_test.cpp" \
    "$ROOT/src/functions/Error_Functions.cpp" \
    "$ROOT/src/gui/widgets/ScrollContainer.cpp" \
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
run_or_die "$OUT/error_functions_test"

# --- lib/lua(vendorしたLua本体)が実際にビルド・リンクできること ---
# pc/CMakeLists.txtと同じくLUA_USE_LINUX等は定義しない(実機は
# dlopen等のPOSIX機能を持たないため、ANSI構成で確認する。-ldlも不要になる)。
# 3本のLuaテストで同じオブジェクトを使い回す
mkdir -p "$OUT/lua_obj"
for f in "$ROOT"/lib/lua/src/*.c; do
    compile_or_die gcc -std=gnu99 -g -fsanitize=address,undefined \
        -I "$ROOT/lib/lua/src" -c "$f" -o "$OUT/lua_obj/$(basename "$f" .c).o"
done

compile_or_die g++ $CXXFLAGS -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_smoke_test.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_smoke_test"

echo ""
echo "===== lua_smoke_test ====="
run_or_die "$OUT/lua_smoke_test"

compile_or_die g++ $CXXFLAGS -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_stdlib_test.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_stdlib_test"

echo ""
echo "===== lua_stdlib_test ====="
run_or_die "$OUT/lua_stdlib_test"

compile_or_die g++ $CXXFLAGS -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_alloc_budget_test.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_alloc_budget_test"

echo ""
echo "===== lua_alloc_budget_test ====="
run_or_die "$OUT/lua_alloc_budget_test"

# --- LuaEngine(Lua<->C++バインディング本体)をウィジェット層と繋げた結合テスト ---
compile_or_die g++ $CXXFLAGS $INCLUDES -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_engine_test.cpp" \
    "$ROOT/src/lua/LuaEngine.cpp" \
    "$ROOT/src/functions/Notification_Functions.cpp" \
    "$ROOT/src/functions/Pad_Functions.cpp" \
    "$ROOT/src/functions/Sound_Functions.cpp" \
    "$ROOT/src/functions/Battery_Functions.cpp" \
    "$ROOT/src/sound/Chip_Synth.cpp" \
    "$ROOT/src/sound/Mml_Compiler.cpp" \
    "$ROOT/src/sound/Music_Player.cpp" \
    "$ROOT/src/sound/Gb_Apu.cpp" \
    "$ROOT/src/sound/Gb_Audio_Link.cpp" \
    "$ROOT/src/sound/Wav_Decoder.cpp" \
    "$ROOT/src/sound/Wav_Stream.cpp" \
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
    "$ROOT/src/gui/widgets/RectShape.cpp" \
    "$ROOT/src/gui/widgets/EllipseShape.cpp" \
    "$ROOT/src/gui/widgets/LineShape.cpp" \
    "$ROOT/src/gui/widgets/TriangleShape.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSaveDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSelectDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/ColorDialog.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardPanel.cpp" \
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
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$OUT"/lua_obj/*.o \
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    -o "$OUT/lua_engine_test" -lssl -lcrypto

echo ""
echo "===== lua_engine_test ====="
run_or_die "$OUT/lua_engine_test"

# --- LuaScene(SD上のLuaスクリプトを読んで実行する画面)をシーン遷移と組み合わせた結合テスト ---
compile_or_die g++ $CXXFLAGS $INCLUDES -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_scene_test.cpp" \
    "$ROOT/src/gui/scenes/LuaScene.cpp" \
    "$ROOT/src/lua/LuaEngine.cpp" \
    "$ROOT/src/functions/Notification_Functions.cpp" \
    "$ROOT/src/functions/Pad_Functions.cpp" \
    "$ROOT/src/functions/Sound_Functions.cpp" \
    "$ROOT/src/functions/Battery_Functions.cpp" \
    "$ROOT/src/sound/Chip_Synth.cpp" \
    "$ROOT/src/sound/Mml_Compiler.cpp" \
    "$ROOT/src/sound/Music_Player.cpp" \
    "$ROOT/src/sound/Gb_Apu.cpp" \
    "$ROOT/src/sound/Gb_Audio_Link.cpp" \
    "$ROOT/src/sound/Wav_Decoder.cpp" \
    "$ROOT/src/sound/Wav_Stream.cpp" \
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
    "$ROOT/src/gui/widgets/RectShape.cpp" \
    "$ROOT/src/gui/widgets/EllipseShape.cpp" \
    "$ROOT/src/gui/widgets/LineShape.cpp" \
    "$ROOT/src/gui/widgets/TriangleShape.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSaveDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSelectDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/ColorDialog.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardPanel.cpp" \
    "$ROOT/src/gui/widgets/apps/FileExplorer.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$OUT"/lua_obj/*.o \
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    -o "$OUT/lua_scene_test" -lssl -lcrypto

echo ""
echo "===== lua_scene_test ====="
run_or_die "$OUT/lua_scene_test"

# --- LuaAppScanner(SD走査によるLuaアプリの自動登録) ---
compile_or_die g++ $CXXFLAGS $INCLUDES -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_app_scanner_test.cpp" \
    "$ROOT/src/lua/LuaAppScanner.cpp" \
    "$ROOT/src/gui/scenes/LuaScene.cpp" \
    "$ROOT/src/lua/LuaEngine.cpp" \
    "$ROOT/src/functions/Notification_Functions.cpp" \
    "$ROOT/src/functions/Pad_Functions.cpp" \
    "$ROOT/src/functions/Sound_Functions.cpp" \
    "$ROOT/src/functions/Battery_Functions.cpp" \
    "$ROOT/src/sound/Chip_Synth.cpp" \
    "$ROOT/src/sound/Mml_Compiler.cpp" \
    "$ROOT/src/sound/Music_Player.cpp" \
    "$ROOT/src/sound/Gb_Apu.cpp" \
    "$ROOT/src/sound/Gb_Audio_Link.cpp" \
    "$ROOT/src/sound/Wav_Decoder.cpp" \
    "$ROOT/src/sound/Wav_Stream.cpp" \
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
    "$ROOT/src/gui/widgets/RectShape.cpp" \
    "$ROOT/src/gui/widgets/EllipseShape.cpp" \
    "$ROOT/src/gui/widgets/LineShape.cpp" \
    "$ROOT/src/gui/widgets/TriangleShape.cpp" \
    "$ROOT/src/gui/widgets/LayoutContainer.cpp" \
    "$ROOT/src/gui/widgets/GridContainer.cpp" \
    "$ROOT/src/gui/widgets/TabBar.cpp" \
    "$ROOT/src/gui/widgets/dialogs/MsgDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/InputDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSaveDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/FileSelectDialog.cpp" \
    "$ROOT/src/gui/widgets/dialogs/ColorDialog.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardNum.cpp" \
    "$ROOT/src/gui/widgets/keyboards/KeyboardPanel.cpp" \
    "$ROOT/src/gui/widgets/apps/FileExplorer.cpp" \
    "$ROOT/src/gui/widgets/interfaces/ITextColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IBorderColor.cpp" \
    "$ROOT/src/gui/widgets/interfaces/IFontImplementation.cpp" \
    "$ROOT/src/gui/icons/icon_render.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$OUT"/lua_obj/*.o \
    "$ROOT/src/functions/KeyInput_Functions.cpp" \
    -o "$OUT/lua_app_scanner_test" -lssl -lcrypto

echo ""
echo "===== lua_app_scanner_test ====="
run_or_die "$OUT/lua_app_scanner_test"

# --- Luaアプリのゲームの規則(pico.*を差し替えてLuaだけで動かす) ---
compile_or_die g++ $CXXFLAGS -I "$ROOT/lib/lua/src" \
    "$ROOT/script/host_test/lua_script_test.cpp" \
    "$OUT"/lua_obj/*.o \
    -o "$OUT/lua_script_test"

echo ""
echo "===== tetris_test ====="
run_or_die "$OUT/lua_script_test" "$ROOT/script/host_test/tetris_test.lua" "$ROOT"

# --- SSHアプリ(端末エミュレータと暗号まわりの小道具) ---
compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/vt_terminal_test.cpp" \
    "$ROOT/src/ssh/Vt_Terminal.cpp" \
    -o "$OUT/vt_terminal_test"

echo ""
echo "===== vt_terminal_test ====="
run_or_die "$OUT/vt_terminal_test"

compile_or_die g++ $CXXFLAGS $INCLUDES \
    "$ROOT/script/host_test/ssh_util_test.cpp" \
    "$ROOT/src/ssh/Ssh_Util.cpp" \
    -o "$OUT/ssh_util_test"

echo ""
echo "===== ssh_util_test ====="
run_or_die "$OUT/ssh_util_test"
