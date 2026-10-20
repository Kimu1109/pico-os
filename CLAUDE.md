# CLAUDE.md — pico-os プロジェクトコンテキスト

> このファイルは `Kimu1109/pico-os` リポジトリ直下に置く、Claude Code向けのプロジェクト背景資料。
> 元はClaude.aiのProject knowledgeとして管理されていた内容(2026-09-06時点情報)を統合したもの。
> **最終同期: 2026-09-25(実コードと突き合わせ済み)。2026-10-20に機能ごとの設計記録を `docs/design/` へ分けた(下の「設計記録の索引」)。**
> **一次情報源は常にこのリポジトリのコードと `SUMMARY.md`。このファイルは「相談の前提を素早く掴むための地図」であり、
> 実装と乖離があれば実コード側を信じること。**

## プロジェクト概要

Raspberry Pi Pico 2 W (RP2350, `rpipico2w`) 上で動く自作タッチGUI OS。PlatformIO + Arduinoフレームワークで書かれたC++プロジェクト。
過去に一度スクラップ&リビルドしており、現行版はSerenityOS的な設計思想を参考にしつつ、タッチ操作の組み込みGUIフレームワークを自前実装している。

- リポジトリ: https://github.com/Kimu1109/pico-os/tree/main
- TODO一次情報源: https://raw.githubusercontent.com/Kimu1109/pico-os/refs/heads/main/SUMMARY.md
  (「TODO」= 項目名だけのチェックボックス一覧 / 「詳細」= 補足が要る項目の説明、の2部構成。
  項目の**有無と進捗**はSUMMARY.md、**設計の背景**はこのファイルが持つ)
- コメント・ログメッセージは日本語、コード自体(識別子)は標準的な英語命名。

## ハードウェア構成

| 項目 | 内容 |
|---|---|
| MCU | Raspberry Pi Pico 2 W (RP2350) |
| ディスプレイ | 240x320 TFT, LovyanGFXドライバ |
| タッチ | XPT2046(割り込みIRQあり) |
| SDカード | SPI1専用(SdFat) |
| SPI0(TFT+タッチ共有) | SCK=18, MOSI=19, MISO=16 / TFT: CS=17, DC=20, RST=21 / TOUCH: CS=13, IRQ=9 |
| SPI1(SD専用) | CS=15, SCK=10, MOSI=11, MISO=12, 10MHz |
| バックライト | TFT_LED=22(PWM調光。LovyanGFXの`Light_PWM`、PWMスライス3のチャンネルA) |
| 音声(I2S) | MAX98357A: BCLK=2, LRCLK=3(=BCLK+1固定), DIN=4 / 検出=5(アンプ側でGND、内部プルアップ) / 休止(SD)=6(任意)。VINは5V(VBUS/VSYS) |
| 音声入力(I2Sマイク) | SPH0645: BCLK=26, LRCLK=27(=BCLK+1固定), DOUT=28 / SEL=GND(左)/ 3V3・GND。出力とは別のI2S(48kHz・32bitスロット)。下記「マイクと録音」 |
| 外部コントローラー | Wiiクラシックコントローラー(I2C0: SDA=GP0, SCL=GP1、3.3V、アドレス0x52。CardKB2と同じバス)。ドライバはあるが**実物では未確認**。USBシリアル経由のPCのキーボードでも代用できる(下記「外部コントローラー」) |
| TFT_MAX_SPEED | 80MHz |

画面/カラー定数は `src/consts.hpp` に集約(`SCREEN_WIDTH=240`, `SCREEN_HEIGHT=320`, PICO-8風16色パレット `PICO_BLACK`〜`PICO_WHITE`、既定は `PICO_BACKGROUND=15` / `PICO_FORECOLOR=0`)。

**組み込み環境(RAM/Flash制約あり)前提でアドバイスすること。標準的なPC向けC++の常識(動的確保多用、例外多用など)を安易に持ち込まない。**

## ビルド構成 (`platformio.ini`)

```ini
[env:rpipico2w]
platform = raspberrypi
board = rpipico2w
framework = arduino
lib_deps =
    lovyan03/LovyanGFX@1.2.28
    https://github.com/PaulStoffregen/XPT2046_Touchscreen.git#v1.4
board_build.filesystem_size = 1728k     ; ファームの更新の置き場(下の「ファームの更新」)
extra_scripts = pre:script/pio_firmware.py, post:script/pio_firmware_targets.py
monitor_speed = 115200
```
モニタはUTF-8。`extra_scripts`はファームの版(`git rev-list --count HEAD`等)を埋め込み、`-t fwpack`/`-t fwpush`を足す。

**LovyanGFXの版はこの`lib_deps`が唯一の情報源**で、`pc/CMakeLists.txt`がこの行を読んで同じタグを取得する。
範囲指定(`^1.2.26`等)だとPlatformIOが実際に落とす版が確定せずPCビルドと食い違うため、**完全固定にしてある**
(範囲指定へ戻すとPCビルドのconfigureがその旨を出して止まる)。

## ディレクトリ構成

