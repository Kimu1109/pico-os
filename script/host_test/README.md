# ホストテスト

実コード(`src/`)を PC の g++ + ASan/UBSan で動かし、解放漏れ・二重解放・バッファ越え・未定義動作と、
各機能の回帰を見る。実機(RP2350)のビルドとは独立。LovyanGFX / SdFat / WiFi 等は `stubs/` のダミーに差し替える。

## 回し方

```sh
sh script/host_test/run.sh                 # 全グループ
sh script/host_test/run.sh core lua-engine # 指定したグループだけ(作るのもそのグループのテストだけ)
sh script/host_test/run.sh -j 4 core       # 同時に回す数(既定: CPU数)
sh script/host_test/run.sh --list          # グループ名の一覧
```

中身は `cmake -S script/host_test -B script/host_test/build` → `cmake --build` → `ctest -L <グループ>`。
ビルドの結果は `script/host_test/build/` に残るので、2回目からは変えたファイルだけ作り直す。
1本だけ回すなら `ctest --test-dir script/host_test/build -R '^sound_test$' --output-on-failure`。

## しくみ(`CMakeLists.txt`)

- `src/` の `.cpp` は層(`script/check_layers.py` の段)ごとの静的ライブラリになる(`picoos_base` 〜 `picoos_boot`)。
  どのファイルがどの段かは `check_layers.py --sources` が答えるので、ファイルの一覧はどこにも書かない。
  `src/` に `.cpp` を足しても何もしなくてよい(スタブで作れないものだけ `HOST_EXCLUDE` に理由付きで足す)。
- テストは全部の段を1つのリンクの組(`LINK_GROUP:RESCAN`)でリンクする。静的ライブラリはリンカが
  「足りないシンボルを持つオブジェクト」だけを拾うので、テストが自分で偽物を書いた関数の本物は引っ張られない。
  以前の `run.sh` のように、テストごとにリンクする `.cpp` を並べる必要は無い。
- グループは CTest のラベル。同じグループの中は1本ずつ(`RESOURCE_LOCK`)、別のグループとは並列に回る。
  1本60秒で打ち切る(無限ループで止まらないように)。テストは リポジトリのルートを作業ディレクトリにして動く。
- テストを足すときは `<名前>.cpp`(か `.lua` / `.py`)を書き、`CMakeLists.txt` の「テストの一覧」に1行足す。
  CI(`.github/workflows/host-tests.yml`)のグループの一覧は `HOST_TEST_GROUPS` と揃えること(CIが見張る)。

`run_net.sh`(本物のソケット・TLSサーバ・sshd を使う結合テスト。`pc/compat/` を使う)と
`run_mem.sh`(確保回数の計測。ASanを外し `-Os -fno-rtti`)はフラグもスタブも違うので、今も1本ずつ g++ で作る。

## 収録テスト