```
src/
  main.cpp                 起動・メインループ
  OS_Data.hpp               グローバル状態 (OSData namespace)
  consts.hpp                 ピン/画面/パレット定数
  config/LGFX_Config.hpp     LovyanGFXのパネル設定
  system/                    OSの土台の「Xxx_Functions」(ログ・設定・時刻・メモリ・入力の列(KeyInput/Pad)・通知の中身・バックグラウンドの登録簿・クリップボード)
  hal/                       ハードの窓口の「Xxx_Functions」(タッチ・SD・電池・音声出力・マイク・Wi-Fi・I2Cのバスとその上の入力元)
  services/                  OSの機能の「Xxx_Functions」(アプリ登録簿・アラーム・タイマー・電源・電源メニュー・ロック・画面の明るさ・スクリーンショット・ファームの更新・USBドライブ・端末の鍵)
  devtools/                  開発者向けの道具(プロファイラ・クラッシュダンプ・debug.cfg・hwコマンド・起動時の自己診断とフォントのカバレッジ)
  boot/                      つなぎ: 登録簿の中身(App_List / Background_List)・打鍵の配り先(KeyInput_Dispatch)・通知の出し入れ(Notification_Sources)
  gui/
    core/                   GUIの仕組みの「Xxx_Functions」(GFX=合成と液晶への転送・Font・Widget・Focus・Scene(+Scene基底)・Keyboard・Error)
    icons/                  アイコンデータ(tabler_iconsから生成)
    scenes/                 機能の中身を持たない画面(基底のSceneは gui/core/)(LuaScene/ClocksScene/SettingsScene/FileExplorerScene/RecorderScene等)。機能の中身を持つアプリの画面は apps/<機能>/
    widgets/                汎用ウィジェット + 基底 (Widget / WidgetID / WidgetRegistry)
      apps/                 複数のアプリ・Luaから使う部品のうち、用途が決まっているもの(MarkdownView/FileExplorer/AnalogClock/DurationPicker/MonthGrid/SeekBar/MediaButtons/CalibView)。1つのアプリ専用の部品は apps/<機能>/ui/
      dialogs/              モーダルダイアログ(オンスクリーンキーボードのダイアログ枠 KeyboardDialog を含む)
      keyboards/            オンスクリーンキーボードのキー盤3種(Keyboard/KeyboardEng/KeyboardNum)と基底KeyboardPanel
      interfaces/            ミックスイン的インターフェース
      systems/               OSのシェル部品(Statusbar / AppGrid / PerfOverlay)
  ime/                       SKK方式かな漢字変換辞書エンジン
  apps/                      機能ごとの置き場(1フォルダ=1機能)。直下=機能の中身(段は lib)、ui/=その機能専用のウィジェット・ダイアログ(ui)、*Scene(分けたものは *Scene_*.cpp)=画面(app)。下記「層と置き場所」
    browser/                 Markdownブラウザ。取得〜キャッシュの配線(Doc_Fetch)・サーバ情報(Discovery)・検索(Doc_Search)・マニフェスト(Manifest)・文書キャッシュ(Doc_Cache)・画像参照の走査(Md_Scan)・MarkdownScene・ui/SearchDialog
    calculator/              式の評価(Calc_Eval)・CalculatorScene・ui/CalculatorKeypad・ui/GraphView
    calendar/                iCalendar(.ics)の読み取りと繰り返しの引き当て(Ical) / 取得元URLからの取得(Calendar_Sync) / 予定の通知(Calendar_Reminders)・CalendarScene・ui/EventDetailDialog
    chat/                    チャットサーバの応答の読み取り(Chat_Proto) / 通信係(Chat_Client) / 閉じている間の未読の見張り(Chat_Watch)・ChatScene・ui/ChatLogView。下記「チャット」「バックグラウンド実行の枠組み」参照
    dict/                    英和/和英辞書(Word_Dict)・DictScene
    firmware/                ファームの更新(.pfw)の見出しと署名(Fw_Image)・受け取りの流れ(Fw_Update)・置き場(Fw_Store: 実機はLittleFS+PicoOTA)・版(Fw_Version)・焼き込みの公開鍵(Fw_Keys)・FirmwareScene。OSへのつなぎは services/Firmware_Functions。下記「ファームの更新」参照
    gb/                      Game Boyエミュ本体(Gb_Emu。lib/peanut_gbを包む)・内蔵フラッシュのROMの置き場(Gb_Rom_Flash)・コントローラーのボタンの対応(Gb_PadMap)・GameBoyScene・ui/GameBoyView・ui/GameBoyPad。音源チップは sound/。下記「ゲームボーイ」参照
    home/                    ホーム画面(ランチャ)。並びと詰め方・home.cfg(Home_Layout)・描き方(Home_Widget / Home_Widgets)・予定の拾い方(Home_Agenda)・AppGrid・HomeScene。直下の段は service。下記「ホーム画面のウィジェット」参照
    music/                   再生の順番と常駐の再生(Music_Service / Play_Queue)・MusicScene・ui/MusicPlayerPanel。音源とMMLは sound/
    ssh/                     SSHクライアント(Ssh_Client)・端末エミュレータ(Vt_Terminal)・SHA-256(Ssh_Sha256)・鍵/known_hosts(Ssh_Util)・SshScene・ui/TerminalView・ui/TermKeyBar。下記「SSHクライアント」参照
    store/                   アプリストア。配布物の読み取りと署名の確認(App_Repo)・通信とインストール(App_Store)・焼き込みの公開鍵(App_Repo_Keys)・StoreScene。直下の段は app。下記「アプリストア」参照
    texteditor/              元に戻す/やり直しの記録(Edit_History)・TextEditorScene
    todo/                    Todoist API v1 の応答の読み取りと日付(Todoist_Proto) / 通信係(Todoist_Client) / リマインダー(Todo_Reminders) / ホーム画面の控え(Todo_Cache)と取り直し(Todo_Watch、段は service)・TodoScene。下記「TODOアプリ」参照
    video/                   動画(.pvid)の読み取り(Pvid_Player)・VideoScene・ui/VideoView。下記「動画(.pvid)」参照
  crypto/                    暗号化して保存する値(Secret_Cipher: enc1:/enc3:)・XChaCha20-Poly1305とArgon2id(Secret_Aead)・端末ごとの鍵の用途別の鍵(Device_Key)
  iso/                       2.5D(斜め上から見た)ボクセルの箱庭のエンジン(Iso_World: チャンク・生成・保存・描画・影・引き当て・人や物(エンティティ) / Iso_Blit: 面の絵の写し方 / Iso_Path: 条件つきの経路探索 / Iso_Flow: 流れの場(フローフィールド))。Luaの`pico.iso`。下記「ブロック」「2.5Dエンジンのタワーディフェンス向けの道具」参照
  sound/                     マイクの計算(Mic_Dsp)とWAVへの書き出し(Wav_Writer)・チップチューン音源(Chip_Synth)・WAVの読み取り(Wav_Decoder)と2コア目への列(Wav_Stream)・音名→周波数(Note_Name)・MMLの読み取り(Mml_Compiler)・2コア目のシーケンサー(Music_Player)と演奏データの取り決め(Music_Data)・ゲームボーイの音源チップ(Gb_Apu)とエミュからの時刻付きの列(Gb_Audio_Link)。下記「音声出力」「曲データ」「ゲームボーイの音」参照
  lua/                        Lua<->C++バインディング本体(LuaEngine。土台は LuaEngine.cpp、pico.* の本体は分野ごとの LuaEngine_{Widgets,System,Sound,Draw,Sd,Http,Require}.cpp、拡張は LuaEngine_Ext / _Net / _Crypto / _Iso(pico.iso) / _Mic(pico.mic_*)。画像の置き場は LuaImages。同梱モジュールのソースは builtin/*.lua(LuaBuiltinModules.hpp は生成物))。LuaAppScannerはSD走査によるアプリ自動登録。LuaDebugger/LuaDebugScreenはデバッガ
  net/                        HTTPレスポンスの解釈 / http・httpsの接続(Http_Transport + 焼き込みのルート証明書Tls_Roots_Data) / GET(Http_Get)・汎用の要求(Http_Request)・Wi-Fiのスキャン(NetworkScan) / 保存済みのWi-Fiネットワーク(Wifi_Profiles)
  util/                       Rect(矩形) / FixedString(固定長文字列) / Utf8Byte / Url / Base64 / Json_Reader・Xml_Reader(流しながら読む)。機能を持たない部品だけ
  storage/                    SDカードI/O・パス定数・フォルダの走査(Sd_Walker)・内蔵フラッシュの生の置き場(Flash_Area)・USBドライブのセクタの受け渡し(Msc_Sector_Bridge)
  task/                       非同期タスク基底(Task / Task_Functions) + StepBudget(実行時間の区切り)
script/                       開発補助スクリプト(Python。cli-tool/picoos が名前で呼び分ける)
  dict/                       辞書の材料(src/: SKK-JISYO・英和/和英の単語辞書・豆腐文字の一覧)と変換スクリプト。build.sh がSD用のファイルを dict/out/ へ作る(リポジトリには入れない。README.md)
  assets/                     Luaアプリの絵(.pimg)を作るスクリプト(generate_*.py)と元の絵(blocks_assets/)
  tabler_icons/               アイコン元データ(tabler由来のSVG)
  custom_icons/               アイコン元データ(自作SVG)。tablerが16pxで破綻する場合の受け皿
  check_layers.py             src/ の層の決まり(下の段から上の段を include しない)の確認と、段ごとのソースの一覧(上の「層と置き場所」)
  host_test/                  PCで実コードを動かす検証(run.sh=CMake+CTestでASan付きのテストを回す、説明はREADME.md、scene/label/markdown/config/app/path/cache/http/discovery/calc_eval/calculator/home/file_ops/dict/dict_scene/widget_factory/widget_property/step_budget/error_functions/lua_smoke/lua_stdlib/lua_alloc_budget/lua_engine/lua_ext/pico_mock/lua_sandbox/devtools/lua_scene/lua_app_scanner/ical/calendar_scene/chat_proto/chat_scene/todoist_proto/todo/gb_emu/gb_apu/sound/music/midi2mml/pad/tetris/reversi/minesweeper/breakout/blocks/iso_world/iso_td/vt_terminal/ssh_util/notification/wifi_profiles/key_input/romaji_kana/edit_history/app_repo等の80本 / run_net.sh=参照実装サーバ・テスト用TLSサーバ・チャットサーバ・Todoistの偽物・アプリストアの配布物・OpenSSHのsshd相手の結合テスト(net/calendar_sync/chat_net/todoist_net/app_store_net/ssh_net) / run_mem.sh=確保回数の計測)
  reference_server.py         docs/PROTOCOL.mdの参照実装サーバ(標準ライブラリのみ)。Markdownブラウザの開発相手
  ppm2png.py                  picoos_pcの--shotが書き出すPPMをPNGへ(標準ライブラリのみ)
  build_app_repo.py           アプリストアの配布物を作る(鍵の生成・署名・目録だけの署名(sign)とCIでの組み立て(assemble)・確認。標準ライブラリのみ。docs/APP_REPO.md)
  firmware_tool.py            ファームの更新ファイル(.pfw)を作る・確かめる・Wi-Fiで本体へ送る/鍵を作る(標準ライブラリのみ。docs/FIRMWARE_UPDATE.md)
  pio_firmware.py / pio_firmware_targets.py  PlatformIOの追加スクリプト(ファームの版の埋め込み / fwpack・fwpushのターゲット)
  midi2mml.py                 MIDI(SMF)をpico-os MMLへ変換(標準ライブラリのみ。docs/MUSIC_FORMAT.md「MIDIからの変換」)
  spotify_auth.py             SpotifyのWeb APIの最初のトークンをPCのブラウザで取り(PKCE、戻り先は http://127.0.0.1:8888/callback)、
                              SDの lua/apps/Spotify/spotify_auth.json へ書く(標準ライブラリのみ。`picoos spotify-auth`)。
                              本体ではOAuthを受けられない(デバイスフローが無く、戻り先はHTTPSかループバックだけ)ための道具。
                              ファイルは平文なので、Luaアプリは読んだら store_save{encrypt=true} へ移して消す決まり
  generate_pvid.py            動画を.pvid(16色+8x8タイルの差分)へ変換する(ffmpeg + numpy。docs/VIDEO_FORMAT.md)。再生は動画アプリ(VideoScene)
  pad_serial.py               PCのキーボードを外部コントローラーにする(USBシリアルへ送る。tkinter + pyserial)
lib/lua/                       vendorしたLua 5.4.7本体(lua.c/luac.cを除く)。詳細はlib/lua/README-pico-os.md
lib/monocypher/                vendorしたMonocypher 4.0.2(X25519/Ed25519/ChaCha20/Poly1305。SSHの暗号、無改造)。詳細はlib/monocypher/README-pico-os.md
lib/peanut_gb/                 vendorしたPeanut-GB(Game Boyエミュ、ヘッダ1本・無改造)。詳細はlib/peanut_gb/README-pico-os.md
pc/                            PC/Web実行用ビルド(CMake + SDL2 / Emscripten)。`src/`は実機と同一のまま使う
  compat/                     実機ライブラリの代替ヘッダ(Arduino/SPI/WiFi/SdFat/I2S/LGFX設定/タッチ)
  web/shell.html              Webビルドのページの外枠(canvas + ログ + デバッグ用ボタン)
  sdcard/                     SDカードとして読まれるディレクトリ
    gb/dmg-acid2.gb           ゲームボーイエミュの描画を確かめるテストROM(MIT。ライセンスはpc/sdcard/README.md)
    lua/apps/<名前>/main.lua  LuaAppScannerが走査して自動登録するLuaアプリ(サブディレクトリ1つ=アプリ1つ)
    lua/apps/動作確認/        Lua APIの動作確認をまとめたアプリ(メニューから部品/描画/画面とデータ/コントローラー/音/通知のページを開く。下記「動作確認アプリ」)
    lua/apps/ゾンビTD/        タワーディフェンス(下記「ゾンビTD」)
    lua/apps/Spotify/         Spotifyのリモコン(下記「Spotifyリモコン」)
    lua/apps/テトリス/        テトリス風ゲーム(下記「テトリス」)。リバーシ/マインスイーパー/ブロック崩しと合わせて4本とも pico.game で作ってある
    music/                    ミュージックアプリが並べる曲(chime.wav、サンプル/demo.mml・sample.mml。直下のフォルダ1つ=プレイリスト1つ。docs/MUSIC_FORMAT.md)
examples/doc.md                MarkdownView動作確認用サンプル文書
docs/                          仕様・手順の文書(PROTOCOL / CHAT_PROTOCOL / APP_REPO / MUSIC_FORMAT / VIDEO_FORMAT / FIRMWARE_UPDATE / ZOMBIE_TD / HARDWARE_CHECKLIST)
  design/                     機能ごとの設計記録(下の「設計記録の索引」)
server/chat/                   自前のチャットサーバ(chat_server.py、標準ライブラリのみ)+ Webクライアント + Raspberry Pi/Let's Encryptの設置手順(README.md)
```
`include/`, `test/` はPlatformIO標準雛形ディレクトリで未使用(README以外中身なし)。
`lib/`はLua本体のvendor先として使い始めた(上記参照。従来は未使用だった)。

## コアアーキテクチャ

### OSData (`src/OS_Data.hpp`)
グローバル状態ハブ。`inline`変数として: タッチ状態(touchX/Y/Z, isTouched, isTouchStart/End/Move)、グラフィック(`LGFX* lcd`, `LGFX_Sprite* frame`)、SD(`SdFat SD`, SD_usable)、キーボード3種へのポインタ(keyboard_jpn / keyboard_eng / keyboard_num)を保持。
**`lcd`と`SD`だけは`extern`で`src/OS_Data.cpp`に定義する**(2026-10-19)。作る処理の要る`inline`変数は、ヘッダを読む`.cpp`ごとに初期化のコードがフラッシュへ入る(1つあたり数百バイト)ため。
他のヘッダでも、`new`やコンストラクタの要る`inline`変数を増やすときは同じことに気をつけること(ファイルを分けるほど増える)。

### 層と置き場所 (`script/check_layers.py`) (2026-10-19)

`src/` は下の7段に分かれていて、**ファイルは自分と同じ段か下の段のヘッダだけを `#include` してよい**
(同じ段の中は互いに使ってよい)。`python3 script/check_layers.py` が確かめ、CI(`host-tests.yml` の「層の決まり」)でも回る。
段の割り当て(ディレクトリごと+1ファイルだけ別の段に置くもの)と、理由付きの例外もこのスクリプトの中にある。

| 段 | 置き場所 | 中身 |
|---|---|---|
| 0 base | `consts.hpp` `OS_Data.hpp` `config/` `util/` (+`storage/SD_Path.hpp` `storage/SD_IO.hpp` `gui/icons/icons_data.h` `lua/LuaPermissions.hpp`) | 型・定数・グローバル状態。OSの機能を何も知らない |
| 1 system | `system/` | UIもハードの具体も知らない土台(ログ・設定・時刻・入力の列・通知の中身・バックグラウンドの登録簿…) |
| 2 lib | `hal/` と、UIを持たない機能のライブラリ(`storage` `net` `crypto` `task` `sound` `iso` `ime`)と `apps/<機能>/` の直下 | ハードの窓口と機能の中身。同じ段なので互いに使い合ってよい |
| 3 ui | `gui/`(`core/` `icons/` `widgets/`)と `apps/*/ui/` | 描画・ウィジェット・シーン遷移の仕組みと、機能専用の部品 |
| 4 service | `services/` `devtools/` `gui/widgets/systems/` `apps/home/`(+`apps/todo/Todo_Watch`) | OSの機能と、それを見せるシェルの部品 |
| 5 app | `gui/scenes/` `lua/` `apps/store/` と `apps/*/*Scene.*`・`apps/*/*Scene_*.cpp` | 画面(アプリ)とLua |
| 6 boot | `boot/` `main.cpp` | 起動とつなぎ。全部を知ってよい |

- **下の段から上の段の機能を呼びたいとき**は、下の段に関数ポインタ(差し込み口)を置いて `boot/` で差すか
  (`KeyInputFunctions::intercept`・`UsbDriveFunctions::sd_remounted`・`LuaAppScanner::apps_changed`)、
  下の段のヘッダで宣言した関数を `boot/` で定義する(`AppFunctions::Setup()` → `boot/App_List.cpp`、
  `KeyInputFunctions::Update()` → `boot/KeyInput_Dispatch.cpp`、`NotificationFunctions::Setup()/Update()` → `boot/Notification_Sources.cpp`)。
  「中身」(`system/Notification_Functions.cpp`・`system/KeyInput_Functions.cpp`)と「つなぎ」(`boot/`)を分けてあるので、
  中身だけをリンクしてホストテストできる。
- `OS_Data.hpp` はキーボードを `Widget*` で持つだけなので、`Widget` は前方宣言にしてある(土台の段がUIの段へ依存しないように)。
  `Widget` の中身が要るファイルは自分で `gui/widgets/Widget.hpp` を読むこと。
- 例外は今1件(`devtools/DevTools_Functions.cpp` → `lua/LuaDebugger.hpp`。理由はスクリプトの中)。
  例外を足すときは理由を書く。違反でなくなった例外はスクリプトが「古い例外」として報告する。
- **機能の置き場は `apps/<機能>/`**(2026-10-19)。中身(通信・読み取り・状態)は直下、その機能だけが使うウィジェット・ダイアログは `ui/`、
  画面は直下の `XxxScene`。段はこの置き場所で決まる(`check_layers.py` の `APP_PATTERNS`)。ほかのアプリやLuaからも使う部品は
  `gui/widgets/`、機能の中身を持たない画面(設定・時計・ファイル等)は `gui/scenes/` に置く。新しいアプリで中身(.cpp)を書くなら `apps/` に作る。
- 段の割り当てを変えたら `script/host_test/CMakeLists.txt` が段ごとのライブラリを作り直す(同じスクリプトの `--sources` を読む)。

### `Xxx_Functions` サブシステム (`system/` `hal/` `gui/core/` `services/` `devtools/` `boot/`)

以前は全部 `src/functions/` にあった(2026-10-19に上の段ごとに分けた。名前空間・ファイル名は変えていない)。
`IME_Functions.hpp` は `ime/`、`Task_Functions.hpp` は `task/` へ移した。
各機能は名前空間+`inline`変数/関数のシングルトン的パターン(クラス化しない)。