```
  scene_test … シーン遷移(SceneFunctions / WidgetFunctions)とメモリ計測フックの配線
  label_test … Labelのテキストレイアウト結果(幅/高さ/文字数/カーソル座標)の固定
  markdown_test … MarkdownViewのブロック高さとLabelの実高さの整合(重なり検出)
  config_test   … 設定ファイル(key=value)の読み書き
  secret_cipher_test… Wi-Fi SSID/パスワードの暗号化保存(crypto/Secret_Cipher.hpp)。
                  往復・用途(purpose)ごとに鍵ストリームが変わること・enc1:接頭辞の
                  無い値は平文として読める後方互換・壊れたデータへの安全な失敗
  wifi_profiles_test… 保存済みのWi-Fiネットワーク(net/Wifi_Profiles、/sys/wifi.cfg)。network.cfgの
                  旧形式からの取り込み・追加/更新/上限/削除・接続できたものを先頭へ(自動接続の順番)・
                  ON/OFFの保存・番号ごとの暗号化
  app_test      … アプリ登録簿とランチャのタイル配置/当たり判定(アプリストアの登録/更新/削除も)
  home_test     … ホーム画面のウィジェット(並び・詰め方・home.cfg・ウィジェットのタップ・TODOの控え・予定)
  firmware_test … ファームの更新(apps/firmware/Fw_Image・Fw_Update)。見出しの検査・署名(信頼/なし/知らない鍵/壊れた)・
                  版の確認・置き場への書き込みと読み直し・gzipの末尾の大きさ・途中で切れる/化ける/取り消す。
                  firmware_tool_test(Python)が script/firmware_tool.py で作ったものも読ませる
  app_repo_test … アプリストアの配布物の読み取り(apps/store/App_Repo、APP_REPO.md)。カタログ/ファイル一覧の行・
                  名前とパスの検査・版の比較・権限・Ed25519の署名(Pythonの署名をMonocypherで確かめる)
  bmp_test      … BMPの読み書き(gui/icons/Bmp_Codec)。1/4/8/16/24/32bpp・上下の並び・窓・OSのパレットへ寄せる/番号のまま/
                  フルカラーのまま・断る形式・途中で切れたファイル・スクリーンショットの書き出しと読み直し
  path_test     … パスの正規化と相対解決(Markdownブラウザのリンク追従の土台)
  file_ops_test … ファイルアプリのコピー/移動/名前の変更の下請け(ファイルのコピー・重ならない名前・パスと名前の判定)
  touch_filter_test… タッチ座標のmedian-of-3ノイズ抑制(hal/TouchFilter.hpp)。
                  単発のスパイクを無視すること・滑らかな動きへの追従・タッチ開始ごとの
                  履歴リセット
  touch_calib_test… タッチ位置の補正(hal/TouchCalib.hpp)。既定の式・最小二乗でずれを消す・
                  ノイズ・決まらない点の拒否・押し間違いの検出
  cache_test    … 文書キャッシュ(半端なファイルを残さないこと/目録の書き換え)とマニフェストの引き当て
  http_test     … URLの分解/解決と、HTTPレスポンスの解釈(ソケット抜きで検証)
  discovery_test… サーバ情報(/.well-known/pico-os)の解釈と前方互換
  ical_test     … iCalendar(.ics)の読み取り(折り返し/エスケープ/UTC→現地時刻)と
                  繰り返し(RRULE/EXDATE/上書き予定)がどの日に出るか
  calendar_scene_test… カレンダーアプリのGUI配線(MonthGridのタップ位置→日付、
                  CalendarSceneの生成/解放、SDや.icsが無いときの案内)
  calendar_sync_secret_test… /calendar/sources.cfgの暗号化保存(CalendarSync::WriteSource/
                  ReadSourceがcrypto/Secret_Cipher.hppを通すこと)。長いURLの往復・複数件・
                  同名の上書き・後方互換(平文の既存行がそのまま読める)
  file_search_test… ファイル検索(FileSearchScene)・容量(StorageScene)とSDを1件ずつたどる部品(storage/Sd_Walker)。
                  深さの上限・名前の比べ方・大きい順・フォルダの中身の合計
  chat_proto_test… チャットの応答(1行1件のTSV)の読み取りと本文のエスケープ
  chat_scene_test… チャットアプリのGUI配線(ChatLogViewの折り返し/スクロール、
                  ChatSceneの生成/解放、chat.cfgが無い/足りないときの案内)
  todoist_proto_test… TODOアプリ(Todoist)の応答の読み取り(apps/todo/Todoist_Proto)と、その下のJSONの
                  読み取り(util/Json_Reader)。エスケープ/サロゲート/切り詰め/1バイトずつ/誤り、
                  一覧から必要なキーだけ拾う・完了済みを飛ばす・next_cursor、期限の読み取りと表記・並べ替え
  xml_reader_test… XMLの読み取り(util/Xml_Reader)。RSSから題名/リンクを拾う・実体参照/CDATA/コメント/DOCTYPE・
                  1バイトずつ・切り詰め・誤り・壊した入力で落ちない
  todo_test     … TODOアプリのリマインダー(apps/todo/Todo_Reminders。本物のNotificationFunctionsへ予約する)と
                  TodoSceneの生成/解放・トークンが無いときの案内
  gb_emu_test   … Game Boyエミュ(GbEmu/GameBoyPad/GameBoyView)。テスト内で組み立てたROMで、
                  読み込みの断り方・セーブ(.sav)の往復・ボタン・不正な命令で落ちないこと・
                  変わった行だけdirtyにすること・操作パッドのタップ位置→ボタン・
                  音源チップへの書き込みが時刻付きで渡ること/読み出しの答え方・
                  内蔵フラッシュへのROMの置き場(GbRomFlash。NORの偽物へ書く/使い回す/書き直す/失敗)を確認する
  gb_apu_test   … ゲームボーイの音源チップ(GbApu)と時刻付きの列(GbAudioLink)。矩形波の高さ/デューティ・
                  長さ・エンベロープ・スイープ・波形メモリ・ノイズ・電源・振り分け、フレームの中の位置どおりに
                  効くこと・エミュが止まったら無音・溜まりすぎたら追いつく
  sound_test    … 音声出力(SoundFunctions)。アンプの検出(ばたつきを採らない)、刺さっている間だけ
                  I2Sを動かすこと、未接続の間も音が時間どおりに進み刺し直すと続きから鳴ること、
                  矩形波の中身、sound.cfg(output=off/volume)、I2Sを開始できなかったとき
  wav_test      … WAVの再生。WavDecoder(形式ごとの変換・チャンネルの平均・周波数の変換・チャンクの読み飛ばし・
                  ループ・断り方)とSoundFunctionsの配線(音量・止める/切り替えで先読みを捨てる・途切れの数え方・
                  アンプが無い間も時間どおりに進む)
  music_test    … 曲データ(pico-os MML、MUSIC_FORMAT.md)。読み取り(音の高さ/長さ/繰り返し/マクロ/
                  誤りの行・列/警告)、シーケンサー(サンプル単位の音の位置・テンポ・繰り返し・ループ・
                  効果音への貸し出し)、SoundFunctionsの配線(置き場の入れ替え・効果音との同居)
  mic_test      … マイク(SPH0645)の入力と録音。計算(sound/Mic_Dsp.hpp: 18bitの取り出し・直流・増幅・間引きで折り返さない・
                  信号の有無・列)と MicFunctions(2コア目と1コア目を交互に: 開始/返す・左右の組がずれない・WAVの見出し・
                  取りこぼし・見出しの書き直し・I2Sの失敗・mic.cfg)
  play_queue_test … ミュージックアプリの再生の順番(apps/music/Play_Queue.hpp)。順番通り/ミックス・リピート(しない/全曲/1曲)
  midi2mml_test … MIDI→MMLの変換(script/midi2mml.py、Python)。テストの中で組み立てたMIDIを変換し、
                  出てきたMMLを本物の読み取り(mml_dump.cpp)へ通して音の位置/高さ/長さ/テンポを確かめる
  pvid_test     … 動画(.pvid)。変換(script/generate_pvid.py、Python+numpy)のタイルの往復・目録・差分の判定と、
                  本体の読み取り(apps/video/Pvid_Player、pvid_dump.cpp)が全部のフレーム・シークで同じ絵になること・壊れたファイルで落ちないこと
  key_input_test … 物理キーボードの窓口(KeyInputFunctions)。"key ..."の行の読み取り、padの行との同居、
                  画面のonKey()→開いているキー盤の順に配ること、キー盤の編集(挿入/削除/移動/決定)、数字のキー盤の制限、
                  日本語入力の入り切り(Tab2回・半角/全角・Ctrl+Space)と日本語のキー盤のかな漢字変換(小さい辞書を作って)
  romaji_kana_test … ローマ字→かな(ime/Romaji_Kana.hpp)。ん・っ・拗音・記号・送りの印
  edit_history_test … テキストエディタの元に戻すの記録(apps/texteditor/Edit_History.hpp)とクリップボード(Clipboard_Functions)。
                  乱数の編集を1件ずつ戻す・溢れ・打鍵のまとめ・ひとまとまり・保存した時点、\rの除去と4KiBでの切り方
  cardkb_test   … CardKB2(I2Cのキーボード)の入力元。変換・未接続で静か・検出・打鍵・抜き差し
  msc_bridge_test … USBドライブ(マスストレージ)の64バイトずつの読み書き ⇔ SDの512バイトのセクタ。
                  RAMディスクで、読むのは1セクタ1回・頭からの書き込みは溜めて1回・途中からは混ぜる・誤り
  wii_classic_test … Wiiクラシックコントローラー(I2C)。6バイトの読み取り・左スティック・初期化の手順・
                  検出と抜き差し・PadFunctionsへ渡りUSBシリアルとORで重なること
  pad_test      … 外部コントローラーの窓口(PadFunctions)。USBシリアルの行("pad XXXX")の読み取り、
                  押した/離したのはそのフレームだけ、行が途切れたら外れて押しっぱなしにならないこと、
                  Game Boyのボタンへの対応
  power_test    … スリープ(PowerFunctions)。操作が無いまま sleep-timeout が過ぎると入り、画面/Wi-Fi/音の
                  省電力が掛かること、タッチで起きて起こした指は離すまで画面へ渡さないこと、KeepAwake()・
                  音・Wi-Fi接続中は入らないこと
  devkey_test   … 端末ごとの鍵(DeviceKeyFunctions、OTPはメモリの中で真似る)。作る前は固定鍵・作るとSDの設定をenc3:へ
                  書き直す・起動し直しても読める・鍵が無いとenc3:/Luaのversion 3/PINのv2は読めない・PINのv1→v2・書きかけの枠
  lock_test     … 画面ロック(LockFunctions/LockScreen)。PINの記録(塩+BLAKE2b)・決める/変える/外す・スリープでロック・
                  打鍵とタッチ・間違えたときの待ち時間・鳴っているアラームを止める・起動時のロック
  notification_test … 通知(NotificationFunctions)の中身。予約の種類ごとの発火、置き換え・上限、履歴、
                  控えめ、保存と読み込み、起動理由
  alarm_test    … アラーム(AlarmFunctions)。alarm.cfgの読み書き、時刻での発火と確認ダイアログ、
                  繰り返し(1回/毎日/平日/土日)、止める/5分後/放置での停止、ダイアログが消えたときの出し直し
  calc_eval_test… 電卓アプリの式評価(四則演算/関数電卓の関数/省略した掛け算/Ans/x/エラー)
  calculator_test… 電卓アプリのGUI配線(キーパッドの当たり判定/SHIFT/HYP/グラフ/履歴/画面切替)
  dict_test     … 単語辞書(en-ja-and-ja-en.tsv形式)の部分一致検索(前方一致の即時性/全体走査/重複無し/件数上限)
  dict_scene_test… 辞書アプリ(DictScene)のGUI配線(入力欄→検索→一覧への逐次反映→タップで詳細欄)
  widget_factory_test… WidgetFactory(WidgetType→new Xxx)とWidgetRegistry::Resolve()
                        (Lua統合向けの発行側/消費側で、以前は呼び出し元・テストとも無かった)
  focus_test… ウィジェットのフォーカス(FocusFunctions)。読む順と矢印の移り先・候補にならないもの・決定で押す・
               消えたとき・タッチで枠を消す・ダイアログの範囲・Checkbox/TabBar/NumberSlider/ScrollList/DropdownMenu/ScrollContainer
  widget_property_test… WidgetProperty(WidgetType非依存のget/set共通口)。
                         WidgetFactory対応20種それぞれの代表プロパティの読み書きと、
                         型不一致/非対応id/nullptrがfalseで安全に弾かれることを確認。
                         Icon::IconOpaque/GridContainer::HAlign・VAlignのget、
                         NumberInput::Text(setNum/getNum)、ScrollList/DropdownMenuの
                         ItemCountは元々getterが無く未対応だった項目(2026-09-21解消)
  step_budget_test… Task::update()内の作業ループを時間で区切るStepBudgetの検証。
                     他と違いstubs/ではなくpc/compat/を先に見る(実時間のmicros()が要るため。CMakeLists.txt の COMPAT)
  error_functions_test… ErrorFunctions::ShowFatal()(エラーの見せ方の共通口)。
                         MsgDialogの生成・登録・自己破棄の配線を確認
  lua_smoke_test… vendorしたLua本体(lib/lua)が実際にビルド・リンクでき、
                   lua_newstate/luaL_dostring/lua_closeが動くことの確認
                   (「ビルドの二重管理」の解消)
  lua_stdlib_test… Lua本体の言語機能・標準ライブラリ(数値/文字列/テーブル/
                    クロージャ/メタテーブル/pcall/コルーチン/GC)が一通り動くことの確認。
                    LuaからCの関数(check())を呼べることも兼ねて確認する
  lua_alloc_budget_test… lua_newstateへカスタムアロケータを渡し、確保量に上限を
                          課しても安全に動く(予算超過でabortせずLUA_ERRMEM、
                          lua_close後は必ずused=0)ことの確認。RAM/Flash予算(200KB枠)の
                          実現方式の裏付け
  lua_engine_test… LuaEngine(Lua<->C++バインディング本体、src/lua/)を実際の
                    ウィジェット層と繋げて動かす結合テスト。pico.create/set/get/on/
                    add_child/destroyの一連の導線、コールバックの発火、コンテナへの
                    動的追加の重なり順、ScrollContainerの子を個別destroyしても
                    二重解放しないこと、スクリプトエラー時にErrorFunctions経由で
                    ダイアログが出ることまでを確認する
  lua_sandbox_test… Luaのサンドボックス(debug/io/os.exit等が無い・loadはテキストだけ・__gcは使えない)、
                   打ち切りを握り潰せないこと(pcall/xpcall/coroutineで包んで繰り返す・メッセージハンドラ・
                   入れ子のコールバックで予算を積み直す抜け道)、スタックトレース、デバッガ(偽物の画面で
                   ブレークポイント・ステップ実行・エラーで止まる・停止・変数・シリアルのdbgコマンド)
  devtools_test… プロファイラ(区間の時間・窓の集計・履歴)、クラッシュダンプ(次の起動で/crash/へ書く・
                   レジスタとCFSR・壊れた記録・通知・Luaのエラー)、/sys/debug.cfg
  lua_scene_test… LuaScene(SD上のLuaスクリプトを読んで実行する画面)を実際の
                   シーン遷移(Scene_Functions.cpp)と組み合わせて動かす結合テスト。
                   SDからの読み込み・pico.pop()での実際のランチャ復帰・
                   ファイル不在時のダイアログ表示・大きすぎるスクリプトの
                   打ち切り警告までを確認する
  lua_app_scanner_test… LuaAppScanner(SD上の"/lua/apps/<名前>/main.lua"を走査して
                   ランチャの登録簿へ自動登録する)。ディレクトリの走査自体は
                   ホストのSdFatスタブでは再現できないため、SD無し/ディレクトリ
                   が無い場合に安全に0件を返すことまでを確認する
                   (完全な走査結果はPCビルドの--shotで確認済み。CLAUDE.md
                   「SDを走査してLuaアプリを見つける処理」参照)
  pico_mock_test… script/host_test/lua/pico_mock.lua(Luaアプリのゲームの規則やUIを、実機/PCビルド無しで
                普通のLuaだけで確かめるための、pico.*の偽物)自身のテスト。ウィジェット・イベント・タイマー・
                ダイアログ・HTTP・SD・保存・画面遷移を記録して返せる
  tetris_test… Luaアプリ「テトリス」(pc/sdcard/lua/apps/テトリス/)のゲームの規則。
                pico.*をLuaの偽物に差し替え、lua_script_test(vendorしたLuaで.luaを
                動かすだけの下請け)で tetris_test.lua を実行する。ライン消し・壁蹴り・
                HOLD・長押しの連続移動・ゲームオーバーとハイスコア保存・タッチの操作ボタン
  oshite_test… Luaアプリ「おしてのぼれ」(pico.gameの軽い物理を使うパズルアクション)の
                3ステージを、操作を流し込んで実際に解けることを確かめる(pico.gameは本物)。
  reversi_test / minesweeper_test / breakout_test… Luaアプリ「リバーシ」「マインスイーパー」「ブロック崩し」
                (どれもpico.gameで作ってある)のゲームの規則。tetris_testと同じく、pico.gameは本物で
                pico.*だけ描画を捨てる偽物に差し替え、タップや十字キーを流し込んで確かめる
                (打つ・裏返す・パス・終局 / 最初の1手は安全・連鎖・旗・難易度 / 反射・ブロック・
                アイテム・ステージクリア・全20ステージ・自動プレイ)
  iso_world_test… 2.5Dの箱庭のエンジン(src/iso/Iso_World・Iso_Blit。Luaアプリ「ブロック」の pico.iso)。
                地形の生成がLua版と同じ(ハッシュで比べる)・チャンクの読み込みと手放し・保存と読み込みの往復・
                前の版からの移し替え・描画(見えない面・影・水・Lua版と同じ並び)・隠れたブロックを省いても
                画素が変わらないこと・タップ位置の引き当て・面の写し方・人や物(ブロックとの前後・水・影・
                頼まれた矩形だけ描き直しても全体と同じ)
  iso_td_test… 2.5Dの箱庭のタワーディフェンス向けの道具: 流れの場(Iso_Flow。経路探索と同じ値段・pass・
                少しずつ作る)・視線・近くの人や物・押し合い・弾・HPバーと印・世界全体を読み込んだままにする・ARENA の地形(平らな所・道があるか)
  blocks_test… Luaアプリ「ブロック」の画面の流れと操作(pico.iso は偽物)。作る/開く/移す→読み込む→遊ぶ→
                保存して戻る・置く/壊すの規則・カーソルと視点・タップ・ブロックを選ぶ画面
  vt_terminal_test… SSHアプリの端末エミュレータ(src/apps/ssh/Vt_Terminal)。折り返し・カーソル移動・
                消去・色(256色/RGB→16色)・全角(2セル)・スクロールバック・範囲スクロール・
                代替画面・問い合わせ(6n等)への返事・大きさの変更
  ssh_util_test… SSHクライアントの小道具(Ssh_Sha256/Ssh_Util)。SHA-256の既知の値・Base64・
                OpenSSH形式の秘密鍵の読み取り(パスフレーズ付き/RSAは断る)・ホスト鍵の指紋・
                known_hostsの照合と追記(本物のsshd相手の往復は run_net.sh の ssh_net_test)

```