| モジュール | 役割 |
|---|---|
| GFX_Functions | LovyanGFX初期化、ダーティリージョン管理(`dirtyRects`)、`FlushDirty()`で差分描画。`SetBrightness()`(バックライトのPWM調光。下記「画面の明るさ調整と自動調光」参照)も持つ |
| Display_Functions | 画面の明るさ(0〜100)の管理と、無操作が続いたときの自動調光。下記「画面の明るさ調整と自動調光」参照 |
| Widget_Functions | ウィジェット/ダイアログの登録・削除・毎フレーム更新・当たり判定の中枢 |
| Touch_Functions | XPT2046からのタッチ座標取得 |
| Task_Functions | `Task`のリスト管理・毎フレームupdate |
| Network_Functions | Wi-Fi非ブロッキング接続・スキャン(Task化)・NTP同期・電波強度アイコン |
| IME_Functions | SKK辞書ベース変換候補検索 |
| Keyboard_Functions | オンスクリーンキーボードの窓口。`Show(target, layout, docked)`でダイアログ表示/画面下への据え置き表示を開く。下記「オンスクリーンキーボード」参照 |
| Font_Functions | U8g2フォントサイズ切替(Small16px/Normal24px/Big32px/Bigger48px) |
| SD_Functions | SDカード初期化 |
| Scene_Functions | シーン(画面)の遷移管理。Change/Push/Popをフレーム境界まで保留して適用 |
| Config_Functions | `key=value`形式の設定ファイルパーサ + 書き込み(`SetValue()`は一時ファイル経由で1キーだけ差し替え) |
| Log_Functions | システムログ(LOG_SYS_OK/WARN/FAIL/MSG) |
| Time_Functions | 時刻管理(NTP同期後) |
| App_Functions | アプリ登録簿(`App_List.cpp`が一覧、`App_Functions.cpp`が仕組み) |
| Mem_Functions | ヒープ計測(`mallinfo`ベース)。シーンごとの使用量レポート |
| UTF8_Functions | UTF-8のエンコード/デコード(文字列操作は`FixedString`側の担当) |
| HitBox_Functions | 当たり判定のヘルパ |
| Test_Functions | フォントカバレッジ等の起動時セルフチェック |
| Sound_Functions | 音声出力(I2S)と音源の窓口。1コア目はアンプの抜き差しの検出と要求の受付、2コア目(`loop1()`)が音源を回してI2Sへ流す。下記「音声出力」参照 |
| Alarm_Functions | アラーム。時計アプリを閉じていても鳴るようOS側で見張る。下記「アラーム」参照 |
| Notification_Functions | 通知。予約(時間・時刻・電池・Wi-Fi)の見張り、トースト、通知センターの履歴。Luaの`pico.notify`。下記「通知」参照 |
| Power_Functions | スリープ(省電力)。自動調光のさらに先の段階。下記「スリープ(省電力)」参照 |
| Clipboard_Functions | OS全体で1つのクリップボード(テキスト1KiBまで、入れた大きさだけmalloc)。下記「テキストエディタ」参照 |
| KeyInput_Functions | 物理キーボードの窓口。1打鍵1件の列を持ち、今の画面(`Scene::onKey()`)→開いているキー盤の順に配る。今の入力元はUSBシリアル(PCのキーボード)。下記「物理キーボード」参照 |
| Pad_Functions | 外部コントローラーの窓口。押しているボタンのビットマスクを`loop()`の頭で1回だけ更新する。入力元はWiiクラシックコントローラー(`WiiClassic_Functions`)とUSBシリアル(PCのキーボード)のOR。下記「外部コントローラー」参照 |
| WiiClassic_Functions | Wiiクラシックコントローラー(I2C)のドライバ。読んだボタンを`PadFunctions::SetDevice()`へ渡す。I2Cのバスの準備は`I2C_Bus.hpp`(CardKB2と共有) |
| UsbDrive_Functions | USBドライブ(マスストレージ)。SDをPCへ貸し出す間は`SD_usable=false`、終わったらマウントし直す。下記「USBドライブ」参照 |
| Error_Functions | 「ユーザーへ見せるべき失敗」をログ+MsgDialogの両方へ出す共通口(`ShowFatal()`)。Lua着手前の受け皿の1つ |
| Profiler_Functions | `loop()`の区間ごとの時間とフレーム時間の集計。下記「開発者向けの道具」参照 |
| CrashDump_Functions | クラッシュダンプ(HardFault・ウォッチドッグ・Luaのエラー)とパンくず。下記「開発者向けの道具」参照 |
| DevTools_Functions | `/sys/debug.cfg`(プロファイラの表示/ログ・Luaデバッガ・ウォッチドッグ)の窓口 |
| PowerMenu_Functions | 電源メニュー(スリープ / 再起動 / ロック)。ステータスバーの長押しと設定アプリの「電源」。下記「時刻の手動設定・電源メニュー」参照 |
| Timer_Functions | タイマーとストップウォッチ(ラップ)。時計アプリを閉じても動く。下記「BMP・予定の通知・…・画面ロック」参照 |
| Lock_Functions | 画面ロック(PIN)。同上 |
| Firmware_Functions | ファームの更新(SDの`.pfw` / PCからWi-Fiで)のOSへのつなぎ。下記「ファームの更新」参照 |
| Mic_Functions | I2Sマイク(SPH0645)の入力とWAVへの録音。2コア目が受け取って列へ、1コア目がSDへ書く。下記「マイクと録音」 |
| Background_Functions | バックグラウンドの仕事(画面を閉じても動くもの)の登録簿と入り切り。登録は`Background_List.cpp`。下記「バックグラウンド実行の枠組み」参照 |

### 起動・ループ (`main.cpp`)
`setup()`: GFX→SD→Log→Display→Touch→Task→Network→Keyboard→IME→Time→Sound→Testの順にSetup()を呼び、Statusbar・FileExplorer・MarkdownView・各種ダイアログを生成して`WidgetFunctions`へ登録。

`loop()`: (`ProfilerFunctions::BeginFrame()`/`CrashDumpFunctions::Feed()`) → Touch更新 → Wiiクラシック(I2C)の読み出し → Pad更新(外部コントローラー。物理キーボードの打鍵もここで列へ積まれる) → Display更新(自動調光の判定) → Power更新(スリープの判定) → `SceneFunctions::Update()`(保留中のシーン遷移の適用) → `KeyInputFunctions::Update()`(打鍵を配る) → `WidgetFunctions::UpdateAll()` → `GFX::FlushDirty()` → Task/Log/Time/Network/Sound/Battery/Alarm/Notification/DevTools更新 → `PowerFunctions::IdleWait()`(スリープ中だけ少し休む)、という単純なポーリングループ。各処理の後の`ProfilerFunctions::Mark(区間)`はプロファイラとクラッシュダンプのパンくず(下記「開発者向けの道具」)。

**2コア目(`setup1()`/`loop1()`)は音声専用**(`SoundFunctions::LoopCore1()`だけを回す)。1コア目とは`std::atomic`とロック無しのコマンドの列だけでやり取りする。
**2コア目からログを出したり、ウィジェット/SD/`OSData`に触ったりしないこと**(どれもロックを持たない1コア目専用の作り)。

`main.cpp`が直接newするのは**常駐ウィジェット(Statusbar)と最初のシーンだけ**で、画面ごとのウィジェットは各`Scene`の`onEnter()`が生成する。

## Widgetシステム

### 基底クラス (`src/gui/widgets/Widget.hpp`)
- `l_rect`(ローカル矩形)/ `prev_l_rect` を保持。親子は生ポインタ(`Widget* parent`)+ 仮想関数 `getChildren()`。
- `visitAll(F&&)` で自分+全子孫を再帰走査。
- 描画モード `WidgetTools::RenderMode { OPAQUE, CLEAR, TRANSLUCENT }`。
- 座標系: `getLocalRect()` / `getScreenRect()` / `getScreenClipRect()`。
- タッチ: `hitTest(px,py)`、`causeOnPress{Start,Move,End,Out}` コールバック(`std::function<void()>`)。
- 再描画: `needsRender()` / `markdirty(Rect)`。
- `disable_markdirty`: 親が描画反映を一括保証する場合の子markdirty無効化フラグ(**乱用厳禁、バグりやすい**)。
- `hit_transparent`: **当たり判定を素通りさせるフラグ**。`WidgetFunctions::Add()`は`visitAll()`で**子孫も全て`widgets`へ積む**ため、子は親とは別の「根」として`HitTest()`の対象になる。つまり**親が自分でタップを処理したい場合、表示のために置いただけの子がタップを奪う**。`MarkdownView`のプール(Label/Image/Icon)がこれで、リンクのタップが一切反応しなかった。表示専用の子にはこれを立てる。

### ウィジェットの置き場所
**`widgets/`直下に置けるのは「どのアプリからでも使える汎用部品」だけ**。専用のものはサブフォルダへ入れる。

| 置き場所 | 何を入れるか | 中身 |
|---|---|---|
| `widgets/` | 汎用部品と基底 | `Widget` / `WidgetID` / `WidgetRegistry` + 下のカタログのうち専用でないもの(`TextView` / `ImageView`を含む) |
| `widgets/apps/` | 用途は決まっているが、複数のアプリ・Luaから使うウィジェット | `MarkdownView` / `FileExplorer` / `AnalogClock` / `DurationPicker` / `MonthGrid` / `SeekBar` / `MediaButtons` 等 |
| `src/apps/<機能>/ui/` | **1つのアプリ専用**のウィジェット・ダイアログ | `ChatLogView` / `GameBoyView` / `GameBoyPad` / `CalculatorKeypad` / `GraphView` / `TerminalView` / `SearchDialog` 等 |
| `widgets/systems/` | **OSのシェル部品**(特定アプリのものではない) | `Statusbar`(常駐オーバーレイ) / `PerfOverlay` / `NotificationToast` / `LockScreen`(`AppGrid`は`src/apps/home/`) |
| `widgets/dialogs/` | モーダルダイアログ(キーボードのダイアログ枠`KeyboardDialog`を含む) | 下記「ダイアログ」参照 |
| `widgets/keyboards/` | オンスクリーンキーボードのキー盤3種 + 基底`KeyboardPanel` | 下記「オンスクリーンキーボード」参照 |
| `widgets/interfaces/` | ミックスイン的インターフェース | `IBorderColor` / `IFontImplementation` / `ITextColor` / `ITextInputTarget` |

- **includeは常に`src/`起点の絶対パス**(`#include "gui/widgets/apps/MarkdownView.hpp"`)。
  相対includeは使っていないので、フォルダを移してもファイル自身の中身は書き換え不要。
- **PlatformIOもPCビルドも`src/**.cpp`を再帰的に拾う**ので、ファイルを移動してもビルド定義に触る必要はない。
  ホストテスト(`script/host_test/CMakeLists.txt`)も段ごとに `src/` を拾うので直さなくてよい。
  **`run_net.sh`/`run_mem.sh` だけはソースを1本ずつ並べている**ので、そこへ載っているファイルを移したら直すこと。
  移したら `python3 script/check_layers.py` で層の決まりを確かめる。
- `FileExplorer`が`apps/`なのは、`FileSaveDialog`/`FileSelectDialog`から使われていても
  **「SD上のファイルを見せる」という用途に特化した部品**だから。汎用部品の定義は「役割が特定の
  画面に紐付いていないこと」で、「複数箇所から使われていること」ではない。

### メモリ管理方針(重要・相談時の大前提)
- **基本は各ウィジェットが`new`で子生成、デストラクタで`delete`する素朴な方式。**
- **例外: `MarkdownView`だけは既にオブジェクトプール方式**: `labelPool`/`imagePool`/`checkboxIconPool`という固定長配列を起動時に一度だけ確保し、`boundXxxBlock[]`でスロット使用状況(-1=未使用)を管理、スクロールに応じて使い回す。→ 「事前確保→使い回し、全消去時は中身クリアのみ」構想の**実例プロトタイプ**。
- `WidgetFunctions::DestroyLater()` + `pending_deletes`: 非表示化→次フレーム末尾で`ProcessPendingDeletes()`によりまとめてdelete(フレーム途中delete事故防止)。SUMMARY.md「ウィジェットのメモリ解放」チェック済み項目に相当。
- **開発者はRAM断片化回避のため固定長バッファ/オブジェクトプールを志向している。新規実装で`new`/`delete`を安易に増やす提案より、MarkdownViewのプールパターンに寄せた提案を優先すること。**
- **`Widget::operator new/delete`が全ウィジェットの確保の唯一の入口**(`src/gui/widgets/Widget.cpp`)。現状は`malloc`を呼ぶだけで`MemFunctions`へ量を通知する。将来アリーナを入れる場合はここの実装を差し替えるだけで済み、`new Button(...)`のような既存コードは書き換え不要。
- **子ウィジェットの解放は親のデストラクタの責任**。`WidgetFunctions::ClearSceneWidgets()`は親を持たないルートしか`delete`しないので、子を`new`するウィジェットにデストラクタが無いと丸ごとリークする(過去に`MarkdownView`/`ScrollList`/`CanvasRaster`で発生)。
- ウィジェットIDベース管理(32bit: 種別enum/generation/index)は**発行側のみ実装済み**(`WidgetID.hpp`/`WidgetRegistry`、上記「ウィジェットID」参照)。OS内部のウィジェット間参照は従来どおり`std::vector<Widget*>`+生ポインタで、IDはあくまでLua等の外部向け。

### 文字列の扱い
**Arduino `String` は現在どこでも使っていない。** 文字列はすべて `src/util/FixedString.hpp` の
`FixedString<N>`(固定長・ヒープ非使用)に統一されている。**新規実装でも `String` を持ち込まないこと。**

- 実体は `char buf_[N]` のみ。`new`/`delete` を一切しないので断片化しない。
- `length()` はホットパス(1文字ずつのappendループ等)で毎回呼ばれるため、バイト長を `len_` に
  キャッシュしている。**`buf_` を変更するメソッドは必ず `len_` を追従更新すること**(不変条件)。
- `assign`/`append` は**切り詰めが起きたかを`bool`で返す**。戻り値を無視しないこと(黙って
  切れるとバグの温床になる)。
- UTF-8境界を跨いで文字が欠けないよう、バイト単位の操作はすべて継続バイト(`10xxxxxx`)を
  巻き戻すガードが入っている。`charCount()`/`byteOffsetOfChar()`/`removeCharAt()`/
  `insertAtChar()` など「文字単位」のAPIを持つ。
- UTF-8の**文字列操作はFixedStringの担当**、`system/UTF8_Functions.hpp` は
  **エンコード/デコードのみ**。`FixedString.hpp` から `UTF8_Functions.hpp` を
  includeしてはいけない(循環include回避)。最下層の `util/Utf8Byte.hpp` だけを使う。
- 長さは `consts.hpp` のプリセットから選ぶ:
  `PICO_STR_S=24` / `M=48` / `L=96` / `LL=192` / `256B` / `512B` / `1KiB` … `32KiB`、
  パスは `PICO_PATH_LEN=255`。
- 使用例: `Label::raw_text`(`FixedString<N>`、Nはテンプレート引数) /
  `MarkdownView::doc_text`(`FixedString<kMdMaxSourceBytes>`) /
  `FileExplorer::currentPath`(`FixedString<PICO_PATH_LEN>`) /
  `NetworkFunctions::currentSSID`(`FixedString<PICO_STR_M>`)。
- **まだ生`char[]`のまま残っているのは `IME_Functions::candidates[][IME_MAX_CAND_BYTES]` だけ**
  (2次元配列なので置き換えが機械的でない)。
- パスの組み立ては `storage/SD_IO.hpp`(全て`inline`、SdFatに依存しないので単体でテストできる):
  `join()` / `parent()` / `filename()` に加え、**`normalize()`(`.`と`..`を畳む)と
  `resolve()`(文書基準の相対リンクを絶対パスへ)** がある。`resolve()`はルートを超える`..`を
  捨てるので、リンク先がSDのルート外を指すことはない。挙動は `script/host_test/path_test.cpp` で固定。

## コーディング上の慣習

- コメント・ログメッセージは日本語、コードは標準的な英語命名。
- 機能単位は「`XxxFunctions`」名前空間+`inline`変数/関数(クラス化せずシングルトン的に扱う)。
- get/setアクセサ+`needsRender()`呼び出しの定型パターンが各ウィジェットで繰り返される。
- 定数は「クラス内`constexpr static int`」と「`consts.hpp`に`#define`集約」の二系統が混在。
- アイコンは`script/generate_icons.py`で`script/tabler_icons/`(tabler)と`script/custom_icons/`(自作)から
  `src/gui/icons/icons_data.h`を事前生成(ビルド前処理)。
  **tablerの絵柄は24pxグリッド前提なので16pxで破綻することがある**。細い要素が丸ごと消えるため、
  16pxで使うアイコンは生成後に必ず目視すること。破綻する場合は`custom_icons/`へ
  `viewBox="0 0 16 16"`・整数座標・`fill`の矩形で描き起こす(判断基準は`script/custom_icons/README.md`)。
  電波強度アイコンがこの理由で自作に差し替わっている(tablerの`wifi-0`は16pxで0ピクセルだった)。
- **「状態の否定」は専用アイコンを作らず、基底アイコンの上に`IconID::X`を`PICO_RED`で重ねて表す。**
  SD無しが`SdCard`+`X`、Wi-Fi圏外が`WifiSignal1`+`X`(`Statusbar::render()`)。
  そのため`GetWifiStateIconID()`は**圏外でも最弱の棒を返す**(判定は`NetworkFunctions::IsConnected()`)。
- 日本語IMEはSKK辞書方式、`script/dict/convert_skk_dict.py`で辞書データ(`skk_body.tsv`/`skk_index.tsv`)をSD収録用に変換。

## PC / Web実行環境 (`pc/`)

実機に書き込まずに、PC上のウィンドウでもブラウザでもpico-osを動かせる。詳細は `pc/README.md`。

```sh
# ネイティブ(SDL2のウィンドウ)
sudo apt-get install libsdl2-dev      # 前提: SDL2開発パッケージ
cmake -S pc -B pc/build && cmake --build pc/build -j
./pc/build/picoos_pc                  # マウス左ドラッグ = タッチ
SDL_VIDEODRIVER=offscreen ./pc/build/picoos_pc --shot shot.ppm 40   # ヘッドレス確認

# ヘッドレスではSDLへマウスが来ないので、撮りたい画面まで --tap で操作を進める
#   --tap X,Y@FRAME[:HOLD]   FRAMEフレーム目に(X,Y)をHOLDフレーム押す(既定3、最大16件)
SDL_VIDEODRIVER=offscreen ./pc/build/picoos_pc \
    --tap 61,65@30:5 --shot md.ppm 250        # ランチャの1枚目のアプリを開いて撮る

python3 script/ppm2png.py md.ppm md.png 2     # PPMは見づらいのでPNGへ(2倍)

# Web(WebAssembly)。前提: emsdk(SDL2はemscriptenのportsが持つので不要)
emcmake cmake -S pc -B pc/build-web -DCMAKE_BUILD_TYPE=Release
cmake --build pc/build-web -j
emrun --no_browser --port 8080 pc/build-web    # → http://localhost:8080/index.html
```

**画面の確認はこの2つで完結する。** `--tap`はヘッドレスでの動作確認のために用意した
もので、実際にこれで「リンクをタップしても反応しない」「短い文書を開き直すと前の内容が
下部に残る」という2つのバグが見つかっている(いずれもホストテストでは出ない類のもの)。

- **`src/` のコードは実機とまったく同じものを使う**。差し替えているのは実機ライブラリだけで、
  `pc/compat/` をインクルードパスの先頭に置いて `Arduino.h`/`SPI.h`/`WiFi.h`/`SdFat.h`/
  `XPT2046_Touchscreen.h`/`I2S.h` を置き換える(`script/host_test/stubs` と同じ考え方)。
- **例外は2ファイルだけ**: `src/config/LGFX_Config.hpp` と `src/hal/Touch_Functions.hpp` が
  `#if defined(PICOOS_PC)` で `pc/compat/` 側(`<config/LGFX_Config_PC.hpp>` /
  `<hal/Touch_Functions_PC.hpp>`)を取り込む。
  **山かっこで書くこと** — `"config/..."` だとインクルード元(`src/`)のディレクトリが優先され、
  自分自身を読んで多重定義になる。
- **ネイティブ関数(`millis`等)はPCビルドのときだけ上書きされる**。`pc/compat/Arduino.h` が
  実機の`Arduino.h`の代わりに拾われるだけで、**呼び出し側(`src/`)は一切書き換えていない**。
  `src/`が実際に使っているのは以下だけ(`micros`/`delay`/`analogWrite`/`random`は未使用):

  | API | `src/`での使用箇所 | PCでの実装 |
  |---|---|---|
  | `millis()` | 26 | `steady_clock`の経過ms(起動時刻を原点にする) |
  | `constrain()` | 5 | テンプレート関数 |
  | `Serial.*` | 7 | 書き込みは標準出力へ、`available()`/`read()`は標準入力から(外部コントローラーの代用入力) |
  | `pinMode()` | 6 | 空実装 |
  | `map()` | 2 | そのまま計算 |
  | `digitalWrite()`/`digitalRead()` | 4 / 2 | 空実装 / 既定は`HIGH`(`PicoPcGpio::read_hook`で差し込める。音声のアンプ検出が使う) |

  **`min`/`max`/`constrain`はマクロではなくテンプレート関数にしてある。** 実機のArduinoは
  マクロだが、マクロのままだと`std::min`や標準ライブラリ内部の`min`を食い荒らして
  LovyanGFXと標準ライブラリのビルドが壊れる。
- 画面は `lgfx::Panel_sdl`。既定で2倍表示(`PICOOS_PC_SCALE`)。マウス座標はSDL側でパネル座標へ
  戻されるため、拡大率はタッチに影響しない。
- SDカードは `pc/sdcard/` を実ファイルシステムとして読む(`PICOOS_SD_ROOT` 環境変数で差し替え可)。
  IME辞書は大きいのでリポジトリには含めていない(無くても起動する)。
- **Wi-Fiは母艦の疎通を見て接続/切断を返す**(既定`pc-wifi-state=auto`)。UDPソケットを
  `connect()`して経路の有無を見るだけで、パケットは飛ばさない。
  **母艦のWi-Fi設定は変更しない** — `ConnectWiFiAsync()`が来ても実際にSSIDへは繋ぎに行かない。
  `pc/sdcard/sys/network.cfg` の`pc-`始まりのキー(`pc-wifi-state`/`pc-wifi-rssi`/
  `pc-wifi-ssid`/`pc-wifi-scan`)か、同名の環境変数(`PICOOS_WIFI_STATE`等、環境変数が優先)で
  「切断」「電波1本」「SSID未検出」などを狙って再現できる。UIの状態確認にはこちらが早い。
- **時刻はPCのOSの時計がそのまま出るのでNTPは要らない**。`TimeFunctions`が読む`time(nullptr)`が
  最初から実時刻を返すため。表示タイムゾーンは`network.cfg`の`timezone`(例`JST-9`)で決まる。
  ※`TimeFunctions::Update()`は333msごとにしか更新しないので、`--shot`のフレーム数が少ないと
  初期値の`00:00`が写る。時刻を確認したいときは200フレーム以上回すこと。
- **液晶への書き込みは、実機のSPI転送にかかる理論上の時間だけ待つ**(`pc/compat/config/Panel_sdl_SpiWait.hpp`)。
  SDLパネルへの書き込みはメモリのコピーで一瞬なので、放っておくと「液晶へ送る量が多すぎて重い」問題が
  PCでは見えない(スクラッチパッドでキャンバス全体を毎回送っていた件がまさにそれで、実機でしか気づけなかった)。
  SPIのクロックはLovyanGFX(rp2040)の`FreqToClockDiv()`と同じ計算で求める: CPSR=2固定・clk_peri=150MHzなので、
  `TFT_MAX_SPEED`=80MHzを要求しても**実際は75MHz**。1ピクセル16bit+範囲指定88bitで、**画面1枚16.4ms**。
  書き込みの呼び出し元をその時間だけ止める(残り200µsまではsleep、最後は空回りで合わせる)。
  **4bpp→RGB565変換のCPU時間は含まない**ので実機より少し速い(=下限)。`PICOOS_SPI_WAIT=off`で無効、
  Webは既定で無効(メインスレッドを止めるため。`?spi_wait=on`で有効)。
- GPIO/SPIは空実装。
- **LovyanGFXの版は`platformio.ini`の`lib_deps`から読む**(`pc/CMakeLists.txt`が正規表現で拾う)ので、
  上げるときに直すのは`platformio.ini`の1行だけ。以前はCMake側にも`GIT_TAG`を直書きしていて
  追随が人間の記憶頼みだった。`CMAKE_CONFIGURE_DEPENDS`に入れてあるので、`platformio.ini`を
  書き換えれば次のビルドでconfigureが走り直す。
  `-DLOVYANGFX_DIR=...`(手元のソース) / `-DLOVYANGFX_TAG=...`(タグだけ差し替え)で上書きもできる。
- `pc/build/` は `.gitignore` 済み。
- **漏れはビルドで検出できる**。`src/*.cpp` を全部リンクするので、代替を用意し忘れた実機APIが
  あれば未定義参照になる。逆に言えば、`src/`へ新しい実機依存(`analogRead`/I2C等)を足すと
  PCビルドが即座に壊れて気づける。
- **ネットワーク越しの確認もPCで完結する**。母艦で`python3 script/reference_server.py`を立て、
  `pc/sdcard/sys/network.cfg`の`browser-home`へそのURLを書けば、Markdownブラウザが実際に
  取りに行く。`PICOOS_SD_ROOT`でSDのルートを一時ディレクトリへ向ければ、
  リポジトリの`pc/sdcard/`を汚さずに試せる。
- **Webビルド(WebAssembly)の作り・canvasの大きさの罠・公開の手順は `docs/design/pc-web.md`**。
  ブラウザではスレッド・生ソケット・ブロッキング待ちが使えない(`src/`へ依存を足すときはWebビルドも通すこと)。

## 設計記録の索引 (`docs/design/`)

機能ごとの設計の背景・判断の理由・不具合の調査・検証の手順は `docs/design/` に分けてある(2026-10-20に、このファイルから移した)。
**この節と上の地図で足りない話は、ここから該当のファイルを開いて読むこと。** コードのコメントや他の文書にある
「CLAUDE.md「節の名前」」「下記「節の名前」」という参照は、下の表で節の名前から探せる(節の名前は移したときのまま)。

| ファイル | 中身 | 節 |
|---|---|---|
| [`gui.md`](docs/design/gui.md) | GUIの仕組み(ウィジェット・アプリ・シーン・ダイアログ・キーボード) | 「ウィジェットカタログ」 / 「ウィジェットID」 / 「アプリの枠組み」 / 「シーン」 / 「ダイアログ」 / 「オンスクリーンキーボード」 |
| [`input.md`](docs/design/input.md) | 入力(コントローラー・物理キーボード・フォーカス・CardKB2)とUSBドライブ | 「外部コントローラー」 / 「物理キーボード」 / 「ウィジェットのフォーカス」 / 「物理キーボードのかな漢字変換」 / 「CardKB2」 / 「USBドライブ(USBマスストレージ)」 |
| [`power-display.md`](docs/design/power-display.md) | 画面の明るさ・スリープ・時刻と電源メニュー | 「画面の明るさ調整と自動調光」 / 「スリープ(省電力)」 / 「時刻の手動設定・電源メニュー」 |
| [`os-services.md`](docs/design/os-services.md) | OSのサービス(アラーム・通知・ホーム画面・バックグラウンド・スクリーンショット他) | 「アラーム」 / 「通知」 / 「ホーム画面のウィジェット」 / 「バックグラウンド実行の枠組み」 / 「スクリーンショット」 / 「BMP・予定の通知・ファイル検索/容量・タイマー常駐・ダークテーマ・画面ロック」 |
| [`battery-touch.md`](docs/design/battery-touch.md) | 電池とタッチパネル | 「バッテリー残量表示」 / 「バッテリー駆動中の音割れ対策」 / 「タッチ位置の補正」 / 「タッチ座標のノイズ抑制」 |
| [`security-wifi.md`](docs/design/security-wifi.md) | 暗号化・端末の鍵・Wi-Fi | 「Wi-Fi認証情報の暗号化保存」 / 「端末ごとの鍵(OTP)」 / 「保存済みのWi-Fiネットワーク・Wi-FiのON/OFF・自動再接続」 / 「Wi-Fiの新規接続(周辺スキャン→選択→パスワード入力→接続)」 |
| [`browser-net.md`](docs/design/browser-net.md) | Markdownブラウザと通信(HTTP/HTTPS) | 「MarkdownView 実装詳細」 / 「文書キャッシュ」 / 「HTTPクライアント」 / 「HTTPS」 / 「取得〜表示の配線」 / 「サーバ情報」 / 「検索」 / 「マニフェスト」 / 「ブラウザのヘッダー」 |
| [`apps-online.md`](docs/design/apps-online.md) | 通信するアプリ(カレンダー・チャット・TODO・Spotify・ニュース) | 「iCalendarの読み取り」 / 「CalendarScene 実装詳細」 / 「チャット」 / 「TODOアプリ(Todoist連携)」 / 「Spotifyリモコン」 / 「XMLの読み取りとニュースアプリ」 |
| [`apps-local.md`](docs/design/apps-local.md) | ローカルのアプリ(テキストエディタ・SSH・ファイルビューワー・電卓・時計) | 「テキストエディタ」 / 「SSHクライアント」 / 「ファイルビューワー」 / 「電卓(関数電卓・グラフ電卓)」 / 「ClocksScene 実装詳細」 |
| [`sound.md`](docs/design/sound.md) | 音(出力・マイク・曲・WAV・ミュージック・動画) | 「音声出力」 / 「マイクと録音」 / 「ステレオ出力とパン」 / 「曲データ」 / 「チップチューン音源の任意チャンネル数対応」 / 「ゲームボーイの音」 / 「WAVの再生」 / 「出力の周波数を選ぶ(22050 / 44100Hz、`SoundFunctions::SetSampleRate()` / PCM5102A)」 / 「ミュージックアプリ(プレイリスト・リピート・ミックス)」 / 「動画(.pvid)」 |
| [`gameboy.md`](docs/design/gameboy.md) | ゲームボーイ | 「ゲームボーイ」 |
| [`lua.md`](docs/design/lua.md) | Luaバインディングの土台(LuaEngine・LuaScene・安全網・自動登録) | 「Luaバインディング」 / 「ライフサイクルとメモリ」 / 「`pico.*` API(Lua側から見える面)」 / 「権限(LuaPermissions」 / 「コールバック中継の設計(ヒープを使わない理由)」 / 「実装中に見つけて直した既存のバグ2件」 / 「`LuaScene`」 / 「Luaは32bit」 / 「Luaの確保がヒープの残りを守る」 / 「pico.* の関数の置き方(Luaのメモリを減らす」 / 「実行時間の安全網(暴走防止」 / 「SDを走査したLuaアプリの自動登録(LuaAppScanner」 / 「現時点のスコープ外(次回以降)」 / 「Lua着手前の受け皿の状態」 |
| [`lua-api.md`](docs/design/lua-api.md) | Lua API の設計記録(pico.*) | 「Love2D比較で足した拡張API」 / 「直接描画」 / 「直接描画エリア」 / 「SDカードアクセス」 / 「設定ファイルとapp.cfgの書き込み制限」 / 「画像」 / 「ラスタキャンバスの保存/読み込み(CanvasRasterのリサイズ・`pico.canvas_clear/save/load`」 / 「実機未検証だったCanvasRasterの潜在クラッシュ」 / 「CanvasRasterのドラッグ描画が重い(線がカクカク・短い線が引けない」 / 「Buttonのアイコン化」 / 「シーン制御」 / 「ダイアログ」 / 「ネットワーク」 / 「ウィジェット固有イベント」 / 「コンテナからの取り外し / リストへの項目追加」 / 「時刻取得」 / 「タップ位置の取得(pico.get_touch」 / 「Lua APIの追加」 / 「Lua APIの追加」 |
| [`lua-apps.md`](docs/design/lua-apps.md) | Luaアプリと pico.game(ゲーム・ストア・動作確認) | 「ペイント」 / 「テトリス」 / 「ブロック崩し / マインスイーパーの効果音・コントローラー」 / 「2Dゲームの簡易エンジン」 / 「ボードゲーム4本の`pico.game`への移行(リバーシ・マインスイーパー・テトリス・ブロック崩し」 / 「アプリストア」 / 「動作確認アプリ」 |
| [`iso.md`](docs/design/iso.md) | 2.5Dエンジン(pico.iso)・ブロック・ゾンビTD | 「ブロック(2.5Dマインクラフト風、`pc/sdcard/lua/apps/ブロック/` + C++のエンジン `src/iso/`」 |
| [`devtools.md`](docs/design/devtools.md) | 開発者向けの道具・実機の確認・ファームの更新 | 「開発者向けの道具 (プロファイラ / Luaデバッガ / クラッシュダンプ / Luaサンドボックス)」 / 「実機の確認用の道具」 / 「ファームの更新」 |
| [`performance.md`](docs/design/performance.md) | 速さとメモリ(最適化の記録とメモリ計測) | 「OSのCPU・RAMの無駄を削る」 / 「液晶への転送を減らす」 / 「dirty矩形を固定長配列化」 / 「文書キャッシュ目録(index.tsv)の書き込みをO(n)からほぼO(1)へ」 / 「Luaコールバックの探索を線形からid昇順+二分探索へ」 / 「メモリ計測の結論」 / 「✅ 解決済み: PCビルドでシーン遷移を繰り返すと「ヒープ下限」が際限なく増える」 / 「✅ 解決済み: 同じ`pc/build`を2回目以降configureすると必ず失敗する」 |
| [`pc-web.md`](docs/design/pc-web.md) | PC/Webビルドの詳細 | 「Webビルド (WebAssembly)」 |
| [`roadmap-ideas.md`](docs/design/roadmap-ideas.md) | ロードマップ(2026-09-21時点)と未実装の設計アイデア | 「未実装の設計アイデア(旧pico-osからの持ち越し議論)」 / 「ロードマップ・TODO状況」 |

仕様・手順の文書(`docs/`): `PROTOCOL.md`(文書サーバ)/ `CHAT_PROTOCOL.md`(チャット)/ `APP_REPO.md`(アプリストアの配布物)/
`MUSIC_FORMAT.md`(MML)/ `VIDEO_FORMAT.md`(.pvid)/ `FIRMWARE_UPDATE.md`(ファームの更新)/ `ZOMBIE_TD.md`(ゾンビTDの仕様)/
`HARDWARE_CHECKLIST.md`(実機で確かめる項目の手順と結果)。Lua APIの利用者向けの説明は `lua-api-doc/`。

## Claude Codeへの申し送り

### 人にしかできない確認はTodoistで管理する(2026-10-19に利用者と決めた運用)

実機・実物のハードを人が操作しないと確かめられない作業が生じたら、**確認を取らずに**TodoistのMCPコネクタ(`mcp__Todoist__*`)で登録する。
クラウドセッションでもローカルでも同じ。**未完了かどうかの唯一の情報源はTodoist**で、`docs/HARDWARE_CHECKLIST.md`は手順・見方・結果の記録に使う
(進捗の印は付けない)。コネクタが無い環境では登録できないので、その旨を最後に報告し、`docs/HARDWARE_CHECKLIST.md`へ手順を足しておく
(後でTodoistに入れられるように、報告に項目名を挙げる)。

- **対象**: 実機・実物の周辺機器・電流計/テスター等が要る確認(実機ビルド、I2S/PWM/OTP/USB、Wii・CardKB2・マイクの実物、実機の速さ・メモリ・消費電流、
  本物のサーバ/証明書への接続)。ホストテスト・PCビルドの`--tap`/`--shot`で自分が確かめられるものは**入れない**。
- **いつ**: 実機で確かめる必要がある機能を実装したとき(PRを作る前後)。
- **設計記録の書き方**: `docs/design/`の各節には従来どおり「実機では未確認(確かめる中身)」と書いてよい(設計判断の背景として残す)。
  新しく書くときは中身を短くし、末尾に「(Todoist参照)」を添える。確認項目の長い列挙はTodoistとチェックリストにだけ置く。
- **置き先**: プロジェクト「pico-os 実機確認」(ID `6hp9GRmfPXH65cqM`。見つからなければ`find-projects`で名前検索し、無ければ作る)。
- **重複防止**: 入れる前に`find-tasks`(`projectId`+`searchText`)で同じ項目が無いか調べる。関連する既存タスクがあれば新規にせず`update-tasks`で説明に追記する。
- **形**: 題名は「何を確認するか」を短く(例:`マイク(SPH0645)録音確認`)。説明に ①確認手順と期待結果 ②出典(`docs/HARDWARE_CHECKLIST.md N章`や`docs/design/`の節)
  ③関連PRのURL(あれば) ④**クラウドセッションなら、そのセッションのURL**(`セッション: https://claude.ai/code/<session_id>`。session_idは`get_session`(引数なし)の
  `session_id`、またはコミット/PRの帰属表示のURLから取る。ローカルのセッションでは行ごと省く)。
  ラベルは分野(ビルド/クラッシュダンプ/画面・電源/入力/ネットワーク/音/計測/見た目・その他)。優先度は動かない可能性がある・先に通す必要があるものだけp2、期限は付けない。
- **完了後**: 利用者が実機で確認できたと言ったときに、①Todoistのタスクを完了にする ②`docs/HARDWARE_CHECKLIST.md`の`→`欄へ日付つきで結果を書く
  ③`docs/design/`の該当の「実機では未確認」を「実機で確認済み(日付)」に直す。自分で確認済みにしない。
- 既存の「実機では未確認」(2026-10-19時点)はすべてTodoistへ登録済み(`docs/HARDWARE_CHECKLIST.md`が元データ。結果が出ている行は除いた)。

- 組み込み制約(RAM/Flash)を常に意識し、PC向けC++の常識をそのまま持ち込まない。
- 固定長バッファ/オブジェクトプール志向を優先し、安易な`new`/`delete`追加は避ける(MarkdownViewパターンを参照)。
- ダイアログ系(ファイル選択/保存/色選択)は実装済みなので車輪の再発明をせず、既存クラス(`FileSaveDialog`/`FileSelectDialog`/`FileExplorer`)を拡張する形で提案する。
- 物理キーボードは窓口(`KeyInputFunctions`)とUSBシリアルの代用入力まで入っている。「キーを自分で扱いたい画面」は`Scene::onKey()`を実装する(「物理キーボード」参照)。
- 外部コントローラーは窓口(`PadFunctions`)とUSBシリアルの代用入力まで入っている。実物(Wiiクラシック)のドライバは`Source`を1つ足す形で書く(「外部コントローラー」参照)。
- 新しい画面を追加する話は`Scene`を継承して`onEnter()`でウィジェットを生成する形に寄せる。常駐させたいウィジェットは`AddOverlay()`。
- 新規ダイアログ/ウィジェットは既存の骨格(`children_`保持、`setOnClose`コールバック、`setVisible(false)`終了)にトーンを合わせる。
- コメント・ログは日本語、識別子は英語という言語使い分けを踏襲する。
- 文字列は`FixedString<N>`を使う。**Arduino `String`は現在どこでも使っていないので復活させないこと。**
- GUIの挙動を確かめたいときは実機ビルドの前にPCビルド(`pc/`)で回すのが速い。`src/`へ実機ライブラリ依存を
  足すときは `pc/compat/` 側にも代替を用意すること(PC/Webビルドが壊れる)。ブラウザで動かす場合は
  スレッド・生ソケット・ブロッキング待ちが使えない点にも注意。
- 格子状・多ボタンのUIは、部品ごとに`Button`を`new`せず「`render()`で直接描いてタップ位置から逆算する」型へ寄せる
  (`AppGrid`/`ColorDialog`/`KeyboardNum`/`TabBar`/`DurationPicker`)。
- **新しいウィジェットの置き場所は上記「ウィジェットの置き場所」に従う**。`widgets/`直下は汎用部品専用で、
  1つのアプリのために作ったものは`src/apps/<機能>/ui/`へ。**迷ったらそのアプリの`ui/`へ置く**
  (後で他のアプリでも使うと分かったら`gui/widgets/apps/`か直下へ上げる。直下に溜まると分類し直す手間が大きい)。
- 判断に迷ったら `SUMMARY.md`(https://raw.githubusercontent.com/Kimu1109/pico-os/refs/heads/main/SUMMARY.md)と実コードを突き合わせて確認する。
- **新しい機能の設計記録は`docs/design/`の該当するファイルへ書く**(合うものが無ければファイルを足して「設計記録の索引」へ1行)。
  このファイルには地図(置き場所・仕組みの要点・決まり)だけを足す。長い経緯・不具合の調査・検証の手順をここへ書き足さないこと。
- **`SUMMARY.md`を書き換えるときは体裁を崩さないこと**。「TODO」は**項目名だけ**の一覧に保ち、
  説明を足したくなったら下の「詳細」側へ書く(TODO欄に長文をぶら下げると一覧として読めなくなるため、
  この形へ整理した)。**新しい大項目を足したら冒頭の「全体の進捗」表にも1行足す。**
- **ホストテストはPR時にCIで回る**(`.github/workflows/host-tests.yml`。`run.sh`のグループごとに別ジョブ。matrixの一覧が`run.sh --list`と
  ずれると`groups-in-sync`ジョブが落ちるので、グループを足したらworkflowのmatrixにも足すこと)。`run_net.sh`/`run_mem.sh`/PCビルドの確認は手動のまま。
  実機ファームのビルド(`pio run -e rpipico2w`)もPR時にCIで回る(`.github/workflows/firmware-build.yml`。`platform = raspberrypi`はCI内でだけ maxgerhardt の版へ差し替える)。
  層の決まり(`python3 script/check_layers.py`)もPR時にCIで回る。
  CIの他はWebビルドの公開(`.github/workflows/web-pages.yml`)。`sh script/host_test/run.sh`(ASan、80本。グループ名を渡すとそのグループだけ作って回す: `run.sh core lua-engine`、一覧は`--list`、`-j N`。中身は CMake + CTest で、別のグループとは並列に回る。`run_parallel.sh` は今は run.sh へ渡すだけ)/
  `sh script/host_test/run_net.sh`(実通信)/ `sh script/host_test/run_mem.sh`(確保回数)/ PCビルドは
  変更のたびに自分で回すこと。
  **`script/host_test/stubs/SdFat.h`は常に`<fcntl.h>`の`O_CREAT`等を使う(2026-09-23)**。以前は「先に取り込まれていれば
  そちら、無ければ自前の値」で、翻訳単位ごとに値が変わり、inlineの`open()`がどちらの値でリンクされるか次第で
  「書き込み用に開けない」ことがあった(TLSのヘッダ経由で`<fcntl.h>`を取り込むファイルが増えて`run_net.sh`で踏んだ)。
  スタブで`#ifndef`して定数を足すときは同じ罠に注意すること。
  **ホストテストのビルド(2026-10-19に CMake へ移した)**: `script/host_test/CMakeLists.txt` が `src/` を段ごとの静的ライブラリにし、
  テストは全部を `LINK_GROUP:RESCAN` でリンクする(リンカが要るオブジェクトだけ拾うので、テストごとのリンクの一覧は無い)。
  テストを足すときは「テストの一覧」に `host_test(<名前> GROUP <グループ>)` を1行。スタブで作れない `src/` のファイルは
  `HOST_EXCLUDE` に理由付きで。テストの説明は `script/host_test/README.md`。ビルドは `script/host_test/build/` に残る(.gitignore済み)。
  **`run.sh`はビルド全体に`timeout`(1800秒)、テストは1本60秒(CTestの`TIMEOUT`)を掛けてある(2026-09-21追加)**。
  「まれにrun.shが終わらない」という報告を受けて入れた安全網(`timeout -k 10`でSIGTERM無視にも備え、猶予後SIGKILLする)。**`if ! cmd; then rc=$?`や
  `if cmd; then ... fi; rc=$?`ではPOSIX上`$?`にcmdの本当の終了コードが乗らない
  (前者は`!`による論理反転、後者は「条件が偽で分岐未実行のifは exit status 0」という
  規定のため)ので、`rc=0; cmd || rc=$?`の形を使うこと**(`run.sh`の
  `run_step`参照。一度この罠を踏んで学んだ教訓なので、
  同種のラッパーを足す際は再現しないこと)。
