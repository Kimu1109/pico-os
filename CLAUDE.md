# CLAUDE.md — pico-os プロジェクトコンテキスト

> このファイルは `Kimu1109/pico-os` リポジトリ直下に置く、Claude Code向けのプロジェクト背景資料。
> 元はClaude.aiのProject knowledgeとして管理されていた内容(2026-09-06時点情報)を統合したもの。
> **最終同期: 2026-09-25(実コードと突き合わせ済み)。**
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
| 外部コントローラー(予定) | Wiiクラシックコントローラー(I2C0: SDA=GP0, SCL=GP1、3.3V)。**まだ配線もコードも無い**。今はUSBシリアル経由のPCのキーボードで代用(下記「外部コントローラー」) |
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
monitor_speed = 115200
```
モニタはUTF-8。

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
  functions/                 「Xxx_Functions」名前空間群
  gui/
    icons/                  アイコンデータ(tabler_iconsから生成)
    scenes/                 Scene基底と各画面(HomeScene/MarkdownScene/ClocksScene/InputTestScene/LuaScene/CalendarScene/GameBoyScene等)
    widgets/                汎用ウィジェット + 基底 (Widget / WidgetID / WidgetRegistry)
      apps/                 特定のアプリ専用のウィジェット(MarkdownView/FileExplorer/AnalogClock/DurationPicker/MonthGrid/ChatLogView/GameBoyView/GameBoyPad/TerminalView/TermKeyBar等)
      dialogs/              モーダルダイアログ(オンスクリーンキーボードのダイアログ枠 KeyboardDialog を含む)
      keyboards/            オンスクリーンキーボードのキー盤3種(Keyboard/KeyboardEng/KeyboardNum)と基底KeyboardPanel
      interfaces/            ミックスイン的インターフェース
      systems/               OSのシェル部品(Statusbar / AppGrid / PerfOverlay)
  ime/                       SKK方式かな漢字変換辞書エンジン
  calendar/                  iCalendar(.ics)の読み取りと繰り返しの引き当て(Ical) / 取得元URLからの取得(Calendar_Sync)
  chat/                      チャットサーバの応答の読み取り(Chat_Proto) / 通信係(Chat_Client)。下記「チャット」参照
  todo/                      Todoist API v1 の応答の読み取りと日付(Todoist_Proto) / 通信係(Todoist_Client) / リマインダー(Todo_Reminders)。下記「TODOアプリ」参照
  ssh/                       SSHクライアント(Ssh_Client)・端末エミュレータ(Vt_Terminal)・SHA-256(Ssh_Sha256)・鍵/known_hosts(Ssh_Util)。下記「SSHクライアント」参照
  iso/                       2.5D(斜め上から見た)ボクセルの箱庭のエンジン(Iso_World: チャンク・生成・保存・描画・影・引き当て・人や物(エンティティ) / Iso_Blit: 面の絵の写し方 / Iso_Path: 条件つきの経路探索 / Iso_Flow: 流れの場(フローフィールド))。Luaの`pico.iso`。下記「ブロック」「2.5Dエンジンのタワーディフェンス向けの道具」参照
  gb/                        Game Boyエミュ本体(Gb_Emu。lib/peanut_gbを包む)と外部コントローラーのボタンの対応(Gb_PadMap)。下記「ゲームボーイ」参照
  sound/                     チップチューン音源(Chip_Synth)・WAVの読み取り(Wav_Decoder)と2コア目への列(Wav_Stream)・音名→周波数(Note_Name)・MMLの読み取り(Mml_Compiler)・2コア目のシーケンサー(Music_Player)と演奏データの取り決め(Music_Data)・ゲームボーイの音源チップ(Gb_Apu)とエミュからの時刻付きの列(Gb_Audio_Link)。下記「音声出力」「曲データ」「ゲームボーイの音」参照
  lua/                        Lua<->C++バインディング本体(LuaEngine。拡張は LuaEngine_Ext.cpp / LuaEngine_Crypto.cpp / LuaEngine_Iso.cpp(pico.iso)、同梱モジュールは LuaBuiltinModules.hpp)。LuaAppScannerはSD走査によるアプリ自動登録。LuaDebugger/LuaDebugScreenはデバッガ
  net/                        HTTPレスポンスの解釈 / http・httpsの接続(Http_Transport + 焼き込みのルート証明書Tls_Roots_Data) / 取得〜キャッシュの配線(Doc_Fetch) / サーバ情報(Discovery) / 検索(Doc_Search) / マニフェスト(Manifest) / 保存済みのWi-Fiネットワーク(Wifi_Profiles)
  util/                       Rect(矩形) / FixedString(固定長文字列) / Utf8Byte / Url / Md_Scan(画像参照の走査) / Json_Reader(流しながら読むJSON)
  storage/                    SDカードI/O・パス定数・文書キャッシュ(Doc_Cache)
  task/                       非同期タスク基底 + NetworkScan / HttpGet タスク + StepBudget(実行時間の区切り)
  test/                       フォントカバレッジチェック等
script/                       開発補助スクリプト(アイコン生成/SKK辞書変換/pimg生成等, Python)
  tabler_icons/               アイコン元データ(tabler由来のSVG)
  custom_icons/               アイコン元データ(自作SVG)。tablerが16pxで破綻する場合の受け皿
  host_test/                  PCで実コードを動かす検証(run.sh=ASanで解放漏れ検出、scene/label/markdown/config/app/path/cache/http/discovery/calc_eval/calculator/dict/dict_scene/widget_factory/widget_property/step_budget/error_functions/lua_smoke/lua_stdlib/lua_alloc_budget/lua_engine/lua_ext/pico_mock/lua_sandbox/devtools/lua_scene/lua_app_scanner/ical/calendar_scene/chat_proto/chat_scene/todoist_proto/todo/gb_emu/gb_apu/sound/music/midi2mml/pad/tetris/reversi/minesweeper/breakout/blocks/iso_world/iso_td/vt_terminal/ssh_util/notification/wifi_profiles/key_input/romaji_kana等の58本 / run_net.sh=参照実装サーバ・テスト用TLSサーバ・チャットサーバ・Todoistの偽物・OpenSSHのsshd相手の結合テスト(net/calendar_sync/chat_net/todoist_net/ssh_net) / run_mem.sh=確保回数の計測)
  reference_server.py         PROTOCOL.mdの参照実装サーバ(標準ライブラリのみ)。Markdownブラウザの開発相手
  ppm2png.py                  picoos_pcの--shotが書き出すPPMをPNGへ(標準ライブラリのみ)
  midi2mml.py                 MIDI(SMF)をpico-os MMLへ変換(標準ライブラリのみ。MUSIC_FORMAT.md「MIDIからの変換」)
  pad_serial.py               PCのキーボードを外部コントローラーにする(USBシリアルへ送る。tkinter + pyserial)
  generate_tetris_blocks.py   テトリスのミノの絵(blocks.pimg)とアイコンを作る(標準ライブラリのみ)
  generate_reversi_tiles.py / generate_minesweeper_tiles.py / generate_breakout_blocks.py   リバーシ・マインスイーパー・ブロック崩しの盤面のタイル画像(tiles*.pimg)を作る(標準ライブラリのみ)。下記「ボードゲーム4本のpico.gameへの移行」
lib/lua/                       vendorしたLua 5.4.7本体(lua.c/luac.cを除く)。詳細はlib/lua/README-pico-os.md
lib/monocypher/                vendorしたMonocypher 4.0.2(X25519/Ed25519/ChaCha20/Poly1305。SSHの暗号、無改造)。詳細はlib/monocypher/README-pico-os.md
lib/peanut_gb/                 vendorしたPeanut-GB(Game Boyエミュ、ヘッダ1本・無改造)。詳細はlib/peanut_gb/README-pico-os.md
pc/                            PC/Web実行用ビルド(CMake + SDL2 / Emscripten)。`src/`は実機と同一のまま使う
  compat/                     実機ライブラリの代替ヘッダ(Arduino/SPI/WiFi/SdFat/I2S/LGFX設定/タッチ)
  web/shell.html              Webビルドのページの外枠(canvas + ログ + デバッグ用ボタン)
  sdcard/                     SDカードとして読まれるディレクトリ
    gb/dmg-acid2.gb           ゲームボーイエミュの描画を確かめるテストROM(MIT。ライセンスはpc/sdcard/README.md)
    lua/hello.lua             LuaEngine/LuaSceneの動作サンプル(ランチャに「Lua Hello」タイルあり)
    lua/apps/<名前>/main.lua  LuaAppScannerが走査して自動登録するLuaアプリ(サブディレクトリ1つ=アプリ1つ)
    lua/apps/コントローラー確認/ 外部コントローラーの動作確認(押しているボタンを図で出す)
    lua/apps/ゾンビTD/        タワーディフェンス(下記「ゾンビTD」)
    lua/apps/テトリス/        テトリス風ゲーム(下記「テトリス」)。リバーシ/マインスイーパー/ブロック崩しと合わせて4本とも pico.game で作ってある
    music/*.mml               ミュージックアプリが並べる曲(demo.mml / sample.mml。MUSIC_FORMAT.md)
examples/doc.md                MarkdownView動作確認用サンプル文書
PROTOCOL.md                    ドキュメントサーバとの通信仕様(v1は一通り実装済み)
CHAT_PROTOCOL.md               チャットサーバとの通信仕様(下記「チャット」参照)
MUSIC_FORMAT.md                曲データ形式(pico-os MML)の仕様(下記「曲データ」参照)
server/chat/                   自前のチャットサーバ(chat_server.py、標準ライブラリのみ)+ Webクライアント + Raspberry Pi/Let's Encryptの設置手順(README.md)
```
`include/`, `test/` はPlatformIO標準雛形ディレクトリで未使用(README以外中身なし)。
`lib/`はLua本体のvendor先として使い始めた(上記参照。従来は未使用だった)。

## コアアーキテクチャ

### OSData (`src/OS_Data.hpp`)
グローバル状態ハブ。`inline`変数として: タッチ状態(touchX/Y/Z, isTouched, isTouchStart/End/Move)、グラフィック(`LGFX* lcd`, `LGFX_Sprite* frame`)、SD(`SdFat SD`, SD_usable)、キーボード3種へのポインタ(keyboard_jpn / keyboard_eng / keyboard_num)を保持。

### `Xxx_Functions` サブシステム (`src/functions/`)
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
| KeyInput_Functions | 物理キーボードの窓口。1打鍵1件の列を持ち、今の画面(`Scene::onKey()`)→開いているキー盤の順に配る。今の入力元はUSBシリアル(PCのキーボード)。下記「物理キーボード」参照 |
| Pad_Functions | 外部コントローラーの窓口。押しているボタンのビットマスクを`loop()`の頭で1回だけ更新する。今の入力元はUSBシリアル(PCのキーボード)。下記「外部コントローラー」参照 |
| Error_Functions | 「ユーザーへ見せるべき失敗」をログ+MsgDialogの両方へ出す共通口(`ShowFatal()`)。Lua着手前の受け皿の1つ |
| Profiler_Functions | `loop()`の区間ごとの時間とフレーム時間の集計。下記「開発者向けの道具」参照 |
| CrashDump_Functions | クラッシュダンプ(HardFault・ウォッチドッグ・Luaのエラー)とパンくず。下記「開発者向けの道具」参照 |
| DevTools_Functions | `/sys/debug.cfg`(プロファイラの表示/ログ・Luaデバッガ・ウォッチドッグ)の窓口 |

### 起動・ループ (`main.cpp`)
`setup()`: GFX→SD→Log→Display→Touch→Task→Network→Keyboard→IME→Time→Sound→Testの順にSetup()を呼び、Statusbar・FileExplorer・MarkdownView・各種ダイアログを生成して`WidgetFunctions`へ登録。

`loop()`: (`ProfilerFunctions::BeginFrame()`/`CrashDumpFunctions::Feed()`) → Touch更新 → Pad更新(外部コントローラー。物理キーボードの打鍵もここで列へ積まれる) → Display更新(自動調光の判定) → Power更新(スリープの判定) → `SceneFunctions::Update()`(保留中のシーン遷移の適用) → `KeyInputFunctions::Update()`(打鍵を配る) → `WidgetFunctions::UpdateAll()` → `GFX::FlushDirty()` → Task/Log/Time/Network/Sound/Battery/Alarm/Notification/DevTools更新 → `PowerFunctions::IdleWait()`(スリープ中だけ少し休む)、という単純なポーリングループ。各処理の後の`ProfilerFunctions::Mark(区間)`はプロファイラとクラッシュダンプのパンくず(下記「開発者向けの道具」)。

**2コア目(`setup1()`/`loop1()`)は音声専用**(`SoundFunctions::LoopCore1()`だけを回す)。1コア目とは`std::atomic`とロック無しのコマンドの列だけでやり取りする。
**2コア目からログを出したり、ウィジェット/SD/`OSData`に触ったりしないこと**(どれもロックを持たない1コア目専用の作り)。

`main.cpp`が直接newするのは**常駐ウィジェット(Statusbar)と最初のシーンだけ**で、画面ごとのウィジェットは各`Scene`の`onEnter()`が生成する。

### 画面の明るさ調整と自動調光 (`src/functions/Display_Functions`) (2026-09-27)

SUMMARY.md未掲載(小粒の機能のため新規の大項目は起こさず、この節にだけ残す)。

- **明るさの実体はバックライト(`TFT_LED`=GP22)のPWM調光**(`PICO_GFX::SetBrightness(percent)` →
  `OSData::lcd->setBrightness()` → LovyanGFXの`lgfx::Light_PWM`)。`LGFX_Config.hpp`が
  `Light_PWM`を`_panel_instance.setLight()`へ付けている(`pin_bl=TFT_LED`、`pwm_channel=TFT_LED&1`=0)。
  **2026-09-30にパレット減光(frameのパレット16色を暗くする「ソフト輝度」)から置き換えた**。
  旧方式は実機の消費電力が変わらず、`CanvasRaster`等の自前パレットも対象外だったため。
  今は描画内容に一切触れないので、全ウィジェットが一様に暗くなり、設定直後の全画面再描画も要らない。
  - **LovyanGFXのrp2040の`Light_PWM`はPWMのwrapを100に固定している**(clkdiv 50、RP2350の150MHzで
    約30kHz。可聴域の外)。だから明るさ0〜100がそのままデューティ比(%)になる。
    レベル100はwrap100でも「101段中の100段(約99%)」なので、**百分率の100だけ255を渡して常時HIGH**にする
    (`SetBrightness()`)。
  - **`GFX_Functions::Setup()`で`TFT_LED`へ`pinMode()`/`digitalWrite()`をしてはいけない**(端子がPWMからSIOへ戻り
    調光が効かなくなる。以前あった`digitalWrite(22, HIGH)`は削除済み)。GP22のPWMスライス3
    (GP22/GP23)を他でPWMに使わないこと(wrap/clkdivがスライス単位のため。今はI2SがPIOで、PWMの利用者は他に無い)。
  - **PC/Webビルド**: `Panel_sdl_SpiWait::setBrightness()`が同じ0〜100を受け取り、表示用SDLテクスチャの色の
    掛け率(`SDL_SetTextureColorMod`)にする。`readRect()`(`--shot`)には写らないので、`main_pc.cpp`の
    `writeScreenshot()`が同じ掛け率を掛けて書き出す(バックライトは液晶のピクセルの中身ではないため)。
    テクスチャは描画スレッドが作り直すことがあるので、書き込みのたびに軽い比較で当て直している。
- **`DisplayFunctions`が「今どの明るさを見せるか」の方針を持ち、`PICO_GFX::SetBrightness()`は
  値をバックライトへ適用するだけの機構**という役割分担(`Sound_Functions`が方針、`ChipSynth`が機構、
  という分け方と同じ形)。
- 設定は`/sys/display.cfg`(無くてよい): `brightness = 0〜100`(既定100)、`auto-dim = true|false`(既定true)。
  `SettingsScene`が音量(`sound.cfg`)と全く同じ流儀(ドラッグ中は`SetBrightness()`で即反映、
  指を離したときに1回だけ`SetValue()`で書く)で編集できる。**画面が真っ黒になり操作不能になるのを
  防ぐため、`kMinBrightness=10`未満には設定できない**(スライダーの最小値もここに合わせてある)。
- **自動調光**: `OSData::isTouched`と`PadFunctions::IsDown(kAllButtons)`のどちらも
  `kIdleTimeoutMs`(既定30秒)の間ずっと無ければ`kDimBrightness`(既定40。パレット減光の時代に
  「15だと真っ黒にしか見えない」との指摘を受けて上げた値をそのまま引き継いだ。バックライトのPWMは
  光量そのものを絞るので同じ数値でもパレット減光より明るく見えるはずで、**暗さが足りなければ
  実機で見て下げる**)まで即座に暗くする
  (フェードはしない。`ClocksScene`と同じ`millis()`差分の考え方)。触れる/ボタンを押すと
  即座に通常の明るさへ戻る。設定側の`SettingsScene`の自動調光チェックボックスは
  `Checkbox`の既存の当たり判定の都合上(`causeOnPressStart()`がアイコン部分の24px幅しか
  見ない。`Checkbox.cpp`参照)、ラベルではなくチェックの四角そのものをタップする必要がある
  (他の`run_test_checkbox`等、既存の全チェックボックスと共通の制約で、この機能で新たに
  作ったものではない)。
- ホストテストは無し(`GFX_Functions`/`Touch_Functions`と同じく実描画・実タッチに強く依存するため、
  この2つと同様ASanホストテストの対象外にしてある)。検証はPCビルドの`--shot`/`--tap`で行った:
  明るさスライダーで画面全体が実際に暗くなること、無操作からの自動調光・タッチでの復帰、
  `display.cfg`への書き込みと再起動後の読み込みを確認済み。`--shot`は`readRect()`にバックライトが
  写らないため`main_pc.cpp`が掛け率を掛けて書き出す(明るさ30で平均輝度237→約71、自動調光後は約95=40%を確認)。
  **実機(RP2350の`Light_PWM`でGP22を駆動)は未確認**: このリモート環境にはRP2350のボード定義・実機ビルド手段が無く、
  実機側のコード(`LGFX_Config.hpp`の`Light_PWM`設定)はコンパイルすらできていない。実機ビルドで
  `lgfx::Light_PWM`の未解決や、明るさが効かない/ちらつく場合は、まずここ(pin_bl/pwm_channel、
  他コードがGP22のPWMスライスを使っていないか)を疑うこと。自動調光の30秒閾値の実測も未確認。

### スリープ(省電力)(`src/functions/Power_Functions`) (2026-09-30)

SUMMARY.md未掲載(小粒の機能のため新規の大項目は起こさず、この節にだけ残す)。
自動調光(バックライトを絞るだけ)の先に、消費電力そのものを下げる段階を足した。

```
Active → (30秒: kIdleTimeoutMs) → Dim(DisplayFunctions) → (sleep-timeout。既定120秒) → Sleep
```
タイムアウトは**最後の操作からの通算**(自動調光が切れていてもスリープは別に効く)。Sleepに入ると:

1. **バックライトを消し、液晶パネルもSLPINで休ませる**(`DisplayFunctions::SetSleeping()`。`lcd->sleep()/wakeup()`)。
   起きるとき**120ms待つ**(ILI9341/ST7789はSLPOUTから次のコマンドまで必要。LovyanGFXの`Panel_LCD::setSleep()`は待たない)。
   スリープ中は`ApplyEffectiveBrightness()`が何もしない(起きるときに今の明るさへ戻す)。表示内容(GRAM)は保たれるので再描画は要らない。
2. **Wi-Fiを積極的な省電力へ**(`NetworkFunctions::SetLowPower()` = `WiFi.aggressiveLowPowerMode()`、起きると`defaultLowPowerMode()`)。接続は保つので
   生存確認(5秒ごと)や再接続はそのまま動く。再接続で設定が戻るかもしれないので、接続が確立するたびに掛け直す。
3. **何も鳴っていなければI2Sとアンプ(休止端子)を止め、2コア目をゆっくり回す**(`SoundFunctions::SetPowerSave()`。
   鳴っているものが無い間だけ`want`を偽にする。要求が来れば次の周回で動き直す。2コア目の休みは1ms→`kPowerSaveIdleDelayMs`=20ms)。
4. **1コア目のloop()を間引く**(`IdleWait()`が`delay(kSleepLoopDelayMs=30)`。タッチで起きるまでの最大の遅れ)。
   CPUクロックは変えていない(SPI/I2S/CYW43のPIOの分周が`clk_sys`基準で、変えると全部の再初期化が要るため。効果を実測してから検討)。

**スリープへ入らない条件**: 各画面が**毎フレーム`PowerFunctions::KeepAwake()`を呼ぶ**(ヘッダの`inline`関数で、フラグを立てるだけ。
前のフレームに呼ばれたかで判断するので、**呼び忘れても「スリープに入る」だけで戻し忘れの事故が無い**。`Power_Functions.cpp`をリンクせずに
画面のホストテストを通せる)/ 音・曲が鳴っている / Wi-Fiへ接続中。スリープ中に`KeepAwake()`が来たら起きる
(ClocksScene: タイマーが鳴って`Finished`になったとき)。

KeepAwakeを呼んでいる画面: ゲームボーイ・SSH・チャット(常時)、カレンダー(取得中)、Markdown(取得/検索中)、Lua(`pico.http_request`中。`LuaEngine::HttpBusy()`)、
設定(Wi-Fiスキャン中)、時計(タイマーが鳴っているとき)。**計測中のタイマー/ストップウォッチはスリープしてよい**(`millis()`の差分で積むので、間引いても進む)。
新しく「操作が無くても動き続ける画面」を作るときは`onUpdate()`で呼ぶこと。

**スリープから起こしたタッチは画面へ渡さない**(暗い画面の見えないボタンを押さないよう。`swallow_touch`)。指を離すまで
`OSData::isTouched`等を毎フレーム下ろし続ける(Touch_Functionsは`isTouched`が偽なら新しいタッチとみなすので、離した瞬間に自然に終わる)。
コントローラーのボタンで起きた場合は握りつぶさない。

- 設定は`/sys/display.cfg`の`sleep-timeout = 秒`(0で無効、既定120)。`SettingsScene`の「スリープ」ドロップダウン(しない/1/2/5/10/30分)で編集する
  (一覧に無い値は一番近い項目を選んで見せる)。**設定画面は10行になった**(`ROW_H` 26→24。ドロップダウンの箱は行より背が高いので、
  箱の下端を行の下端へ揃えて下の行への食い込みを防いでいる。タイムゾーンも同じ)。
- ホストテスト: `power_test`(run.sh。入り方・タッチで起きて指を離すまで握りつぶす・コントローラーで起きる・KeepAwake/音/Wi-Fi接続中は入らない・
  スリープ中のKeepAwakeで起きる・0で無効)、`sound_test`(省電力でI2Sとアンプが止まり、要求で動き直し、解けば戻る)。
  依存する窓口は偽物へ差し替えている(`stubs/Arduino.h`に`delay()`、`stubs/WiFi.h`/`pc/compat/WiFi.h`に省電力モードの記録を足した)。
- PCビルドの検証: `display.cfg`に`sleep-timeout = 2`を置いて`--shot 1000`でスリープへ入る/`--tap`で復帰しそのタッチでアプリが開かない
  (2回目のタッチで開く)ことをログで確認。**PCの`--shot`はスリープ中の暗転が写らない**(`lcd->sleep()`は`PICO_GFX::SetBrightness()`を通らず、
  `main_pc.cpp`の掛け率が動かないため)。**起動から10秒はWi-Fi接続中扱い(PCは疎通が無いと`TRYING_CONNECT`のまま)でスリープに入らない**ので、
  確かめるときは10秒以上(1000フレーム程度)回すこと。
- **実機では未確認**: `WiFi.aggressiveLowPowerMode()/defaultLowPowerMode()`の名前(arduino-picoのWiFiクラス。ビルドが通らなければここ)・
  `delay()`が実際にCPUを寝かせるか(arduino-picoの`delay()`はWFE/sleep系のはずだが未確認)・SLPINしたILI9341/ST7789への書き込み・
  復帰時の120msで足りるか・消費電流の実測(LiPo/USBの電流計で、スリープ前後を比べること)。

### アラーム (`src/functions/Alarm_Functions` / `ClocksScene`の「アラーム」タブ) (2026-09-30)

SUMMARY.md未掲載(小粒の機能のため、この節にだけ残す)。

- **鳴らす側はシーンではなくOS(`main.cpp`の`loop()`、`TimeFunctions::Update()`の後)が持つ**。
  タイマーは`ClocksScene`のメンバで、シーンを閉じると止まるが、アラームはアプリを閉じていても鳴る必要があるため。
  時計アプリの「アラーム」タブは4件の編集画面でしかない。
- 設定は`/sys/alarm.cfg`(無くてよい)。`alarm1 = 07:30,daily,on`(時刻 , `once|daily|weekdays|weekends` , `on|off`)、4件まで。
  書式違いの行は警告して読み飛ばす。`Set()`は1キーだけ差し替える(`SetValue()`)。
- **鳴らし方**: 確認ダイアログ(`MsgDialog`「止める」/「5分後」)+ビープ音(ch0、4拍のうち3拍)+毎フレーム`PowerFunctions::KeepAwake()`
  (スリープ中でも画面が起きる)。止められなければ`kRingMaxMs`(60秒)で自動停止。
  **ダイアログは`WidgetId`で持って毎フレーム`Resolve()`する**(シーン遷移で`dialog_roots`ごと破棄されうるので、
  生ポインタだとダングリングになる)。鳴っている間に消えたら出し直す。
- **NTP同期前(2020年より前)は鳴らさない**。「同じ分に2回鳴らさない」は日付込みの通算分(`last_minute_key`)で見る。
  **1回だけ(`once`)のアラームは鳴った時点でoffにして保存する**。同じ分に複数あっても鳴らすのは1つ。
- **UIの編集はSDへ即書きしない**(`Set(idx, a, persist=false)`)。▲▼の長押しは110msごとに値が変わり、
  SDへの書き込みは1回数十msかかるため、`ClocksScene`が最後の変更から600ms後(または画面を離れる/別のアラームを選ぶ/`onExit()`)に
  `Save()`する。時刻を編集したら自動でオンにする。DurationPickerは秒の桁も出るが、変えても0へ戻す(アラームは分単位)。
- **タイマーの完了音**: `ClocksScene`のタイマーが`Finished`になると、アラームと同じビープ(`AlarmFunctions::PlayBeepStep()`。3拍鳴らして1拍休む)を
  `kRingMaxMs`(60秒)まで鳴らす。開始/リセットを押すと止まる(点滅は従来どおり)。**音を出すのはこのシーンが動いている間だけ**
  (タイマー自体がシーンのメンバで、別の画面へ`Push()`している間は進まない既存の制約のまま。アラームのようにOS側へ上げてはいない)。
- **タブ名は半角カナ(`ﾀｲﾏｰ`/`ｽﾄｯﾌﾟ`/`ｱﾗｰﾑ`)**。4タブにすると1タブ約58pxで、全角4文字(64px)が収まらず崩れて折り返した。
- 検証: `alarm_test`(run.sh。時刻はUpdateAt()へ直接渡す)と、PCビルドの`--tap`/`--shot`(編集→保存、時刻を今の分にして起動すると鳴る)。
  **PCビルドの表示タイムゾーンは`network.cfg`の`timezone`(既定JST)なので、鳴らして試すときは`TZ=JST-9 date +%H:%M`で合わせること**
  (`date`だけだとUTCで1分もずれて鳴らない)。**実機では未確認**(I2Sでの音の聞こえ方・スリープからの復帰)。

### 通知 (`src/functions/Notification_Functions` / `Notification_Sources.cpp` / `NotificationScene` / `widgets/systems/NotificationToast`) (2026-09-30)

SUMMARY.md未掲載。アプリを開いていなくても、時間・時刻・電池・Wi-Fiの条件で知らせる仕組み。**Luaアプリも`pico.notify`で使える**。

- **条件を見張るのはOS(`main.cpp`の`loop()`、アラームの直後)。アプリは予約するだけ。** `LuaScene`は閉じると`LuaEngine`ごと消えるので、
  Luaのコードで条件を判定することはできない(**任意のLuaで判定する条件は対象外**と決めた。必要になったら「数秒おきに小さな予算で1回だけ走る
  使い捨てのエンジン」を別に検討する)。
- **中身とつなぎを分けてある**: `Notification_Functions.cpp`は予約・履歴・条件の判定だけ(描画・Wi-Fi・電池・登録簿に依存しない。
  `UpdateAt(now_ms, tm, epoch, Sensors)`でホストテストする)。`Notification_Sources.cpp`が実際の時刻/電池/Wi-Fiの取り込み・トーストの出し入れ・
  通知音・`AppFunctions::LaunchByName`の差し込み・`OpenCenter()`を持つ(`Setup()`/`Update()`の定義もこちら)。
- 予約(`Rule`、`kMaxRules=16`、送り主ごとに`kMaxRulesPerOwner=4`): `Delay`(millis差分。再起動で消える)/ `At`(エポック秒。NTP同期前は待ち、
  過ぎていれば同期した時点で出す)/ `Daily`(HH:MM。アラームと同じく同じ分に2回出さない。**登録した分は見送る**)/ `Every`(10秒以上。遅れても溜めて何回も出さない)/
  `BatteryLow`(下回ったら1回、+5%戻るまで次は出さない)/ `WifiConnected`・`WifiDisconnected`(変わった瞬間だけ。登録時の状態では出さない)。
  **送り主(`owner`)はLuaアプリのディレクトリ**(`LuaEngine::app_dir_`。C++からは空)。同じ送り主・同じ`tag`は置き換え。他の送り主の予約はLuaから消せない。
- 保存: `Delay`以外を`/sys/notify_rules.tsv`(1行1件のタブ区切り。値が`Config_Functions`の上限160Bを超えるので自前の書式)へ、
  変更から`kSaveDelayMs`(1秒)後にまとめて一時ファイル→差し替えで書く。文字列は`Sanitize()`でタブ/改行を空白にしてから持つ。
  起動時に`RefreshOwners()`で登録簿(`AppFunctions::NameForDir()`=argの親ディレクトリで引く)と突き合わせ、**送り主が消えた(アンインストールした)予約は捨てる**。
  そのため`NotificationFunctions::Setup()`は`AppFunctions::Setup()`より後、かつトーストを最前面に置くためキーボードより後。
- 履歴(`kMaxHistory=8`の輪。2026-10-10に16から減らした)はRAMだけ(再起動で消える)。見せ方は`/sys/notify.cfg`の`mode = on|quiet`と`sound = true|false`(通知センターの[通常/控えめ][音あり/なし])。
  quietはトーストも音も出さず、ステータスバーの印(右端の赤いベル+未読数)と通知センターだけ。**`Scene::quietNotifications()`がtrueの画面(ゲームボーイ)の間も同じ扱い**。
  **`Scene::keepForeground()`がtrueの画面(SSH・ゲームボーイ。離れると接続が切れる/ROMを閉じる)では、ステータスバーやトーストをタップしても別の画面へ移らない**
  (トーストは既読にして閉じるだけ)。うっかり触って接続やゲームを失わないため。
- **トースト**(`NotificationToast`)はオーバーレイの一番上(キーボードや半透明のダイアログの上にも出る)、約5秒(後ろに待ちがあれば2.5秒)。
  本体タップで送ったアプリを`Open()`(起動できなければ通知センター)、右端の×で既読にして閉じる。見せている間は`PowerFunctions::KeepAwake()`(スリープ中なら起きる)。
  **消すときは新設の`PICO_GFX::MarkDirtyBelow()`**: 普通の`MarkDirty()`だと`FlushDirty()`が「TRANSLUCENTの下は描き直さない」近道を使うため、
  半透明のダイアログ/キーボードの上に出していたトーストの跡が残る。`MarkDirtyBelow()`の矩形だけその近道を使わず一番下から描き直す
  (ヘッダの`inline`なので`MarkDirty()`を偽物にしたホストテストでもそのまま使える)。PCビルドでキーボードのダイアログの上で跡が残らないことを確認した。
- 通知音は最後のチャンネル(`SoundFunctions::kChannels-1`)を借りて2音(E6→A6)。同じフレームに何件出ても1回。
- **起動理由**: `Open()`が送り主・tag・dataを覚えてアプリを起動し、`LuaScene::onEnter()`が`TakeLaunchReason(正規化したapp_dir)`で受け取って
  `LuaEngine::SetLaunchReason()`へ渡す(`pico.launch_reason()`)。1回きりで、`kLaunchReasonTtlMs`(3秒)を過ぎたら捨てる(起動に失敗した理由が後の別の起動に混ざらないように)。
- **通知センター**(`NotificationScene`、ランチャの「通知」、ステータスバーのタップでも開く=`main.cpp`が`status->setOnPressEnd()`): [履歴|予約]のタブ、
  1回目のタップで下に詳しく、2回目で開く/取り消す(予約はどのアプリのものでも取り消せる)。`Revision()`が変わったら一覧を作り直す。画面を離れるときに全部既読にする。
- Lua: `pico.notify{title=, body=, tag=, data=, sound=, delay_ms|at|daily|every_ms|when(+below)}` → id(すぐ出したら0)/ `nil, 理由`(権限無し・上限)、
  `pico.notify_cancel([id|tag])` → 件数、`pico.notify_list()`、`pico.launch_reason()`。**権限`LuaPermissions::notify`(app.cfgの`permission_notify`)が要る**
  (アプリを閉じた後にも画面と音へ出るため既定では許さない)。引数の誤りは権限より先に`luaL_error`。ドキュメントは`lua-api-doc/content/api/notify.md`。
- 動作確認アプリ「通知テスト」(`pc/sdcard/lua/apps/通知テスト/`、`permission_notify=true`)。
- RAMは静的に約9KB(予約16件×約380B + 履歴8件×約360B)。
- 検証: `notification_test`(run.sh。種類ごとの発火・置き換え/上限・履歴の輪・控えめ・保存と読み込み・送り主の掃除・起動理由)、`lua_engine_test`(Lua API)、
  PCビルドの`--tap`/`--shot`(アプリで10秒後を予約→閉じてランチャでトースト→タップでアプリが`launch_reason`付きで開く、ステータスバーから通知センター、
  再起動しても`daily`/`every`の予約が残る、キーボードのダイアログの上でトーストの跡が残らない)。**実機・Webビルドは未確認**。
- 未: C++の標準アプリからの利用(チャットの未読・カレンダーの予定はシーンを閉じると通信係ごと止まるので、使うにはOS側へ上げる必要がある)、
  アラーム/タイマーの完了を通知へ統合すること。

### 開発者向けの道具 (プロファイラ / Luaデバッガ / クラッシュダンプ / Luaサンドボックス) (2026-10-03)

設定は`/sys/debug.cfg`(`DevToolsFunctions`。無くてよく、全部既定で切れている)。設定アプリの「その他」タブの
「フレーム時間を表示」「Luaデバッガ」「固まったら再起動する」でも切り替えられる(`perf-log`と`watchdog-ms`はファイルだけ)。

**プロファイラ(`Profiler_Functions`)とフレーム時間の表示(`widgets/systems/PerfOverlay`)**
- `loop()`の頭で`BeginFrame()`、各処理の後に`Mark(区間)`(**区間の終わりで呼ぶ**。直前のMarkからの時間をその区間へ足す)、
  `IdleWait()`の手前で`EndWork()`。区間は 入力/電源/シーン/画面更新/描画/タスク/Wi-Fi/サービス(`main.cpp`の並び順)。
  0.5秒の窓で平均・最大・休みを除いた仕事の時間・1フレームあたりのLuaの時間(`LuaEngine::ProtectedCall()`の一番外が
  `AddLuaMicros()`する。ヘッダだけで完結させてあるのでLuaEngineはProfiler_Functions.cppへ依存しない)を出す。
  直近60フレームは輪で持つ。全部固定長。無効の間は`BeginFrame()`/`Mark()`がパンくず(下)だけ書いて戻る。
- 表示は右下132x50pxの常駐オーバーレイ(トーストより上)。書き換えは窓が閉じたとき(0.5秒ごと)だけ(毎フレーム描くと
  表示そのものが測る時間を押し上げるため)。`hit_transparent`でタップは下へ素通り(ただし下の部品は隠れる)。
  文字はLovyanGFXの`Font0`(6x8)。グラフは緑≦16.7ms・黄≦33.3ms・赤。
- `perf-log = true`で5秒ごとに`[PROF] ...`をシリアルへ(SDのログには書かない)。`FlushDirty()`の旧来の`fps:`の行はそのまま。
- PCビルドの値は`main_pc.cpp`の`sleep_for(5ms)`を含むので、フレーム時間は約5ms+仕事になる。**実機の値は未計測**。

**Luaデバッガ(`src/lua/LuaDebugger` / `LuaDebugScreen`)**
- `LuaDebugger::GlobalEnabled()`(debug.cfgの`lua-debugger`)が立っている間に作られた`LuaEngine`だけが`LuaDebugger`を持つ
  (約3KB、`new`)。**今動いているエンジンには後から付けない**(次に開いたアプリから)。
- 止まる仕組みはLuaのフックの`LUA_MASKLINE`。行フックは重いので、ブレークポイントがある・ステップ実行中・`dbg pause`の予約が
  あるときだけ入れる(`LuaEngine::ApplyHook()`が`wantsLineHook()`を見る)。**フックはスレッドごと**なので、作った後の
  コルーチンへは`coroutine.resume`/`wrap`の包み(下)と、フックの中(`ApplyHook(L)`)で合わせる。
- 「止まる」= フックの中で`Frontend::onPause()`を呼んで戻るのを待つこと。OSの`loop()`はその間止まる(2コア目の音は鳴る)。
  実機/PCのFrontendは`LuaDebugScreen`: ウィジェットを使わず`OSData::lcd`へ直接描き、`PICO_Touch::Update()`/
  `PadFunctions::Update()`/`KeyInputFunctions::Pop()`/シリアルを自分で読む小さなループ(10msごと、ウォッチドッグへFeed)。
  続けるときは`OSData::frame`を液晶へ戻し、`RequestRedraw()`→次のフレームの`main.cpp`が`MarkDirtyBelow(全画面)`
  (半透明のダイアログの下も描き直すため。`MarkDirty()`だと止まる前の画面が残った)。**Webビルドは持たない**(メインスレッドを
  止められない)ので、止まる代わりにシリアルへ場所を出して続ける。
- 止まるきっかけ: 行のブレークポイント(16個、ファイル名は`MatchFile()`で後ろ一致)、`pico.breakpoint()`、ステップ
  (1行/次へ=深さ≦/抜ける=深さ<。`pico.breakpoint()`で止まったときはCの関数の段を引いて数える)、`dbg pause`、
  **捕まえられなかったエラー**(`LuaEngine::MessageHandler()`の中。続けてもエラーのまま進む)。`pcall`の中のエラーでは止まらない
  (Luaのpcallはメッセージハンドラを持たないため)。
- 変数はローカル(`(`で始まる中間値は除く)と上位値(`^名前`。トップレベルの`local`はこちらに出る)、計16個。値は
  `FormatValue()`で1行に(メタメソッドは呼ばない=Luaを動かさない)。ソースは`@パス`のチャンク名からSDのファイルを読む
  (`ReadSourceLines()`)。**そのため`LuaEngine::Run()`は`/`で始まるチャンク名に`@`を付ける**(エラーも`[string "..."]`ではなく
  `/lua/apps/x/main.lua:12:`の形になった)。`LuaScene`の`lib.lua`/`main.lua`はこれで足りる。
- シリアルのコマンド`dbg ...`は`PadFunctions`の行の振り分け(`extra_line_handler`。Pad_Functions.cppをLuaへ依存させないための
  関数ポインタ。`main.cpp`が`LuaDebugger::FeedSerialLine`を差す)で列(8件)へ入り、止まっていない間は`LuaScene::onUpdate()`の
  `engine->UpdateDebugger()`、止まっている間はFrontendが`pollSerial(L, true)`で処理する。シリアルの1行の上限`PadFunctions::kLineMax`は
  これのため32→96にした。
- Lua API: `pico.traceback([msg])` / `pico.breakpoint([msg])` / `pico.set_breakpoint(file, line)` / `pico.clear_breakpoint([file, line])` /
  `pico.debugger_enabled()`。ドキュメントは`lua-api-doc/content/api/debug.md`と`guide/debugging.md`。
- **スタックトレース**: 捕まえられなかったエラーは`MessageHandler()`が`BuildTrace()`(`main.lua:12 関数名`の行、10段まで)で
  `last_trace_`(1KB)へ作り、`ReportError()`がダイアログ(メッセージ+先頭4段)・ログ(全部)・`LuaDebugger::ReportError()`
  (=`CrashDumpFunctions::SaveLuaError`、`/crash/lua_NNNN.txt`)へ出す。デバッガが無くても常に効く。

**クラッシュダンプ(`CrashDump_Functions`)**
- 落ちた瞬間はSDへ書けないので2段: 消えないRAM(`.uninitialized_data`。pico-sdkのリンカスクリプトが0クリアしない領域)へ
  記録(`Record`: レジスタ・例外フレームの後のスタック16語・CFSR/HFSR/MMFAR/BFAR・パンくず、magic+FNV-1a)を置いて再起動 →
  次の起動の`Setup()`(`LogFunctions::Setup()`の直後)が`/crash/crash_NNNN.txt`へ文章にして書き、印を消す。
  `PostPendingNotice()`(通知の`Setup()`の後)が通知を出し、タップでファイルビューワーが開く(`file:`)。
- **HardFault**: pico-sdkのcrt0の弱いシンボル`isr_hardfault`を上書き(naked。`tst lr,#4`でMSP/PSPを選んで
  `pico_os_hardfault_c(frame, exc_return)`へ)。フレームのアドレスがSRAMの中のときだけ読む(壊れたSPで二重に落ちないため)。
  最後に`watchdog_reboot(0,0,0)`。CFSRの主なビット(IACCVIOL/DACCVIOL/IBUSERR/PRECISERR/STKERR/UNDEFINSTR/STKOF/
  UNALIGNED/DIVBYZERO)はダンプで日本語に読み解き、`arm-none-eabi-addr2line`の打ち方も書く。
- **ウォッチドッグ**(`watchdog = true`、`watchdog-ms`既定8000、1000〜8300): `watchdog_enable(ms, true)`。`loop()`の頭で`Feed()`。
  再起動の前に何も書けないので、`watchdog_enable_caused_reboot()`のときは**パンくず**から組み立てる。パンくず(`Crumbs`、
  これも消えないRAM)は毎フレーム: フレーム番号・時刻(`BeginFrame`)、今の区間(`SetPhase`。`ProfilerFunctions::Mark(s)`が
  **次の区間**を書く。`EndWork()`の後は「休み」)、画面の名前(`SetScene`、ポインタが変わったときだけ写す)、Luaの実行中か+
  直近のアプリ(`SetLua`。`LuaEngine::ProtectedCall()`の一番外が`LuaDebugger::NotifyActivity()`経由で呼ぶ)。
  **実機のウォッチドッグは一度動かすと止められない**ので、設定で切っても次の再起動まで効く。TLSのハンドシェイク(1〜2秒)・
  大きなSDの読み書きより長い同期処理があれば`watchdog-ms`を見直すこと。
- **PCビルド**: シグナル(SEGV/BUS/FPE/ILL/ABRT)のハンドラが記録を`<SDのルート>/sys/crash.pending`へ書いてから既定の動作で落ちる
  (`PICOOS_NO_CRASH_HANDLER`で無効)。ウォッチドッグはスレッドで真似る(固まったら記録を書いて`_exit(3)`)。Webは対象外。
  `crash.pending`は`<fcntl.h>`の`open()`ではなく`fopen()`で書く(ホストテストのSdFatスタブとO_*の値が食い違う罠を避けるため)。
- Luaのエラーは落ちていないが`/crash/lua_NNNN.txt`へ残す(1回の起動で8件まで)。

**Luaサンドボックスの強化(`LuaEngine`)**
- **打ち切りは握り潰せない**: 命令数の上限で「打ち切り中」(`aborting_`)になり、フックを1命令ごとにして次の命令で投げ直す。
  `pcall`/`xpcall`/`coroutine.resume`/`coroutine.close`は包んであり(`l_guarded_call`、yieldをまたげるよう継続関数つき)、
  打ち切り中なら捕まえた結果を捨てて投げ直す。`coroutine.wrap`は自前(`l_wrap`)。打ち切りが解けるのは次の一番外の
  `ProtectedCall()`の入口だけで、**入れ子の呼び出し(`pico.set`から鳴るコールバック=`Dispatch`)では予算も打ち切りも積み直さない**
  (`call_depth_`。積み直すと、それを繰り返して上限を逃れられた)。
- **投げ方はメモリ不足(`LUA_ERRMEM`)**: 打ち切り中は`Alloc()`が増える確保を全部断り、`RaiseAbort()`がわざと確保して起こす。
  `lua_error()`だと`xpcall`のメッセージハンドラ(Luaの関数)が呼ばれ、**フックの中から投げるとそのハンドラはフック無し
  (`allowhook=0`)で動く**ので、ハンドラの中の終わらないループで固まった(テストで踏んだ)。メモリ不足はハンドラを呼ばない。
  表示は`ReportError()`が`abort_msg_`(場所付き)へ差し替え、トレースは打ち切りに入った時点でフックの中で作る。
- **ライブラリ**: `luaL_openlibs()`をやめ、基本/coroutine/table/string/math/utf8/osだけを開く(`openSandboxedLibs()`)。
  外したもの: `debug`(`debug.sethook()`で安全網のフックを外せた)、`io`/`package`/`require`/`dofile`/`loadfile`(権限の確認を
  通らずにSDへ触れた)、`os.exit`/`execute`/`remove`/`rename`/`getenv`/`tmpname`/`setlocale`、`string.dump`。
  `load`はモードを"t"に固定(書き換えたバイトコードはVMを壊せる)。`print`はログへ。
- **`__gc`を持つメタテーブルは`setmetatable`がエラーにする**。LuaはGCのメタメソッドをフック無しで動かすので、中の終わらない
  ループは止めようがなく、GCが走るところ(どこでも)でOSごと固まる。5.4ではsetmetatableの時点で`__gc`があったときだけ
  ファイナライザが付くので、そこで見れば足りる。
- **残っている限界**: C関数の中(重い`string.find`のパターン等)はフックが来ないので止められない → ウォッチドッグで拾う
  (PCビルドで`string.find(("a"):rep(40), ("a*"):rep(30).."b")`が2秒のウォッチドッグで落ちてダンプが書かれることを確認)。
  `LUAI_MAXCCALLS`(Cのスタック)は無改造のvendorのまま。
- 以前の`lua_engine_test`の「pcallで打ち切りを捕まえれば続けられる」は振る舞いが変わったので、「握り潰せない」へ書き換えた。

**検証**: `lua_sandbox_test`(run.sh。サンドボックス・打ち切り7通り+入れ子のコールバック+yieldをまたぐpcall+`__gc`・
スタックトレース・偽物のFrontendでのデバッガ一式)、`devtools_test`(run.sh。プロファイラの区間/窓/履歴・クラッシュダンプの書き出し/
crash.pending/壊れた記録/通知/Luaのエラー・debug.cfg)。PCビルド: 表示の`--shot`、`kill -SEGV`→次の起動でダンプと通知、
重いパターンでのウォッチドッグ、`pico.breakpoint()`とエラーで止まったデバッガの画面と「続行」(一時的に画面を書き出す
仕掛けを入れて撮った。コミットには含めていない)、設定アプリの切り替え。
**実機では未確認**(このリモート環境にはRP2350のボード定義が無くビルドもできていない): `isr_hardfault`がarduino-picoで
差し替えられるか(既に定義していれば多重定義)・`.uninitialized_data`がarduino-picoのリンカスクリプトにあるか・
`watchdog_enable_caused_reboot()`の名前・RP2350(Cortex-M33)のnakedのアセンブリ・デバッガの画面の描画と`Font0`・
プロファイラの実機の値。

### スクリーンショット (`src/functions/Screenshot_Functions` / ステータスバー右端のカメラ) (2026-10-02)

SUMMARY.md未掲載(小粒の機能のため、この節にだけ残す)。

- ステータスバーの**右端のカメラのアイコン(右端24px)をタップすると撮る**。それ以外の場所のタップは従来どおり通知センター(`main.cpp`の`status->setOnPressEnd()`が`OSData::touchX`で振り分ける)。
  カメラは自作アイコン(`custom_icons/camera.svg`、`IconID::Camera`は末尾)。未読のベルはカメラの左へ寄せた。
- `OSData::frame`(合成済みの全画面、4bppパレット)を**.pimg**(`IconRender::EncodePimg()`。4bpp+RLE)で`/screenshots/shot_0001.pimg`,`0002`…へ書く。
  本体はファイルアプリ→ファイルビューワーで見られ、PCでは`script/pimg2png.py`でPNGへ直せる。書き込みに失敗したら半端なファイルは消す。タップのフレームでは直前に合成した絵が写る(ステータスバーも入る)。
- 結果は通知(`NotificationFunctions::Post()`、音なし)で出す(控えめモードならトーストは出ず通知センターだけ)。成功時は通知の`app`へ`"file:<パス>"`を入れ、**通知をタップするとファイルビューワーで開く**(`Notification_Sources.cpp`の`Launch()`が`file:`接頭辞をアプリ名ではなくファイルとして扱う)。
- 検証: PCビルドの`--tap 230,8@30:5`で保存・内容を確認。ホストテストは無し。**実機では未確認**(1フレーム76800回の`readPixelValue`+SD書き込みの時間)。

### バッテリー残量表示(`src/functions/Battery_Functions`) (2026-09-28)

SUMMARY.md未掲載(小粒の機能のため新規の大項目は起こさず、この節にだけ残す)。
PiCoLiPoSHIM(Pimoroni LiPo SHIM相当、VBUS/VSYS/GND/3V3_EN/3V3_OUTをジャンパワイヤで
Picoへ配線)を実際に導入したのを受けて、VSYS電圧からバッテリー残量を読み取る機能を追加した。

- **RP2350の無線チップ(CYW43439)とADC3(GP29)はSPIのCLKピンを共用している。** 素の
  `adc_read()`だけでは、無線チップとのSPI通信(タイマー割り込みで非同期に走ることがある)と
  読み取りタイミングが衝突しうる。**「時々しか読まない」だけでは衝突を避けられない**
  (読んでいるまさにその瞬間に割り込みで通信が挟まる可能性が残るため)。
  Raspberry Pi公式`pico-examples`の`adc/read_vsys/power_status/power_status.c`と同じ手順
  (`cyw43_thread_enter()`/`cyw43_thread_exit()`で読み取り区間をロックし、無線チップの
  バックグラウンド処理を一時停止させる)を踏んでいる。**このロックは頻度に関わらず必須**で、
  読み取り間隔(`kSampleIntervalMs=60000`)を空けているのは「ロックによる無線側の一瞬の
  処理停止」が起きる回数を減らすための追加の緩和策という位置づけ(ロックの代替ではない)。
- **電圧→百分率はLiPoの3.0V(空)〜4.2V(満充電)の単純な線形近似**(公式`read_vsys.c`と
  同じ換算式)。放電カーブの非線形性は無視している。
- **`LGFX_Config.hpp`/`Touch_Functions.hpp`と同じ「実機とPCで丸ごと処理を分ける」数少ない
  例外にこのファイル自身がなった**(`#if defined(PICOOS_PC)`で分岐)。ただし専用のpc/compat
  差し替えヘッダは作らず、`Battery_Functions.cpp`1ファイル内に両方の実装を閉じ込めている
  (ロジックが数十行程度で、ヘッダを分けるほどの規模ではないため)。PCビルドでは環境変数
  `PICOOS_BATTERY_PERCENT`(0〜100、未指定なら80固定)で疑似的な残量を固定できる
  (`pc-sound-state`/`PICOOS_SOUND_STATE`と同じ「環境変数で状態を固定してテストする」流儀)。
- **`IsExternallyPowered()`(VBUSが届いているか)も一緒に読める**(`cyw43_arch_gpio_get(2)`。
  無線チップのWL_GPIO2がVBUS検出ピン(HIGH=VBUS有り)。無線チップを起こすためのアクセスの
  ついでに分かる)が、**SHIM側の「充電完了」を伝える信号線は配線していない**ため、充電中と
  充電完了後どちらでUSBに繋がっているかは区別できない。設定画面ではこれを
  「電池 USB給電中」という文言で表示する。
- **ステータスバーには残量4段階のアイコン(`Battery0`〜`Battery4`)だけを出し、給電中の
  区別は出さない。** `IconID::BatteryCharging`(tabler由来の`battery-charging-2.svg`)は
  16pxで絵柄が崩れて判読できなかった(実際にPCビルドの`--shot`で確認して気づいた。
  `custom_icons/README.md`が警告している「tablerの絵柄は24pxグリッド前提」の実例が
  また1つ増えた形)。**自作アイコンへの差し替えはこの機能のスコープ外として見送った**
  (`Battery0`〜`Battery4`は同じ元絵でも問題なく16pxで読めたため、崩れるのは
  `battery-charging-2`固有の細い枝分かれ線が原因と見られる)。給電中かどうかは
  設定画面のテキストでのみ確認できる。
- **設定画面(`SettingsScene`)は「戻る」ボタンと同じ上部の行へ横に同居させた**(専用の行を
  割く余白が無いため。8行→9行で既に画面いっぱい、という既存の制約は変えていない)。
  `Label::setText()`は変化が無ければ再レイアウトしないため、`onUpdate()`から毎フレーム
  呼んでも実害は無い(`ClocksScene`の数字表示と同じ考え方)。
- ホストテストは無し(`GFX_Functions`/`Touch_Functions`/`Display_Functions`と同じ理由。
  実ADC・実CYW43に強く依存するためASan対象外)。検証はPCビルドの`--shot`のみ
  (`PICOOS_BATTERY_PERCENT`で5%/45%/100%を固定して確認)。
  **実機(RP2350+CYW43439)でのVSYS読み取り・`cyw43_thread_enter/exit`の実際の効果は
  未確認**(このリモート環境にはRP2350の実機もボード定義も無いため)。
  - **実機ビルド(PlatformIO)で、Raspberry Pi公式`pico-examples`が前提にしているボード定義
    由来のマクロが2つとも無いことが分かった(2026-09-28、ユーザーの実機ビルドで発覚)**:
    1. `'PICO_VSYS_PIN' was not declared` — `hardware/adc.h`/`pico/cyw43_arch.h`を
       includeしても入ってこない。このプロジェクトはPico 2 W専用(無線チップの無いPico単体は
       対象外)なので、ボードごとの値の違いを気にする理由が無いと判断し、`#ifndef`で29を
       直接定義する形にした(`CYW43_USES_VSYS_PIN`の有無で分岐する設計もやめ、
       `cyw43_thread_enter/exit`は無条件に取るよう単純化した)。
    2. `'CYW43_WL_GPIO_VBUS_PIN' was not declared`(候補として`CYW43_WL_GPIO_COUNT`が
       示された=関連するマクロ自体は一部存在する)。調べたところ、arduino-pico周辺の
       複数のissue/discussionで「`cyw43_arch_gpio_get(2)`のようにWL_GPIO2の**番号を
       直接渡す**」のが実際に使われている形だと分かった(WL_GPIO2=VBUS検出ピンという
       CYW43439のハードウェア上の割り当てはSDKのバージョンやフォークで変わらない)。
       マクロ名(`CYW43_WL_GPIO_VBUS_PIN`)への依存をやめ、数値`2`を直接渡す形にした。
    **どちらも標準pico-sdkのボードヘッダ(`boards/pico_w.h`相当)がarduino-picoの
    `rpipico2w`ビルドでは自動でincludeされないことが原因と見られる**(`#include
    "boards/pico_w.h"`を明示すれば定義自体は復活する、という報告も見つかったが、
    このプロジェクトはPico 2 W専用で値も固定なので、ボードヘッダへ依存し直すより
    直値定義のほうがシンプルと判断した)。**この2回の修正で実機ビルドは通り、実機で
    バッテリー残量表示が動作することをユーザーが確認済み**(下記「バッテリー駆動中の
    音割れ対策」参照。この確認自体は動作報告の副産物で、残量表示単体の実機検証結果として
    別途まとめたものではない)。

### バッテリー駆動中の音割れ対策(`src/functions/Sound_Functions`) (2026-09-28)

上のバッテリー残量表示を実機で動かしたユーザーから、「バッテリー駆動中に音が割れる。
音量を20%以下にすると割れない」という報告を受けて追加した対策。

- **原因は電圧不足による、アンプ(MAX98357A)のアナログクリップ。** 上の「音声出力」節の
  ハードウェア表の通りMAX98357AのVINは**VBUS/VSYS**から取っている。USB給電時のVBUSは
  安定した5Vだが、バッテリー駆動時のVSYSは**LiPoセルの電圧そのもの**(満充電4.2V〜空3.0V、
  5Vより低い)。MAX98357Aはこの時点のVINを基準にデジタル入力の振幅をアナログ出力へ変換するため、
  VINが低いとアンプが物理的に出せる電圧の天井(ヘッドルーム)も下がり、**同じデジタル音量でも
  天井を超えた波形の頭が潰れる**。音量を下げる=デジタル信号の振幅そのものを縮めることなので、
  天井の低いバッテリー駆動でも収まるようになる、という説明で実際の症状(音量依存・USB給電時は
  発生しない)と整合する。`ChipSynth`側の「4chまでは16bitに収まるよう頭打ちする」設計
  (上の「チップチューン音源」節)はデジタル信号の範囲内の保証であり、この後段のアナログの
  電圧不足までは関知しないため、既存の頭打ち設計のバグではない。
- **対策は「バッテリー駆動中(`BatteryFunctions::IsExternallyPowered()==false`)は、
  2コア目が実際に音源へ渡す音量だけを`kBatteryVolumeCapPercent=20`(ユーザーの実測値)で
  頭打ちする」**。`sound.cfg`/`SettingsScene`に保存されている設定値(`master_volume`)自体は
  変えない(`DisplayFunctions`の自動調光と同じ「保存値」と「実効値」を分ける設計。
  USB給電に戻せば設定値どおりの音量へ自動的に戻る)。
  - `battery_cap_active`という新しいatomic(1コア目→2コア目)を追加し、1コア目の
    `SoundFunctions::UpdateAt()`が`BatteryFunctions::HasSample() && !IsExternallyPowered()`を
    毎フレーム書き込む。2コア目の`Core1StepAt()`は`master_volume`を読んだ直後に
    `battery_cap_active`が立っていれば`kBatteryVolumeCapPercent`で頭打ちしてから
    `engine.setMasterVolume()`/`gb_apu.setMasterVolume()`へ渡す。
  - `SoundFunctions`が`BatteryFunctions`へ依存する形になった(`#include`1本)。
    どちらも`src/functions/`の対等な`XxxFunctions`同士で、既存の`Display_Functions`→
    `Pad_Functions`の依存と同種(循環はしていない)。
  - 診断用に`SoundFunctions::IsBatteryVolumeCapActive()`を追加したが、**設定画面への
    表示は見送った**(9行で既に画面いっぱいという既存の制約があり、動的なテキスト行を
    増やすレイアウト変更まではこの対策のスコープ外と判断した)。
- **`Sound_Functions.cpp`が`Battery_Functions.hpp`をincludeしたことで、`Sound_Functions.cpp`を
  リンクする5本のホストテスト(sound_test/music_test/lua_engine_test/lua_scene_test/
  lua_app_scanner_test)が軒並み`Battery_Functions.cpp`未リンクでリンクエラーになる状態に
  なった。** `Battery_Functions.cpp`は実ADC/実CYW43に依存しGFX_Functions等と同じくASan対象外の
  方針だったが、他のテストがそれに依存する形になった以上リンクだけは避けられないため、
  **`script/host_test/run.sh`の共通`CXXFLAGS`へ`-DPICOOS_PC`を追加し(他のstubs/はどれも
  参照していないことをgrepで確認済みなので無害)、Battery_Functions.cpp自体はPCビルドと
  同じ「環境変数で疑似値を返す」簡易実装のほうでホストテストに参加させる**ことにした
  (対象の5本の`Sound_Functions.cpp`の並びへ`Battery_Functions.cpp`を追加)。
  実ADC/実CYW43のコード自体は変わらずASan対象外のまま。
- 検証はPCビルドの`--shot`(バッテリー駆動を模した状態でも`SoundFunctions`が
  クラッシュせず初期化されることを確認)と、ホストテスト全34本(`sh script/host_test/run.sh`)の
  再実行(全件パス、リンクエラー解消を確認)。**実機での「20%以下なら音割れしない」
  効果そのものの再検証(この対策を入れた後、実際に音割れが収まるか)はユーザーからの
  報告待ち**(このリモート環境には実機が無いため確認できない)。

### タッチ座標のノイズ抑制(`src/util/TouchFilter`) (2026-09-28)

バッテリー駆動に切り替えてから、ペイント系アプリで「滑らかに描いたはずの線に規則的な
棘(とげ)状の乱れが出る」という報告をユーザーから受けた。実機の写真(ブレッドボード上で
Sの字を1ストロークで描いたもの)を確認したところ、人の手ぶれというより**単発フレームだけ
実際の位置から外れた値が混じるスパイク**に見える描画になっていた(なめらかな曲線から
短い棘が規則的に飛び出す形)。

- **原因の切り分け**: `Touch_Functions.hpp`の`Update()`はXPT2046の生の読み取り値を
  そのまま`OSData::touchX/Y`へ入れており、**座標側には元々一切の平滑化/外れ値除去が
  無かった**(Z値(圧力)の閾値`TOUCH_Z_THRESHOLD`はあるが、これは「触れているか」の
  判定であって座標の妥当性は見ていない)。バッテリー駆動時に限って目立つようになった
  理由は未特定(電源ノイズが載っている可能性、ブレッドボード配線を手で持って描くこと
  自体の振動・接触不良の可能性など複数の仮説があり、どれか一つに絞れていない)が、
  **ソフト側での外れ値抑制自体はどちらの原因でも有効な緩和策**であり、かつ独立した
  ロジックとして低リスクに追加できるため、原因特定を待たずに実装した。
- **`TouchFilter`(median-of-3フィルタ)を新設**。直近3フレームの生座標を保持し、
  x/yそれぞれ独立に中央値を返す。**移動平均ではなく中央値にしたのは、外れ値を
  ブレンドして薄めるのではなく実質無視できるため**(移動平均だと単発スパイクの影響が
  数フレームに渡って薄く広がり、線全体が常に細かく波打つ結果になる)。3点(直近3
  フレーム)だけの最小構成でヒープ確保は無し。x/yを独立に中央値化する簡略化をしている
  (2次元の幾何学的中央値ではない)が、XPT2046のノイズはX/Y各軸で独立に起きるため
  実用上十分と判断した。
- **`reset()`は新しいタッチが始まるたびに呼ぶ**(`!OSData::isTouched`の判定を流用。
  下のisTouchStart判定と同じ条件)。履歴を最初の1点で埋め直すので、前のタッチの
  座標が新しいタッチの最初の数フレームへ混ざらない。
- **実機専用(`src/functions/Touch_Functions.hpp`)にだけ組み込み、PCビルドの
  マウス入力(`Touch_Functions_PC.hpp`)は変更していない**。マウス/`--tap`は
  そもそもノイズが無く、既存の`--tap`ベースのPC検証(フレーム単位のドラッグ操作等)に
  意図しない遅延を持ち込みたくなかったため。フィルタ自体はプラットフォームに依存しない
  実装(`src/util/TouchFilter.hpp`)にしてあるので、ホストテストで単体検証できる。
- ホストテストは`script/host_test/touch_filter_test.cpp`(単発スパイクをほぼ無視すること、
  滑らかな動きには過剰な遅れが無いこと、`reset()`で前のタッチの名残りが混ざらないこと、
  `reset()`を呼ばずに`push()`しても安全に自動primeされること)。**実ハードウェアに
  一切依存しない純粋なロジックのテストなので、GFX_Functions/Touch_Functions本体とは
  違いASan対象外にする必要が無い**(むしろ積極的にホストテストへ入れた)。
- **実機での効果そのもの(実際にスパイクが消えるか)は未確認**(このリモート環境には
  実機が無いため)。原因がもし電源ノイズでなく物理的な配線の接触不良だった場合は、
  このフィルタだけでは不十分な可能性がある(その場合は配線の当たり直し・ブレッドボードの
  差し直しが必要になる)。

**追記(同日)**: フィルタ適用後、ユーザーから「マシにはなったが依然として微妙」
「USB給電・机に置いた静止状態でもノイズが出る」との報告を受け、**バッテリー駆動固有でも
ハンドリング(振動)固有でもない**ことが判明した。切り分けの過程で`git diff`から
**`TFT_MAX_SPEED`が本セッションより前に70MHz→60MHzへ下げる試行が既にされていた
(それでも直っていない)ことが分かった**。`LGFX_Config.hpp`のコメント
「ILI9341は40MHz程度まで安定」に対し、60MHzでもなお上回っている点は引き続き気になるが、
「下げても直らなかった」という既知の事実がある以上、TFT側速度**だけ**が原因とは断定できない。

- **タッチ側のSPIクロックはこれまで一度も下げられていなかった**(1MHz固定)。
  XPT2046はブレッドボード上でTFTと同じSPI0バスを共有しており、高速なTFT読み書きに
  伴う信号の乱れ(リンギング/クロストーク)の影響を受けやすいというのは、
  XPT2046_Touchscreenライブラリ周りでよく報告される既知の症状(参照: 後述のissue/フォーラム)。
  試されていなかった手として、**`TOUCH_SPI_HZ`という名前付き定数を新設し1MHz→500kHz
  へ下げた**(`Touch_Functions.hpp`)。データシート上XPT2046は最大2MHz程度までしか
  保証されておらず、そもそも1MHzという既存値も別に高速である必要は無い箇所だった。
- **この変更の効果も未検証**(実機が無いため)。`TFT_MAX_SPEED`を下げても直らなかった
  という既知の事実を踏まえ、ユーザーには「タッチ側クロックを下げても直らない場合、
  ブレッドボード配線そのもの(ジャンパワイヤの長さ・タッチ用配線とTFTの高速配線が
  隣接して這っていないか)を疑う」という切り分けの続きを提案した。

**追記2(同日)**: ユーザーが実機でTFT 40MHz・タッチ100kHzまで両方下げて確認したが、
**「マシにはなるが完治しない」という結果は変わらなかった**。これは重要な否定的結果で、
1MHz→100kHzという10分の1近い低速化でも直らないなら、**原因はクロック周波数に依存する
信号の乱れ(リンギング/クロストーク)ではなく、周波数と無関係な要因**(ブレッドボードの
接触不良、電源/グラウンド経由のノイズ等)である可能性が高いと判断した。クロックを
これ以上下げる方向のチューニングは切り上げ、**「配線を1本ずつ指で揺らして反応を見る」
という工具不要の切り分け**(接触不良なら特定の配線に反応が出るはず)を提案した。

ユーザーは「いつか基板(ハンダ付け)へ移行する予定で、それまではこのノイズを許容する」
方針を選び、**ソフト側の緩和策の強化(median-of-3→median-of-5)**を依頼された。

- `TouchFilter`の窓を3点から5点へ拡張(`kWindow`定数化)。**単発のスパイク1点だけでなく、
  5点中2点までの連続ノイズも中央値が多数派(3/5)に従うことで無視できる**ようになった
  (median-of-3では2点中1点が外れ値でも中央値が引っ張られ得たのに対し、median-of-5は
  より頑健)。トレードオフとして実際の動きへの追従の遅れが増える(3点なら約1フレーム、
  5点なら約2フレームの遅れ)。**根本解決ではなく延命策であり、基板へ移行してノイズ源が
  減れば窓を再び狭める余地がある**、という位置づけをコード内コメントに明記した。
  中央値の実装は5要素の挿入ソート(要素数が少ないので十分速い)。
- ホストテスト(`touch_filter_test.cpp`)も窓5点の挙動(単発スパイクの無視・2フレーム
  連続ノイズへの耐性・滑らかな動きへの追従・`reset()`の履歴クリア)に合わせて書き直した。
  全34本再実行し全件パスを確認済み。
- **2026-09-29にユーザーが追従の遅れを優先して窓を3点へ戻した**("median adjust"コミット)。テストが窓5点を前提に
  「2フレーム連続のノイズを消す」を確かめていたため失敗していたので、**テストを窓の大きさ(`TouchFilter::Window()`)に
  合わせる形へ直した**(2026-09-30): 「(窓-1)/2フレームまで続くノイズは消える」「過半数を超えて続いた値には追従する」。
  窓3と窓5の両方で通ることを確認した。

Sources(この追記時点の調査で参照): [XPT2046 touch controller pinout and wiring guide](https://inairspace.com/blogs/learn-with-inair/xpt2046-touch-controller-pinout-and-wiring-guide-for-reliable-touchscreens)、
[rp2040 and Touch XPT2046 · Issue #216 · lovyan03/LovyanGFX](https://github.com/lovyan03/LovyanGFX/issues/216)

### OSのCPU・RAMの無駄を削る (2026-10-10)

OS側の洗い出しの結果、次を直した。実機ファームの静的RAMは 140,000B → 113,540B(-26.5KB)。

- **合成の外の描画を捨てる(二重描画の解消)**: `WidgetFunctions::UpdateAll()`→`Widget::update()`→`render()`で、変化したウィジェットが
  `frame`へ実際に描き、直後の`FlushDirty()`が背景ごと塗り直してもう一度描いていた。今は`Widget::update()`が`render()`の間だけ
  **frameのクリップを空にし**(書き込みは全部捨てられる)、`PICO_GFX::render_suppressed`を立てる。文字を描く`Label`/`Button`/`Statusbar`は
  これを見てdirtyを積むだけで戻る(グリフの展開も省く)。**新しいウィジェットの`render()`は、合成の外の回でも状態の確認とdirtyの積み上げは
  今まで通り行い、重い描画(文字・画像の展開)は`render_suppressed`なら省いてよい**。描画の結果を前提にした処理(frameを読み戻す等)を
  合成の外の回に置かないこと(空のクリップで何も描かれていない)。合成の中(`isDirtyDeactivates`)で状態の変化を見つけて覚えると
  `MarkDirty()`が効かず描き直しが失われるので、`Statusbar`のように変化の確認は合成の外の回だけで行う。
- **`MemFunctions::Update()`は`mallinfo()`を`kPeakSampleFrames`(32)フレームに1回だけ**(空きブロックを全部たどるので毎フレームは重い。
  PCビルドでは1フレーム約0.13ms食っていた)。
- **`FlushDirty()`の`hit`、`HitTest()`の作業用の`std::vector`は静的に使い回す**(フレーム/タッチごとのヒープ確保をやめた)。
- **ステータスバーは変化したときだけ描き直す**(時刻は分が変わったとき、電波・電池・SDは5秒ごとに見て段階が変わったとき)。
- **ログ**: `log.txt`は起動のたびに消さず追記し(区切りに`----- boot -----`)、起動時に256KBを超えていれば`/sys/log.old.txt`へ1世代回す。
  以前の`preAllocate(64KB)`+`truncate(0)`はやめた(truncateが確保した領域を返すので意味が無く、前回のログも消えていた)。
  バッファは4KB→1KB、`Log()`のスタックは768B→320B(1回だけ整形する)。
- **アプリ登録簿**: `AppEntry::icon_path`(96B)を`icon_file`(argのディレクトリからの相対パス、32B)にした。絶対パスは
  `AppFunctions::IconPathOf()`で組み立てる。`Register()`は従来どおり絶対パスを受け取り、アプリのディレクトリの外・32Bに収まらない
  ものは既定アイコンへ落とす。
- **IMEの索引**(最大約6.8KB)は`IME_Functions::Setup()`ではパスを覚えるだけにし、**初めて引いたときに件数ぶん`malloc`**、
  日本語のキー盤が見えなくなったとき(`KeyboardPanel::onPanelHidden()`)に`IME_Functions::Release()`で返す。辞書が無いときの失敗は
  返すまで覚えて毎回探さない。
- `FlushDirty()`の行のハッシュを64bit→32bit(5KB→2.5KB)。
- `Label::utilityInstance()`(テンプレートの種類ごとの約650Bの静的なLabel×3)をやめ、`DrawPlain()`等は`FontFn`と`setTextColor()`を直接呼ぶ。
- **`UpdateAll()`は要るウィジェットにだけ`update()`する**: タッチのあったフレーム(押した/離した/外を押したを全員へ配る)と、
  `getNeedsRedraw()`・`is_pressing`・**`wantsFrameUpdate()`**が真のウィジェットだけ(クリップの計算もそのときだけ)。
  `render()`の中で毎フレーム外の状態や時間を見張るウィジェットは`wantsFrameUpdate()`で真を返すこと(今は点滅カーソルのLabel・
  Statusbar・PerfOverlay・`redraw_below_frames`中のKeyboardDialog・長押し中のDurationPicker)。値の変化をsetter(`needsRender()`)で
  知らせるだけのウィジェットは何もしなくてよい。**見張りを`render()`に足したのに`wantsFrameUpdate()`を足し忘れると、
  タッチか描き直しの要求があるまで見張りが止まる**。
- **自動調光で暗くなっている間は約30fps**(`PowerFunctions::kDimFrameMs`=33ms。以前は100fpsのまま)。`KeepAwake()`を呼ぶ画面・
  音が鳴っている間・Wi-Fiの接続中(`Busy()`)は落とさない。
- クラッシュダンプの文章のバッファ(2KB/3KB)は書き出す間だけ`malloc`する(Luaのエラーで確保できなければ保存を諦める)。
  TextViewの全角の文字幅の控え(約2KB)はTextViewが1つでも生きている間だけ持つ(確保できなければ毎回測る)。
- **lwIPの領域(`PBUF_POOL`約36KB・`ram_heap`16KB)は減らせなかった**: arduino-picoはlwIPをビルド済みの`liblwip.a`で配っていて、
  プールの大きさ(`include/lwipopts.h`の`PBUF_POOL_SIZE`/`MEM_SIZE`)はそのビルド時に決まる。こちらのビルドフラグでは変わらず、
  変えるにはlwIPをソースからビルドし直してフレームワークのものと差し替える必要がある(TLSの受信の速さにも効くので見送った)。
- 検証: ホストテスト全部(`app_test`にアイコンの相対パス、`key_input_test`に索引の読み直し、`notification_test`は件数に依らない形へ)、
  PCビルドの`--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0: ランチャ・SDのアイコン・電卓・テキストエディタ+日本語キー盤)、
  実機ファームのビルド。`power_test`に暗いときの間隔。**実機での速さ・見た目は未確認**。

### 液晶への転送を減らす(`GFX_Functions::FlushDirty()` / `LuaCanvas` / `util/ScopedClip.hpp`) (2026-10-06)

「pico.gameのゲームが24fps出ない」の調査。PCビルド(SPIの待ちを実機と同じ速さで真似る)で、カメラが動き続ける場面を
測ると**1フレームに約15万画素(画面2枚ぶん)を送って11fps**だった。直したもの:

- **`LuaCanvas`が1フレームに2回描いていた**: `WidgetFunctions::UpdateAll()`の`Widget::update()`→`render()`でも
  Luaの`render`を呼んでいて、合成の外なので`pico.draw_*`が描いた範囲をそれぞれdirtyに積み(タイルマップで全面、
  ボタン1つずつ…)、`FlushDirty()`でもう一度描いて送っていた。`GameBoyView`と同じく、**合成の外(`!isDirtyDeactivates`)では
  自分の矩形をdirtyに積むだけ**にした。
- **`FlushDirty()`**:
  - 先に**重なる/近いdirty矩形を1枚にまとめる**(`CoalesceDirtyRects()`。和の面積が2枚の合計+1024画素以下なら1枚に)。
    同じ矩形が2回積まれると2回描いて2回送っていた。まとめると矩形同士が重ならないので、**全部描いてから全部送る**順にした。
  - **変わっていない行は送らない**(`PushChangedRows()`)。`FlushDirty()`の後は液晶の中身がframeと同じ、という前提で、
    送った行のframeの中身のハッシュ(64bit、`row_hash[320]`)を覚え、次にdirtyになってもハッシュが同じ行は飛ばす。
    スクロールで空だけの行、動いていないボタンの帯、描き直しても同じ絵の所を送らない。3行以内の切れ目は帯を分けずに送る。
    `MarkDirtyBelow()`の矩形は必ず送る(Luaデバッガが液晶へ直接描いた後の描き直しがこれ)。frameを通さずに液晶へ描くときは
    `InvalidateLcdRows()`。5秒ごとのシリアルの`fps:`の行に`px/frame`(送った画素)と`skipped`(飛ばした画素)を出す。
- **描画中にクリップを外していた**(以前からのバグ。上の2回描きに隠れていた): `Label::DrawPlain()`(`pico.draw_text`等)・
  `IconRender::DrawIconRaw()`・`AppGrid`・`MarkdownView`の表が`setClipRect()`→`clearClipRect()`していたため、
  `FlushDirty()`が掛けた「dirty矩形の内側だけ」のクリップが外れ、その後の描画がdirty矩形の外へはみ出していた
  (送られないのでframeと液晶が食い違い、行を飛ばす最適化と組み合わさると古い絵が残る。テトリスのSCOREの数字で踏んだ)。
  **`util/ScopedClip.hpp`**(今のクリップとの重なりへ狭め、抜けるときに元へ戻す)に置き換えた。
  `pico.set_draw_area/clear_draw_area`も、描画中(`PICO_GFX::render_clip_active`)は**ウィジェットの描画範囲∩dirty矩形**
  (`render_clip`)の外へ広げない/そこへ戻すようにした。**部品の中で一部だけクリップして描くときは`ScopedClip`を使うこと**
  (`clearClipRect()`を描画の途中で呼ばない)。
- `pico.game`: カメラが動いたときの描き直しをワールドの見える範囲だけにした(画面ボタンの帯は描き直さない)。
- **確かめ方(PCビルドだけ)**: `PICOOS_VERIFY_LCD=1`で、`FlushDirty()`のたびに液晶(SDLパネル)の中身をframeと全画素比べ、
  食い違えば`[VERIFY]`と場所をログへ出す(`VerifyLcdMatchesFrame()`)。`PICOOS_NO_ROW_SKIP=1`で行を飛ばす最適化だけを切れる(比較用)。
  わざと「変わった行も飛ばす」ように壊すと食い違いを報告することを確かめてある。ランチャ・入力テスト(キーボードのダイアログ)・
  ペイント・テトリス・時計・ジャンプアクションで食い違い0。
- 結果(PCビルド、SPIは下の37.5MHzを真似る): カメラが流れ続ける場面で**11fps・15万画素/フレーム → 約60fps・1.6万画素/フレーム**。
  **実機では未計測**(Luaの描画・4bpp→RGB565の変換のCPU時間はPCでは見えない)。
- **⚠ `TFT_MAX_SPEED`(60MHz)は実際には37.5MHzで動いている**: LovyanGFX(rp2040)は`clk_peri`(150MHz)を2で割った75MHzを
  さらに整数(1+SCR)で割るので、選べるのは75 / 37.5 / 25MHz…だけで、**37.5〜74.99MHzを指定するとすべて37.5MHz**になる
  (`FreqToClockDiv()`。`pc/compat/config/Panel_sdl_SpiWait.hpp`も同じ計算)。全画面1枚の転送は37.5MHzで約33ms(30fps止まり)、
  75MHz(`80000000`等、75MHz以上を指定)なら約16ms。タッチのノイズの調査で下げたが、ノイズはクロックに依らなかった
  (上の「タッチ座標のノイズ抑制」)。上げるかは実機の安定性次第。

### dirty矩形を固定長配列化(`src/functions/GFX_Functions`) (2026-09-28)

パフォーマンス監査で見つかった、`PICO_GFX::dirtyRects`が`std::vector<Rect>`のままだった点への対応。
`FlushDirty()`のたびに`dirtyRects.clear()`→次フレームの`push_back()`で伸縮を繰り返しており、
「確保ゼロ」方針(MarkdownViewのプール等)から外れていた。加えて`reserve(48)`は**シーン遷移直後の
実測で軽く超える**件数で、実質恩恵が無かった。

- **`std::vector<Rect> dirtyRects`を`Rect dirtyRects[kMaxDirtyRects]`(固定長配列)+
  `int dirtyRectCount`へ置き換えた**(`kMaxDirtyRects=128`。48→128へ拡張)。ヒープ確保が
  完全に無くなる。
- **128件を超えて`MarkDirty()`された場合は、個々の矩形を追うのを諦めて画面全体を
  1枚のdirty矩形として扱う**(`dirtyOverflowed`フラグ)。溢れた時点で積んであった
  個別の矩形は(どうせ全画面転送に飲み込まれるので)破棄し、以降の`MarkDirty()`も
  静かに無視する。`FlushDirty()`が呼ばれた瞬間に`dirtyRects[0]`を`{0,0,SCREEN_WIDTH,SCREEN_HEIGHT}`
  へ差し替えて1件だけ処理する(取りこぼしが無く、128枚ぶんの当たり判定・転送より軽い)。
- 検証はPCビルドで実施: 通常の`--shot`/`--tap`で見た目に変化が無いこと、
  `kMaxDirtyRects`を一時的に3まで下げてほぼ毎フレーム溢れさせた状態でも
  クラッシュせず正しく描画されること(画面全体を1枚として扱うので、部分描画時と
  見た目は変わらない)を確認した。ホストテストは無し(`GFX_Functions`は上記
  「画面の明るさ調整と自動調光」と同じ理由でASan対象外)。

### 文書キャッシュ目録(index.tsv)の書き込みをO(n)からほぼO(1)へ (`src/storage/Doc_Cache.cpp`) (2026-09-28)

パフォーマンス監査で見つかった、`PICO_DocCache::SetEntry()`/`RemoveEntry()`が常に
`rewriteIndex()`(目録全体を読みながら一時ファイルへ書き写し、最後にrenameする)を
呼んでいた点への対応。Markdownブラウザで新しい文書を開くたび(=キャッシュに無い
host/pathへ初めて`SetEntry()`する)、その時点までに溜まった目録**全件**を読み直し・
書き直していた。ブラウジングで未訪問のページを開き続けるセッションでは、
1件追加するたびのコストが目録の総件数に比例して伸び続ける(n件目の追加がO(n)、
合計でO(n²))。

- **`Lookup()`の中身を`scanIndexFor()`という読み取り専用の走査へ切り出した**
  (振る舞いは変えていない、単純な抽出)。`SetEntry()`/`RemoveEntry()`からも
  「本当に既存行があるか」を確認するためだけに呼べるようにするための下準備。
- **`SetEntry()`は、`scanIndexFor()`で既存行が無いと確認できたら`appendIndexEntry()`
  (目録の末尾へ1行追記するだけ)で済ませる**。既存行がある(内容が更新された)場合
  だけ、これまで通り`rewriteIndex()`で全体を書き直す。ブラウジングで新しい文書を
  開くたびに通る経路(未訪問ページの初回キャッシュ)が最も軽くなるよう狙った
  (読み取りは相変わらずO(n)だが、**書き込みが「全件読み書き+一時ファイル+rename」
  から「1行の追記」へ縮む**。SDは書き込みの方が読み込みより重いため、体感の差は
  読み取り側の計算量が変わらない以上に大きいはず)。
- **`RemoveEntry()`も同様に、`scanIndexFor()`で対象行が無いと分かれば目録に一切
  触れず即`true`で返す**。`Remove()`(本体を消す口)は本体が無くても目録の掃除を
  試みる作りのため、この経路は実際によく通る。**以前は目録が存在しない状態から
  でも`rewriteIndex()`が空の`index.tsv`を新規に作ってしまっていた**(削除対象が
  見つからなくても、読み書きの手順自体は素通りで最後まで進んでいたため)。今回の
  変更でこの副作用も無くなった。
- **`Doc_Fetch`側の呼び出し元(`DocFetch::begin()`/`update()`)は一切変更していない**。
  `SetEntry()`/`RemoveEntry()`の入口だけで「新規か更新か」を判断しており、
  外から見た振る舞い(目録には1つのhost/pathにつき常に高々1行、という不変条件含む)
  は完全に保たれる。
- 検証は`script/host_test/cache_test.cpp`(既存の全項目に加えて、
  「存在しないエントリの削除は目録に一切触れない(ファイルすら作らない)」
  「新規追加5件それぞれが正しく引ける」を追加)。PCビルドで`script/reference_server.py`を
  相手に実際にMarkdownアプリから文書を開き、`index.tsv`に正しく1行追記されること、
  同じページを再訪問しても(304になり`SetEntry()`が呼ばれないため)目録が
  1バイトも変わらないことを確認した。

### Luaコールバックの探索を線形からid昇順+二分探索へ (`src/lua/LuaEngine`) (2026-09-28)

パフォーマンス監査で見つかった、`LuaEngine::Dispatch()`/`DispatchClosed()`/
`DispatchSelectItem()`が毎回`callbacks_`(**アプリ全体**のコールバック登録の合計)を
先頭から線形探索していた点への対応。1ウィジェットが持てるイベント種別は
`EventKind`の12種までなので「1ウィジェットあたりは十分速い」という当初の判断
(クラスコメント「コールバック中継の設計」参照)は正しいが、**アプリ全体**では
ウィジェット数×イベント数ぶん(テトリス/ペイントのような多ボタンLuaアプリでは
数十〜百程度)に比例して線形探索のコストが伸びる。`render`イベントは毎フレーム
`Dispatch()`されうるため、この経路はホットパスになりうる。

- **`callbacks_`を`WidgetId`の昇順に保つという不変条件を導入した**
  (`BindCallback()`/`PruneCallbacksFor()`が維持する)。新設した
  `FindCallback(id, kind)`が`std::lower_bound()`でidの区間をO(log n)に絞り、
  そこから先はEventKindの種類数(高々12)ぶんだけ線形に見る(実質定数時間)。
  `Dispatch()`/`DispatchClosed()`/`DispatchSelectItem()`は全てこれ1つに寄せた。
- **`BindCallback()`の新規挿入は`std::lower_bound()`で見つけた位置へ`insert()`する**
  (同じidの中での並び順は`FindCallback()`が線形走査するので問わない)。
  `PruneCallbacksFor()`も同様にidの区間を二分探索で絞ってから、その範囲をまとめて
  1回の`erase(first, last)`で消す(以前は1件ずつ`erase(it)`していたため、複数件
  持つウィジェットの破棄はO(件数×n)だった)。
- **`Dispatch()`系は`FindCallback()`が返すポインタから`ref`(int)を即座に値コピー
  してから`ProtectedCall()`(Lua呼び出し)へ入る**。Lua側のコールバックが
  `pico.on()`/`pico.destroy()`を呼ぶと`callbacks_`(`std::vector`)が再確保・移動
  されうるため、ポインタ/参照をLua呼び出しの前後で跨いで持たない(元のコードも
  `ref`を値でコピーしてから使っていたので、その規律をそのまま踏襲しただけ)。
- コンテナ自体は変わらず`std::vector<CallbackBinding>`のまま(確保回数・メモリ
  footprintに変化は無い)。挿入・削除の実行時間計算量自体はvectorの性質上O(n)
  (要素の詰め直しが要る)のままだが、そちらは`pico.on()`/`pico.destroy()`という
  低頻度の操作でしか起きない。**高頻度(毎フレームありうる)な`Dispatch()`系だけを
  O(n)からO(log n)へ落とす**のが狙いで、既存の「ヒープ確保を増やさない」方針
  (クラスコメント参照)とも矛盾しない。
- 検証: `script/host_test/lua_engine_test.cpp`(既存の全項目。共通4イベント+
  `render`+`closed`+ウィジェット固有6種+コンテナ操作+HTTPまで、`callbacks_`が
  絡む経路を一通り踏む)、`script/host_test/lua_scene_test.cpp`をASan/UBSan付きで
  実行し、並び替え後も全件パスすることを確認。PCビルドでは「テトリス」
  (タッチボタン6つ+HOLD/BGM枠のタップ+盤面のCanvas `render`を毎フレーム
  dispatchする、最もコールバック数の多いLuaアプリ)を実際に起動し、ゲーム開始・
  ソフトドロップ操作・スコア加算・盤面の差分描画までタップで操作して確認した。

### Wi-Fi認証情報の暗号化保存 (`src/util/Secret_Cipher.hpp`) (2026-09-27)

SDカードだけを紛失/盗難された場合に、`/sys/network.cfg`の`wifi-ssid`/`wifi-password`を
テキストエディタでそのまま読まれないようにする対策。相談の発端は「フラッシュに鍵を焼けば
SD紛失時も安全では」という提案で、そこから以下の設計に落とした。

- **守れるのは「SDカードだけの紛失/盗難」のケースだけ**。本体(基板)ごと持ち去られた場合は
  無力(鍵はソースにハードコードされ実機のフラッシュに焼かれるだけなので、SWD等でフラッシュを
  吸い出せば鍵も一緒に読める)。さらに**鍵は既定値のままリポジトリに公開されている**ため、
  本気の攻撃者(ファームウェアの吸い出しを厭わない相手)には無力で、「肩越しに見られた・
  SDだけ他人に渡した」程度の偶発的な漏洩を防ぐのが目的と割り切った。本格的に守るなら
  RP2350のOTP/Secure Bootでデバイス固有鍵を焼く必要があるが、実機ビルド・検証手段が
  このリモート環境に無く実装コストも高いため見送り、この簡易版(相談時の案で言う「a」)を採用した。
  **forkして実運用する場合は`PICO_Secret::kKey`を必ず書き換えること**(コメントに明記)。
- **方式はXTEA(128bit固定鍵)をCTRモードのストリーム暗号として使うだけ**。ブロック暗号の
  出力(暗号文ではなく鍵ストリーム)を平文とXORするので、暗号化と復号が同じ関数
  (`XorStream()`)で済む。TLS(BearSSL/OpenSSL)のような重い依存を持ち込まず、PC/実機で
  同じコードがそのまま動く(他のsrc/コード全般と同じ方針)。
- **「用途文字列(purpose)」を鍵ストリームの種に混ぜる**(`Fnv1aHash(purpose)`を初期カウンタにする)。
  SSIDとパスワードで別々のストリームになるので、同じ鍵ストリームを使い回して2つの暗号文を
  XORすると平文同士のXORが漏れる、という典型的な弱点を避けられる。
- **保存形式は`"enc1:"`接頭辞+16進文字列**。接頭辞が無い値は「まだ暗号化されていない平文」と
  みなしてそのまま返す(後方互換)。既存の`network.cfg`(`pc/sdcard/sys/network.cfg`のサンプルも
  含め平文のまま)は変更不要で、次に`SettingsScene`から保存し直したときに暗号化形式へ移行する。
- **`PICO_Config::kConfigMaxValueLen`を128→160、`kConfigMaxLineLen`を192→256へ拡張した**
  (`Config_Functions.hpp`)。64バイトのWi-Fiパスフレーズ(64桁PSKの16進表記を想定した上限)を
  暗号化すると`"enc1:"`(5)+16進128文字+終端=134文字になり、旧上限の128に収まらないため。
  他の設定キー(NTPサーバー名等)には影響しない(バッファが大きくなるだけ)。
- **暗号化・復号の呼び出し元は2箇所**: `NetworkFunctions::Setup()`(起動時、読み込んだ値を
  復号してから使う)と`SettingsScene`(`loadValues()`でSSID表示用に復号、`commitEdit()`で
  保存前に暗号化)。`SD_Functions`/`Config_Functions`自体には手を入れていない
  (暗号化はWi-Fi認証情報という特定の値の扱いであり、汎用のconfigパーサの仕事ではないため)。
- ホストテストは`script/host_test/secret_cipher_test.cpp`(往復・用途違いで暗号文が変わること・
  取り違えた用途では正しく復号できないこと・空文字列の扱い・後方互換・壊れたデータやバッファ
  不足への安全な失敗・上限ちょうどの長さの往復)。PCビルドでも、ホストテストのSdFatスタブ経由で
  `SettingsScene::commitEdit()`と同じ手順(暗号化→`SetValue()`→ファイルの生内容確認→
  `ParseFile()`→復号)を通し、SD上には`enc1:...`の形で保存され、読み込み側で元の文字列に
  戻ることを確認済み。既存の平文`network.cfg`(`pc/sdcard/sys/network.cfg`)がそのまま読める
  ことはPCビルドの`--shot`で確認した(設定画面にSSID/パスワードが正しく表示される)。

**チャットのトークン(`/sys/chat.cfg`のtoken)も同日中に同じ方式へ広げた。** 実装直後にユーザーから
「チャットの認証情報は暗号化されているか」と問われて気づいた漏れで、この時点では`Chat_Client::loadConfig()`
がtokenを平文のまま読んでいた。

- **`ChatScene`に「設定」ボタンを新設した**(一覧画面でだけ、`[招待]`ボタンと同じ位置に出す。
  部屋の中では招待、一覧では設定という切り替え)。押すと`InputDialog`で「チャットサーバURL:」→
  (1フレーム空けて)「トークン(空欄で変更なし):」の順に編集する
  (`openChatSettings()`/`openTokenDialog()`。ダイアログからダイアログは1フレーム空ける、
  という「ブラウザのヘッダー」節の既存ルールをそのまま踏襲)。
- **サーバURLは暗号化しない。** Wi-FiのSSIDと同じ判断で、URL自体は秘匿情報ではなく、
  `chat_server_value`としてプレフィル・表示に使うため平文のまま保持・保存する。
  **トークンだけ暗号化する**(用途文字列は`"chat-token"`。Wi-Fiの`"wifi-ssid"`/`"wifi-password"`と
  同じ`PICO_Secret`を使うので新規実装は無し)。トークンの編集ダイアログは
  Wi-Fiパスワードと同じく**常に空欄から始まり**、空欄のまま決定すると既存の値を変更しない
  (`commitChatToken()`。`ChatScene`はトークンの平文を保持しない)。
- `Chat_Client.cpp::loadConfig()`のtoken読み込み箇所に`PICO_Secret::Decrypt("chat-token", ...)`を
  挟んだだけ(後方互換で`enc1:`接頭辞が無ければ平文のまま使う)。`configure()`自体
  (ホストテスト等からの直接注入経路)は平文のtokenを受け取る前提のまま変えていない
  (暗号化を意識するのは「SDのconfigファイルを読む」`loadConfig()`側だけでよいため)。
- 検証: `chat_scene_test`(run.sh、既存)が設定ボタン追加後も全項目パス。
  ホストテストのSdFatスタブ経由で、`ChatScene::commitChatToken()`相当の手順
  (`Encrypt("chat-token",...)`→`SetValue()`)で`/sys/chat.cfg`を書き、
  `ChatClient::loadConfig()`が実際にそれを復号して`State::Ok`まで進むことを確認した
  (このリポジトリには含めていない検証用の使い捨てプログラムで確認。手順はWi-Fiの
  検証と同じ)。PCビルドの`--shot`で「設定」ボタンから実際にサーバURL入力ダイアログが
  開くことまで確認済み(オンスクリーンキーボードでの実入力までは行っていない。
  暗号化ロジック自体は上記の往復検証で担保されている)。

**カレンダーの非公開URL(`/calendar/sources.cfg`)も同日中に同じ方式へ広げた。** Googleカレンダーの
非公開URLはSDカードから読める平文のままだと、SDだけ紛失した場合にWi-Fi/チャットと同じ脅威に晒される。

- **`PICO_Secret::kMaxPlainBytes`を64→255へ拡張した**(`Secret_Cipher.hpp`)。Googleの非公開URLは
  200文字を超えることがあり(`Url::path`を`PICO_STR_LL`へ広げた経緯と同じ理由)、Wi-Fi/チャット向けの
  64では全く足りないため。既存の暗号文字列の復号には影響しない後方互換な変更(上限を緩めるだけ)。
- **用途文字列はカレンダーの名前ごとに分けた**(`"calendar-url:" + 名前`)。sources.cfgは複数件
  持てるため、Wi-Fiの`"wifi-ssid"`/`"wifi-password"`のように固定1種類の用途文字列を使い回すと、
  複数のURLの暗号文をXORして平文同士のXORが漏れる、という`Secret_Cipher.hpp`の弱点をカレンダーが
  複数あるときに実際に踏むことになるため。
- **`CalendarSync::WriteSource(name, url)`を新設した**(`Config_Functions::SetValue()`と同じ
  「一時ファイル経由で1行だけ差し替え、無ければ追記」の手順だが、暗号化後の値(最大約516文字)が
  `Config_Functions::kConfigMaxValueLen`(160、Wi-Fi/チャット向け)を大きく超えるため、
  `Config_Functions`自体には触れず`Calendar_Sync.cpp`内に専用の大きめバッファ(`PICO_STR_512B`)で
  実装した。ロジックはほぼ`SetValue()`のコピーになるが、汎用パーサの制約(全設定ファイル共通の
  バッファサイズ)を変えるより影響範囲を`Calendar_Sync.cpp`へ閉じ込める方を優先した)。
- **`CalendarScene`に「追加」ボタンを新設した**(`[更新]`の左、`source_count`に関わらず常に表示)。
  押すと名前→(1フレーム空けて)URLの順に`InputDialog`で編集し、`WriteSource()`で暗号化して保存する。
  名前が不正/保存に失敗した場合は`ErrorFunctions::ShowFatal()`で理由を出す(GameBoyScene等、
  既存のC++シーンからの利用例と同じ)。
- **`CalendarSync::ReadSource()`(≒`ParseSourceLine()`)がenc1:接頭辞を見て復号する**(後方互換:
  接頭辞が無ければ平文のまま使う)。母艦のSDカードリーダーで`sources.cfg`を直接編集する既存の運用
  (URLを平文で書く)は変わらず使える。`webcal://→https://`の変換は復号した後の値に対して行う
  (暗号化された16進文字列は`webcal://`では始まらないため、順序を間違えると判定が効かなくなる)。
- ホストテストは`script/host_test/calendar_sync_secret_test.cpp`(189文字の長いURLでの往復・
  `enc1:`接頭辞で保存されること・平文がSDにそのまま書かれないこと・複数件・同名の上書き・
  後方互換(平文の既存行がそのまま読める)・不正な名前の拒否)。`calendar_scene_test`は
  「追加」ボタン追加後も既存の全項目がパスすることを確認済み。PCビルドの`--shot`で
  「追加」ボタンから実際に名前入力ダイアログが開くことまで確認済み(実際の保存はホストテストの
  SdFatスタブ経由で確認。暗号化ロジック自体は上記の往復検証で担保されている)。

### 保存済みのWi-Fiネットワーク・Wi-FiのON/OFF・自動再接続 (`src/net/Wifi_Profiles` / `NetworkFunctions` / `SettingsScene`) (2026-10-01)

SSID/パスワードを1組しか持てなかった(`network.cfg`の`wifi-ssid`/`wifi-password`)のを、複数保存して
任意のものへ繋げるようにした。合わせて設定アプリをタブで分けた。

- **一覧は`WifiProfiles`(`src/net/Wifi_Profiles.hpp/.cpp`)、置き場所は`/sys/wifi.cfg`**: `enabled = true|false`と
  `ssid1`/`pass1` … `ssid8`/`pass8`(`kMaxProfiles=8`、固定長の静的領域で約1.2KB、確保なし)。
  **番号の若い順 = 直近で接続できた順**で、`NetworkFunctions`が接続に成功するたびに`MarkConnected()`でその組を先頭へ
  繰り上げる(並びが変わったときだけ保存)。この並びがそのまま自動接続の順番になる。
  - 暗号化はWi-Fiの単一設定と同じ`PICO_Secret`。**用途文字列は番号ごと**(`"wifi-ssid:1"`/`"wifi-password:1"`)。
    同じ用途の暗号文を1ファイルに並べると平文同士のXORが漏れるため(カレンダーのURLと同じ理由)。
  - 書き込みは`Config_Functions::SetValue()`を使わず、**ファイル丸ごとを一時ファイル→差し替え**(`Save()`)。
    1件で2キー、並べ替えで全キーが変わるので、1キーずつ書き換えるより単純。
  - SSIDは32バイト・パスワードは64バイトまで(WPAの上限。暗号化後133文字で`kConfigMaxValueLen`=160に収まる)。
    超えると`Put()`が`Invalid`、9件目は`Full`(自動で古いものを消さない。利用者に消してもらう)。
  - **移行**: `wifi.cfg`が無いときだけ`network.cfg`の`wifi-ssid`/`wifi-password`を1番として取り込み、`wifi.cfg`を作る。
    以後その2キーは読まない(全部消した後に古いSSIDが復活しないように)。`network.cfg`側のキーは消していない。
    PCビルドは起動のたびに`pc/sdcard/sys/wifi.cfg`を作るので`.gitignore`に入れてある。
- **自動接続(`NetworkFunctions`)**: 起動時は先頭(直近で接続したもの)へ繋ぐ。接続に失敗した
  (`TIMEOUT`/`SSID_NOT_FOUND`/`FAILED`)まま`RETRY_INTERVAL`(30秒)経つたびに、保存済みを**先頭から1つずつ**試す
  (`retryRank`。一巡したら先頭へ戻る。接続できたら/手動で選んだら0へ戻す)。依頼は「直近で接続したWi-Fiへ試みる」で、
  先頭がそれ。2番目以降も巡るのは、場所を移った(家⇔職場)ときに繋がらないままにならないため。
  接続中に切れた場合は従来どおり同じSSIDへ繋ぎ直す(5秒ごとの生存確認)。
  試行中(10秒)は`TRYING_CONNECT`なのでスリープに入らないが、30秒のうち残りの20秒で入れる。
- **ON/OFF**(`NetworkFunctions::SetEnabled()`): 新しい状態`NetStatus::OFF`。OFFは`WiFi.disconnect()`して自動接続を止めるだけで、
  **無線チップ自体(CYW43)は止めない** — 電池の残量表示がVBUSの検出に無線チップのGPIO(WL_GPIO2)を使うため
  (`Battery_Functions`参照)。`WiFi.mode(WIFI_OFF)`/`end()`まで落とせばもっと省電力になるが、その場合は電池の読み取りとの
  兼ね合いを実機で確かめること。手動で接続先を選ぶ(`ConnectWiFiAsync()`)とONに戻る。ステータスバーはOFFのとき
  満タンの扇を薄い灰色で出す(バツは付けない)。Wi-Fiの状態が変わったら5秒の定期更新を待たずに描き直す。
- `NetworkFunctions::ConnectProfile(index)`/`RemoveProfile(index)`(今繋いでいる/繋ごうとしているものを消したら切断し、
  30秒後に残りへ自動接続)。
- **設定アプリ(`SettingsScene`)は画面下のタブ**: `Wi-Fi`(ON/OFFのチェックボックス+状態の1行、保存済みの一覧=`ScrollList`、
  [検索][追加][接続][削除]) / `本体`(音量・輝度と自動調光・スリープ) / `時刻`(タイムゾーン・NTP1/2) / `その他`(ブラウザのホーム・
  起動時の自己診断)。全タブぶんのウィジェットを`onEnter()`で作り、`tab_widgets[]`へ登録して表示中のタブだけ見せる
  (`ClocksScene::applyVisibility()`と同じ考え方)。ドロップダウンは開いた一覧が下の行に重なるので最後に`Add()`する(従来どおり)。
  タブ名は4つとも2〜4文字の幅に収める(「画面/音」は58pxに収まらず2行に折れたので「本体」にした)。
  - 一覧: 1回目のタップで選択、2回目で接続(`ScrollList`の流儀)。鍵のアイコン=パスワードあり、扇=オープン。
    接続中のものは緑で「(接続中)」。状態の1行と一覧は接続状態/接続先/ON・OFFが変わったときだけ作り直す(選択はSSIDで覚えて戻す)。
  - [検索]: 従来の`WifiScanDialog`。選んだSSIDが**保存済みならそのまま繋ぎ**、未保存ならパスワードを聞いて保存して繋ぐ。
  - [追加]: SSIDの手入力→パスワード(隠れたSSID用)。**同じSSIDを追加し直すとパスワードの書き換え**(パスワード変更の手段はこれ)。
  - [削除]: 確認の`MsgDialog`の後に消す。確認中に並びが変わっても別の組を消さないよう、SSIDで覚えておく。
  - ダイアログを閉じた直後に次のダイアログを出すときは1フレーム空ける(`Pending`。「ブラウザのヘッダー」節と同じ理由)。
  - 旧来のSSID/パスワードの「編集」行は無くなった。
- ついでに直したもの: **`pc/compat/Arduino.h`の`min`/`max`が引数(値渡しの一時変数)への参照を返していた**
  (`decltype(a < b ? a : b)`はTとUが同じ型だと左辺値参照になる)。`NumberSlider::setValue()`の`min(max(...))`が壊れた値を読み、
  タブ化で生成順が変わった途端に音量/輝度が0で保存される形で表に出た(PCビルドをASan付きでビルドして特定)。`std::decay`で値を返すようにした。
  実機のArduinoはマクロなので無関係。
- 検証: `wifi_profiles_test`(run.sh。旧形式の取り込み・追加/更新/上限/削除・先頭への繰り上げ・ON/OFFの保存・番号ごとの暗号化・番号の抜け)、
  PCビルド(ASan付きでも)の`--tap`/`--shot`: 4タブの表示、OFF→再起動してもOFF→ONで再接続、検索→未保存のネットワークを選択→パスワード空欄で決定→
  保存されて接続し一覧の先頭へ、削除の確認、`PICOOS_WIFI_STATE=failed`で「直近のもの→次→直近のもの」と30秒ごとに巡ること。
  **実機では未確認**(`WiFi.disconnect()`後の`beginNoBlock()`での復帰、別のSSIDへ切り替えるときに切断を挟まなくてよいか)。

### Wi-Fiの新規接続(周辺スキャン→選択→パスワード入力→接続) (`SettingsScene` / `task/NetworkScan.hpp` / `gui/widgets/dialogs/WifiScanDialog`) (2026-09-27)

`SettingsScene`のSSID/パスワード編集は元々`InputDialog`への手入力のみだった。
一般的なスマホ/PCの「Wi-Fi設定」と同じ、周辺をスキャンして一覧から選び、
パスワードを入れて繋ぐという操作を追加した。

- **既存の手入力(`ssid_edit_button`「編集」)は残したまま**、SSID行の左隣に
  「検索」ボタンを新設した(`wifi_scan_button`)。隠れたSSIDや特殊な入力が要る場合は
  従来通り手入力できる。このボタン1つぶんだけSSID行のラベル幅(`ssid_label_w`)を
  詰めており、他の行(NTP/ホーム等)のラベル幅は変えていない。
- **`NetworkScan`(既存、未使用のまま放置されていたTask)を初めて実利用する過程で
  所有権の設計を直した。** 元の実装は`NetworkFunctions::ScanAsync()`が生成した
  `NetworkScan`を`PICO_Task::Add()`でグローバルリストに乗せていたが、これは
  「呼び出し側が結果を読み出す前に消える」バグを持っていた: `PICO_Task::Update()`は
  `status()`がPROCESSING以外になったその場で即座に`delete`する。main.cppの`loop()`は
  シーンの`onUpdate()`を`PICO_Task::Update()`より先に呼ぶため、呼び出し側が完了を
  検知できるのは早くても次のフレームであり、その時点では既に解放済み
  (実際にPCビルドで試して初めて踏んだuse-after-free。スキャン自体は成功したのに
  ダイアログが「スキャンに失敗しました」を表示する、という壊れ方をした)。
  **`HttpGet`/`HttpRequest`と同じ「呼び出し側が生ポインタとして持ち、自分の
  `onUpdate()`から毎フレーム`update()`を呼び、終わったら自分で`delete`する」流儀へ
  変更し、`PICO_Task::Add()`には乗せないようにした**(`ScanAsync()`の戻り値の型も
  `Task*`から`NetworkScan*`へ変えて、呼び出し側が`getCount()`/`getResult()`を
  キャストなしで読めるようにした)。
- **`NetworkScan`自体に結果の保持(`Result{ssid, rssi}`の固定長配列
  `kMaxResults=16`)を足した**(以前は見つけたSSID/RSSIを`Serial.printf`で
  ログへ流すだけで、呼び出し側が読み取る手段が無かった)。電波の強い順に
  挿入する(`CalendarScene::reload()`のファイル名ソートと同じ、高々16件の
  挿入ソート)。隠しSSID(空文字列)は選びようが無いので一覧に出さない。
- **`WifiScanDialog`(新設、`gui/widgets/dialogs/`)はSearchDialog(MarkdownScene)と
  全く同じ骨格**(状態1行+`ScrollList`+ボタン群、2回タップで選ぶ流儀)。
  スキャンそのものはせず、結果を詰めるのは`SettingsScene`の仕事(SearchDialogが
  検索そのものをしないのと同じ役割分担)。`ScrollList`のアイコンに
  `IconID::WifiSignal1〜4`(ステータスバーと同じ4段階、`NetworkFunctions::GetWifiStateIconID()`と
  同じ閾値)を使い、電波の強さを一覧内で見せる。ダイアログは開くたびに`new`し、
  閉じたら`DestroyLater()`する(SearchDialogと同じ、使い回さない)。
- **選択後はパスワード入力(`InputDialog`、空欄のまま決定可)を1フレーム空けて開く**
  (`pending_wifi_password_dialog`。ダイアログからダイアログは1フレーム空ける、
  という「ブラウザのヘッダー」節の既存ルールをそのまま踏襲)。
  **`connectScannedNetwork()`は既存の`commitEdit(EditField::Password)`とは
  空文字列の扱いが違う**: 手入力の編集は「空欄なら既存のパスワードを保持」だが、
  スキャンからの新規接続は選んだネットワークへの新規接続そのものなので、
  空欄(オープンネットワークのつもり)でも常に上書きする(別のネットワーク用の
  古いパスワードを引きずらないため)。SSID/パスワードとも暗号化して保存するのは
  既存の「Wi-Fi認証情報の暗号化保存」と同じ経路(`PICO_Secret::Encrypt()`)。
- 検証はPCビルドの`--shot`/`--tap`で行った(ホストテストは無し。GFX_Functions/
  Touch_Functionsと同じく実描画・実タッチに強く依存するため対象外にした):
  スキャン→3件表示(電波の強い順、アイコンの本数が変わること)→選択→
  パスワード入力(空欄で決定)→`NetworkFunctions::ConnectWiFiAsync()`が
  実際に呼ばれ、SSID/パスワードのラベルが更新されること。「再スキャン」
  「閉じる」ボタン、パスワード入力の「キャンセル」(接続を試みずに戻ること)も
  確認済み。**実機での確認は未**(このリモート環境には実機が無い。PC側の
  `WiFi.scanNetworks()`は`pc/sdcard/sys/network.cfg`の`pc-wifi-scan`が返す
  固定リストなので、実際の電波状況に応じた挙動は実機でしか確かめられない)。

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
| `widgets/apps/` | **特定のアプリ専用**のウィジェット | `MarkdownView` / `FileExplorer` / `AnalogClock` / `DurationPicker` / `MonthGrid` / `ChatLogView` / `GameBoyView` / `GameBoyPad` / `CalculatorKeypad` / `GraphView` 等 |
| `widgets/systems/` | **OSのシェル部品**(特定アプリのものではない) | `Statusbar`(常駐オーバーレイ) / `AppGrid`(ランチャのタイル) |
| `widgets/dialogs/` | モーダルダイアログ(キーボードのダイアログ枠`KeyboardDialog`を含む) | 下記「ダイアログ」参照 |
| `widgets/keyboards/` | オンスクリーンキーボードのキー盤3種 + 基底`KeyboardPanel` | 下記「オンスクリーンキーボード」参照 |
| `widgets/interfaces/` | ミックスイン的インターフェース | `IBorderColor` / `IFontImplementation` / `ITextColor` / `ITextInputTarget` |

- **includeは常に`src/`起点の絶対パス**(`#include "gui/widgets/apps/MarkdownView.hpp"`)。
  相対includeは使っていないので、フォルダを移してもファイル自身の中身は書き換え不要。
- **PlatformIOもPCビルドも`src/**.cpp`を再帰的に拾う**ので、ファイルを移動してもビルド定義に触る必要はない。
  ただし**`script/host_test/*.sh`はソースを1本ずつ明示列挙している**ので、移動したらここだけ直すこと。
- `FileExplorer`が`apps/`なのは、`FileSaveDialog`/`FileSelectDialog`から使われていても
  **「SD上のファイルを見せる」という用途に特化した部品**だから。汎用部品の定義は「役割が特定の
  画面に紐付いていないこと」で、「複数箇所から使われていること」ではない。

### ウィジェットカタログ
Button / Label / Textbox(Labelを継承、単一行/複数行対応の入力欄) / NumberInput(数字キーボード専用の1行入力欄) / Checkbox / Icon(tabler_icons由来、`IconSize`指定) / Image / NumberSlider / ScrollContainer / ScrollList / CanvasRaster(ピクセル単位描画) / LayoutContainer(縦横1方向の自動整列) / GridContainer(列数固定の2次元流し込み) / AppGrid(ランチャのアプリタイル) / TabBar(横並びのタブ) / AnalogClock(アナログ時計の文字盤) / DurationPicker(「時:分:秒」の表示/入力欄) / DropdownMenu / FileExplorer(SDのファイル一覧・作成/削除/選択、`currentPath`は`FixedString<PICO_PATH_LEN>`) / MarkdownView(最も作り込まれたウィジェット) / Statusbar / TextView(見えている行だけを描くプレーンテキストの表示欄。任意でカーソル) / ImageView(.pimgを1回だけ解いて持ち、ドラッグでスクロール) / LuaCanvas(中身を持たず`render()`でLua側コールバックを呼ぶだけ。Lua側からは`"Canvas"`。詳細は下記「直接描画」参照) / ProgressBar(表示専用の進捗バー。タップ素通り)。
**`TextView` / `ImageView` / `MarkdownView` / `AnalogClock` / `DurationPicker` / `MonthGrid` も2026-10-05からLuaの `pico.create` で作れる**(下記「Lua APIの追加(2026-10-05 その2)」)。

`LayoutContainer` / `GridContainer` は**Luaアプリが子を動的に積むこと**を想定して足したコンテナ。`add()`で所有権を引き取りデストラクタで`delete`する。子の位置(x/y)だけを面倒見てサイズは子自身に委ねる(`Widget`基底に`setW`/`setH`が無いため)。コンストラクタの`reserve_hint`は上限ではなく単なるヒントで、超えても`std::vector`の再確保で動き続ける。

**「子を持たず`render()`で直接描き、タップ位置から逆算する」型のウィジェット**が増えている:
`AppGrid` / `ColorDialog` / `KeyboardNum` / `TabBar` / `AnalogClock` / `DurationPicker` / `MonthGrid` / `GameBoyPad`。
部品1つごとに`Button`を`new`しないのでヒープを食わず、上記`hit_transparent`の問題(表示用の子がタップを奪う)とも
無縁になる。**格子状・多ボタンのUIを新設するときはまずこの型を検討すること。**

- `TabBar`: ラベルの固定長配列(`kMaxTabs=4`)。選択中は黒塗り+白抜き。`setOnChanged()`は**選択が変わったときだけ**
  呼ばれる。幅をタブ数で割った余りは最後のタブへ足す(切り捨てると右端の罫線が1本浮く)。
  1行に収まらないラベルは`wrapOffset()`が2行へ折り返す(「ストップウォッチ」がこれに当たる)。
- `AnalogClock` / `DurationPicker`: **時刻を自分では取りに行かず、シーン側が`setTime()`/`setTotalMs()`で流し込む**
  (`TimeFunctions`への依存をウィジェットに持たせないため)。どちらも値が変わったフレームだけ再描画するので、
  毎フレーム呼んでよい。`setTotalMs()`では`on_changed`を**飛ばさない** — 飛ばすとシーン側の流し込みで再入する。
- `AnalogClock`の針は「先端+根元」の三角形で描く。1pxの線は細すぎ、LovyanGFXの`drawWideLine()`は
  アンチエイリアスのため**4bitパレットに無い中間色を要求する**ので使えない。
  (2026-09-23追記: `CanvasRaster`では単なる色化けでは済まず、`readRect()`経由のアルファブレンドが
  実際に**SEGVする**ことが判明した。`drawWideLine`/`drawWedgeLine`/`drawSpot`/`drawSmoothLine`系は
  4bppパレットスプライトへは一切使わないこと。詳細は下記「実機未検証だったCanvasRasterの潜在クラッシュ」参照)
- `DurationPicker`は総ミリ秒だけを保持し、時/分/秒は描くときに割り出す(タイマーの「設定値」と「残り時間」を
  同じウィジェットで見せるため)。カウントダウン中は`setEditable(false)`で▲▼が消える。
  ▲▼は**長押しで連続加算**(450ms後に110ms間隔)— 25分を1タップずつ積むのは現実的でないため。
  桁は繰り上がらず、その桁だけが巡回する。上限は`23:59:59`。

### ウィジェットID (`src/gui/widgets/WidgetID.hpp` / `WidgetRegistry.hpp` / `WidgetFactory.hpp`)
Lua等の外部から安全にウィジェットを指すための32bit ID。**発行側・ファクトリ・`Resolve()`の実利用(Lua統合本体)まで実装済み**(2026-09-19、下記「Luaバインディング」参照)。

- ビット配分は `[31:26] type(6bit, WidgetType)` / `[25:10] generation(16bit)` / `[9:0] index(10bit)`。`generation==0`は未割り当ての予約値なので**ID 0は常に無効**。
- 具象ウィジェットは`getWidgetType()`の実装が必須(純粋仮想)。種類を足すときは`WidgetType`へ追記する(64種を超えると`static_assert`で落ちる)。
- **IDは`getId()`の初回呼び出し時に遅延発行**する。外部から触られないウィジェットはスロットを消費しない。解放は`~Widget()`が`WidgetRegistry::Unregister()`を呼び、スロットのgenerationを進める。
- `WidgetRegistry::Resolve(id)`はindex範囲・generation・typeの3点を検証して`Widget*`を返す(不一致ならnullptr)。**破棄済みIDの誤参照(use-after-free)はここで弾かれる。**
- **`Resolve()`はホストテスト(`script/host_test/widget_factory_test.cpp`)で初めて検証された**: 発行済みIDからの解決、type改ざんの検出、破棄済みID(use-after-free)の検出、スロット再利用時のgeneration不一致検出を確認済み。(2026-09-19追記: 実コード側の呼び出し元も`LuaEngine`(`pico.set/get/on/destroy/add_child`等)として実装済み)
- **`WidgetType` → `new Xxx` のファクトリを追加した**(`src/gui/widgets/WidgetFactory.hpp/.cpp`)。`WidgetFactory::Create(WidgetType)`がwidgets/直下の汎用部品15種(Button/Label/Textbox/NumberInput/Checkbox/Icon/Image/NumberSlider/ScrollContainer/ScrollList/CanvasRaster/LayoutContainer/GridContainer/TabBar/DropdownMenu)を生成する。widgets/apps・systems・dialogsの専用ウィジェットは対象外(SD走査やシーン固有状態への依存が強いため)。生成直後は仮の位置・大きさなので、呼び出し側がsetX/setY/setW/setH等で整える前提。Textboxは`Textbox.cpp`が明示インスタンス化済みの`N`(`PICO_STR_LL`)に合わせてあり、任意のNは使えない(未使用の組み合わせを増やすには明示インスタンス化をもう1行足す必要がある)。
- **プロパティのget/setをLuaへ通す共通口(`WidgetProperty.hpp/.cpp`)を追加した(2026-09-19)**。`WidgetProperty::Get/Set(Widget*, Id, Value)`が`WidgetFactory::Create()`対応15種それぞれの代表的なプロパティ(Text/Value/Checked/FontSize/BorderColor等)を読み書きする。Widget基底の仮想関数にはしていない(`setW()`/`setH()`が基底に無く型ごとに意味が違うため。`WidgetFactory`と同じ「WidgetType→switch」形式)。値は`Value{type, i, f, b, FixedString<PICO_PATH_LEN> s}`という固定長のタグ付き共用体もどきで、ヒープを使わない。ホストテストは`script/host_test/widget_property_test.cpp`。
  (2026-09-19追記: この時点では「共通口」自体は揃ったが、Lua側からこれを叩くバインディング本体は
  まだ無かった。`LuaEngine`実装により`pico.set/get`から実際に呼ばれるようになった)
  (2026-09-21追記: 当初「NumberInputの入力値、Icon::opaqueのget、GridContainerのHAlign/VAlignのget等はウィジェット側に対応するgetter/setterが元々無いため未対応」だったが、Lua APIの細部の穴埋めで`NumberInput::getNum/setNum()`・`Icon::getOpaque()`・`GridContainer::getHAlign/getVAlign()`を追加し全て解消した。`ScrollList`/`DropdownMenu`には新たに`item_count`プロパティ(読み取り専用)も足した。詳細は「Luaバインディング」の「コンテナからの取り外し / リストへの項目追加」参照)
- `Widget::operator new`が確保失敗(nullptr)した際、以前は無言で失敗していたが、唯一の確保入口である`Widget.cpp`側でLOG_SYS_FAILを出すようにした。個々の`new Xxx(...)`呼び出し元がnullチェックしていない問題そのものは残っている(下記「Lua着手前の受け皿の状態」の「確保失敗(OOM)」参照)。

### アプリの枠組み (`src/functions/App_Functions.hpp`)
`AppEntry`(名前/アイコン/引数/シーン生成関数)の固定長テーブルに登録し、`HomeScene`の`AppGrid`がそれを並べる。

- **アプリを増やすときに触るのは `src/functions/App_List.cpp` の `Setup()` に1行足すだけ**。シーン側にも`HomeScene`にも手を入れない。
- 仕組み(`Register`/`Launch`/`Get`)は`App_Functions.cpp`、載せるアプリの一覧は`App_List.cpp`に分けてある(前者はシーン実装に依存しないのでホストテストが軽い)。
- 生成関数は`std::function`ではなく素の関数ポインタで、シグネチャは`Scene* (*)(const AppEntry&)`。登録簿を確保ゼロの静的テーブルに保つため。
  - `&AppFunctions::MakeScene<XxxScene>` … 引数なしで生成する
  - `&AppFunctions::MakeSceneWithArg<XxxScene>` + `Register()`の第4引数 … `entry.arg`をコンストラクタ(`const char*`1つ)へ渡す。**同じシーン型を別のargで何件でも登録できる**ので、「文書ごとに1タイル」「Luaスクリプトごとに1タイル」が作れる
- **`name`と`arg`は`FixedString`でコピーして持つ**(以前は`const char*`で静的寿命のリテラル必須だった)。SDを走査して見つけたアプリのように、寿命の短い文字列からそのまま登録できる。
  - `name`は`FixedString<PICO_STR_M>`(48B)。切り詰めは表示が縮むだけなので警告のみで登録は通す。
  - `arg`は`FixedString<PICO_STR_L>`(96B)。切り詰まるとパスが別物を指すので、**長すぎる場合は登録ごと拒否**する(`Register()`が`false`)。
  - 代償として`apps[24]`が**約3.8KBのstatic RAM**を常時占める(1件160B。以前は288B)。上限や文字列長を動かすときはここを意識する。
- 実行中に登録簿を書き換えてもよい(Luaアプリのスキャン等)。ただしランチャの再描画までは面倒を見ないので、表示中に増減させたら`AppGrid`へ`needsRender()`すること。
  実例: `src/lua/LuaAppScanner.cpp`が`/lua/apps/<名前>/main.lua`を走査して`Register()`する(詳細は「Luaバインディング」の「SDを走査したLuaアプリの自動登録」参照)。今のところ`App_List.cpp::Setup()`の起動時1回のみ呼んでおり、起動後の再スキャンには対応していない。
- `Launch()`は`SceneFunctions::Push`なので、アプリ側から`Pop()`すればランチャへ戻る。
- `AppGrid`はタイルごとに子ウィジェットを作らず、`render()`で直接描いてタップ位置から逆算する(`ColorDialog`の色グリッドと同じ方式)。`WidgetFunctions::HitTest()`は子から先に判定するため、タイルをIcon+Labelの親として作ると子がタップを奪ってしまう。
- レイアウトは2列×3行=6個/ページ(タイル111x78px)。3列だとタイル幅72px=日本語4文字しか入らず大半のアプリ名がはみ出したため2列にした。名前は`drawName()`がUTF-8の文字境界で切って最大2行へ折り返す(`DrawPlain()`は折り返さないため自前)。

### シーン (`src/gui/scenes/`)
`Scene`基底クラス(`getName()`/`onEnter()`/`onExit()`/`onUpdate()`/`contentRect()`)と`SceneFunctions`による画面遷移。

- **レイヤとシーンの対応**: `widgets`(通常レイヤ)と`dialog_roots`(ダイアログ層)は**シーンの所有物**で遷移時に一括破棄。`overlays`(Statusbar/キーボード3種)はOS常駐でシーンをまたいで生き続ける。→ 「常駐させたいものは`AddOverlay()`に置く」が唯一のルール。
- **遷移API**: `SceneFunctions::Change/Push/Pop`。いずれも要求を登録するだけで、実際の遷移は`SceneFunctions::Update()`(フレーム境界)で実行される。ボタンのコールバック内から呼んでも自分自身をdeleteしない(`DestroyLater`と同じ発想)。1フレーム1遷移で、2件目以降の要求は却下して即delete。
- **ウィジェットの寿命**: シーンがアクティブな間のみ。`Push`でスタックへ退避されたシーンもウィジェットは解放済みで、`Pop`で戻った時に`onEnter()`から作り直される(シーンオブジェクト本体は数十バイト)。スタック上限は`kMaxSceneDepth=4`の固定長配列。
- **`onExit()`でdeleteしてはいけない**: ウィジェット本体の破棄は`WidgetFunctions::ClearSceneWidgets()`が行う。`onExit()`は自分の生ポインタのnull化と、次回復元したい状態の退避のみ。
- 遷移時は`isDirtyDeactivates`で破棄/生成中のdirtyを抑止し、最後に全画面1枚だけを`MarkDirty`する。
- 実装例: `HomeScene`(ランチャ) / `MarkdownScene`(Markdownブラウザ。下記) / `ClocksScene`(時計。下記) / `InputTestScene`。
- ホスト側の検証: `sh script/host_test/run.sh`(実コードをPCのg+++ASanで動かし解放漏れを検出。実機ビルドとは独立)。

### ダイアログ (`src/gui/widgets/dialogs/`)
`WidgetFunctions`内で`dialog_roots`という独立リストで管理(当たり判定・描画順ともに最優先)。共通の骨格: 「`children_`ベクタで子を保持」「`setOnClosed(std::function<void(bool is_ok)>)`で結果通知」「`setVisible(false)`で自身を隠して終了」。**新規ダイアログを提案する際はこの型に合わせる。**

- **本文とボタンの配置は`dialogs/DialogLayout.hpp`で共通化**(MsgDialog/InputDialog、2026-10-02):
  - 本文は`ScrollContainer`で包んだ`Label`(512Bまで)。`FitDialog()`がまず既定の大きさ・大きい文字(24px)で置き、収まらなければ
    ①小さい文字(16px)→②ダイアログを大きく(幅を画面いっぱい`kMaxWidth`まで→高さを本文に合わせてステータスバーの下から
    画面の下端まで`kMaxHeight`)→③上限の大きさで本文の枠をスクロール、の順で収める(収まる間は枠もバーも出さない)。
    ダイアログの枠はメンバ`dlg`(画面座標)で持ち、`DialogLayout::Place()`が中央へ置く(ステータスバーには重ねない)。
  - ボタンは縦に積む。**文字が空(`""`/nullptr)のボタンは作らず、その分だけ本文の枠を広げる**。両方空だと閉じられなく
    なるので、そのときだけ1つ(「OK」/「決定」)を出す。`ScrollContainer::setSize()`/`kScrollBarWidth`はこのために足した。
  - `ErrorFunctions::ShowFatal()`はこれでボタン1つ(「閉じる」)になった(以前は同じ動きのOK/閉じるの2つで、アイコンと合わせて本文が1行しか入らなかった)。
- `MsgDialog`: メッセージ+アイコン+OK/キャンセル。`RenderMode::TRANSLUCENT`。
- `InputDialog`: ラベル+テキスト入力+決定/キャンセル(単一行/複数行切替可)。ボタンの文字はコンストラクタの3・4番目の引数で変えられる
  (既定「決定」「キャンセル」。Luaの`pico.show_input`も4・5番目の引数で)。
  決定/キャンセルの時点で`KeyboardFunctions::HideAll()`を呼ぶ(入力対象がこの後消えるため)。
- `FileSaveDialog`: `FileExplorer`+ファイル名`Textbox`+OK/キャンセル。**保存専用**。
- `FileSelectDialog`: `FileExplorer`+OK/キャンセルのみ。**選択専用**(ファイル名欄なし)。
  - ※旧設計では1クラスで兼用予定だったが、実装では保存/選択で別クラスに分離された。
- `EventDetailDialog`: カレンダーの予定1件の詳細。題名 + `ScrollContainer`で包んだ本文 + 閉じる。中身は`CalendarScene`が作る。
- `SearchDialog`: Markdownブラウザの検索結果。状態1行 + `ScrollList` + 再検索/次へ/閉じる。
  **通信はしない**(判断は`MarkdownScene`側)。結果は2回タップで開く。
- `PickerDialog`: 選ぶ・入れる系(Choice/Date/Time/Number/Progress)を1クラスにしたもの(2026-10-05。Luaの `pico.show_choice` 等が使う)。
- `ColorDialog`: 実装済み(直近コミット)。4×4=16色グリッド(`getIndexToColor(x,y)=x+y*4`)+OK/キャンセル。`selected_color`(未選択-1)、`getSelectedColor()`。
### オンスクリーンキーボード (`src/gui/widgets/keyboards/` / `dialogs/KeyboardDialog` / `functions/Keyboard_Functions`) (2026-09-29)

キーボードは**「キー盤」と「ダイアログ枠」に分かれている**。以前は3種とも全画面を覆う1つのウィジェットで、
背景の斜線・上部の入力欄(共有の`Label`)・キーを全部自分で描いていたが、テキストエディタで
キーボードを画面下に据え置いて本文へ直接書き込めるようにするため分離した。

- **キー盤**(`KeyboardPanel`派生の`Keyboard`(日本語フリック) / `KeyboardEng`(QWERTY) / `KeyboardNum`(数字))。
  `l_rect`は**画面下端の自分の領域だけ**(日本語151px / 英字112px / 数字140〜168px)で、`OPAQUE`。
  テキスト・カーソル・変換中の読みは自分で持ち、変わるたびに`notifyChanged()`→`KeyboardFunctions::OnPanelChanged()`。
  日本語の最上段を上へフリックしたときの候補の枠は、キー盤の外へはみ出さないよう候補の欄へ重ねて出す。
- **ダイアログ枠**(`KeyboardDialog`)。全画面の`TRANSLUCENT`で、背景の斜線(下へのタップを塞ぐ)と上部の入力欄だけを持つ。
  キー盤より先に`AddOverlay()`してあるのでキー盤がその上に重なる。変換中の読みを`~`で囲むのはこちらの仕事(`refresh()`)。
  日本語→英字のように**低いキー盤へ切り替えると、旧キー盤が覆っていた所に絵が残る**(TRANSLUCENTは下を描き直さない)ので、
  その1フレームだけ`CLEAR`として振る舞って下の画面ごと描き直させる(`redraw_below_frames`)。
- **開き方は`KeyboardFunctions::Show(target, layout, docked)`の1本**(`Textbox`/`NumberInput`もこれを使う)。
  `docked=false`(既定)なら従来どおりのダイアログ、`true`ならキー盤だけが画面下に出る(据え置き表示)。
  据え置き表示の入力先は`ITextInputTarget::onDisplayChanged()`を受けて自分の画面へ反映し、
  `KeyboardFunctions::VisibleTop()`で自分の表示領域を縮める。キー盤が閉じると表示方法はダイアログへ戻る
  (次に`OSData::keyboard_jpn->setVisible(true)`と直接開かれても据え置きにならないように)。
- **`ITextInputWidget`/`ITextInputTarget`に足したもの**: キー盤側に`getCursorByteOffset()`/`setCursorByteOffset()`/
  `getComposition()`、ターゲット側に任意の`onDisplayChanged()`/`onBackspaceAtStart()`(行頭で1文字削除)/
  `onCursorAtEdge()`(端でさらに←→)。後の2つは1行ずつキーボードへ渡す複数行の編集欄が、行をまたぐために使う。
- 日本語⇔英字の切り替えは`KeyboardFunctions::SwitchPanel()`。**`onShow`/`onHide`を挟まず**テキストとカーソルを引き継ぐ
  (以前は切り替えのたびにターゲットへ`onHide`→`onShow`が飛んでいた)。決定キーも`onHide`は1回だけになった
  (以前は決定キーが`onHide`を明示的に呼んだ上で`setVisible(false)`でもう一度呼んでいた)。
- ホストテストの各`*_test.cpp`は`KeyboardFunctions`の関数を個別にスタブしているので、`Keyboard_Functions.hpp`へ関数を
  足したら使われるものをスタブへも足すこと。`KeyboardNum.cpp`をリンクするテストは`KeyboardPanel.cpp`も要る。

キー盤それぞれの中身:

  - **入力位置は3種ともカーソル基準**(末尾への追記ではない)。挿入も削除(1文字戻し)もカーソルの位置で起きる。
  - 3種とも、カーソル位置の**真を持つのはキー盤側**(自分のテキスト上の文字インデックス)で、
    表示側へは`getCursorByteOffset()`のバイト位置として渡す(ダイアログの入力欄は
    `Label::setCursorToByteOffset()`へそのまま渡す)。
    **Labelのカーソルスロット番号を位置として使ってはいけない** — `**`や`~`のマークアップ記号は
    描画されずスロットも持たないため、元テキストの文字数とスロット番号は一致しない。
    (以前は`KeyboardNum`だけ位置をラベルのスロット番号で持っていたが、分離の際に他の2つと揃えた)
  - 移動キーの置き場所: `KeyboardEng`は最下段(`123` `かな` `←` `space` `→` `enter` [`go`])。
    **この行は20セル(1セル12px)を使い切っていて余りが無い** — 幅やラベルを変えると
    すぐ文字が枠線に重なるので、PCビルドの`--shot`で実際の描画を見て確かめること
    (ホストテストはフォントがスタブなので幅を検証できない)。`return`→`enter`、
    `ABC`→`abc`、シフト中もコマンドキーを大文字にしないのは全てこの幅の都合。
    `Keyboard`(日本語)はグリッドが埋まっているので、**変換中にしか働かない`カナ`/`送り`のセルを
    流用する** — 変換中でないとき(と数字モード)は同じセルが`←`/`→`として描かれ、そう振る舞う
    (`isCursorKeyCell()`。`改`/`行`が状況で`決定`/`改行`に変わるのと同じ仕組み)。
  - 日本語キーボードは**変換中の読みをカーソル位置へ挟んで**表示・確定する(`done_cursor`)。
    読みを囲む`~`は波線のマークアップで、**変換中でないときは囲みを出さない** —
    空の`~~`は取り消し線の開始として解釈され、カーソル以降の確定済みテキストに線が入るため。
    カーソル移動キーは読みが残っていれば先に確定させてから動く。
  - `KeyboardNum`は電卓向けの数字専用。「0〜9・カーソル移動・決定・削除」を常時固定で表示し、その上に`Digit`(数値入力の補助記号)/`Arith`(四則演算)/`Math`(√π e ^ % ±)の3タブで切り替わる記号行を載せる。`MODE_DIGIT | MODE_ARITH`のようなビットマスクで**使えるタブを呼び出し側から制限できる**(1つだけ許可ならタブ行自体が消えて1行詰まる)。
  - キー1つごとに`Button`を`new`せず、配列テーブル + `causeOnPressStart()`での座標判定で処理する(ヒープ節約。`AppGrid`/`ColorDialog`と同じ方式)。

### メモリ管理方針(重要・相談時の大前提)
- **基本は各ウィジェットが`new`で子生成、デストラクタで`delete`する素朴な方式。**
- **例外: `MarkdownView`だけは既にオブジェクトプール方式**: `labelPool`/`imagePool`/`checkboxIconPool`という固定長配列を起動時に一度だけ確保し、`boundXxxBlock[]`でスロット使用状況(-1=未使用)を管理、スクロールに応じて使い回す。→ 「事前確保→使い回し、全消去時は中身クリアのみ」構想の**実例プロトタイプ**。
- `WidgetFunctions::DestroyLater()` + `pending_deletes`: 非表示化→次フレーム末尾で`ProcessPendingDeletes()`によりまとめてdelete(フレーム途中delete事故防止)。SUMMARY.md「ウィジェットのメモリ解放」チェック済み項目に相当。
- **開発者はRAM断片化回避のため固定長バッファ/オブジェクトプールを志向している。新規実装で`new`/`delete`を安易に増やす提案より、MarkdownViewのプールパターンに寄せた提案を優先すること。**
- **`Widget::operator new/delete`が全ウィジェットの確保の唯一の入口**(`src/gui/widgets/Widget.cpp`)。現状は`malloc`を呼ぶだけで`MemFunctions`へ量を通知する。将来アリーナを入れる場合はここの実装を差し替えるだけで済み、`new Button(...)`のような既存コードは書き換え不要。
- **子ウィジェットの解放は親のデストラクタの責任**。`WidgetFunctions::ClearSceneWidgets()`は親を持たないルートしか`delete`しないので、子を`new`するウィジェットにデストラクタが無いと丸ごとリークする(過去に`MarkdownView`/`ScrollList`/`CanvasRaster`で発生)。
- ウィジェットIDベース管理(32bit: 種別enum/generation/index)は**発行側のみ実装済み**(`WidgetID.hpp`/`WidgetRegistry`、上記「ウィジェットID」参照)。OS内部のウィジェット間参照は従来どおり`std::vector<Widget*>`+生ポインタで、IDはあくまでLua等の外部向け。

### MarkdownView 実装詳細
`MdBlockType`: H1/H2/H3/Paragraph/Image/Link/CodeBlock/ListItem/HorizontalRule/Quote/TableRow。`MdBlock`はオフセット/長さ参照方式(`srcOffset`/`srcLength`、`doc_text`をコピーせず範囲参照)。固定上限: `kMaxBlocks=128`, `kMdMaxSourceBytes=8192`, `kMdBlockTextBytes=512`(1ブロックの表示テキスト上限。日本語で約170文字), `kLabelPoolSize=16`, `kImagePoolSize=2`, `kMaxListLevels=6`, テーブル最大列`kMdTableMaxCols=4`。`kMdBlockTextBytes`と`kMdMaxSourceBytes`はクラス外定義(クラス外に書くメンバ関数定義の戻り値型はクラススコープより前に解決されるため)。上限に当たった場合は`load()`が警告ログを出す。画像は`onRAM=false`でSDからストリーミング描画する(RAMに載せると占有量が開いた文書次第で青天井になるため)。テーブル/水平線/引用バーは`Label`を介さず`frame`へ直接描画(`renderDecorations()`)。リンクタップ用`on_link_tap`あり。フロントマターは`skipFrontMatter()`で読み飛ばし。**ヘッダー/フッター機能は現状なし。**

**文書内の参照(画像)は`doc_path`基準で解決する**(`resolveRef()`)。`load(path)`が`doc_path`を覚え、`layoutBlocks()`(画像ヘッダを読んで**ブロックの高さ**を決める)と`bindImageSlot()`(**表示用のパス**)の**2箇所**で使う。片方だけ直すと「高さは合うが表示されない」類のずれ方をするので必ず両方を通すこと。キャッシュがサーバのパスをミラーしているため、この1つの規則でローカルもリモートも足りる(View側にネットワークの知識は不要)。

### 文書キャッシュ (`src/storage/Doc_Cache.hpp`)
サーバから取った文書をSDへ書き、次回以降はSDから読むための層(`PROTOCOL.md`)。
**MarkdownViewは「SD上のファイルを開く」ことしか知らないままでよく、ネットワークとの境界がファイルシステムで切れる**のが狙い。

- 配置は `/cache/<正規化したホスト>/<サーバ上のパス>`。ハッシュ名にせずミラーするのは**FileExplorerでそのまま中身を覗ける**ようにするため。
- **ホスト名は`SanitizeHost()`で正規化してから使う**。FATでは`:`が使えず(ポート番号!)大小も区別しないため、小文字化して`: * ? < > | " \ /`と制御文字を`_`へ潰す。
- **`Writer`が本体**: 一時ファイル(`.part`)へ書き、`commit()`で初めて本来の名前へ差し替える。`commit()`せずに破棄されるとデストラクタが一時ファイルごと消す。**通信が途中で切れた半端なファイルを「正常なキャッシュ」として残さない**ためで、これがこのモジュールの存在理由。
- `kMaxEntryBytes=64KiB`で頭打ち。相手のサーバが何を返してきてもSDを埋め尽くさないようにする(`PROTOCOL.md`の「巨大なレスポンス」対策)。
- 目録は `/cache/index.tsv`(行指向TSV、1行1文書): `host / path / validator / fetched_epoch / size`。書き換えは**元を読みながら一時ファイルへ書き写して最後に差し替える**ので、目録全体をRAMへ載せない(`Config_Functions::SetValue()`と同じ手順)。
- `fetched_epoch`は**参考値**。NTP同期前の時計は当てにならないので、鮮度の判断は`validator`(ETag)で行う。
- **追い出し(LRU等)は実装しない。** 1文書8KiBに対しSDはGB単位あり、1000件貯めても8MB程度なので枠を管理する対価に見合わない。全消去の`Clear()`だけ用意してある。
- **`Clear()`だけはホストテストで検証できていない**。ディレクトリの再帰削除に`isDir()`/`openNext()`が要るが、`script/host_test/stubs/SdFat.h`はパス→内容のフラットな`map`でディレクトリの実体が無いため。PCビルド(`pc/compat/SdFat.h`は実ファイルシステム)側で確かめること。

### HTTPクライアント (`util/Url.hpp` / `net/Http_Response` / `task/Http_Get`)
`PROTOCOL.md` のHTTPを喋る側(http/https。HTTPSは下の「HTTPS」参照)。3つに割ってあるのは**テストできる形にするため**。

- **`Url`**: `http://host:port/path?query` を分解して持つ型。`"http://"` を各所で`strncmp`しないための入れ物で、**schemeをここに閉じ込めてある**(実際にHTTPS対応で触ったのは接続処理=`net/Http_Transport`だけで済んだ)。`path`は`PICO_STR_LL`(Googleカレンダーの非公開URLのパスが96Bを超えるため)。パスとクエリを分けて持つのは、相対解決(`PICO_IO::resolve`を再利用)がクエリ内の`/`まで畳んでしまわないようにするため。
- **`HttpResponse`**: **ソケットを持たない**増分パーサ。受信したバイト列を`feed()`へ渡すだけなので、ネットワーク無しに全経路をホストテストできる(`http_test.cpp`は1バイトずつ食わせた場合も同じ結果になることまで見ている)。見るヘッダは`Content-Length`/`ETag`/`Last-Modified`/`Location`/`Transfer-Encoding`だけ。**chunkedは解いて本文だけをシンクへ渡す**(2026-09-23。以前は検出したらエラーにしていたが、HTTPSで繋ぐ一般のサーバは動的な応答をchunkedで返す)。chunked以外の転送符号化(gzip等)はエラー。**読まないヘッダは長すぎても読み飛ばす**(Googleは`Set-Cookie`/CSPで`kMaxLineLen=256`を平気で超える)。中身を使うヘッダ(Location等)とステータス行だけは切れていたら失敗にする。本文の行き先は`IHttpSink`で差し替える。
- **`HttpGet`**: `Task`派生。`Connection: close`を送り、`Accept-Encoding`は送らない。リダイレクト最大3回、全体10秒で打ち切り。手元の検証子を渡すと条件付きGETになる(`GMT`を含むかで`If-None-Match`と`If-Modified-Since`を出し分ける — 目録が「どちらのヘッダで来たか」を覚えていないための割り切り)。**3xx/4xxの本文はシンクへ流さない**(`BodyGate`が200を見てから開く)のでキャッシュが汚れない。
- **接続(`connect`)だけは同期的**。到達しない相手を指すと最大`kConnectTimeoutMs=3000`ぶん画面が止まる。受信は全てポーリングなので、繋がってしまえばフレームは止まらない。非同期接続にはlwIPを直に叩く必要があり、別の段の仕事。
- PC側の`WiFiClient`は`pc/compat/WiFiClient_PC.h`にある(TLS版`WiFiClientSecure`はその派生で`WiFiClientSecure_PC.h`。そのため主要メソッドは仮想関数)。**Wi-Fiの「状態」(`pc/compat/WiFi.h`)は偽物のままだが、通信そのものは本物のソケット**。SDにもLovyanGFXにも依存しないので、ホストテスト(`script/host_test/stubs/WiFi.h`)からも**同じ実装**を使う(通信経路のテストで別物を使っては意味が無いため)。

### HTTPS (`src/net/Http_Transport` / `Tls_Roots_Data.hpp`) (2026-09-23)

`HttpGet`(文書/カレンダー)と`HttpRequest`(Luaの`pico.http_request`)の下の「繋ぐ」部分を
`HttpTransport`へ切り出し、URLが`https://`ならTLSで繋ぐ。schemeを見て分岐するのはここだけ。
Markdownブラウザ・Lua・カレンダーの3つとも、これで`https://`を扱える
(以前は`url.secure`を見て各所で「httpsは未対応」と断っていた)。

- **実機はarduino-pico同梱のBearSSL**(`BearSSL::WiFiClientSecure`/`X509List`)。新しいライブラリは足していない。
  **TLS 1.2まで**(BearSSLの上限)なので、TLS 1.3だけを受け付けるサーバには繋がらない。
- **PCはOpenSSL**(`pc/compat/WiFiClientSecure_PC.h`)。実機と同じ呼び方に揃え、**わざとTLS 1.2に固定**し、
  **信頼するのも同じルートだけ**(OSの証明書ストアを見ない)。「PCでは繋がるのに実機では繋がらない」を防ぐため。
  PCビルドは`libssl-dev`が要る(`find_package(OpenSSL)`)。**Webビルドは常に繋がらないスタブ**
  (生のソケットが無いので、そもそもTLSも無い)。
- **信頼するルート**は`script/generate_tls_roots.py`が母艦の`/etc/ssl/certs`から`src/net/Tls_Roots_Data.hpp`へ焼き込む
  (GTS Root R1/R4=Google、ISRG Root X1/X2=Let's Encrypt、DigiCert Global Root G2/CA、USERTrust RSA=Sectigo、
  Amazon Root CA 1=AWS(api.todoist.com。2026-10-01追加)の8枚)。**`ROOTS`は末尾へ足すこと** — 母艦のストアから消えたルート
  (DigiCert Global Root CAは既に無い)は、今焼き込まれている同じ位置の証明書を引き継いで生成し直す。
  足りない相手(自己署名の自前サーバ等)はSDの**`/sys/tls/ca.pem`**へPEMを置けば足される。
  **フラッシュに置くだけで、RAMへ展開するのは接続中だけ**(1枚あたり約1.5KB)。
- **TLSの道具一式は`connect()`で確保し`close()`で返す**。BearSSLは受信バッファ16KB+専用スタック6.4KB+
  ルートの展開等で**接続中だけ約40KB**を使う。常駐させると使わないアプリにまで負担させるため。
- **時計が合っていない(NTP同期前、2020年より前)と繋がない**(`Error::ClockNotSet`)。証明書の有効期限を
  確かめられず、実機では全ての証明書が「まだ有効でない」になって理由が分かりにくいため。
- **TLSのハンドシェイクは`connect()`の中で同期的に進む**(素のTCPの接続と同じ制約)。実機で1〜2秒程度の見込みで、
  その間は画面が止まる。**実機での実測はまだ**(このリモート環境にはRP2350のボード定義が無く、実機ビルドもできない)。
- 失敗の理由は`HttpGet::Fail::ClockNotSet`/`TlsFailed`で分かり、`failureToStr()`はTLSライブラリの理由も付けて返す。
- 検証は`run_net.sh`の`calendar_sync_test`(使い捨てのCAを`openssl`で作り、`script/host_test/tls_test_server.py`を
  相手に、chunked転送・ETag→304・信頼していないCA・名前の違う証明書を本物のTLSで確かめる)。

### 取得〜表示の配線 (`src/net/Doc_Fetch.hpp`)
「URLを1本取ってきて、SD上の開けるパスにする」係。`Http_Get`(取得)と`Doc_Cache`(保存)を繋ぐだけの薄い層だが、**ブラウザとして必要な判断はここに集めてある**。

- 手元にキャッシュがあれば検証子を添えて条件付きGETし、**304ならそのまま使う**(`Writer`は`abort()`するので本体に触らない)
- 200なら一時ファイル経由でキャッシュを差し替えてから使う
- **取得に失敗しても、古いキャッシュがあればそれを開く**(圏外でも読める)。黙って古い内容を出すと更新が反映されていないように見えるので、`Source::CacheAfterError`で呼び出し側へ伝え、`MarkdownScene`はフッタへ「オフライン表示(保存済み)」と出す
- キャッシュのキーは**ポートまで含めたホスト**(`host:port`)。ポートが違えば別のサーバとして扱う
- `MarkdownView`へ渡すのは常にSD上のパスなので、**View側はネットワークの存在を知らない**

### サーバ情報 (`src/net/Discovery.hpp`)
`GET /.well-known/pico-os` の中身(`ServerInfo`)。`PROTOCOL.md`「サーバ情報」参照。

- **discoveryを特別扱いしない。** 置き場所が決め打ちなだけで、取得も保存も`Doc_Fetch`/`Doc_Cache`をそのまま通す。おかげで**条件付きGET(304)も、圏外のときに前回の内容を使うことも、何も書かずに手に入る**。
- **知らないキーは無視する**(`PROTOCOL.md`の前方互換ルール)。サーバが将来キーを足しても古いpico-osが壊れない。`discovery_test.cpp`で固定してある。
- **`search`の行が無い = 検索非対応**。`hasSearch()`で判定し、対応していないサーバでは検索UIを無効にする。
- **404は正常な結果**。「素の静的ファイルサーバと分かった」という確定した答えなので、`checked`を立ててページごとに問い合わせ直さない。
- `MarkdownScene`は**ホストが変わったときだけ**問い合わせる。`home`が申告されていればヘッダの「ホーム」ボタンの行き先になる(無ければ`network.cfg`の`browser-home`)。

### 検索 (`src/net/Doc_Search.hpp`)
`GET <discoveryのsearch>?q=...&limit=10&offset=N` の応答(1行1件のTSV)を読む。`PROTOCOL.md`「3. 検索」参照。

- **検索だけは`Doc_Cache`を通さない。** キャッシュ上の置き場所は**URLのパスだけで決まりクエリを見ない**ため、
  通すと検索語違いの応答が同じファイルへ重なり、条件付きGETで別の検索語の304まで起きる。
  応答は高々20行なのでRAMへ載せる(`SearchHit`が1件160B × `kMaxHits=10`)。
- 読むのは**1列目(path)と2列目(title)だけ**。`version`/`snippet`は画面で使わないので読み飛ばす(前方互換)。
  **pathは`/`始まりのサーバ絶対パスでなければ捨てる**(何を基準に解決するか決まらないため)。
  pathが収まらない行も捨てる(別の文書を指してしまう)。titleは表示専用なので切り詰まってよい。
- 検索語は`UrlTools::EncodeComponent()`でパーセントエンコードする。**日本語1文字が9バイトになる**ので、
  `Url::query`は`PICO_STR_L`ではなく`PICO_STR_LL`にしてある(`RequestTarget()`の受けも`PICO_STR_256B`)。
- 総件数は返ってこないので、**要求ちょうどの件数が返ったら「続きがあるかもしれない」**と見なす
  (`mayHaveMore()`)。「次へ」は`offset += kMaxHits`で引き直す。
- **404は失敗**(discoveryは404が正常な結果だったが、こちらは違う)。
  `search`の行が無いサーバへは`begin()`が接続すら試さない。

UI側は`gui/widgets/dialogs/SearchDialog`(状態1行 + `ScrollList` + 再検索/次へ/閉じる)。
**通信は一切せず**、何件目から取るかの判断も含めて`MarkdownScene`が持つ。結果は**2回タップで開く**
(`ScrollList`の流儀。1回目は選択)。

### マニフェスト (`src/net/Manifest.hpp`)
`GET <discoveryのmanifest>` の中身(1行 `path<TAB>version`、**pathの昇順**)。`PROTOCOL.md`「4. マニフェスト」参照。

- **狙いは「開くたびの条件付きGETを省く」こと。** 手元の検証子とマニフェストのversionが一致すれば、
  その文書は**サーバへ何も聞かずに開ける**(304の往復すら要らない)。
  `DocFetch::begin()`が`Source::Manifest`で即`Ready`になる。
- 取得と保存はdiscoveryと同じく`Doc_Fetch`/`Doc_Cache`をそのまま通すので、**マニフェスト自体も304になる**。
  `MarkdownScene`はdiscoveryの直後(=ホストが変わったとき)と、**更新ボタンを押したとき**に引く。
- **一致しない/載っていない/サーバが非対応なら、今までどおり条件付きGETへ落ちる。**
  つまり**あれば速くなるだけ**で、無くても挙動は変わらない。
- 引き当ては`Manifest::VersionOf()`が**pathを追い越した時点で打ち切る**(昇順という取り決めの使いどころ)。
  **検証子が`FixedString`に収まらない行は「分からなかった」扱い**にする — 切り詰めて比べると別物を同じと見なす。
- **滞在中にサーバ側が更新されても気づけない**(次にそのホストへ来るまで引き直さないため)。
  そこを埋めるのが更新ボタンで、押すと`manifest_stale`が立ってマニフェストごと引き直す。
- `DocFetch::setManifest()`は**ホストごとの設定**なので`begin()`/`cancel()`では消えない。
  ホストが変わったとき(`openCurrent()`/`abortNavigation()`)に明示的に捨てる。

### ブラウザのヘッダー

`[<][>][ホーム][更新][検索]` … [終了]。左側は**各ボタンの実測幅で左から詰めて並べる**
(日本語の文字幅はフォント任せなので、`<`/`>`のように字が細いものへ最低幅を与えるだけにしてある)。
「終了」だけ右端。

- **更新** = キャッシュを無視して取り直す。検証子を送らないので200が返り、キャッシュごと差し替わる。
  **挿絵も引き直す**(文書だけ新しくて絵が古いままにならないように)。履歴は積まず、スクロール位置も保つ。
  `bypass_cache`フラグが`DocFetch::begin(url, bypass_cache)`まで届く仕組みで、`commit/abortNavigation()`で下りる。
- **検索** = 押せるのは「リモートの文書を開いていて、そのサーバがsearchを申告している」ときだけ
  (`currentServerCanSearch()`)。

**ダイアログからダイアログへ移るときは1フレーム空けること**(`MarkdownScene::Pending`)。
`PICO_GFX::FlushDirty()`は**TRANSLUCENTなウィジェットの下を描き直さない**(半透明の下は変わらない前提の最適化)
ため、同じフレームで次のダイアログを開くと、閉じたキーボードや前のダイアログの跡がその下に残ったままになる。
1フレーム空ければ「ダイアログが何も無い状態」で描き直される。

### iCalendarの読み取り (`src/calendar/Ical.hpp`) (2026-09-23)

カレンダーアプリの第1段。**Google CalendarのOAuth/APIではなくiCal(.ics)を選んだ**
(OAuthはHTTPS・トークン管理・JSONパーサが全て要り、いずれも今のコードに無い。
iCalは行指向のテキストで、Googleも「iCal形式の非公開URL」で同じものを出す)。
当初は「Google側のURLはHTTPSなので母艦のサーバがHTTPで中継する」予定だったが、**arduino-picoにBearSSL(`WiFiClientSecure`)が同梱されていると分かり、本体でHTTPSに対応した**(2026-09-23。下の「HTTPS」参照。取得は`CalendarSync`)。

- **`Ical::Parser`はバイト列を好きな切れ目で受け取る**(`HttpResponse`と同じ増分方式)。
  行の折り返し(次の行頭の空白)もここで畳む。`ParseFile()`はSDから256Bずつ読んで食わせるだけ。
  **`IcalCalendar`へ追記する**(clearしない)ので、複数の.icsを1つへ合成できる。
- **時刻は全て現地時刻へ揃えて持つ**(`IcalTime{day=1970-01-01からの通算日数, sec=0時からの秒/-1で終日}`)。
  `...Z`は`Options::utc_offset_sec`でずらし、`TZID=`付きは現地時刻とみなす(VTIMEZONEは解釈しない。サマータイムも無視)。
- **`Options::window_from_day/to_day`で窓の外の予定を読み捨てる。** Googleの非公開URLは**過去の予定を全部**
  返すので、窓を切らないと`kMaxEvents=64`(約16KB)がすぐ昔の予定で埋まる。
- **RRULEは一部対応**: DAILY/WEEKLY/MONTHLY/YEARLY + INTERVAL/COUNT/UNTIL/BYDAY/WKST、
  MONTHLYの「第n曜日」(1〜5, -1)、DTSTARTと食い違わないBYMONTHDAY/BYMONTH。
  **それ以外(BYSETPOS等)は`rule.supported=false`にして初回だけ出す**(間違った日に出すよりまし)。
- **引き当て(`StartsOn()`/`OccursOn()`)は日ごとの算術で、回を1つずつ展開しない。** 月表示の42マス×64件を
  毎回引いても軽いように。COUNTが絡み、かつ「存在しない日」(31日の無い月・2/29・第5週)が
  あり得る場合だけ周期をループで数える(RFC 5545どおり存在しない日はCOUNTに数えない)。
- **例外**: EXDATEと、RECURRENCE-ID付きの上書き予定。後者は`finish()`で親(同じUIDの32bitハッシュ)の
  `exdates`へ畳み込み、上書き側は単発の予定として残す(STATUS:CANCELLEDなら残さない)。
  除外は「日」で持つ(対応する繰り返しは1日1回までなので足りる)。1件あたり`kMaxExDates=8`まで。
  **覚えるのは読み込みの窓(の`kMaxSpanScan`日前から)にかかる例外だけ**(2026-09-23)。Googleは何年分もの
  例外を全部書いてくるので、以前は長く続く定例の`exdates`が昔の分で埋まり、窓の中の「削除した回/移動した回」が
  元の日にも出ていた。上書き予定の控え(`kMaxOverrides=32`)も同じ理由で窓の近くだけ。
- VEVENTの**直下だけ**を読む。VALARMにもSUMMARYがあり、拾うと予定名が通知文で上書きされる。
- ホストテストは`script/host_test/ical_test.cpp`(RFC 5545のWKSTの例、第n曜日、31日/2/29の飛ばし、
  1バイトずつ食わせた場合、UTF-8の途中での折り返し等)。
- `EventsOn(cal, day, out, max)`が1日ぶんの予定を「終日と前日からの続きが先、残りは開始時刻順」で並べる(一覧の表示順)。
- **説明文(DESCRIPTION)は持たない**(64件ぶん持つとRAMを食う)。各予定は`file_index`(どの.icsか)と
  `ordinal`(そのファイルで何番目のVEVENTか。読み捨てた分も数える)だけを持ち、詳細を開くときに
  `ReadDescription(path, ordinal, out)`でその1件だけ読み直す(`Parser`の「予定を集めず説明文だけ拾う」読み方。
  目当てのVEVENTを読み終えたらファイルの残りは読まない)。説明文は1論理行の上限(512B)で切れる。
- 取得は`calendar/Calendar_Sync`(下の「CalendarScene 実装詳細」参照)。

### CalendarScene 実装詳細 (2026-09-23)

`[戻る] … [<] 2026年9月 [>] [今日]` + 月の格子(`MonthGrid`) + 選んだ日の予定一覧(`ScrollList`)の1画面。

- **SDの`/calendar/`直下の`*.ics`を全部読んで1つの`IcalCalendar`へ重ねる**(`PICO_Path::DIR::CALENDAR`)。
  Googleのカレンダーごとの非公開URLを1ファイルずつ置く想定。
- **読むのは表示中の格子(前後の月の空きマスを含む6週間)にかかる予定だけで、月を移るたびに読み直す**
  (窓を切らないと`kMaxEvents`が過去の予定で埋まるため)。`onEnter()`でも毎回読み直す
  (別のアプリで書き換わっているかもしれないので)。読み直しは同期で、大きな.icsだと一瞬止まる。
  重くなったら`Word_Dict`のようにフレーム分割する。
- **シーン本体は約18KB**(`IcalCalendar`約16KB + 取得用の`CalendarSync`約2.4KB をメンバに持つ)。`MarkdownScene`と同じく「シーン本体は数十バイト」の例外。
- **`utc_offset_sec`は`LocalUtcOffsetSec()`が`localtime_r`と`gmtime_r`の差から求める**(newlibに`tm_gmtoff`が無いため)。
  TZは`network.cfg`の`timezone`(`TimeFunctions`が`setenv("TZ")`済み)。
- **NTP同期前(2020年より前)は「今日」が分からない扱い**で、仮に1970年1月を出す。`onUpdate()`が時計が合った
  時点で今日の月へ飛ぶ(PCビルドでも起動直後の数フレームはこの状態を通る)。0時を回ったら今日の印だけ動かす。
- 一覧の時刻欄は「終日」「09:30-10:30」「22:00-」(翌日へまたぐ)「02:00まで」(前日からの続きが今日終わる)「(続き)」。
  **`~02:00`にしないのは、16pxフォントの`~`が上線のような形で読めないため**(PCビルドの`--shot`で気づいた)。
- 一覧は`ScrollList`なので**長い予定名は右で切れる**(折り返さない)。**2回タップで詳細**(`dialogs/EventDetailDialog`)が開き、
  題名・日時(日をまたぐ回は始まりと終わり)・場所・繰り返し・カレンダー名・説明文を全文スクロールで読める
  (本文は`DictScene`の詳細欄と同じく`ScrollContainer`+`Label`)。
- **複数のカレンダーを重ねると色分けする**。`/calendar/*.ics`を**ファイル名順**に並べて`file_index`を振り、
  `kCalendarColors`(白地で読める濃い8色)から色を決める。**名前順にするのは、SdFatの列挙順が「作った順」で、
  取得のたびにファイルを差し替えると入れ替わる(=色が変わる)ため**。格子の点(`MonthGrid::setDots()`。
  違うカレンダーの色を先に並べ、余った点は同じ色を繰り返す)と一覧の文字色(`ScrollListTools::Item::color`)に使う。
  .icsが1つだけなら今までどおり(緑の点・黒い文字)。読む.icsは8つまで。
- **取得(`calendar/Calendar_Sync`)**: `/calendar/sources.cfg`の「名前 = URL」を1件ずつ取り、`/calendar/<名前>.ics`へ置く
  (`webcal://`は`https://`として扱う)。**`Doc_Fetch`/`Doc_Cache`は通さない** — キャッシュは1件64KiBで頭打ちなのに
  Googleの非公開URLは過去の予定を全部返して数百KBになる上、`/cache/<ホスト>/<パス>`へミラーすると非公開URLの
  秘密の部分がディレクトリ名としてSDに散らばるため。上限は1件1MB。
  - **取得に失敗しても手元の`.ics`は壊さない**: `.ics.part`へ書き、200で最後まで読めて**先頭が`BEGIN:VCALENDAR`**のとき
    だけ差し替える(ログイン画面のHTMLを返されても上書きしない)。ETagは`<名前>.etag`に覚えて次から条件付きGET(304)。
  - 名前はファイル名になるので英数字と`_ -`だけ(20文字まで)。値が`Config_Functions`の上限(128B)を超えるので
    `ParseLine()`だけを借りて自前の512Bバッファで読む。
  - `CalendarScene`は**ランチャから開いたとき1回だけ自動で取りに行く**(Wi-Fiに繋がっているときだけ。
    `Push()`から戻っただけでは取り直さない)。**1回描いてから**始めるので、TLSのハンドシェイクで止まる前に
    ボタンの「取得中」が見える。[更新]ボタンで手動でも取れる。失敗が残ればボタンが赤の「再試行」になり、
    表示は前回の`.ics`のまま。中身が変わったものがあったときだけ読み直す(全部304なら何もしない)。
  - `onExit()`で取得は打ち切る(`onUpdate()`が来なくなるため)。
  - Web版は取りに行けない(HTTPSのスタブが常に失敗する)。
- `ScrollListTools::Item`に**項目ごとの文字色`color`(-1で一覧の色)**を足した(カレンダーの色分けのため。汎用の拡張で、選択中の反転表示が優先)。

`MonthGrid`(`widgets/apps/`)は「子を持たずrender()で直接描き、タップ位置から逆算する」型。
日曜赤・土曜青、今日は赤の二重枠、選択中は黒塗り+白抜き、予定のある日は数字の下に点(最大3つ)。
**予定そのものは知らず、シーンが`setMonth()`/`setCounts()`で流し込む**(`AnalogClock`と同じ理由)。
幅を7で割った余りは土曜の列へ足す(`TabBar`と同じ)。「2026年10月」は84pxで2行へ折り返したのでタイトル幅は100px。

### チャット (`src/chat/` / `ChatScene` / `server/chat/`) (2026-09-24)

SUMMARY.md #8の「チャットツール」。**Discordとの連携は見送り、自前のサーバにした。**
Discordは自分のアカウントでの自動操作(self-bot)が規約違反で、Bot名義での発言にしかならない上、
Pico側にJSONパーサ・WebSocket(即時受信したい場合)が要る。自前なら`PROTOCOL.md`と同じく
「pico-osが苦手なことは全部サーバでやる」設計にできる。仕様は`CHAT_PROTOCOL.md`。

- **サーバ**: `server/chat/chat_server.py`(Python標準ライブラリのみ、SQLite)。Raspberry Piで動かし、
  **Let's Encryptの証明書でHTTPS化**する(手順は`server/chat/README.md`)。
  - 80番でACME(http-01)の確認ファイルを配り、それ以外はhttpsへ転送する(`--http-port`/`--acme-root`)。
    **証明書がまだ無ければ置かれるまで80番だけで待つ**ので、初回もcertbotの`--webroot`で取れる
  - **証明書はファイルの更新時刻を見て、次の接続から読み直す**(certbotの自動更新後に再起動が要らない)。
    certbotの`--deploy-hook`(`certbot-deploy-hook.sh`)が`/etc/pico-chat/tls/`へ写す
  - 認証は、pico-osは`Authorization: Bearer <トークン>`、WebはCookie + 書き込みに`X-Pico-Chat`ヘッダ必須。
    トークン/セッションはハッシュだけを保存。トークンはWebの「pico-os の設定」で発行し直せる
  - 未読の数は人ごと・部屋ごとにサーバが覚える(発言を取った分が既読になる。Picoとで共通)
- **Webクライアント**: `server/chat/web/index.html`(1ファイル、同じサーバが配る)。ロングポーリング(`wait=25`)で即時受信
- **pico-os側**: `ChatScene`(部屋の一覧 ⇔ 部屋の中を1シーンで切り替え、`applyMode()`)。設定は`/sys/chat.cfg`
  (`server = ...` / `token = ...` / 任意で`poll-ms`)
  - `chat/Chat_Proto`: 1行1件のTSVの読み取り(本文だけ`\n` `\t` `\\`をエスケープして送る)と行の切り出し(`LineSink`)。ソケットを持たない
  - `chat/Chat_Client`: `HttpRequest`で問い合わせる係。**同時に1本、優先順は送信 > 開いている部屋の新着(3秒ごと) > 部屋の一覧(10秒ごと)**。
    **ロングポーリングにしない**のは、接続が1本しか無く、待っている間は送信できなくなるため。失敗は5秒→60秒で取り直し、401は取り直さない
  - **`HttpRequest`にkeep-alive(`setKeepAlive()`)と任意のヘッダ1行(`setExtraHeader()`)を足した**。HTTPSはハンドシェイクで
    1〜2秒画面が止まるので、使い回さないと数秒ごとに固まる。**使い回している間はTLSの約40KBを持ち続ける**(`ChatScene::onExit()`で閉じる)。
    使い回すのは、応答が長さ付きで`Connection: close`でない場合だけ(`HttpResponse::canReuseConnection()`)。
    接続が死んでいたら(`begin()`での`connected()`確認、または1バイトも来ずに閉じた場合)1回だけ繋ぎ直す。既定は無効なので、
    Luaの`pico.http_request`やMarkdownブラウザの挙動は変わらない
  - `widgets/apps/ChatLogView`: 発言の一覧。**「子を持たずrender()で直接描く」型**。折り返しの計算(1文字ごとの`textWidth`)は
    **発言1件につき1回だけ**で、高さをidごとに覚える(新着1件で全件を測り直さない)。一番下を見ている間だけ新着に付いていく
  - **シーン本体は約25KB**(`ChatClient`が発言30件ぶん約17.5KB + 部屋の一覧 + 行バッファ + `HttpRequest`約2KBを持つ)。`MarkdownScene`/`CalendarScene`と同じ例外
  - **Picoから送れるのは1回に約60文字**(オンスクリーンキーボードの入力欄が`Label<PICO_STR_LL>`=191バイトのため)。
    受け取るのは500バイトまで
  - 本文は`Label`を通さず直接描くので、`*`や`~`がマークアップとして消えることは無い
  - 部屋の一覧は参加している部屋だけ(下の「部屋の種類・参加コード・役割」)
- **部屋の種類・参加コード・役割(2026-09-27、プロトコルv2)**: Discord風に**オープンチャット**(検索に出て誰でも参加)と
  **プライベートチャット**(検索に出ず、参加していない人には有ることも404で隠す。参加者が発行する**参加コード**でだけ入れる)の2種類。
  一覧・発言の読み書きは参加している部屋だけ。仕様は`CHAT_PROTOCOL.md`「部屋の種類」「参加コード」「役割と権限」
  - 参加コード: 8文字(`0O1I`抜きの32種=40bit、`XXXX-XXXX`)、**30分で無効**、任意で回数(1回きり等)。**参加者なら誰でも発行でき**、
    管理者以上は全部無効化できる。サーバはハッシュだけ持つ。外し続けると(人/IPごとに10分10回)429。
    「無い/期限切れ/使い切り/追放中」は区別せず404(総当たりの手がかりを与えない)。入力は小文字・区切り無し・全角でも通る(NFKC)
  - 役割: 部屋ごとに owner(1人)> admin > member。権限の表はサーバの`PERMS`1か所(`can(role, action)`)。追い出し(kick)と
    追放(ban。検索からもコードからも戻れない)は自分より弱い人だけ。オーナーが抜けたら一番強く古い人が継ぎ、最後の1人が抜けたら部屋ごと消す
  - **DBは`PRAGMA user_version`で版を持ち、v1のDBは起動時に自動移行**(roomsを作り直して`name`のUNIQUEを外し、
    オープンチャットの名前だけ部分インデックスで重複禁止。既存の部屋は全部オープン、その時点のユーザーは全員を参加者にする)。
    外部キー(ON DELETE CASCADE)があるので、作り直しは`foreign_keys=OFF`の間に行う
  - pico-os側: 一覧はアイコンで区別(吹き出し=オープン、鍵=プライベート)。下の[部屋を探す](`InputDialog`→検索結果の画面`Mode::Search`、
    タップで参加して開く)と[コードで参加]、プライベートチャットの中では上部の[招待]で参加コードを`MsgDialog`に出す
    (期限は時計が合っていれば「13:59まで有効」)。部屋を作る・抜ける・メンバー管理はWebだけ。
    `ChatClient`は利用者の操作(検索/参加/招待)を1つだけ持ち(`pending_action_`)、優先順は送信の次。結果は`actionRevision()`で知らせる。
    開いている部屋の新着取得が403/404になったら「追い出された/部屋が消えた」として部屋を閉じ`roomLostRevision()`を進める(画面は一覧へ戻す)
  - Web: サイドバーをオープン/プライベートに分け、探す/参加コード/作る(種類を選ぶ)、部屋のヘッダに招待・メンバー(役割の変更・
    追い出し・追放・削除)・退出
- **サーバ管理者(2026-09-27、プロトコルv3)**: `users.is_admin`(何人でも)。ユーザーの作成/パスワードの決め直し/無効化/管理者の任命、
  全部屋の一覧、サーバ設定(`settings`表。今は`room_create = all|admin`)。部屋では**管理の操作(`MODERATION_ACTIONS`)だけ**、
  参加していなくてもオーナーより強い立場(`SERVER_ADMIN_ROLE`、`role_rank()`で4)で行える。**発言の読み書き・参加コードは
  管理者でも参加が要る**(プライベートを黙って読めないように)。自分を外す/無効にするのは不可(管理者が0人になるのを防ぐ)。
  最初に`adduser`したユーザーが自動で管理者、v2からの移行では誰も管理者にしない(起動ログで`admin <名前>`を促す)。
  Webは「サーバ管理」ダイアログ(ユーザー・全部屋・設定)。部屋の「管理」はメンバーのダイアログ(`openMembers()`)を
  参加していない部屋にも開く形で使い回す。pico-os側の変更は無い(`/api/v1/me`の3・4列目も読まない)
  - ついでに直したもの: `Store.write()`が失敗した文(重複のINSERT等)の暗黙のトランザクションを残し、DBの書き込みの鍵を
    持ったままになって他のスレッドの書き込みが全部10秒待たされていた(`rollback()`するようにした)
- 検証: `chat_proto_test`/`chat_scene_test`(run.sh)、`chat_server_test.py`(run_net.sh。サーバを同じプロセスで立て、部屋の種類・参加コードの
  期限/回数/無効化/総当たり・役割ごとの可否・譲渡と退出・サーバ管理者の権限の範囲・v1のDBの移行を確かめる)、`chat_net_test`(run_net.sh。本物の`chat_server.py`を平文と使い捨てCAのHTTPSで立て、
  送受信・未読・keep-alive・無通信で切られた後の繋ぎ直し・401・検索と参加・参加コード・開いている部屋が消えたときを確かめる)、
  PCビルドの`--tap`で一覧→部屋→キーボード入力→送信まで、v2では一覧のアイコン・検索→参加・招待のダイアログを確認した。
  WebクライアントはChromium(Playwright)で作成/検索/参加コード/メンバー管理(追放すると相手の画面から部屋が消える)を確認した。
  **実機(RP2350 + BearSSL)とLet's Encryptの本物の証明書での接続は未確認**

### TODOアプリ(Todoist連携) (`src/todo/` / `TodoScene` / `util/Json_Reader`) (2026-10-01)

ランチャの「TODO」。**Todoist(https://todoist.com)のタスクを見る・足す・完了にする + 時刻つきの期限をOSの通知で知らせる**。
連携先の比較(Google Tasks/Microsoft To Do=OAuth必須、iCloud=CalDAVのXML、Notion=応答が大きい)の結果、
**個人用のAPIトークン(Bearer)だけで使えるTodoist**にした。OAuthは使わない。

- **API は v1**(`https://api.todoist.com/api/v1/`。旧REST v2/Sync v9は廃止の流れ)。使うのは
  `GET /tasks`(すべて)・`GET /tasks/filter?query=`(今日=`today | overdue`、7日間=`overdue | next 7 days`)・
  `POST /tasks`(`{"content","due_string","due_lang":"ja"}`)・`POST /tasks/<id>/close`(204)。一覧は
  `{"results":[...],"next_cursor":...}`のページ分けで、`kMaxTasks`(30)件に達するまで続きを取る(上限`kMaxPages`=8)。
  フィルタは`todoist.cfg`の`today-filter`/`week-filter`で差し替えられる(英語で書けば利用者の言語設定に依らず通る)。
- **JSONは流しながら読む(`util/Json_Reader`、SAX型)**。応答は1件約800バイト(user_id/added_at/order_key…)あり、30件で20KBを超えるが、
  RAMに載るのはタスク1件ぶんの読みかけと値のバッファ(256B)だけ。`Todoist::TaskParser`が`results[].{id,content,priority,
  checked,is_deleted,parent_id,due.{date,string,is_recurring}}`だけを拾う。文字列は`\uXXXX`/サロゲートペアをUTF-8へ戻し、
  長いものは文字の途中で切らずに切り詰める。**C++側に汎用のJSONパーサが入ったのはこれが初めて**(他で要るときもこれを使う)。
- **期限は現地時刻へ揃えて持つ**(`Todoist::Due{day, sec}`。Icalと同じ)。`due.date`は`"2026-10-01"`(日付だけ)/
  `"…T15:00:00"`(浮動=現地)/`"…T06:00:00.000000Z"`(UTC。`setUtcOffset()`の分ずらす)。一覧は期限順(期限なしは最後、
  同じ日は日付だけ→時刻順、次に優先度)に並べ替える。表記は「今日 15:00」「明日」「10/3(土)」。期限切れは赤。
  優先度はTodoistの`priority`(4=画面のP1)で、一覧では`!!!`(P1)〜`!`(P3)。
- **通信係(`TodoistClient`)はChatClientと同じ形**: keep-alive(TLSの約40KBを開いている間だけ持つ。`onExit()`で閉じる)、
  同時に1本、操作(完了>追加)は一覧のページの間にも割り込む、5分ごとに取り直し、失敗は5秒→2分のバックオフ、401/403は
  `AuthError`で手動の[更新]まで待つ。一覧は受信用の配列へ取り終えてから入れ替える(途中で失敗しても前の一覧が残る。
  その代わり30件×2=約18KB)。完了は取り直しを待たずに一覧から消し、すぐ取り直す(繰り返しのタスクは次の回で並び直す)。
  失敗の理由は応答のJSONの`error`(`Todoist::ErrorParser`)。
- **トークン**: `/sys/todoist.cfg`の`token`。画面の[設定]から入れると`PICO_Secret`(用途`"todoist-token"`)で暗号化して保存する。
  **母艦で平文のまま書いて置いてもよく、読んだときに暗号化して書き直す**(SSHの秘密鍵と同じ流儀)。ほかのキー:
  `api`(既定`https://api.todoist.com`。テストで偽物へ向ける)/`reminders = true|false`/`remind-before-min`(既定0)/
  `today-filter`/`week-filter`。PCビルドで試すときの`pc/sdcard/sys/todoist.cfg`は`.gitignore`済み。
- **TLS**: api.todoist.com の証明書は Amazon(`Amazon RSA 2048 M01` ← `Amazon Root CA 1`)、TLS 1.2の`ECDHE-RSA-AES128-GCM`で
  BearSSLで話せる。**焼き込みのルートに Amazon Root CA 1 が無かったので足した**(上の「HTTPS」)。実際の鎖が新しいルートで
  検証でき、足す前のルートでは失敗することを`openssl verify`で確認した。
  **このリモート環境からの直接の接続は出口のゲートウェイが証明書を差し替えるため、PCビルドから本物には繋がらない**(正しく拒否される)。
- **リマインダー(`Todo_Reminders`)**: 取った一覧の時刻つきの期限を、通知(`NotificationFunctions`)の`At`の予約にする。
  OSが見張り`/sys/notify_rules.tsv`へ保存されるので、**アプリを閉じていても・再起動しても期限の時刻に知らせ、タップでTODOが開く**。
  送り主は空(C++)、tagは`td:<タスクのid>`。**空の送り主の予約は`kMaxRulesPerOwner`(4)件までなので、近い順に4件まで**
  (ほかのC++の予約があればその分減る)。表示(今日/7日間/すべて)ごとにしか一覧を取らないので、
  「一覧が網羅している期限の範囲(今日=今日の終わりまで、7日間=7日後まで、すべて=全部、溢れていたら無し)」の中で
  一覧に無くなった予約だけを消す(範囲の外の予約は別の表示で入れたものとして残す)。変わっていない予約は入れ直さない
  (SDへの書き込みを減らす)。時計が合う(NTP同期)まで入れない。
- **画面(`TodoScene`)**: `[戻る] TODO(件数) [設定][更新]` / `TabBar`(今日|7日間|すべて) / `ScrollList` / 選んだタスクの欄(3行:
  全文・期限(繰り返しは`due.string`)・優先度) / 状態の1行 / `[追加][完了]`。1回のタップで選び、もう一度タップか[完了]で
  確認の`MsgDialog`。[追加]は名前→(1フレーム空けて)期限の`InputDialog`
  (今日の表示からなら期限の初期値は「今日」)。日付が変わる/期限を過ぎると見た目が変わるので10秒ごとに見直す。
  通信中だけ`KeepAwake()`(待っているだけならスリープしてよい)。**シーン本体は約18KB**(ChatSceneと同じ例外)。
  アイコンは既存の`IconID::CheckboxOn`(新しいアイコンは足していない)。
- **未対応**: プロジェクト/セクション/ラベルの表示と絞り込み、説明文、サブタスクの入れ子(一覧では字下げだけ)、
  完了の取り消し(Todoistの画面からはできる)、編集、期限の変更。追加の期限は Todoist の自然言語(`due_lang=ja`。
  「明日 15時」「毎週月曜」)に任せていて、読めなければ`Date is invalid`等の理由が状態の行に出る。
- 検証: `todoist_proto_test`(run.sh。JSON: 入れ子/エスケープ/サロゲート/切り詰め/1バイトずつ/誤り、一覧: 必要なキーだけ・完了済みを
  飛ばす・上限・next_cursor、期限の読み取り/表記/期限切れ/並べ替え、失敗の理由、JSONの書き出し)、`todo_test`(run.sh。本物の
  NotificationFunctionsへの予約: 近い順に枠の数だけ・過ぎたもの/日付だけは入れない・何分前・範囲の中だけ消す・入れ直さない・
  保存される、TodoSceneの生成/解放とトークンの案内)、**`todoist_net_test`(run_net.sh。`script/host_test/todoist_fake_server.py`=
  Todoist API v1の偽物を平文/HTTPS/ページ分けで立てる)**: 3つの表示・並び順・UTC・ページを跨ぐ・30件で打ち切り・追加(" と \ の往復、
  期限)・読めない期限の理由・完了・繰り返しが次の回へ・404・keep-alive・401で止まる・todoist.cfgの読み込みと暗号化の書き直し。
  PCビルドの`--tap`/`--shot`(偽物の`api = http://127.0.0.1:8150`): 一覧・期限切れの赤・選択と詳細・完了の確認→一覧から消える・
  7日間の表示・追加のダイアログ・明日15時のタスクが`notify_rules.tsv`に`At`で入ること。
  **本物のTodoistアカウント・実機(BearSSL)での確認は未**(この環境には本物のトークンも実機も無い)。

### ゲームボーイ (`src/gb/` / `GameBoyScene` / `lib/peanut_gb/`) (2026-09-24)

SUMMARY.md #9。**エミュ本体は[Peanut-GB](https://github.com/deltabeard/Peanut-GB)**(MIT、C99のヘッダ1本、DMG専用)を
`lib/peanut_gb/src/peanut_gb.h`へ無改造でvendorした(Luaと同じ`lib/<名前>/src/`の形なのでPlatformIOが自動で拾う。
PCビルドは`pc/CMakeLists.txt`がインクルードパスを1行足しただけ。C++のままコンパイルできる)。

- **選んだ理由**: ROMの読み出しがコールバック(置き場所をOS側で決められる)・1行ずつ描画を渡す(画面全体のバッファが要らない)・
  RP2040でもフルスピード・MIT。比べたもの: Pico-GB(YouMakeTech、MIT。Peanut-GB+SPI液晶+SDで一番近い参考例、ROMはFlashへ書く)、
  pico-peanutGB(GBC対応だがGPL-3でHDMI出力)、gb-rp2350(Rust)、gnuboy(GPL・重い)、SameBoy/Gambatte/mGBA(正確だが重すぎる)。
- **`peanut_gb.h`の実装を取り込むのは`src/gb/Gb_Emu.cpp`だけ**(2箇所で取り込むと多重定義)。他は`GbEmu`クラスだけを見る。
  `ENABLE_SOUND=1`(音源チップの読み書きを`audio_read()/audio_write()`で受け取る。下記「ゲームボーイの音」)、`PEANUT_GB_12_COLOUR=0`(4段階だけ)。
- **ROMの置き場所は段階を分ける**。Peanut-GBはROMを1バイトずつコールバックで読むので、SDから都度読むことはできない。
  - **第1段(実装済み)**: 256KB(`GbEmu::kMaxRomBytes`)までを**RAMへ丸ごと`malloc`**。ROMの大きさは2のべき乗なので、
    境目は実質「256KBまで入る、512KB以上は入らない」(テトリス・Dr.マリオ32KB、マリオランド64KB、カービィ256KB)。
    **実機で256KBを確保できるかは未確認**(OSのピーク約150KB + ROM + エミュ約23KB + セーブ最大32KBで、RP2350の520KBの中で際どい)。
    確保できなければ`OutOfMemory`で断るだけで落ちない
  - 第2段(未): 先頭16KB+直近のバンク数枚だけをRAMに持ち、無いバンクに当たったらSDから読む(1フレーム程度引っかかる)
  - 第3段(未): SDからFlashの空き領域へ書き、XIPで読む(Pico-GBの方式)。ファームの実サイズとWi-Fi動作中のFlash書き込みの確認が要る
- **エミュの状態・ROM・セーブは`load()`で確保し`unload()`で全部返す**。`GameBoyScene`は`onExit()`で`unload()`するので、
  使っていない間は1バイトも持たない(シーン本体は約1.3KB、パス2本ぶん)。上へ別のシーンが載って戻ると同じROMを最初から起動し直す。
- **Peanut-GBの誤り通知(`gb_error`)から戻ってはいけない**(戻ると`__builtin_unreachable()`へ落ちる)。
  `GbEmu::runFrame()`の入口で`setjmp`し、`onError()`が`longjmp`で戻る。以降は`crashed()`で止まったまま
  (シーンがエラーダイアログを1回出す)。ホストテストで不正な命令`0xD3`を踏ませて確認している。
- **セーブ**: カートリッジRAMは`<ROM名>.sav`(拡張子だけ差し替え)。起動時に大きさが一致すれば読み、
  **書き込みがあったときだけ**終了時(`unload()`)に一時ファイル→差し替えで書く。大きさの違う`.sav`は読まず上書きもしない。
  **遊んでいる途中では保存しない**(SDへの書き込みでゲームが引っかかるため)。電源を落とす前に「戻る」を押す必要がある。
  MBC3の時計(RTC)は合わせていない。
- **画面(`GameBoyView`)**: 160x144を**1.5倍の240x216**(横幅ちょうど)で描く。最近傍で「2画素→3画素」。
  - エミュ側は1画素2bitで詰めて持ち(5760B)、**行ごとに前回と比べて変わった行の範囲だけをdirtyにする**(静止画面なら液晶へ何も送らない)
  - `writePixel()`を5万回呼ぶと実機で数十msかかるので、**`OSData::frame`(4bpp)のバッファへ直接書く**。
    元の1バイト(4画素)→6画素(3バイト)の対応表を先に作り、1行は表を40回引いて`memcpy`するだけ。
    4bppは左の画素が上位4bit、1行120バイト(LovyanGFXの`Panel_Sprite::drawPixelPreclipped`で確認)
  - **描くのは`FlushDirty()`の合成の中(`PICO_GFX::isDirtyDeactivates`中)だけ**。`UpdateAll()`経由のrender()ではdirtyを積むだけで描かない
    (描いても直後の合成でもう一度描くため)。合成の中でも**今のクリップ(`getClipRect()`)の内側しか書かない** —
    バッファ直書きには`setClipRect()`が効かず、はみ出すと上に重なったダイアログの下を塗ってしまう
  - 色は白/薄灰(7)/濃灰(8)/黒。パレットに緑は無いので本物の液晶の色にはしていない
- **操作パッド(`GameBoyPad`)**: 下の84px。render()直描き型。十字キー(中心からの角度で8方向、tan22.5°≒5/12で斜めを判定、中心6pxは無反応)/
  SELECT/START/A/B(近いほう)/ROM(別のROMを選ぶ)/戻る。**XPT2046は1点しか取れないので同時押しはできない**
  (「十字キー+A」のようなジャンプ操作が要るゲームは厳しい。外部コントローラー(`PadFunctions`)の入力を**ORで重ねる**ので、そちらでなら同時押しできる。
  コントローラーのHOMEは「戻る」。下記「外部コントローラー」参照)。
  指を滑らせると押しているボタンが切り替わる。ROM/戻るはその上で離したときだけ反応する。
- **速さ**: `onUpdate()`で`millis()`の差を積み、16.743msごとに1フレーム(1回に最大2フレーム、追いつけないぶんは捨てる=遅く動く)。
  **5秒ごとに「実行したフレーム数/捨てたフレーム数」を`LOG_APP_DEBUG`へ出す**ので、実機の速さはこれで分かる。
  PCビルドでは約299フレーム/5秒(=59.7フレーム/秒)で捨てはゼロ。**実機での速さは未計測**
  (RP2040をオーバークロックした実例でフルスピードなので、RP2350の150MHzでも届く見込みだが、液晶への転送が
  1.5倍表示で最大約11ms/フレームかかる点が効くかもしれない。足りなければ等倍表示・フレームスキップ・2コア目での転送を検討)。
- **ROMの選択は開いた次のフレームで出す**(`Pending::OpenPicker`)。シーンの切り替えと同じフレームで半透明のダイアログを出すと、
  ランチャの絵がダイアログの下に残る(「ブラウザのヘッダー」節の「ダイアログからダイアログへは1フレーム空ける」と同じ理由)。
- 検証: `gb_emu_test`(run.sh。テストの中でヘッダと数命令だけのROMを組み立てる。読み込みの断り方・セーブの往復・JOYPでAが読めること・
  不正な命令で落ちないこと・変わった行だけdirty・パッドの当たり判定)、PCビルドで`pc/sdcard/gb/dmg-acid2.gb`(描画のテストROM。正しければ笑顔)と
  Blargg氏の`cpu_instrs`(リポジトリには含めていない)が正しく動くことを`--tap`/`--shot`で確認。
  **市販ゲームのROMはリポジトリに含めない。**

### 音声出力 (`src/functions/Sound_Functions` / `src/sound/`) (2026-09-25)

SUMMARY.md #11。**出力の土台 + チップチューン音源(既定8チャンネル。2026-09-28に4→任意数対応)+ 2コア目での合成**。曲(MML)は下の「曲データ」、GBエミュの音は「ゲームボーイの音」。

- **配線はI2SのD級アンプ MAX98357A**(ピンは上の「ハードウェア構成」)。PWM+RCフィルタも検討したが、
  3.3Vの電源ノイズ(Wi-FiとSPIの液晶が同じ基板で動いている)がそのまま音に乗るため見送った。
  アンプの電源は**3V3ではなく5V**(スピーカーの電流の揺れを液晶/Wi-Fiの電源へ乗せないため)。
  - arduino-picoの`I2S`はPIOで動き、**LRCLKはBCLK+1に固定**。GP14/15はGP15がSDのCSなので使えず、
    ADCの使えるGP26〜28は外部コントローラー(#10)用に残して GP2/3/4 にした。
- **I2Sは一方通行なので、アンプの有無は信号線からは分からない**。検出用に1本(GP5)足し、アンプ側でGNDへ落とす。
  内部プルアップで読んで LOW=接続。MAX98357AのSD端子の電圧で見分ける案は、チップ内部の100kΩと基板の1MΩの
  分圧で刺さっていても外れていてもLOW付近になるため使えない。
  - 100msごとに読み、**3回続けて同じ値のときだけ採用**(抜き差しの瞬間のばたつき)。起動時だけは待たずに採用する。

**2つのコアの分担**(`Sound_Functions.cpp`の中で「共有」「1コア目だけ」「2コア目だけ」の3区画に分けてある):

| | 1コア目(`loop()` → `Update()`) | 2コア目(`loop1()` → `LoopCore1()`) |
|---|---|---|
| 持つもの | 検出の状態・`output`・ログ | `I2S`・`ChipSynth::Engine`・書き込み前の一時置き場(64サンプル) |
| 仕事 | ピンを読んで`want_run`を出す、`Play()/Stop()`をコマンドの列へ積む、2コア目の状態をログへ出す | コマンドを取り出して音源へ渡す、I2Sの開始/終了、波形を作ってI2Sへ書く、未接続の間は時間で進める |

- やり取りは**`std::atomic`だけ**(ロック無し)。1コア目→2コア目: `want_run`/`retry_epoch`/`master_volume`/コマンドの列(32件の固定長リング、
  1対1なので`head`/`tail`の2つのatomicで足りる。溢れたら捨てて`DroppedCommands()`で数え、1コア目がWARNを出す)。
  2コア目→1コア目: `core1_running`/`core1_failed`/`core1_active`(鳴っているチャンネル)/`core1_processed`(音源へ渡し終えた数)。
- **I2Sの`begin()`/`end()`は2コア目で呼ぶ**(DMA割り込みが呼んだコアに付くため)。休止端子(GP6)も2コア目が上げ下げする。
- **2コア目はログを出さない**(`LogFunctions`はロックを持たない)。I2Sの開始/停止/失敗は`core1_*`を1コア目が見てログへ出す。
- `IsPlaying()`は「積んだ数 != 渡し終えた数 なら鳴っている扱い、追いついていれば`core1_active`」。2コア目は
  `core1_active`→`core1_processed`の順に書くので、追いついた後に読むチャンネルは必ずその後の状態。
  `Play()`した直後から`true`になる(チャンネル単位の`ActiveChannels()`は2コア目が受け取るまで数ms遅れる)。
- 1コア目の`Setup()`が済むまで(`ready`)2コア目は何もしない。arduino-picoは`setup()`と`setup1()`を同時に走らせるため。
- `loop1()`は仕事が無ければ(I2Sのバッファが満杯、または未接続で何も鳴っていない)`delay(1)`で休む。
- **I2Sのバッファは64ワード×8本 = 512サンプル ≒ 23ms / 2KB**(2コア目が専任で流すので短くした。要求から音が出るまでの遅れもこの程度)。
  鳴らせる間だけ持ち、未接続や`output = off`の間は`end()`で返す。`begin()`に失敗したら刺し直すまで試し直さない。
- **つながっていないときは「鳴らさないだけ」**: 2コア目が`millis()`の差の分だけ音源を空回し(`render(nullptr, n)`)して進めるので、
  長さ/エンベロープは時間どおりに進み、**途中で刺すとその時点の続きから鳴る**。鳴らせる間は**I2Sが引き取ったサンプル数**が時計。
  I2Sを開始する回も、開始する時刻までの分を先に進めてから開始する(逆にすると最後の1回分が抜ける。テストで踏んだ)。

**音源(`src/sound/Chip_Synth`)**:
- **チャンネル数は`ChipSynth::kChannels`(既定8。2026-09-28に4→8。「任意チャンネル数対応」参照)**、22050Hz、
  モノラル(I2Sへは左右同値で送る)。ゲームボーイのAPUが手本だが**チャンネルごとの波形は固定しない**:
  矩形(デューティ12.5/25/50/75%)・三角・のこぎり・ノイズ(15bitのLFSR)・短いノイズ(7bit、127段で一巡)をどのチャンネルでも選べる。
- 1音 = `ChipSynth::Note{wave, freq_x16(Hzの16倍), volume 0〜15, envelope -7〜7, length_ms(0=止めるまで)}`。
  エンベロープはゲームボーイと同じ「|env|/64秒ごとに1段上げ下げ」だけで、下げて0になったら音が終わる。
- 位相は32bit(2^32で1周期)。ノイズだけは「1サンプルで何段進めるか」を16.16で持つ(1サンプルに1段が上限)。
  32bitの位相の増分を`0xFFFFFFFF`で頭打ちにする書き方だと最初の1段がずれて周期が崩れた(テストで踏んだ)。
- 振幅: 1チャンネルの最大は`kChannelAmplitude = 7800`(音量15・全体100)。**`kHeadroomChannels`(=4)個までなら
  最大で鳴らしても16bitに収まる**。それを超えて同時に鳴らすと、超えた比率ぶん全体を下げて歪みを防ぐ(2026-09-28。
  詳細は「任意チャンネル数対応」参照)。全体の音量(`sound.cfg`の`volume`、既定50)が最後に掛かる。
- 状態は全て固定長の配列で、確保は一切しない。帯域制限はしない(高い音は折り返しで濁るが、チップチューンの味の内)。
- `sound/Note_Name.hpp`: 音名("C4" "A#3" "Eb5")/MIDIノート番号 → 周波数(平均律、A4=440Hz)。

**外から使う口**:
- C++: `SoundFunctions::Play(ch, Note)`/`Stop(ch)`/`StopAll()`/`Beep(freq, ms)`(ch0の矩形波)/`IsPlaying()`/`ActiveChannels()`。
  入力テスト画面の「テスト音」がBeep(880Hz 300ms)。
- Lua: `pico.sound_play(ch 1〜kChannels, freq, ms, {wave=, volume=, envelope=})`/`sound_stop([ch])`/`sound_playing([ch])`/`note_freq(音名|番号)`/
  `beep(freq, ms)`/`sound_available()`。**チャンネルはLuaでは1始まり**。音を使った`LuaEngine`は壊れるとき(=アプリを閉じるとき)に`StopAll()`する
  (長さ0の音が鳴り止まなくなるため)。
- 動作確認アプリ「チップチューン」(`pc/sdcard/lua/apps/チップチューン/main.lua`): 1オクターブの鍵盤(Canvas 1枚 + `pico.get_touch()`)、
  波形/減衰の切り替え、デモ曲(同じフォルダの`demo.mml`を`pico.music_play`で鳴らす。鍵盤の音は効果音として曲のチャンネル1を借りる)。
- 曲(MML)は下の「曲データ」。

**設定・表示**:
- `/sys/sound.cfg`(無くてよい): `output = auto | off`、`volume = 0〜100`(既定50)。`SetOutput()`/`SetVolume()`は今だけ切り替える。
  音量は設定アプリ(`SettingsScene`)の`NumberSlider`でも変えられる(ドラッグ中は`SetVolume()`だけ、指を離したら`sound.cfg`へ書いて確認音)。
- ステータスバー: 鳴らせる=スピーカー、`off`=消音のスピーカー、未接続=スピーカー+赤のX(SD/Wi-Fiと同じ組み立て方)。
  `Active`になるのは**2コア目がI2Sを開始した後**(1コア目が接続を採用しただけではまだ`Disconnected`)。

**PC/Webビルド**:
- `pc/compat/I2S.h`がSDLの音声出力で置き換える(`src/`は同じ)。アンプの検出ピンは`pc/compat/Arduino.h`の
  `PicoPcGpio::read_hook`で代わりに答える(音声デバイスを開けたら「刺さっている」)。`/sys/sound.cfg`の
  `pc-sound-state = auto | connected | disconnected`か環境変数`PICOOS_SOUND_STATE`(Webは`?sound=`)で固定できる。
- **2コア目の代わり**: ネイティブは`pc/main_pc.cpp`が別スレッドで`setup1()`→`loop1()`を回し続ける(終了時は止めて`join`)。
  Webはスレッドが無いので、フレームごとに`loop()`の後で`loop1()`を1回呼ぶ(1回で最大512サンプル書けるので60fpsで足りる。
  フレームのぶれで途切れないよう、Webだけリングを2048サンプルにしてある)。
- ヘッドレスなら`SDL_AUDIODRIVER=disk`で`SDL_DISKAUDIOFILE`へ生の音(22050Hz/16bit/ステレオ)を書き出せるので、波形を数値で確かめられる。
  Webはブラウザが利用者の操作までAudioContextを止めるので、`shell.html`が最初の操作で`resume()`する。

**検証**: `sound_test`(run.sh。前半は音源: デューティ比・三角/のこぎりの形・ノイズの周期(127/32767)・長さ・エンベロープ・4ch足し合わせ・音名。
後半はSoundFunctionsで、1コア目と2コア目を1本のスレッドで交互に呼ぶ: 検出のばたつき、I2Sの開始は2コア目、コマンドの列の溢れ、
抜き差しで続きから鳴る、sound.cfg、begin()失敗)、`lua_engine_test`(Lua API)。PCビルドで`SDL_AUDIODRIVER=disk`の出力を確認
(テスト音: 300ms・約880Hz、デモ曲: 4ch足して最大約12000で音割れなし)。
**実機(arduino-picoのI2S・2コア目・MAX98357A)では未確認**(このリモート環境には実機もRP2350のボード定義も無い)。**Webビルドも未確認**(emsdkが無い)。

### 曲データ (`MUSIC_FORMAT.md` / `src/sound/Mml_Compiler` / `Music_Player` / `MusicScene`) (2026-09-25)

SUMMARY.md #11「標準ファイル形式を探す/考える」。**標準はMML**(pico-os向けの方言。書き方は`MUSIC_FORMAT.md`)。
他の形式(MIDI=PC側の`script/midi2mml.py`で変換(下記)、VGM/GBS=ゲームボーイの音源チップの再現後)は、どれも同じ「演奏データ」へ
変換してから鳴らす方針で、2コア目は形式を知らない。

- **流れ**: `.mml`(テキスト)→ 1コア目の`MmlCompiler`が「演奏データ」(`Music_Data.hpp`の小さなバイト列。音符1つ4バイト)へ →
  コマンドの列で2コア目へ渡す → 2コア目の`MusicPlayer`がティックを数えて`ChipSynth::Engine`を叩く。
- **読み取り(`MmlCompiler`)**: 1行ずつ読む(`MmlLineSource`。SDのファイル版`MmlFileSource`とメモリ上の文字列版`MmlTextSource`)。
  1周目でヘッダ/マクロ、**チャンネルごとに頭から読み直して**命令列を書く(ファイル全体をRAMへ載せない。1行512バイトまで)。
  繰り返しは展開せず`LoopBegin/LoopBreak/LoopEnd`のまま(飛び先は後から書き込む)、マクロは展開して前後を`SaveState/RestoreState`で挟む
  (波形/音量/減衰/qは再生側で戻す。**繰り返しの中では1周目と2周目で状態が違いうるので、読み取り側の記憶で戻すと間違う**)。
  オクターブ/既定の長さ/移調は読み取り側だけの状態(`[c >]2`は2回とも同じ高さ。警告を出す)。
  **誤りは最初の1つで止め、行・列・理由を返す**(マクロの中の誤りは呼んだ`$`の位置で)。長さの帳尻はティックで数え、
  `L`から後ろの長さがチャンネルで違えば警告する(`[ ]`の回数と`:`も計算に入れる)。`L`の後ろが0ティックなら無限ループになるので誤り。
- **`e`は音符のミなので、減衰は大文字の`E`**(案の段階では`e`だったが、`e-6`が「ミ♭の6分音符」と区別できないと実装時に気づいた)。
- **シーケンサー(`MusicPlayer`、2コア目だけ)**: 1ティック = 22050×60/(テンポ×48) サンプルを**整数の積み上げ**で数える(端数が消えないので
  長く鳴らしてもずれない。テストで2304ティック先の音符が1サンプル以内)。`render()`は「次のティックの境目」で音源の計算を区切るので、
  音符はサンプル単位の位置で鳴る。`q`は「長さ×q/8 ティックで止める」(音源へは長さ0で渡し、止めるのはシーケンサー)。
  1回のティックで1トラックが読む命令は256個まで(壊れたデータで固まらないため)。
- **効果音との同居**: `SoundFunctions::Play()`(=Luaの`sound_play`/`beep`)はそのチャンネルを**借りる**(2コア目の`borrowed`)。
  借りている間`MusicPlayer`はそのチャンネルの音源に触らず、曲は進み続ける。効果音の音が消えたら(長さ切れ・減衰し切り・Stop)
  毎回の`Core1StepAt`で返し、曲は**次の音符から**鳴らす。`Stop(ch)`/`StopAll()`は効果音だけを止める(曲が鳴っていなければ全部)。
- **置き場(`SoundFunctions`、1コア目)**: 演奏データの置き場は**6KiB×2**。2コア目が読んでいる方には書かない
  (置き場ごとに「このコマンド番号が処理されたら空く」を覚え、`core1_processed`と比べる)。3曲目を2コア目が前の曲を手放す前に
  頼むと断る。置き場2つと`MmlCompiler`(約3.5KB)は**最初に曲を鳴らすときに1回`malloc`**して持ち続ける(曲を使わないアプリにRAMを負担させない)。
  読めなかったときは今の曲はそのまま。`MusicPlaying()`は頼んだ直後から結果の扱い(`IsPlaying()`と同じ考え方)。
- **Lua**: `pico.music_play(path)`(SDの権限に従う)/`music_play_text(mml)`/`music_stop()`/`music_playing()`。
  失敗は`nil, "3行12列: 理由"`。曲を使った`LuaEngine`は壊れるとき`MusicStop()`する。
- **ミュージックアプリ(`MusicScene`)**: `/music/`直下の`*.mml`を名前順に並べ、2回タップで鳴らす。読めない曲は理由を**状態の欄へ赤で**出す
  (`MsgDialog`は大きい文字の1行しか見せられず、行・列が切れた)。状態の欄は`setDisableAutoTextDecoration(true)`
  (ファイル名の`_`が下線のマークアップとして消えた)。**アプリを閉じると曲も止める**(外から止める手段が無いため)。
  アイコンはtablerの`music`(`IconID::Music`、81番。末尾へ追加)。
- サンプル: `pc/sdcard/music/demo.mml`(チップチューンアプリのデモ曲)、`sample.mml`(1番/2番かっこ・3連・タイ・途中のテンポ変更・マクロ)。
- 検証: `music_test`(run.sh。読み取り・誤りの行列・警告・シーケンサーのサンプル単位の位置/テンポ/繰り返し/ループ/借用・置き場の入れ替え)、
  `lua_engine_test`(Lua API)、PCビルドの`--tap`でミュージックアプリの再生・誤りの表示、チップチューンアプリで曲+鍵盤(借用)を確認。
  **実機では未確認**。
- **MIDIからの変換(`script/midi2mml.py`、2026-09-26)**: PCで動かすPython(標準ライブラリのみ)。本体はMIDIを知らないまま
  (「形式を増やすときは変換を1つ足すだけ」の方針どおり)。手順と既定の割り当ては`MUSIC_FORMAT.md`「MIDIからの変換」。
  - 時間は4分音符=48ティックへ直して格子へ揃える。格子は「音の出だしの97%以上が乗る、いちばん粗いもの」を自動で選ぶ
    (楽譜から書き出したMIDIは3連符も16分もずれない。演奏を録ったMIDIは16分音符で妥協し、`--grid`で変えられる)
  - 和音はパート(トラック×チャンネル)ごとに「同時に1音の線」へ分けてから選ぶ。和音のまま1チャンネルへ畳むと、
    主旋律が内声に負けて消える(高い音優先でも、伴奏のほうが高いことがある)
  - **テンポは空いているチャンネルへ`t`だけを書く**(`t`は全チャンネル共通なので、どこに書いても効く)。
    4チャンネルとも埋まっていて、しかも全部が音符の途中なら、その音符を2つに分ける(鳴らし直しになるので警告)
  - **演奏データのバイト数を読み取りと同じ数え方で数える**(音符4・休符3・`v`/`@`/`E`/`q`は2・`t`は3・`L`は1・終わり1・
    見出し`8+DEVICE_CHANNELS*2`)。6KiBを超えたら収まる小節数を二分探索で探して切る。見積もりが本物と一致することは
    テストで確かめている。**見出しの大きさは変換先pico-osビルドの音源チャンネル数(`DEVICE_CHANNELS`、既定8)で決まり、
    `--channels`(このスクリプトが何本の声部を割り当てるか、既定4)とは別の値**(下記「任意チャンネル数対応」参照)
  - 長い休符は`[r1]n`(6バイト)。`r1^1^1…`だと1行512バイトを超えうる
  - 検証は`midi2mml_test.py`(run.sh。`mml_dump.cpp`=本物の`MmlCompiler`で読んで演奏データを1行ずつ書き出す下請けを通す)。
    music21のテスト用MIDI(ピアノ曲、演奏を録ったもの等)でも変換→読み取りまで確かめた(リポジトリには含めていない)

### チップチューン音源の任意チャンネル数対応 (`src/sound/Chip_Synth` 他) (2026-09-28)

「もっと同時発音数を増やしたい」という相談から、`ChipSynth::kChannels`(旧・固定4)を単なる定数へし、
増減がMML・Lua API・演奏データのヘッダまで1箇所の変更で伝わるようにした。GBエミュ側の音源(`GbApu`、
実機のハードウェア仕様どおり常に4チャンネル)とは無関係で、そちらは変更していない。

- **`ChipSynth::kChannels`を4→8へ既定値を上げた**うえで、`Chip_Synth.hpp`に`ChannelMask`
  (`uint32_t`)という型を新設し、`activeMask()`・`MusicPlayer::borrowed_`・`SoundFunctions`の
  `core1_active`/`borrowed`など、これまで`uint8_t`だったビットマスク類を全て置き換えた
  (`uint8_t`は8chまでしか表せないため)。`static_assert(kChannels <= 32, ...)`で型の上限を明示。
- **`MusicData::kChannels`は独立した定数をやめ`ChipSynth::kChannels`をそのまま使う**ように
  した(MMLのトラックは音源チャンネルへ1:1で乗るので、2つの定数が食い違うと壊れるため)。
  `kHeaderBytes`(演奏データの見出しの大きさ)も`8 + kChannels*2`の計算式にした
  (以前は`16`固定)。MMLのチャンネル文字は`Mml_Compiler.cpp`の`ChannelMask()`が
  `'A'〜('A'+kChannels-1)`を読むように直した。**上限は26**(`A`〜`Z`)で、
  `Music_Data.hpp`の`static_assert`で強制している(これより多いchを持ちたい場合は
  文字ではなく数値等、別の書き方を考える必要がある。現時点では未対応)。
- **1チャンネルの最大音量(`kChannelAmplitude=7800`)は変えていない** — 既存の4chまでの曲の
  音量を変えないため。代わりに`Engine::render()`に「同時に鳴っている数が`kHeadroomChannels`
  (=4、元の設計が前提にしていた数)を超えたときだけ、超えた比率ぶん全体を下げる」処理を足した
  (`mix = mix * kHeadroomChannels / active_count`)。**4ch以下の同時発音は今までどおりの音量で
  絶対に歪まず、5ch目以降を同時に鳴らすと鳴っている数に応じて少しずつ静かになる(歪みはしない)**。
  単純に総振幅予算をチャンネル数で割る案(全chが等しく静かになる)も検討したが、それだと
  既存コンテンツ(4chまでしか使わない曲)まで一律に音量が下がってしまうため見送った。
- **Lua API(`pico.sound_play`等)・コマンドの列(`Sound_Functions`)は元々`SoundFunctions::kChannels`
  を通して動的にチャンネル数を見ていたため、コード変更はほぼ不要だった**(範囲チェックの
  エラーメッセージが`SoundFunctions::kChannels`を埋め込んで表示するだけで自動的に追随する)。
- **`script/midi2mml.py`にも`--channels N`(既定4、1〜26)を追加**した。既定はこれまでと
  全く同じ4チャンネル(`A`〜`D`)への割り当てで、指定すれば`E`以降も使われる
  (4chまでの役割固定: 主旋律`A`/`B`・ベース三角波`C`・打楽器ノイズ`D`は変えず、
  5ch目以降は`pulse75`→`saw`→…を巡回する追加声部として割り当てる)。
  **演奏データのバイト数見積もりに使うヘッダの大きさは、`--channels`(このスクリプトが
  何本の声部を割り当てるか)ではなく、変換先pico-osビルドの音源チャンネル数
  (`DEVICE_CHANNELS`、既定8。`ChipSynth::kChannels`と手動で合わせる定数)で決まる**。
  ここを取り違えて`len(CHANNELS)`から見積もっていたところ、`midi2mml_test.py`が
  実際の`MmlCompiler`が書く見出しの大きさ(24バイト)と食い違うことで検出できた。
- ホストテスト: `sound_test.cpp`(全チャンネル同時発音・全体音量・範囲外チャンネル)と
  `lua_engine_test.cpp`(範囲外チャンネルのエラー)にあった「4チャンネル固定」前提の
  アサーションを`ChipSynth::kChannels`/`kHeadroomChannels`から動的に導く形へ直した。
  `music_test.cpp`はMMLのチャンネル文字が元々`A`〜`D`しか使わないテストのみで、
  上限を26へ緩めても無改修で全件パス。`midi2mml_test.py`は上記のヘッダサイズ修正後、
  65件全てパス。PCビルドでの実際の音の聞こえ方(5ch目以降の音量低下・26chの実用上の
  読みやすさ)、および**実機(RP2350)でのCPU負荷の実測は未確認**(このリモート環境には
  実機が無い。音源の合成自体は1サンプルあたりの演算が単純な加算/シフトのみで、
  チャンネル数が倍になっても2コア目の負荷は線形にしか増えない見込み)。

### ゲームボーイの音 (`src/sound/Gb_Apu` / `Gb_Audio_Link` / `gb/Gb_Audio_Sink.hpp`) (2026-09-26)

SUMMARY.md #11「GB対応」。Peanut-GBは音源チップ(APU)を持たず、`ENABLE_SOUND=1`で0xFF10〜0xFF3Fの読み書きを
`audio_read()/audio_write()`へ回すだけなので、**音源チップを自前で書いた**(minigb_apu等をvendorしなかったのは、
合成を2コア目で行う・`ChipSynth`と同じ22050Hzモノラルの枠へ足し合わせる・確保をしない、を満たすため)。

```
エミュ(1コア目) audio_write → GbEmu::audioWrite(控え + 時刻) → GbAudioSink(SoundFunctions::GbAudio())
  → GbAudioLink(時刻付きの列) → 2コア目 GbApu.write/renderAdd → ChipSynthの出力へ足す → I2S
```

- **`GbApu`(2コア目だけ)**: ch1(矩形波+周波数スイープ)/ch2(矩形波)/ch3(波形メモリ32段×4bit、NR32の音量)/ch4(ノイズ、LFSR 15/7bit)、
  長さ・エンベロープ・スイープを512Hzのフレームシーケンサーで進める。NR50(左右の音量)/NR51(振り分け)を左右で数えて**足してモノラル**に、
  NR52の電源(切ると NR10〜NR51 が消え書き込みを受け付けない)。DACが切れている(NRx2の上位5bitが0 / NR30のbit7)とトリガーしても鳴らない。
  周波数はクロックではなく**位相の積み上げ**(`ChipSynth`と同じ)で、1サンプル(約45µs)より細かい変化は出ない。
  サンプル周波数の半分を超える高さは「平均の高さ」を出す(直流なので消える。ゲームがわざと超音波にして黙らせる使い方に合う)。
  出口は実機と同じく**直流を落とす**(一次のハイパス、係数0.992)。鳴り始めの段差で振幅が一瞬2倍になるので、
  1チャンネルの振幅を`kChannelAmplitude = 4000`に抑えて4チャンネル同時の鳴り始めでも16bitに収めた。
  実機のクロック単位の癖(長さカウンタの余分な1回、波形メモリへの書き込みの化け、トリガー時の細かい遅れ等)は再現しない。
- **時刻**: エミュは1フレーム(70224クロック)を一気に走らせるので、書き込みをそのまま渡すとフレームの中の位置が失われる
  (1フレームに何度も音量を変える効果が潰れる)。`GbEmu::frameCycle()`が**「フレームの頭(LY=144、VBlankの始まり)から何クロック目か」**を
  LYと`counter.lcd_count`(LCDが切れていれば`lcd_off_count`)から求めて付ける(LCDの入り切りで戻らないよう単調にする)。
  2コア目はフレーム単位で取り出し、その時刻にあたるサンプルの位置(端数はフレームをまたいで持ち越す)で`write()`する。
  **音はエミュより約1フレーム遅れて鳴る**。
- **`GbAudioLink`**: 1対1のロック無しの列(1024件×4バイト、`head`/`tail`/区切りの数`units`の3つのatomic)。
  **取り出すのは区切り(フレームの終わり/始める/止める)まで積み終えたものだけ**。満杯なら書き込みを捨てる(区切りのために1つは空ける。
  捨てた数は1秒に1回まで`LOG_SYS_WARN`)。次のフレームが来なければ今の音を鳴らし続けて1コア目の揺れを吸収し、
  **50ms来なければエミュが止まったとみなして無音**(ダイアログを開いた・シーンを離れた、で最後の音が鳴り続けない)。
  4フレームより多く溜まったら、古いフレームは音を作らず書き込みだけ当てて追いつく。
  列は**初めてROMを起動したときに1回`malloc`**して持ち続ける(2コア目が読んでいる途中で消えないよう解放しない。曲の置き場と同じ考え方)。
- **読み出し**: 音源は2コア目にあるので、`GbEmu`が書いた値の控え(`apu_regs`)に「読むと常に1のビット」を足して答える
  (Peanut-GBの`ortab`と同じ表)。NR52の下位4bit(鳴っているチャンネル)だけは、2コア目が知らせた値(約1フレーム遅れ)と
  **このフレームにトリガーしたチャンネル**を合わせる。
- **`GbAudioSink`**(`gb/Gb_Audio_Sink.hpp`): エミュから見た渡し先。実物は`SoundFunctions::GbAudio()`で、`GameBoyScene`が
  `load()`の前に`setAudioSink()`する。エミュ本体を`SoundFunctions`へ直接つながないのは、`gb_emu_test`を音抜きで軽く保つため
  (テストでは記録するだけの偽物を渡す)。渡し先が無くても控えだけで読み書きは動く。
- 起動ROMは飛ばしているので、`load()`が起動ROMの後の値(NR50=0x77、NR51=0xF3、NR11/NR12)を書いておく(自分で書かないゲームがあるため。トリガーはしない)。
  ROMを閉じる・エミュが止まる(不正な命令)と`end()`で音を止める。
- 曲(MML)・効果音とは**足し合わせる**(チャンネルの貸し借りは無い)。`sound.cfg`の`volume`が両方に掛かる。
  アンプが刺さっていない間も、2コア目が時間どおりに列を消化する(`render(nullptr)`)。
- 検証: `gb_apu_test`(run.sh。矩形波の高さ/デューティ、長さ、エンベロープ、スイープの溢れ、波形メモリの周期と音量、ノイズ、
  電源・振り分け・音量、4チャンネル同時でも頭打ちしない、列: フレームの中の位置どおり・無音への切り替え・追いつき・満杯)、
  `gb_emu_test`(書き込みが時刻付きで渡り、フレームの中で戻らず0〜70223に収まる・読み出しの答え方・電源・閉じる/止まると`end()`)、
  PCビルドでテストの中と同じ要領で組み立てたドレミのROMを`--tap`で開き、`SDL_AUDIODRIVER=disk`の出力で8音の高さと、
  「戻る」で無音になることを確認。**市販ゲームでの聞こえ方・実機(2コア目の負荷)は未確認**(1サンプルあたり4チャンネル+浮動小数点のハイパス1回。
  RP2350のFPUで数%の見込み)。

### WAVの再生 (`src/sound/Wav_Decoder` / `Wav_Stream` / `SoundFunctions::WavPlay`) (2026-10-01)

SDの`.wav`をそのまま鳴らす。曲(MML)・効果音・GBの音と**足し合わせる**(チャンネルの貸し借りは無い)。同時に鳴らせるWAVは1本。

```
1コア目 UpdateAt() → FeedWav(): WavDecoder.read()(SDから1KBずつ読み、モノラル22050Hzへ直す)
  → WavStream(ロック無しの列、8192サンプル ≒ 370ms・16KB) → 2コア目 Pump(): ChipSynth/GBの出力へ renderAdd
```

- **SDから読むのは1コア目**(2コア目からSDに触らない決まりのため)。1回の`Update()`で積むのは`kWavMaxPerUpdate`(1024サンプル)まで、
  鳴らし始めは`kWavPrefillSamples`(4096)をまとめて先読みする。消費は毎フレーム約370サンプル(60fps)なので十分追いつく。
  **1コア目が約370msより長く止まると途切れる**(TLSのハンドシェイク等)。途切れた回数は`WavUnderruns()`、1秒に1回まで`LOG_SYS_WARN`。
  鳴らせない間(アンプ未接続、`out==nullptr`)の不足は聞こえないので数えない。
- **`WavDecoder`**: 整数PCM 8/16/24/32bit(8bitだけ符号なし、24/32bitは上位16bit)・float 32bit・`WAVE_FORMAT_EXTENSIBLE`。
  1〜8チャンネルを平均してモノラルに、周波数は線形補間(16.16の固定小数点、帯域制限なし)で22050Hzへ。fmt/data以外のチャンクは飛ばし、
  dataの長さがファイルより長い(0xFFFFFFFF等)ならファイルの終わりまで。ADPCM・64bit float・9ch以上は`Unsupported`。
  同じ周波数なら入れたのと同じ数だけ出す(最後の1サンプルも`ending_`で出してから終える)。`setLoop(true)`で頭へ戻る。
- **`WavStream`の「捨てる」(止める/切り替え)は1コア目から`flush()`するだけ**: 捨てたい位置(その時点のhead)と世代を書き、2コア目が次に読むときに
  tailをそこまで進める。1コア目は`effectiveTail()`(tailとflush位置の進んだ方)で空きを数える。切り替えの瞬間、2コア目が古い位置を読んでいる間に
  新しいデータが同じ場所へ書かれて数サンプル混ざることはありうる(メモリ安全には影響しない)。
- 音量はWAVごと(0〜100)×全体(バッテリーの頭打ち込み。2コア目の`eff_volume`)。全体100・WAV100でWAVのフルスケールがそのまま出る。
- 列(約16KB)と読み取り係(約1.1KB)は**最初にWAVを鳴らすときに`malloc`して持ち続ける**(GBの列・曲の置き場と同じ。2コア目が読んでいる途中で消えないよう解放しない)。
  読み取り係は1つなので、**読めないWAVを頼むと今のWAVは止まる**(MMLの「読めなければ今の曲はそのまま」とは違う)。
- スリープ: `PowerFunctions::Busy()`が`WavPlaying()`を見る。省電力中の2コア目は列に残りがあれば動き続ける(`AnythingSounding()`)。
- Lua: `pico.wav_play(path[, {loop=, volume=}])` → true / `nil, 理由`(SDの権限に従う。引数の誤りは先に`luaL_error`)、`pico.wav_stop()`、`pico.wav_playing()`。
  使ったアプリは閉じるときに止める(`used_wav_`)。ドキュメントは`lua-api-doc/content/api/sound.md`「WAV」。
- **一時停止・位置・シーク(2026-10-01)**: `WavPause(bool)`/`WavPaused()`/`WavPositionMs()`/`WavDurationMs()`/`WavSeekMs(ms)`。
  - 一時停止は`WavStream::setPaused()`(atomic)で、2コア目が列から取り出さなくなるだけ(積むのは続き、満杯で読み取りも止まる)。
    止めている間は`hasData()`が偽(省電力でI2Sを止めてよい。`Power_Functions::Busy()`も`Paused`を除く)で、途切れとも数えない。
    新しく鳴らす/止めると解ける。`WavPlaying()`は止めている間も真のまま。
  - **位置は「積んだ数 − 列に残っている数」**(`wav_pushed`は1コア目だけが数える。`WavStream::bufferedSamples()`)。2コア目から値をもらわない。
    I2Sのバッファ約23msぶん先を指す。ループ中は長さで巡る。
  - **シーク**: 先読みを`flush()`で捨てて`WavDecoder::seekMs()`で飛び、`wav_pushed`を飛び先へ合わせて先読みし直す。
    読み終えて閉じた後でも(`WavStop()`していなければ)`reopen()`で開き直して飛べる(デコーダがパスを覚える)。
    `seekMs()`は`open()`を呼ぶので、`open()`が自分のパスバッファへ`strcpy`しないよう`path != path_`で守っている(ASanで踏んだ)。
- **MML側の一時停止・時間**: `MusicPause(bool)`/`MusicPaused()`/`MusicElapsedMs()`/`MusicTotalMs(bool* loops)`。一時停止は`music_paused`(atomic)を2コア目が
  `MusicPlayer::setPaused()`へ当てる(曲のチャンネルを無音にして時間も止める。**再開は止めた音を鳴らし直さず次の音符から**)。経過時間は
  `MusicPlayer`がサンプルで数えて`core1_music_ms`で1コア目へ渡す(「鳴らす」を受け取るまでは0)。**曲の長さは読み込み時に1コア目が
  `MusicPlayer::MeasureMs()`で数える**(ティックだけ進める。上限40万ティック)。**L(ループ位置)で戻る曲や上限を超えた曲は終わりが無いので`*loops=true`で、
  戻るまでの長さを返す**。ミュージックアプリはこの場合「ループ」と出す(経過時間だけ数える)。MMLの時間はシークできない(バーを出さない)。
- ミュージックアプリ: `/music/`の`*.wav`も並べる(拡張子の大小は区別しない)。MMLとWAVは同時に鳴らさない(片方を鳴らすともう片方を止める)。
  **操作部**(鳴っている間だけ表示): `0:23 [シークバー] 3:45`の行(バー=`NumberSlider`はWAVだけ。MMLは時間の2つだけ)と`[一時停止/再開][停止]`。
  バーは動かしている間は飛ばず、離したところへ1回だけ飛ぶ(動かすたびにSDを読み直さない)。**押した瞬間にもその位置へつまみを動かす**ので
  (`NumberSlider`は`causeOnPressMove()`でしか値が変わらない)、タップだけでも飛べる。時間の欄は秒が変わったときだけ書き換える。
  検証: `wav_test`/`music_test`(一時停止・位置・シーク・曲長・新しく鳴らすと解ける)、PCビルドの`--tap`(`SDL_AUDIODRIVER=disk`は実時間で消費されるので、
  進行を見るには1000フレーム以上回す。バーのタップでの飛び先・一時停止中の時間の固定・MMLの「ループ」表示を確認)。**実機では未確認**。
  サンプルは`pc/sdcard/music/chime.wav`(8bit・11025Hz・約1秒。周波数の変換の経路も通る)。
- 検証: `wav_test`(run.sh。形式ごとの変換・平均・周波数の変換・チャンクの読み飛ばし・ループ・断り方、SoundFunctionsの配線: 音量・止める/切り替えで
  先読みを捨てる・途切れの数え方・アンプが無い間も時間どおりに進む)、`lua_engine_test`(Lua API)、`power_test`(WAV中はスリープしない)、
  PCビルドで`SDL_AUDIODRIVER=disk`の出力を見てミュージックアプリからchime.wavが約1秒途切れずに鳴ることを確認。
  **実機では未確認**(SPI1の実効速度・1コア目の引っかかりでの途切れ・Webビルドは1フレームに1回の`loop1()`で足りるか)。

### 外部コントローラー (`src/functions/Pad_Functions` / `script/pad_serial.py`) (2026-09-25)

SUMMARY.md #10。**方式は市販のWiiクラシックコントローラー**(I2C0のGP0/GP1、アドレス0x52。抜き差しはACKの有無で分かるので
検出用のピンが要らない。十字キー/ABXY/L R ZL ZR/START SELECT HOMEが揃い3.3Vで動く)に決めたが、**実物がまだ無い**。
そこで先に「アプリが読む窓口」と、**USBシリアル経由でPCのキーボードを代わりにする入力元**を作った。
比べた方式(GPIO直結=ピン不足、SFCパッド=5V・独自コネクタ、UART=相手のファームが要る、Bluetooth=RAMとWi-Fi同居の負担)は
会話の記録なので割愛する。空きピンはGP0/1/7/8/14/26/27/28の8本(GP23〜25/29は無線チップが使う)。

- **窓口(`PadFunctions`)**: 状態は`uint16_t`のビットマスク1つ(`Button`: 十字4 + ABXY + L R ZL ZR + START SELECT HOME の15個)。
  **`loop()`の頭(`PICO_Touch::Update()`の直後、シーンの`onUpdate()`より前)で1回だけ更新**するので、1フレームの間は
  `IsDown()`/`Pressed()`/`Released()`の答えが変わらない(`Pressed`/`Released`はそのフレームだけtrue)。
  **ビットの並びはシリアルの取り決め・`pad_serial.py`・Luaの名前表に直結しているので変えないこと。**
- **USBシリアルの取り決め**: PC→pico-osへ1行`pad XXXX\n`(押しているボタンの16進数、1〜4桁)。ログと同じ`Serial`の逆向き。
  - PC側は**状態が変わったときと、変わらなくても100msごとに**送る。pico-os側は**`kSerialTimeoutMs`(500ms)届かなければ外れた扱いにして全部離す**
    (スクリプトを止めた・ケーブルを抜いた・離した瞬間の行が落ちた、のどれでも押しっぱなしにならない)
  - 形式に合わない行は黙って捨てる(シリアルモニタで`pad 10`と打てばAを押したことになる)。1行は32Bまで、長い行は改行まで読み捨てる。
    1回の`Update()`で読むのは256Bまで(送り付けられてもフレームが止まらない)
  - **キーの割り当てはPC側(`pad_serial.py`)が持つ**。pico-osが受け取るのはボタンの状態だけなので、実物のコントローラーに替えても
    アプリ側は何も変わらない。キーの押下/離しを1件ずつ送る方式にしなかったのは、離した行が1つ落ちるだけで押しっぱなしになるため
- **`script/pad_serial.py`**: tkinterの小窓でキーの押下/離しを拾い、上の行を送る。
  - シリアルはpyserialで開く(Windowsは必須)。無ければLinux/macOSだけ`termios`で開く
  - **ポートは同時に1つのプログラムしか開けない**のでシリアルモニタとは併用できない。代わりに**pico-osのログをそのままターミナルへ出す**
  - ポートが消えても(書き込みでの再起動等)1秒ごとに開き直す。フォーカスが外れたら全部離す
  - X11のキーの自動リピートは「離した→押した」を連続で送ってくるので、離したのは30ms待ってから採る
  - `--stdout`でPCビルドの標準入力へパイプできる
- **使っている所**: `GameBoyScene`(画面の`GameBoyPad`とORで重ねる。対応は`gb/Gb_PadMap.hpp`、BはYでも押せる。HOMEで戻る)、
  Lua(`pico.pad_connected/pad_down/pad_pressed/pad_released`、ボタンは小文字の名前。知らない名前はエラー)、
  ステータスバー(つながっている間だけゲームパッドのアイコン。無いのが普通なのでバツは付けない)、
  動作確認アプリ「コントローラー確認」(`pc/sdcard/lua/apps/コントローラー確認/`)。
  **通常の画面をコントローラーで操作する(フォーカス移動)のは対象外**(ウィジェットにフォーカスの概念が無い。別の大きな仕事)。
- **PCビルド**: `pc/compat/Arduino.h`の`Serial.available()/read()`が**標準入力**を別スレッドで読む(`PICOOS_SERIAL_STDIN=off`で無効)。
  `python3 script/pad_serial.py --stdout | ./pc/build/picoos_pc`、ヘッドレスなら`echo "pad 0011"`を100msごとに流し込めばよい。
- **Webビルド**(2026-09-26): 標準入力が無いので、ページ(`pc/web/shell.html`)の**コントローラー**(Wiiクラシックと同じボタンの並び。
  ポインタごとに覚えるので**複数の指で同時押し**でき、押したまま滑らせると隣のボタンへ移る)と**キーボード**(割り当ては`pad_serial.py`と同じ、
  `KeyboardEvent.code`で引く)が`pad XXXX\n`を作り、`main_pc.cpp`の`picoos_serial_push()`(`EMSCRIPTEN_KEEPALIVE`+`-sEXPORTED_FUNCTIONS`)で
  1バイトずつSerialの受信口へ入れる。**取り決めは実機と同じ**(変わったとき+100msごと、チェックを外すと送らなくなり500msで外れる)ので、
  pico-os側は何も変えていない。タブを離れたら(`blur`)全部離す。Chromium(Playwright)で、キーボードとページのボタンの両方でテトリスを操作できることを確認した。
- 検証: `pad_test`(run.sh。行の読み取り、押した/離したはそのフレームだけ、途切れたら外れる、行が分かれて届く、長すぎる行、1回に読む量、GBの対応)、
  `lua_engine_test`(Lua API)、PCビルドで`pad_serial.py --stdout`→`picoos_pc`をXvfb+xdotoolで通し(十字キー斜め+Bの同時押しで点が斜めに動く)、
  擬似端末を相手にシリアルの経路(pyserial有り/無し、ログの折り返し)。**実機のUSBシリアル(arduino-picoのCDC)では未確認**。
- 次: Wiiクラシックコントローラーのドライバ(`Source::WiiClassic`。I2Cで6バイト読むだけ、見つからない間は500msごとに探す)。
  PCビルドにSDLのキーボード/ゲームパッドを直接つなぐのも手軽な追加候補。

### 物理キーボード (`src/functions/KeyInput_Functions` / `KeyInput_Dispatch.cpp`) (2026-10-01)

SUMMARY.md #10の追加項目。**PCのキーボードを物理キーボードとして使い、そのまま文字を打てるようにした**(第1段: USBシリアル経由)。
本体へ直接つなぐ方式は比べた上で後回しにした(Bluetoothのキーボード=arduino-picoのBluetoothHIDMasterがあるがBTstackでRAMを数十KB食い、
Wi-Fiと無線チップを共有する / PIO-USBのホスト=CPUクロックを12MHzの倍数にする必要がありSPI/I2S/CYW43の分周が狂い、1msごとの処理に
2コア目(音声専用)が要る / 本体のmicro-USBをOTGに=ログとpad_serialが使えず、電池のVSYSでは5Vを出せない / PS/2=軽いがレベル変換が要り入手しにくい)。
次に試すならBluetooth(RAMとWi-Fi同居を実機で測ってから)。

- **出来事の列**: `PadFunctions`が「押している状態」なのに対し、こちらは**1打鍵=1件の出来事**(`Event{key, mods, cp}`)。
  文字入力は回数が大事なので状態にしない。長押しの連打はPC側のキーリピートがそのまま届く。列は32件の固定長(溢れたら捨てて数える)。
- **USBシリアルの取り決め**: `key M CODE\n`。Mは修飾キーの16進1桁(1=Ctrl 2=Alt 4=Shift)、CODEは`u+XXXX`(符号位置)か
  `enter backspace tab esc delete left right up down home end pageup pagedown`。文字はShift・キー配列を反映済みで送る
  (母艦のIMEで確定した日本語もそのまま送れる)。**Serialを読むのは`PadFunctions`の1か所**で、`pad `以外の行を`KeyInputFunctions::FeedLine()`へ回す。
  1行落ちても1文字落ちるだけ(押しっぱなしの事故は無いので、padのような生存確認は要らない)。
- **配り先(`KeyInputFunctions::Update()`、`loop()`で`SceneFunctions::Update()`の後・`WidgetFunctions::UpdateAll()`の前)**:
  1. 今の画面の`Scene::onKey(ev)`(既定はfalse)。SSH(`SshScene`)はここで全部取ってシェルへ送る(Ctrl+文字→制御文字、Alt→ESC前置、
     Esc/Tab/矢印/Home/End(`?1h`ならSS3)/Delete/PageUp/PageDownのエスケープ列。接続前は端末の中の1行へ。Ctrl+Cは中止/打ち直し)。
     テキストエディタ(`TextEditorScene`)は↑↓/PageUp/PageDown(10行)で行を移り、Ctrl+Sで保存、キーボードが閉じていれば打ったときに開いて
     文字そのものはキー盤へ回す(ダイアログを出している間は何もしない)。
  2. 開いているキー盤(`KeyboardFunctions::VisiblePanel()`)の`KeyboardPanel::onPhysicalKey(ev)`。**Textbox/InputDialog/チャット等、
     オンスクリーンキーボードを開いて入力する所はどこでも、入力先側は無改修で打てる**。共通の振る舞いは基底(`KeyboardPanel.cpp`)にあり、
     各キー盤は`physicalInsert/Backspace/Move`(+日本語は`physicalEnter`)で自分の編集操作へ繋ぐだけ:
     文字→カーソル位置 / Enter→単一行は決定・複数行は改行(Ctrl+Enterは常に決定) / Esc→決定して閉じる / Backspace・←→・Home・End /
     Delete→「右へ1つ動いて前を消す」(末尾なら`ITextInputTarget::onDeleteAtEnd()`。テキストエディタが次の行と繋ぐ)。
     ↑↓・Tab・Ctrl/Alt付きの文字はキー盤では扱わない。
     - 日本語のキー盤は**ローマ字かな漢字変換をする**(下の「物理キーボードのかな漢字変換」)
     - 数字のキー盤は数字・`.`と、使えるタブの記号表にあるものだけ(`*`→`×`、`/`→`÷`に直す)。英字は断る(他へも回さない)
  3. どちらも取らなければ捨てる(ウィジェットにフォーカスの概念が無いため。通常の画面のボタン操作等は対象外)。
- **自動調光/スリープ**: 列に打鍵があれば操作とみなす(`DisplayFunctions`/`PowerFunctions`が`Pending()`を見る)。**スリープから起こした打鍵は捨てる**
  (`DiscardPending()`。暗い画面のどこへ入るか見えないため。タッチの`swallow_touch`と同じ考え方)。
- **列・行の読み取り(`KeyInput_Functions.cpp`)は何にも依存しない**ので、Pad/Powerのホストテストはこちらだけをリンクする。
  配り先(`KeyInput_Dispatch.cpp`)は`SceneFunctions`/`KeyboardFunctions`に依存する(`Notification_Functions`/`_Sources`と同じ分け方)。
  **`KeyboardPanel.cpp`をリンクするテストは`KeyInput_Functions.cpp`も要る**(`EncodeUtf8()`)。
- **入力元**:
  - 実機: `script/pad_serial.py --mode text`(F1でコントローラー⇔文字入力を切り替え)。tkinterのkeysym/char/stateから行を作る
    (Ctrl+CはcharがETXになるのでkeysymを送る。AltのビットはOSで違う: Windows 0x20000 / X11 0x8 / macOS 0x10)
  - PCビルド: ウィンドウでのキー入力を`main_pc.cpp`の`SDL_AddEventWatch`が同じ行にして`PicoPcSerial::PushLine()`で受信口へ入れる
    (文字はSDL_TEXTINPUT、名前のあるキーとCtrl/Alt付きはSDL_KEYDOWN)。**LovyanGFXのSDLパネルは修飾キー無しのr/l/1〜6を画面の回転・拡大に
    使うので、`Panel_sdl::setShortcutKeymod(左Ctrl+左Alt)`へずらした**(Webも同じ)。`PICOOS_PC_KEYBOARD=off`で無効
  - Webビルド: ページの「PCのキーボード: 文字入力に使う」でJSが行を作る(`keydown`をcaptureで取り、SDL/コントローラーへは渡さない。
    Cmd/⌘付きはブラウザへ渡す)。ページの入力欄にフォーカスがあるときは取らない
- 検証: `key_input_test`(run.sh。行の読み取り・padの行との同居・列・UTF-8・画面→キー盤の順・英字/数字のキー盤の編集と決定)、
  PCビルドで標準入力へ`key`の行を流して: テキストエディタ(打鍵でキーボードが開く・改行・↑End Backspace ← Delete・↓Home)、
  SSH(接続先の入力・Backspace・Ctrl+Cで打ち直し)、入力テストのTextbox(日本語のキー盤へ英字と「あ」)/NumberInput(英字・使えない記号を断る)。
  `pad_serial.py`の行の組み立て、Webのページ(Chromiumで「文字入力」の行とコントローラーのときは送らないこと)。
  **実機のUSBシリアル・Webビルド本体(emsdk無し)・PCビルドの実ウィンドウでのSDLのキー入力(ヘッドレスでは来ない)・実際のSSHサーバ相手は未確認**。

### 物理キーボードのかな漢字変換 (`src/ime/Romaji_Kana.hpp` / `Keyboard`の物理キーの処理) (2026-10-03)

日本語のキー盤(`Keyboard`)が開いている間、物理キーボードの英字を**ローマ字として読みにし、Spaceで漢字へ変換する**。
辞書・候補の欄・読みの表示(`~読み~`)はフリック入力と共通で、新しく足したのはローマ字→かなと変換の状態だけ。

- **ローマ字→かな(`ime/Romaji_Kana.hpp`)**: 描画にもSDにも依存しないヘッダだけの部品。表で一番長く一致するものを取る。
  `n'`/n+子音 → ん、`nn` は次が母音/y なら「ん+な行」(konnichiha → こんにちは)・それ以外なら ん 1つ(kannji → かんじ)、
  子音の重ね/`tch` → っ、`- , . [ ] ~ /` → ー 、 。「 」 〜 ・。表に無い文字はそのまま読みに残す。
  まだかなにならない分(`ky`等)は`Keyboard::romaji`に持ち、読みの後ろへ続けて見せる。
- **状態**: 何も入力していない → (英字/上の記号)→ 読みを入力中 → (Space/↓)→ 変換中 → (Enter/1〜9/タップ/次の文字)→ 確定。
  - 読みを入力中: Enter=読みのまま確定、Esc=読みを捨てる、Backspace=ローマ字→かなの順に1つ、数字や他の文字は読みに足す、
    ←→等のカーソル移動は扱わない(画面やシェルへも漏らさない)
  - 変換中: Space/↓/→=次、Shift+Space/↑/←=前、1〜9=見えている候補の番号で確定、Esc/Backspace=読みへ戻る、
    文字を打つと選んでいる候補で確定してから次の読みを始める。キー盤のキーをタップしたときも確定するだけ(そのキーは働かない)
  - Ctrl+U / Ctrl+I: 読み(変換中でも)をひらがな/カタカナで確定
  - 何も入力していないときの数字・空白・記号・PC側のIMEで確定した文字は、今までどおりそのまま入る
- **変換は1回に1つの語(単文節)**。文節の区切りの情報が辞書に無いため。候補は「読み全体(送り無し・完全一致)」→
  「最後の1文字を送り仮名」→「最後の2文字を送り仮名」→ ひらがな → カタカナ の順に重ならないように並べる(`startConversion()`)。
  ひらがな/カタカナを最後に足すので、辞書に無い読み・辞書が無いPCビルドでも変換・確定できる。候補は16件(`IME_MAX_CANDIDATES`)、
  1候補24バイトに収まらないもの(送り仮名を付けて溢れたもの)は捨てる。
- **送り仮名は両方の方式**: 指定が無ければ上の自動。**大文字で送り仮名の頭を示すと(SKK式: `aruKu`/`utukusiI`)その切れ目の候補だけを引く**。
  大文字を含むかなが送り仮名の頭になる(母音の大文字ならそのかな自身)。送りの印は`RomajiKana::OkuriMarker()`で、
  **SKK-JISYOどおり母音の送りは母音そのもの**(い → i。フリックの「送り」が使う`IME_Functions::BuildOkuriKey`の表は
  い/う → w にまとめていて形容詞で外れるが、そちらは今回触っていない)。
- **配り順**: 日本語のキー盤が読みを入力中/変換中、または何も入力していない状態でローマ字を始める文字が来たら、
  **今の画面の`onKey()`より先にキー盤へ配る**(`KeyboardPanel::wantsKeyFirst()`、`KeyInput_Dispatch.cpp`)。
  テキストエディタの↑↓・SSHの「文字を全部取る」に負けないため。扱わない打鍵(Ctrl+S等)は画面へ回る。
  そのため`onPhysicalKey()`は**扱わないときに状態を一切変えずfalseを返す**決まり(同じ打鍵で2回呼ばれることがある)。
- **日本語⇔英字の切り替え**: 半角/全角(`key 0 zenhan`。`Key::Zenhan`)・Ctrl+Space・**Tabを400ms以内に2回**(CardKB2向け)。
  `KeyInputFunctions::CheckImeToggle()`(時刻を引数に取るのでホストテストできる)が見張り、`KeyboardFunctions::ToggleJapanese()`が
  `SwitchPanel()`で入れ替える(変換中の部分は確定して引き継ぐ)。**Tabの1回目は普通に配られる**(SSHなら補完のTabが1回飛ぶ。
  日本語の読みの途中ならキー盤が握りつぶす)。2回目はキー盤が開いていれば切り替えに使って捨てる。開いていなければ普通に配る。
  入力元: `pad_serial.py`(`Zenkaku_Hankaku`等)、PCビルド(`SDL_SCANCODE_LANG5`)、Webのページ(`Zenkaku`/`Hankaku`)。
  **PC側のIMEは切っておくこと**(入れたままだと母艦で変換した文字がそのまま届く。それはそれで入る)。
- **キーを畳む**: 物理キーボードの打鍵を扱ったら、日本語のキー盤を**候補の欄だけ(24px)**にする(`setCompact()`→`setPanelHeight()`)。
  右端の「あ」が日本語入力中の印で、タップするとキーを広げる。入力先から開き直す(`setVisible(true)`)と広げた形に戻り、
  日本語⇔英字の切り替え(`setShownSilently()`)では畳んだまま。据え置き表示ではテキストエディタ等の表示領域がそのぶん広がり、
  ダイアログ表示では空いた所に下の画面(斜線)が見える(`KeyboardDialog::attach()`の描き直し)。英字のキー盤は畳まない。
- 変換中は候補の欄で選んでいる候補を反転し、見えている候補に1〜9の番号を青で振る。選んでいる候補が見えるよう自動でスクロールする
  (`ensureCandidateVisible()`)。入力欄では読みは`~`(波線)、変換中の候補は`_`(下線)で囲む(`ITextInputWidget::isConverting()`)。
- 検証: `romaji_kana_test`(run.sh)、`key_input_test`(テストの中で小さい辞書をSDのスタブへ置き、Space/番号/↑↓/Esc/Backspace・
  自動とSKK式の送り仮名・Ctrl+U/I・変換中に打つと確定・画面より先に配ること・Tab2回/半角全角)。そのためスタブに
  `FsFile::seekSet()`とフォント指定の`textWidth()`を足した。PCビルドで標準入力へ`key`の行を流して、テキストエディタ(据え置き・畳んだ候補の欄)と
  入力テストのTextbox(ダイアログ・Tab2回で英字へ・「あ」で広げる)を`--shot`で確認した(辞書は一時的なSDのルートへ小さいものを置いた)。
  **実機・本物のSKK辞書・CardKB2・Webビルドでは未確認**(本物の辞書では1回の変換で最大3回辞書を引く。SDの速さは実機で見ること)。
- **未対応**: 文節の区切り・連文節変換、変換の学習(候補の並び替え)、読みの途中でのカーソル移動、全角英数への変換(F9/F10)。

### CardKB2(`src/functions/CardKB_Functions`) (2026-10-02)

SUMMARY.md未掲載(小粒のため、この節にだけ残す)。M5Stack Unit CardKB2(I2Cの小型QWERTY)を**あれば使える程度**で足した入力元。
外部コントローラーと同じく無いのが普通なので、未接続でもログ・画面に何も出さない(ステータスバーのアイコンも無し)。

- 打鍵は`KeyInputFunctions`の列へ積むだけ。届け先(画面の`onKey()`→開いているキー盤)はUSBシリアルのキーボードと共通なので、入力先側は無改修。
- **プロトコルはM5Unit-KEYBOARD(`UnitCardKB2`)の実装から**: アドレス0x5F(初代CardKBと同じ)、1バイト読むと押したキーのASCIIが1回(0=押していない)。
  Shift/Sym/Fnは反映済みで届く。リピートは本体側(300ms後から50ms)。**I2CモードではFn+D/Z/X/Cのカーソルキーが出ない**(本体の仕様)。
  初代の0xB4〜0xB7はカーソルキーとして読むが、CardKB2では来ない。UARTモード(Fn+Sym+2)のままだと何も返らず、未接続扱い。
- 配線はI2C0のGP0(SDA)/GP1(SCL)。`Wire`のピンと`begin()`はここで行う(**Wiiクラシックのドライバを足すときは同じバスなので`begin()`を共通へ移すこと**)。
- 検出: 未接続の間は1秒ごとに0x5F へ呼びかけ(ACKで接続)、接続中は10msごとに読み、読み出しが3回続けて失敗したら外れた扱い。
- PC/ホストテストは`pc/compat/Wire.h`(`PicoPcWire`で「I2Cの先に居るもの」を表す。既定は誰も居ない)。`script/host_test/stubs/Wire.h`はそれを取り込む。
- 検証: `cardkb_test`(run.sh)とPCビルドの起動(未接続で静か)。**実機・実物のCardKB2は未確認**
  (3.3V系のバスへ5Vのプルアップが乗らないか・`Wire.setSDA/setSCL`の名前・UARTモード出荷品が無いか)。

### テキストエディタ (`TextEditorScene` / `widgets/TextView`) (2026-09-29)

ランチャの「テキスト」(ファイルビューワーの「編集」からも、開いたファイルを渡して起動する)。スマホの文字入力と同じく、本文の下にオンスクリーンキーボードを**据え置いて**直接書き込む
(`KeyboardFunctions::Show(this, Layout::Japanese, true)`。上の「オンスクリーンキーボード」参照)。
右上のキーボードのボタンで出し入れでき、本文をタップするとそこへカーソルが移ってキーボードが開く
(閉じているときも同じ)。本文は上下のドラッグでスクロールする。新規/開く/保存(名前が無ければ名前を付けて保存)。

- **キーボードの入力バッファは192バイトなので、カーソルのある1行だけをキーボードへ渡す**(`attachLine()`)。
  キーボードが何か変えるたびに`onDisplayChanged()`でその行を文書へ書き戻す。行をまたぐ操作はシーンが受け持つ:
  改行キーは受け取ったテキストの`'\n'`で行を割って後ろ半分を次の行として渡し直す、
  行頭の1文字削除は前の行と繋げる(`onBackspaceAtStart()`)、行頭の←/行末の→は前後の行へ移る(`onCursorAtEdge()`)。
  **シーンからキーボードへ`setText()`等をしている間(`syncing`)は、返ってくる通知を無視する**(再入で行がずれるため)。
- 文書は固定長の1本のバッファ(32KiB。2026-10-01に4KiBから拡大)に`'\n'`区切り。上限は32KiB・1000行・1行191バイト。超えるファイルは
  保存で内容が消えないよう**開かずに断る**。入力で上限に当たったときは入力を取り消して状態欄へ理由を出す。
- 本文欄は汎用の`TextView`(下の「ファイルビューワー」参照。当初は`apps/TextEditView`だったのを汎用部品へ上げた)。
  「子を持たずrender()で直接描く」型で、文書のバッファを指すだけでコピーしない。折り返しは
  文書が変わったときだけ全体をやり直す(行の表は最大1024行ぶん、4KB)。1文字ごとの`textWidth()`は重いので、
  **文字の幅を小さな表に覚える**(ASCIIは128の表、それ以外はUTF-8のバイト列をキーに256スロットへ直接写像。Smallフォント固定)。
  カーソルは青の縦棒、変換中の読みは下線。
- ダイアログ(保存/開く/破棄の確認)は据え置きのキーボードより奥に出るので、開く前にキーボードを閉じる。
- 検証はPCビルドの`--tap`/`--shot`(入力・改行・行頭の削除での結合・英字への切り替え・タップでのカーソル移動・
  ボタンでの出し入れ・保存)。**実機では未確認**(1文字ごとに4KiBの折り返しをやり直す重さは実機で見ること)。

### SSHクライアント (`src/ssh/` / `SshScene` / `lib/monocypher/`) (2026-09-30)

ランチャの「SSH」。**画面全体が1つの端末**で、接続先・ホスト鍵の確認・パスワードも端末の中で聞く
(普通の`ssh`コマンドと同じ流れ。パスワードは画面に出さない)。キーボードは**テキストエディタと同じく
端末の下に据え置く**(`KeyboardFunctions::Show(this, English, docked=true)`)。

- **SSHクライアント(`Ssh_Client`)**: SSH2の対話シェル1本だけ(ポート転送・SFTP・X11は無し)。
  **方式は1つずつに絞った**(どれも今のOpenSSHの既定で有効): 鍵交換`curve25519-sha256`(`@libssh.org`も可、
  **`kex-strict`=Terrapin対策あり**) / ホスト鍵`ssh-ed25519` / 暗号`chacha20-poly1305@openssh.com`(MAC込み) /
  認証は公開鍵(`ssh-ed25519`)・パスワード・keyboard-interactive(PAMの「Password:」)。
  RSAのホスト鍵しか無い古いサーバ、AES/HMACしか受け付けないサーバには繋がらない(理由を端末に出す)。
  - 暗号の計算は**Monocypher**(`lib/monocypher/`、Luaと同じく`lib/<名前>/src/`へ無改造でvendor)。
    実機のBearSSLにはEd25519が無く、PCのOpenSSLとは呼び方も違うため、**PC/Web/実機で同じコードが動く**
    Cのライブラリにした。SHA-256はMonocypherに無いので`Ssh_Sha256.hpp`に自前で持つ(FIPSの例でテスト済み)。
  - 乱数は`SshUtil::Random()`: 実機は`rp2040.hwrand32()`(pico_rand)、PC/Webは`/dev/urandom`。
    `Battery_Functions`と同じく`#if defined(PICOOS_PC)`で分けた数少ない箇所。
  - **受信は`update()`から毎フレーム進める**(ソケットのポーリング、フレームを止めない)。TCPの接続だけは
    同期(`HttpTransport`と同じ制約)。鍵交換のX25519/Ed25519の計算も1フレームの中で同期に行う(**実機での時間は未計測**)。
  - 利用者の判断が要るところでは**状態を変えて止まって待つ**(`HostKeyCheck`→`acceptHostKey()`、
    `NeedPassword`→`providePassword()`)。known_hostsの照合はシーン側の仕事。
  - 確保はしない(受信8KB・送信1.3KB等の固定長、全体で約12KB)。**シーンが接続の間だけ`new`する**。
  - 受信窓は16KB・1回のデータは4KBまでとサーバへ伝える(大きな出力でも受信バッファ8KBに収まる)。
    半分読んだら`WINDOW_ADJUST`を返す。サーバからの鍵の交換し直し(rekey)にも応じる(その間の送信は溜める)。
  - 端末の大きさは`pty-req`と、変わるたびの`window-change`(キーボードの出し入れ・文字の大きさの切り替え)で伝える。
    `TERM=xterm-256color`。**`LANG`は送らない**(サーバ側のロケールを上書きしないため。サーバの既定がCロケールだと
    日本語の入力をシェルが受け付けないが、それはサーバ側の設定の問題)。
- **端末エミュレータ(`Vt_Terminal`)**: 描画を知らない文字の格子(ホストテストで中身を直接見られる)。
  xtermの主な制御(カーソル移動・消去・挿入削除・範囲スクロール・SGRの16色/256色/RGB→16色へ丸める・
  代替画面`?1049`・DECの罫線・問い合わせ`6n`/`c`への返事)に対応。全角(東アジアの幅広の文字)は2セル。
  セルは4バイトの固定長配列で、**最大40桁x40行の通常画面+代替画面+スクロールバック100行で約29KB**
  (シーンのメンバ。シーン本体が約30KBになる例外で、`ChatScene`等と同じ扱い)。
  ANSIの16色はパレット(VGA風の16色)とそのまま対応が取れる(`TerminalView`の`kAnsiToPico`)。
- **表示(`widgets/apps/TerminalView`)**: 「子を持たず直接描く」型で、`GameBoyView`と同じく
  **描くのは`FlushDirty()`の合成の中だけ**、`onFrame()`が変わった行(とカーソルの行)だけをdirtyにする。
  文字の大きさは2通り(上の段の「文字:大/小」、`/sys/ssh.cfg`の`font`に覚える):
  大=8x16(ASCIIは`AsciiFont8x16`、全角は日本語16pxフォント)で30桁、
  小=6x8(ASCIIは`Font0`、全角は16pxフォントを半分に縮めて描く=形が分かる程度)で40桁。
  罫線(U+2500〜)とブロック(U+2580〜)は線と塗りで描く(フォントの幅に依らず升目に揃うように)。
  上下のドラッグでスクロールバックを遡る(遡っている間は右上に黄色の目印)。キーを打つと一番下へ戻る。
- **入力**: キーボードの入力欄は常に空にしておき、文字が入るたび(`onDisplayChanged()`)すぐ送って空へ戻す。
  改行キー=CR、空のときの1文字削除=DEL(`onBackspaceAtStart()`)、空のときの←→=カーソルキー(`onCursorAtEdge()`、
  `?1h`ならSS3の形)。**日本語の変換中は送らず、読みを端末のカーソル位置へ重ねて出し、確定してから送る**。
  キーボードに無いキーは端末の下の**補助キーの列(`widgets/apps/TermKeyBar`)**: Esc / Tab / Ctrl(次の1文字を制御文字に。
  点灯する) / ↑↓←→ / ^C。接続前の入力(接続先・yes/no・パスワード)はシーンが1行ぶん自前で持つ(`SshScene::line`)。
- **ファイル**: `/sys/ssh.cfg`(無くてよい)の`target = user@host[:port]`(前回の接続先。次回は空Enterか「接続」ボタンで繋ぐ)
  と`font = small|large`。公開鍵認証の鍵は`/sys/ssh/id_ed25519`(**OpenSSH形式のssh-ed25519・パスフレーズ無し**。
  PCで`ssh-keygen -t ed25519 -N ''`して置き、`.pub`をサーバの`authorized_keys`へ)。パスフレーズ付き/RSAの鍵は理由を出して
  パスワードへ進む。信頼したホスト鍵は`/sys/ssh/known_hosts`へ追記(**OpenSSHと同じ書き方**。22番以外は`[host]:port`。
  PCの`~/.ssh/known_hosts`の行をそのまま置いてもよい。ハッシュ化された行は読まない)。**鍵が変わったら接続を拒否**し、
  該当する行を消すよう案内する。
- **秘密鍵の暗号化(2026-09-30)**: Wi-Fiのパスワードと同じ`PICO_Secret`(`util/Secret_Cipher.hpp`、用途文字列`"ssh-id-ed25519"`)。
  暗号鍵を画面のキーボードで打たせるのは現実的でないので、**PCで作った平文の鍵をSDへそのまま置けばよく、
  読んだときに平文なら暗号化して同じ場所へ書き直す**(`SshUtil::LoadPrivateKey()`。端末に「秘密鍵を暗号化して保存し直しました」)。
  - 見分け方: 暗号化したファイルは`enc1:`+16進の1行、平文は`-----BEGIN OPENSSH PRIVATE KEY-----`で始まる
    (先頭の空白・改行は飛ばして見る。`IsEncryptedKeyText()`)。
  - 暗号化するのは`"pico-ssh-ed25519:"`+秘密鍵64バイトの16進(公開鍵は秘密鍵の後半32バイトなので持たない。
    コメントは捨てる)。`PICO_Secret::kMaxPlainBytes`(255)に収まるよう、OpenSSHの鍵ファイル丸ごと(約400B)ではなくこの形にした。
    復号して目印`pico-ssh-ed25519:`が合わなければ壊れた扱い(`kKey`の違うファームで作られた場合もこれになる)。
  - 書き直しは一時ファイル(`id_ed25519.tmp`)へ書いて**読み戻して同じ鍵に戻ることを確かめてから**差し替える。
    失敗しても元の平文の鍵は壊さず、その回は平文のまま使う(端末に理由を出す)。壊れた鍵・パスフレーズ付きの鍵は書き換えない。
  - **守れるのは「SDだけを落とした/見られた」場合だけ**(Wi-Fiと同じ限界。暗号鍵`kKey`はファームに焼かれた既定値で、
    リポジトリを見れば分かる)。PC側に残した元の鍵ファイルは利用者が管理すること。
- **画面を離れたら切る**(`onExit()`。受信を進める者がいなくなるため)。
- **踏み台(ProxyJump)**(2026-09-30): 接続先を`user@host[:port] -J user@踏み台[:port]`と書くと(OpenSSHの`ssh -J`と同じ)、
  踏み台へSSHしてから`direct-tcpip`(RFC 4254 7.2)の通り道を開き、**その上でもう一度SSHする**。
  **狙いは外からTailscaleのtailnetへ入ること**: Tailscaleそのものに参加するのは現実的でない(公式はGo製で組み込み向けの
  C実装が無く、自前で書くにはWireGuard+非公開寄りの制御プロトコル+DERP+大きなJSONが要り、RAMも足りない)ので、
  Tailscaleの入った常時起動の機械(自宅のRaspberry Pi等)を踏み台にする。`host`はMagicDNSの名前でよい(名前は踏み台の側で引かれる)。
  暗号は相手のホストまで途切れないので、踏み台で中身を見られることは無い。踏み台のsshdは外から届く必要がある
  (ルーターでポートを開ける。パスワード認証は切って公開鍵だけにすることを勧める)。
  - `SshClient::setForward()`でシェルの代わりに通り道を開く。`SshTunnel`(`Ssh_Client.hpp`)が通り道を`SshStream`として見せ、
    中の`SshClient::connectVia()`がTCPの代わりにそれと話す(`SshClient`のTCPの読み書きは`io*()`の4つに集めた)。
  - **流れの制御**: 通り道のデータは`SshTunnel`の受信の輪(8KB)へ溜め、**中が読んだ分だけ**踏み台の受信窓を広げる
    (`setWindow(8KB, manual=true)`+`consume()`)。受け取った時点で窓を広げると、中が読むより速く届いて輪が溢れるため。
    鍵の交換し直しの間に送れなかった窓の調整は、終わったときに送る(`adjustWindow()`)。
  - 送信の溜め(`pending_out_`)を512B→2KBへ(通り道には中のSSHのパケット=最大1.3KBがまとめて来るため)。
  - 踏み台経由の間は `SshClient`2つ+輪で約34KB。片付けは中→通り道→踏み台の順(`SshScene::dropClient()`)。
  - ホスト鍵(known_hostsは踏み台・相手それぞれの名前で持つ)とパスワードは両方について聞く。どちらを聞いているかは`prompt_client`。
    踏み台が切れて中が「接続が切れました」になった場合は、踏み台の理由の方を出す。
  - 検証: `ssh_net_test`に踏み台の項目(同じsshdを踏み台にして`localhost`へ。大きな出力が8KBの輪を何度も跨いでも欠けない・
    踏み台から繋げない相手は理由付きで失敗・中を閉じると踏み台も閉じる)。PCビルドの`--tap`で踏み台経由のログイン→`exit`を確認。
    **本物のTailscale越しの確認はしていない**(この環境にtailnetが無い。踏み台から先は普通のTCPなので、踏み台のsshdが
    `AllowTcpForwarding`(既定yes)なら同じに動くはず)。
- アプリ数: SSHを足したところで`AppFunctions::kMaxApps`(24)が静的13+Lua Hello+SDのLuaアプリ10本で埋まっていたので**32へ広げた**
  (約2KBのstatic RAM増)。アイコンはtablerの`terminal-2`(`IconID::Terminal`、末尾へ追加)。
- 検証: `vt_terminal_test`/`ssh_util_test`(run.sh)、**`ssh_net_test`(run_net.sh。本物のOpenSSH 9.6のsshdを使い捨ての鍵で立てる)**:
  鍵交換→ホスト鍵の確認→公開鍵認証→pty付きシェル→出力が端末に出る・`stty size`で大きさ・数万行の出力(受信窓の調整を跨ぐ)・
  UTF-8・終了コード・信頼しない/認証方式が無い/繋がらない場合。パスワードとkeyboard-interactive(PAM)は母艦に利用者を
  作って手で確かめた(`SSH_TEST_PW_*`)。PCビルドの`--tap`で接続→yes→ログイン→`ls`・`top`(文字:小)・日本語入力(変換中の表示→確定で送信)を確認。
  **実機では未確認**(鍵交換の計算時間・`rp2040.hwrand32()`・`WiFiClient::setNoDelay`・Monocypherのスタック使用量(Ed25519の検証で数KB)は実機で見ること)。
  **Webビルドでは繋がらない**(生のTCPソケットが無い。HTTPと同じ制約)。

### ファイルビューワー (`FileViewerScene` / `widgets/TextView` / `widgets/ImageView`) (2026-09-29)

**ランチャからは開かない。** ファイルを探すのはファイルアプリ(`FileExplorerScene`)に任せ、そこでファイルを
2回タップすると`SceneFunctions::Push(new FileViewerScene(path))`される(「戻る」でファイルアプリへ戻る)。
以前は自前の`FileExplorer`と3種の表示部品を`onEnter()`で全部作っていたが、1ファイルだけを見る画面にして、
**表示部品は開いたファイルの種類の1つだけを作る**(`MarkdownView`は約40KBあるため)。

- `.md`/`.markdown` … `MarkdownView`(変更なし)
- `.pimg` … **`ImageView`**(汎用、`widgets/`)。以前は`Image(onRAM=false)`を`ScrollContainer`へ入れていたため、
  **1px動くたびにSDから.pimgを頭から読み直して1画素ずつ解いていた**。今は開いたときに1回だけスプライトへ解き、
  描くときは`pushSprite()`するだけ。4bppで`w*h/2`が`kMaxFullBytes`(64KiB)に収まれば全体を持ち、
  超える大きな画像は**表示欄と同じ大きさの窓だけ**を持つ(ドラッグ中は窓をずらして見せ、指を離したときに
  その位置の窓だけSDから読み直す)。窓の読み出しは`IconRender::DecodePimgWindow()`(512Bずつまとめて読み、
  窓の外のランは読み飛ばし、窓の最後の行を過ぎたら残りは読まない。ランは`drawFastHLine()`でまとめて書く)。
  表示欄より小さい画像は中央に置く。上下左右のドラッグでスクロール。
- それ以外 … **`TextView`**(汎用、`widgets/`)。`Label`+`ScrollContainer`は**画面外の行まで毎回レイアウト・描画していた**ので、
  テキストエディタの本文欄を汎用部品へ上げて使い回した(見えている行だけを描く)。マークアップは解釈しない。
  読むのは先頭16KiBまで(`kMaxTextBytes`)で、`'\r'`と`NUL`は捨てる。超えたら状態欄に「(途中まで)」と出す。
  行の表(1024行)が溢れた場合も同じ(`TextView::isTruncated()`)。
- **右上の「編集」でテキストエディタを開く**(画像以外=テキスト・Markdown。`SceneFunctions::Push(new TextEditorScene(path))`)。
  エディタは`initial_path`を最初の`onEnter()`で1回だけ読む(`initial_loaded`。`Pop()`で戻ってきたとき読み直して編集内容を
  捨てないため)。エディタの「戻る」で戻るとビューワーは`onEnter()`から作り直されるので、保存した内容が反映される。
  エディタの上限(32KiB・1000行・1行191バイト)を超えるファイルは、エディタが理由を出して開かない(ビューワーの閲覧は16KiBまで)。
  画像の編集エディタは今のところ無いので、`.pimg`には「編集」を出さない(ペイントは`Canvas`のLuaアプリで、ファイルを引数に取れない)。
- 検証はPCビルドの`--tap`/`--shot`(長いテキストのスクロール、480x600の画像=窓モードのドラッグと読み直し、
  48x24の画像=全体モードで中央、Markdown、「戻る」でファイルアプリへ)。**実機での速さは未計測**。

### 電卓(関数電卓・グラフ電卓) (`CalculatorScene` / `util/Calc_Eval.hpp` / `widgets/apps/CalculatorKeypad` / `widgets/apps/GraphView`) (2026-10-03)

タブは「電卓 / グラフ / 履歴」。使える関数の一覧と操作はSUMMARY.md「電卓」。ここには設計の判断だけ残す。

- **式は文字列のまま、評価のたびに読む**(`CalcEval::Evaluate(expr, Context)`。中間形式へ翻訳しない)。
  グラフは画素の列ごと(最大240列×3本)に評価して`GraphView::samples`(float、約2.9KB)へ覚え、式・範囲・角度・Ansが
  変わったときだけ計算し直す。FlushDirty()はdirty矩形ごとにrender()を呼ぶので、毎回評価すると同じ計算を何度もするため。
  描くのは`GameBoyView`と同じく**FlushDirty()の合成の中だけ**(`PICO_GFX::isDirtyDeactivates`)。**実機での評価の速さは未計測**
  (重ければ式を逆ポーランドへ1回だけ翻訳する形にする)。
- **数値は自前で読む**(`strtod`は`inf`/`0x10`を読み、`2e`が定数eとぶつかるため)。指数はEXPキーの大文字`E`。
  結果の表示も`1e+20`ではなく`1E20`にして、履歴から読み戻した式がそのまま評価できるようにした。
- **省略した掛け算は×と同じ強さで左から**(`6÷2(1+2)=9`)。nCr/nPrは×より強い、`-2^2=-4`、累乗は右結合。
  関数名は前方一致を長い順に試す(`asinh`を`asin`/`sin`に食わせない)。`C`/`P`/`E`が大文字なのは小文字の関数名・定数とぶつけないため。
- **直角の倍数の三角関数は表で答える**(`exactQuarter()`)。sin(180°)が1.2e-16、tan(90°)が1.6e16になるのを防ぐ(tanはDomain)。
- キーの表示は**ASCII中心**(`x^2` `x^-1` `10^x`)。²や⁻¹は日本語フォントに無いかもしれないため。収まらない長いラベル
  ("SHIFT" "acosh")だけLovyanGFXの`Font0`で描く(ホストテストのスタブに`fonts::Font0`を足した)。
- **グラフの角度の単位は電卓と別(`graph_angle`、既定RAD)**。度のままだと横-10〜10で`sin(x)`の振幅が約0.17にしかならず平らな線に見えた
  (実際に「sin(x)/cos(x)がうまく動かない」と指摘された)。`DRG`キーは今のページの単位だけを切り替える。
- `=`の後に演算子を押すと`Ans`から続ける(以前は結果の文字列を式へ写していたが、桁が落ちるため)。
- 物理キーボードは`CalculatorScene::onKey()`で受ける(日本語のキー盤が開いていない画面なので、そのまま画面へ届く)。
- 検証: `calc_eval_test`/`calculator_test`(run.sh)、PCビルドの`--tap`+標準入力の`key`行で計算・SHIFT表示・3本のグラフ・トレースを`--shot`で確認。
  **実機では未確認**。

### ClocksScene 実装詳細

画面下部の`TabBar`で「時計 / タイマー / ストップウォッチ」を切り替える1画面のアプリ
(`Feature` enumとタブのindexを一致させてある)。**新しく画面を足すときの手本として一番新しい。**

- **計測は全て`millis()`の差分で積む。** `TimeFunctions`は333msごとにしか更新されない上に、
  **NTP同期で時刻がいきなり飛ぶ**ため計測には使えない。時計の表示だけが`TimeFunctions::timeinfo`を読む。
- **別のタブを見ていてもタイマー/ストップウォッチは進む**(`onUpdate()`が常に両方を回す)。
  タイマーが鳴ったらタイマーのタブへ引き戻す。音が出せないので、鳴った合図は**数字を赤で点滅**させて出す。
- **`Push()`で別のシーンへ移っている間は`onUpdate()`が来ない**ので、その間の経過は積めない。
  `onEnter()`で`timer_last_tick_ms`等の基準を今へ取り直し、復帰時に止まっていた時間を一気に差し引かないようにする。
- **表示/非表示を決めるのは`applyVisibility()`の1箇所だけ**。feature/modeから全ウィジェットの`setVisible()`を
  まとめて決める。隠れている間は値を流し込まないので、**表に出した側は`before_sec`/`before_mday`を`-1`へ戻して
  埋め直す**(`applyFeature()`/`applyMode()`)。
- **見えていないウィジェットを更新しない**のは必須。`needsRender()`は表示状態を見ないため、
  隠れたウィジェットを毎秒更新すると無駄なdirty矩形が積まれ続ける。
- **「毎ティック動かすもの(数字)」と「状態が変わったときだけ動かすもの(ボタンの文字・状態の1行)」を
  関数ごと分けてある**(`refreshXxxDigits()` / `refreshXxxControls()`)。まとめて毎ティック呼ぶと、
  文字が同じでもLabel/Buttonがdirty登録するぶんだけ画面の合成が走り続ける。
- ストップウォッチは1/100秒まで出すが、**書き換えは50ms間隔へ間引く**(毎フレームだと`Label`の再レイアウトが重い)。
- 上部の行の高さ(`top_row_h` / `action_row_h`)は`onEnter()`で**Buttonの実測値**を入れる。
  定数で持つとフォントを変えたときにずれる。表示領域の計算は`bodyRect()`の1箇所へ集約。
- feature/modeはシーンのメンバなので`onExit()`を跨いで残る(上へ別のシーンを`Push()`して戻ると復元される)。
  ただし**ランチャから開き直すとシーンごと作り直されるので既定値へ戻る**。起動をまたいで覚えるなら
  `Config_Functions`で保存する話になる。

このシーンの実装に伴って入った、ウィジェット側の地味な修正:

- `PICO_GFX::MarkDirty()`が**面積ゼロの矩形を捨てる**ようになった。`Label`は「消すべき古い領域」として
  未使用のカーソル矩形(`{0,0,0,0}`)を毎回`markdirty`するので、捨てないと`FlushDirty()`が
  全ウィジェットの当たり判定と`pushSprite()`を1周ぶん空回りする。
- `Button::setText()`を追加し、`setW()`/`setH()`で与えた大きさを`fixed_w`/`fixed_h`に覚えるようにした。
  **押すたびにラベルが変わるボタン**(開始/一時停止/再開)で箱の大きさが伸び縮みすると、並べたボタンの
  位置がずれてしまうため。
- `Label::setText()`の`FixedString`版も、`const char*`版と同じく**変化が無ければ再レイアウトしない**。

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
- UTF-8の**文字列操作はFixedStringの担当**、`functions/UTF8_Functions.hpp` は
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
- 日本語IMEはSKK辞書方式、`script/convert_skk_dict.py`で辞書データ(`skk_body.tsv`/`skk_index.tsv`)をSD収録用に変換。

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
- **例外は2ファイルだけ**: `src/config/LGFX_Config.hpp` と `src/functions/Touch_Functions.hpp` が
  `#if defined(PICOOS_PC)` で `pc/compat/` 側(`<config/LGFX_Config_PC.hpp>` /
  `<functions/Touch_Functions_PC.hpp>`)を取り込む。
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

### Webビルド (WebAssembly)

**ネイティブとの差はループの回し方(`main_pc.cpp`)とビルド設定だけ**で、`src/` も `compat/` も
共有している(`compat/` 内の分岐は `__EMSCRIPTEN__` で3か所のみ)。

- **ループ**: ブラウザのメインスレッドは止められないので、`Panel_sdl::main()`(別スレッドで
  ユーザコードを回す)は使えない。`Panel_sdl::setup()/loop()/close()` が公開されているので、
  `emscripten_set_main_loop()` から「`loop()` 1回 + `Panel_sdl::loop()` 1回」を刻む。
  **実測60fps**(pico-os側のループ負荷は120フレームで数ms)。
- **SDカード**: `pc/sdcard/` を `--preload-file` で `index.data` へ焼き込み、仮想FS(MEMFS)の
  `/sdcard` に載せる。POSIXのまま読めるので `compat/SdFat.h` は無改造。
  **中身を変えたら再ビルドが必要**で、書き込みはリロードで消える。
- **設定**: ブラウザに環境変数が無いので、`main_pc.cpp` が起動時にURLのクエリを `setenv()` する
  (`?wifi=disconnected&rssi=-85`)。`compat/` 側はいつもどおり `getenv` を読むだけ。
- **描画はソフトウェア(canvas 2D)に固定**。`main_pc.cpp`が起動時に
  `SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software")`を立てる(SDLはヒントで名指しした
  ドライバをACCELERATEDの要求と突き合わせないので、**LovyanGFXは無改造でよい**)。
  理由は速度ではなく**GPU描画だと「フレームは進んでいるのに画面が出ない」状態になりうる**こと:
  LovyanGFXの`sdl_create()`が`ACCELERATED|PRESENTVSYNC`を要求する→SDLのGLES2レンダラが
  `SDL_WINDOW_OPENGL`を足すため**`SDL_RecreateWindow()`でウィンドウを作り直す**→
  emscriptenのSDLはウィンドウを壊すとき**canvasを0x0へ縮める**
  (canvasそのものは壊せないため)。つまり**GPU描画は起動のたびにcanvasが0x0を通り**、
  作り直しに失敗すると0x0のまま残る。**WebGLの有無では判別できない**
  (「WebGLあり・canvas 0x0」の実報告あり)。ソフトウェア描画は`SDL_WINDOW_OPENGL`を
  要求しないので作り直しごと起きない。比較用に`?render=gl`でGPU描画に戻せる。
  速度はどちらも60fps。起動直後にcanvasの大きさをC++側で1回確認し、0x0なら
  `SDL_GetError()`ごとログへ出す。
- **canvasの大きさの判定をまたぐ**。emscriptenのSDLは`Emscripten_CreateWindow()`で
  canvasを1x1にしてCSS上の実測値を測り、**`floor(実測値) != 1`ならその実測値を画面の
  大きさに採用する**(external_size)。CSS無指定でもズームや端数で`0.9999998`が返ることがあり、
  `floor`で0→**canvasもウィンドウも0x0**→ソフトウェア描画が`createImageData(0,0)`で例外→
  **1フレーム目でループが止まる**(実報告あり)。そこで`main_pc.cpp`が**ウィンドウを作る間だけ
  canvasを1.5pxに固定**し(`pinCanvasCssSizeForProbe()`)、判定が必ず1になるようにしてから
  `releaseCanvasCssPin()`で外す。レイアウト前(実測値0)の間はウィンドウを作らせず待つ(最大60フレーム)。
  **external_size側へ倒してはいけない** — 480x640と正しく明示してもモードは同じで、
  SDLがウィンドウの内部サイズとCSSの箱を同期しなくなり、LovyanGFXの拡大率とタッチ換算の
  前提が崩れて**縦横比が崩れタップ位置がずれる**(これも実報告あり)。
  `pc/web/shell.html`側はcanvasに`max-width`等を普通に書いてよい(固定が外れた後に効くだけで、
  SDLがマウス座標をCSS上の大きさで割り戻すためタップはずれない)。詳細は`pc/README.md`。
- **Wi-Fiの疎通判定**: ソケットが無いので `navigator.onLine` を見る(`compat/WiFi.h`)。
- **Markdownブラウザのオンライン機能はWebでは動かない**。`compat/WiFiClient_PC.h` は生のTCP
  ソケットを使うが、ブラウザにはそれが無い(emscriptenはWebSocket経由へ流すので中継サーバが要る)。
  コンパイルは通り、SDから読む分(`/tmp/doc.md`)は普通に動くが、`browser-home` を設定したり
  「更新」「検索」を押してもサーバへは繋がらない。**Web公開版で試せるのはローカル文書まで。**
  HTTPSも同じ理由で持たない(`compat/WiFiClientSecure.h`が常に失敗するスタブになる)ので、
  カレンダーの取得もWebでは動かない(SDに焼き込んだ`.ics`の表示はできる)。
- **`delay()`**: 待つとタブが固まるのでWebでは即座に戻る(`src/` は使っていない)。
- **ページの外枠**: `pc/web/shell.html`(emscriptenの `--shell-file`)。canvas・ログ欄・
  Wi-Fi状態の切替・画面のPNG保存ボタンを持つ。デバッグ用の道具を足すならここ。
- **`src/`へ新しい依存を足すときはWebビルドも通すこと**。ネイティブが通ってもemscriptenで
  落ちる依存(生ソケット/スレッド/ブロッキング待ち)があるため。
- **公開**: `.github/workflows/web-pages.yml` が `main` へのpushで
  https://kimu1109.github.io/pico-os/ へ自動デプロイする(プルリクではビルド確認のみ)。
  **リポジトリの Settings > Pages で Source を「GitHub Actions」にしておくことが前提**
  (ワークフローからの自動有効化は`GITHUB_TOKEN`の権限ではできない)。
  emsdkの版はワークフローの `EMSDK_VERSION` で固定。公開中のコミットはページのログ先頭の
  `[WEB] pico-os build: <hash>` で分かる。
  **公開版の `index.js`/`index.wasm`/`index.data` はリビジョン付きの名前(`index.<hash>.js` 等)で置く**(2026-10-08)。
  同じ名前だとGitHub Pagesの`max-age=600`の間にブラウザが古い`index.js`と新しい`index.data`を組み合わせ、
  SDのファイルの位置がずれて別のファイルの途中を読んだ(公開直後に「ブロック」の`palette.lua:1: unexpected symbol near '<\227>'`として出た)。
  `shell.html`の`<meta name="picoos-asset-rev">`をワークフローが書き換え、`Module.locateFile`がそれを見て名前を差し替える
  (ローカルのビルドは書き換えないので従来の名前のまま)。

## ロードマップ・TODO状況(2026-09-21時点)

相談が来た際はまず本表を見て、「既存機能の拡張」か「ゼロから設計する新機能」かを見分けること。
**進捗の一次情報源は`SUMMARY.md`**(番号は同ファイルの大項目と揃えてある)。
本表は**そこへ判断のための一言を足しただけ**なので、**都度 `SUMMARY.md` をfetchして最新状況を確認すること。**

| # | 大項目 | 状況 |
|---|---|---|
| 1 | ダイアログ系統 | **全て実装済み(betaレベル)**。上記ダイアログカタログ参照。数字専用(電卓用)キーボード`KeyboardNum`も実装済み。 |
| 2 | 汎用基盤 | **実装済み**。ウィジェットIDはファクトリ・`Resolve()`ともに実装され、`Resolve()`は`LuaEngine`(`pico.set/get/on/destroy/add_child`等)から実際に呼ばれている。 |
| 3 | スクリーン管理 | メモリ解放(`DestroyLater`)・パネル/グリッドレイアウト(`LayoutContainer`/`GridContainer`)・**シーン遷移+画面スタック(`Scene`/`SceneFunctions`)は実装済み**。**メモリプール化(汎用)は計測の結果いったん保留**(下記「メモリ計測の結論」参照)。**⚠ PCビルドでシーン遷移を繰り返すとヒープ下限が際限なく増える未解決の問題あり**(下記「メモリ計測の結論」内の該当節参照)。 |
| 4 | Wi-Fi管理強化 | **実装済み**。非ブロッキング接続・スキャン・NTP同期・電波強度アイコンに加え、`SUCCESS`中は`HEALTH_CHECK_INTERVAL=5000ms`ごとに`WiFi.status()`を確認し、切断を検知したら`ConnectWiFiAsync()`を呼び直す(`currentPassword`を再接続用に保持)。`SettingsScene`から周辺スキャン→選択→パスワード入力→接続まで一般的な「Wi-Fi設定」と同じ操作でできる(下記「Wi-Fiの新規接続」参照)。**複数のネットワークの保存・ON/OFF・未接続時の自動再接続(直近で接続したものから)も実装済み**(「保存済みのWi-Fiネットワーク」参照)。 |
| 5 | Luaアプリ/API | **2Dゲームの簡易エンジン`pico.game`(2026-10-06)あり**。**`LuaEngine`+`LuaScene`が動き、ランチャから実際にLuaアプリを起動できる(2026-09-19着手)**。ウィジェット操作(生成/破棄/プロパティ/共通コールバック+ウィジェット固有コールバック)・直接描画(Canvas)・SDカードアクセス・画像(.pimg)・シーン制御(push_scene/change_scene/launch_app)・ダイアログ・ネットワーク(HTTPリクエスト)・時刻取得・実行時間の安全網(`lua_sethook`による暴走防止)・SDを走査したLuaアプリの自動登録(`LuaAppScanner`)・**権限管理(network/sd_outside_app_dirの粗いフラグ、2026-09-21追加)**・`pico.remove_child`/`pico.list_add`/`pico.list_clear`/`pico.tab_add`等の細部の穴埋め(2026-09-21)まで実装済み。**既知の欠けは無い**。詳細は下記「Luaバインディング」「Lua着手前の受け皿の状態」を参照。 |
| 6 | PC/Web動作対応 | **実装済み**(`pc/`)。上記「PC / Web実行環境」参照。 |
| 7 | 標準アプリ開発 | **実装済み**。Markdownブラウザ(`PROTOCOL.md` v1を一通り)・時計(`ClocksScene`)・電卓(`CalculatorScene`。関数電卓+グラフ電卓、2026-10-03)・ファイルエクスプローラー(`FileExplorerScene`)・辞書(`DictScene`)・設定(`SettingsScene`)の6本。詳細は`SUMMARY.md`「7. 標準アプリ開発」参照。 |
| 8 | セカンダリアプリ開発 | **C++ネイティブでの本格実装は未着手**(テトリス風・シューティング・リマインダー等)。**チャットは自前のサーバ(`server/chat/`)+ Webクライアント + `ChatScene`として実装済み**(下記「チャット」参照)。**カレンダーは`.ics`の読み取り(`src/calendar/Ical`)・月表示の画面(`CalendarScene`)・HTTPSでの取得(`Calendar_Sync`)まで入った**(下記「iCalendarの読み取り」「CalendarScene 実装詳細」「HTTPS」参照)。**マインスイーパー/リバーシ/ブロック崩し/スクラッチパッド/ペイント/テトリスはLuaアプリ(`pc/sdcard/lua/apps/`、SDスキャンで自動登録)として実装済み**(ペイントは下記「ペイント」参照。リバーシ・マインスイーパー・ブロック崩し・テトリスは`pico.game`の上に作り直した=下記「ボードゲーム4本のpico.gameへの移行」)。**SSHクライアント(`SshScene`)もC++で実装済み**(下記「SSHクライアント」参照)。**TODO/リマインダーはTodoist連携の`TodoScene`として実装済み**(下記「TODOアプリ」参照)。スクラッチパッド(黒/青ペン+消しゴムの手書きメモ)を作る過程で、`CanvasRaster`のリサイズと`pico.canvas_clear/save/load`をLua APIへ追加した(下記「ラスタキャンバスの保存/読み込み」参照)。 |
| 9 | GBエミュ | **Peanut-GBを採用し、第1段(256KBまでのROMをRAMへ丸ごと読む)が`GameBoyScene`としてPCで動作**。音も鳴る(#11)。実機での速さ・256KB超のROM・GBCは未(下記「ゲームボーイ」参照)。 |
| 10 | 外部コントローラー | **入力の窓口(`PadFunctions`)とUSBシリアル経由のPCキーボード入力(`script/pad_serial.py`。Webビルドはページのボタン/キーボード)、GBエミュ・Lua・ステータスバーへの組み込みまで**。方式はWiiクラシックコントローラー(I2C)に決めたが実物・ドライバは未(上記「外部コントローラー」参照)。 |
| 11 | Chiptune音声再生 | **出力の土台・4チャンネルの音源・2コア目での合成・曲データ(MML)まで**: I2S(MAX98357A)・アンプの抜き差しの検出・未接続のときの扱い・ステータスバーのアイコン・PC/Web版(SDL)・Luaの`pico.sound_*`/`pico.music_*`・動作確認アプリ「チップチューン」・ミュージックアプリ。MIDIはPCの`script/midi2mml.py`で取り込む。GBエミュの音(音源チップの再現)も鳴る。**実機での確認は未**(下記「音声出力」「曲データ」「ゲームボーイの音」参照)。 |

**#7は完了しており、#5(Lua)の前提として十分な実例が揃った。** Lua APIの仕様は「C++で標準アプリを
書いてみて必要になったもの」から逆算するのが確実で、`MarkdownScene`/`ClocksScene`/`CalculatorScene`/
`FileExplorerScene`/`DictScene`/`SettingsScene`の6本が実装済み。`AppGrid`/`ColorDialog`/`KeyboardNum`型の
「render()直描き」パターンや`ClocksScene`のvisibility管理パターンなど、バインディング設計の材料はこれで揃った。

## 未実装の設計アイデア(旧pico-osからの持ち越し議論)

- **ウィジェットのメモリプール化(汎用)**: 実測の結果、現時点では保留と判断した(下記「メモリ計測の結論」)。再開する場合は`Widget::operator new/delete`をアリーナへ差し替えるところから。
- **Markdownブラウザのブラウザ化**: 仕様は `PROTOCOL.md`(HTTP/行指向TSV/SDをキャッシュにする方式)、サーバの参照実装は `script/reference_server.py`。
  **第1段(リンク追従・履歴・ナビゲーションヘッダー)と第2段(キャッシュ層)は実装済み。**
  第1段: `MarkdownScene`が履歴を自前で持ち、`MarkdownView::setOnLinkTap()`から`PICO_IO::resolve()`で相対パスを解決して同じシーンのまま開き直す。
  第2段: `storage/Doc_Cache.hpp`(下記)。
  第3段(HTTPクライアント): `util/Url.hpp` / `net/Http_Response` / `task/Http_Get`(下記)。
  第4段(取得→キャッシュ→表示の配線): `net/Doc_Fetch`(下記)。**ここまでで「サーバ上の文書を読む」が成立している。**
  `network.cfg` の `browser-home` にURLを書くと、Markdownアプリがそこを開く。
  第7段(検索・リロード): `net/Doc_Search` + `dialogs/SearchDialog`(下記「検索」)。
  第8段(マニフェスト): `net/Manifest`(下記「マニフェスト」)。
  第5段(画像の解決と先読み)・第6段(discovery)も実装済み。
  **`PROTOCOL.md`のv1はこれで一通り実装できている。**
  - **リンクごとに`SceneFunctions::Push`してはいけない**。スタック上限が`kMaxSceneDepth=4`しかなく4回で詰む。履歴はシーンが持つ(`kMaxHistory=8`、パス+スクロール位置)。
  - `MarkdownScene`の履歴に載るのは**「場所」でSDパスとURLのどちらもあり得る**。見分けは`UrlTools::Parse()`が通るかどうかの**1箇所だけ**で、`"http://"`の判定を各所へ撒いていない。
  - **`MarkdownScene`のオブジェクトは数KBある**(`DocFetch`約3KB + `DocSearch`2.9KB + 履歴1.6KB等。`Url::path`を192Bへ広げたぶん少し増えた)。
    他のシーンと違い「シーン本体は数十バイト」ではないので、シーンスタックへ積んだままの間も乗り続ける。
  - **履歴の現在地(`history_pos`)は「表示中の文書」と必ず一致させる**。ここは相対リンクを解決する
    基準でもあるため、開けなかった場所を現在地のまま残すと、**画面には前の文書が出ているのに
    次に踏んだリンクだけが開けなかった場所を基準に解決される**(実際に出たバグ)。
    遷移が成功したら`commitNavigation()`、失敗したら`abortNavigation()`(積んだ履歴を捨てて
    表示中の位置へ戻し、ホストが変わっていれば`ServerInfo`も捨てる)を必ず通すこと。
  - **画像は「表示前に」取りに行く**(第5段)。`MarkdownView::layoutBlocks()`が画像ファイルのヘッダを読んでブロックの高さを決めているため、表示してから届けると再レイアウト(全ブロックの整形やり直し)が要る。`MarkdownScene`が文書取得後に`MdScan::ImageRefInLine()`で走査し、キャッシュに無いものを**1フレーム1枚ずつ**取ってから`load()`する。表示は全部揃ってからだが**ループは止まらない**(フッタに「画像を取得中 2/5」が出る)。
    - **1枚失敗したら残りは諦める**。相手へ届いていないので残りも同じで、諦めないと接続待ち(最大3秒)を枚数ぶん繰り返す。
    - 1ページ`kMaxPrefetchImages=8`枚まで。既にキャッシュにある画像は取りに行かない(2回目の訪問は取得ゼロ)。ローカル文書は走査ごと飛ばす。
    - `MdScan::ImageRefInLine()`の判定規則は**`parseBlocks()`の画像ブロックと必ず一致させること**。ずれると「取ってきたのに表示されない」「表示されるのに取ってこない」が起きる。
- **Markdownブラウザのヘッダー/フッター**: ナビゲーション用(戻る/進む/パス/検索)ならScene側にウィジェットを並べるだけで**View改修は不要**。文書由来(タイトル固定表示等)をやる場合のみ、`l_rect`内での高さ控除が論点になる — その際は「ビューポート=`l_rect`全体」という前提が7〜8箇所に直書きされているので、`viewportRect()`へ集約するのが先。
- ~~**LuaでのウィジェットID管理**~~: **解消済み(2026-09-19)**。32bit整数IDの発行側・ファクトリ(`WidgetID.hpp`/`WidgetRegistry`/`WidgetFactory`)に加え、`Resolve()`を叩くバインディングと、プロパティのget/setをLuaへ通す共通の口(`WidgetProperty`)も実装され、`LuaEngine`から実際に使われている。詳細は下記「Luaバインディング」参照。

## メモリ計測の結論 (2026-09-12時点、実機RP2350で20回の遷移を計測)

`src/functions/Mem_Functions.hpp` と `script/host_test/run_mem.sh` で実測した結果と判断。
**同じ議論を繰り返さないために、アリーナを提案する前に必ずここを読むこと。**

- **断片化は起きていない**。`frag=0%` / `max_alloc=65536B以上` / 空きブロック数5〜12で推移し、20回の遷移で増加傾向なし。
- **リークも無い**。「ヒープ下限」(シーン破棄直後のused。シーンのウィジェットが1つも生きていない瞬間)は9回時点で+248B、19回時点で+192Bと、回数を倍にしても増えない。
- 当初observedしていた使用量の増加は断片化ではなく解放漏れで、`MarkdownView`/`ScrollList`/`CanvasRaster`のデストラクタ修正で解消済み(Markdownは1訪問あたり約57KBリークしていた)。
- **アリーナの枠を決めるなら根拠は「ウィジェット本体のバイト数」だけ**。レポートの`widget`列がそれ。シーン全体の`peak`には内部の`std::vector`/`std::function`が含まれるが、それらはアリーナではなくグローバルヒープに残るため、枠の根拠にすると3割ほど過大になる。
- 実測値: 永続(Statusbar+キーボード3種)=4,600B / Markdown=41,668B / InputTest=1,552B / Home=756B。
  **プールが全部固定長なので、開く文書が変わっても`Markdown`の41,668Bは動かない(決定論的)**。
  → 入れる場合の推奨枠は「永続8KiB + シーン56KiB = 64KiB」。
- アリーナが捕まえるのは Markdown の全841回の確保のうち**36回(バイトでは79%、回数では4.3%)**だった。残り805回は`Label`の行データ(`vector<vector<TextRun>>`)と`Widget`基底の`std::function`×4で、これはアリーナでは消えない。
  - **※この805回のうち`Label`ぶんはその後の改修で潰した**(行データを`vector<vector<TextRun>>`から
    `runs_flat`+`TextRun::line`へ平坦化し、レイアウトを`needs_relayout`で遅延評価に変更)。
    **現在は `MarkdownView + load()` で88回**(うち本体以外が87回)、**通常のシーン遷移は1回あたり8回**。
    `sh script/host_test/run_mem.sh` で再現できる。**TODOに残る「841回中805回」は既に古い数字**。
  - **残りの実害は「確保回数」ではなく「sizeof」のほう**。`std::function`は32Bで`Widget`基底に4本あるため、
    **1ウィジェットあたり128Bが固定で乗る**(`Button`のsizeof 312Bのうち128B)。関数ポインタ+`void*`の
    Delegate(16B)へ替えれば1個あたり64B減るが、Markdownシーン36個でも約2.3KB/46KB(5%)。
    **単体では旨味が薄い。**
    ~~Luaのコールバック(`lua_State*`+registry refのキャプチャは16B超え=貼るたびにヒープ確保)を
    大量に貼るようになって初めて費用対効果が出る~~ →
    **この前提は実装時に回避できた(2026-09-19、下記「Luaバインディング」参照)**。
    `lua_State*`やregistry refをウィジェット側のstd::functionへ直接キャプチャさせず、
    「`LuaEngine*`(1ポインタ)+`WidgetId`(4B)」だけをキャプチャする中継関数を挟むことで、
    小バッファ最適化の範囲(概ね16B)に収まりヒープ確保が起きないようにした。
    そのためコールバックのDelegate化は今後も「単体では5%程度」の効果しかなく、優先度は低いまま。
- **判断: 断片化もリークも観測されていない以上、64KBを常時占有する対価に見合わないため保留**。アプリが増えて断片化が実際に観測された時点で再検討する。

### ✅ 解決済み: PCビルドでシーン遷移を繰り返すと「ヒープ下限」が際限なく増える(2026-09-19発見 → 2026-09-28特定・修正)

上の結論は**実機RP2350での計測**に基づくが、`LuaScene`の動作確認中、**PCビルド
(`pc/build/picoos_pc`)で`--tap`により同じ画面遷移を繰り返すと、上と同じ「ヒープ下限」
指標が回数に比例して際限なく増え続ける**ことを見つけた。**Luaとは無関係の、既存の
`InputTestScene`だけでも再現する**(検証方法・結果は次の通り):

```sh
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc \
  --tap 178,65@10:5  --tap 40,215@30:5  \
  --tap 178,65@60:5  --tap 40,215@80:5  \
  --tap 178,65@110:5 --tap 40,215@130:5 \
  --tap 178,65@160:5 --tap 40,215@180:5 \
  --tap 178,65@210:5 --tap 40,215@230:5 \
  --shot /tmp/probe.ppm 260
# (178,65)=ランチャの「入力テスト」タイル、(40,215)=InputTestSceneの「戻る」ボタン。
# ランチャ→InputTestScene→ランチャ を5回繰り返すだけ
# (このコマンド自体が「未解決」当時の再現コマンドで、原因のSDL_VIDEODRIVER=dummyを
#  わざと使っている。通常の動作確認ではdummyではなくoffscreenを使うこと。上記
#  「PC / Web実行環境」参照)
```
結果: `ヒープ下限(シーン破棄直後/9回): 初回=188032B 最新=478256B 最大=478256B 差+290224B`
(9サンプルで約290KB増加。1往復あたり約30〜40KB)。

**この現象がLua着手より前から存在することも確認済み**: このセッションでの変更を一切含まない
`git worktree`(コミット`4f8310a`、Lua関連の作業を始める直前)でも全く同じ手順・ほぼ同じ数値
(`初回=188320B 最新=480848B 差+292528B`)が再現した。つまり**`LuaEngine`/`LuaScene`が
原因ではない**(`script/host_test/lua_engine_test.cpp`/`lua_scene_test.cpp`はASan付きの
ホストテストで、Push/Pop相当のシナリオを含めて一切のリークを検出していない。今回の増加は
ASanの通らないPCビルドのGUI経路——実際のLovyanGFX描画・フォント処理・Task/Networkの
どこか——で起きている)。

**実機での20回計測(2026-09-12、上記)と矛盾しているように見える点に注意**: 実機計測は
`sh script/host_test/run_mem.sh`(ASan相当ではないがWi-Fi/描画も含めた実機実行)による
もので、その時は増加傾向が無かった。今回の再現条件(同一シーンの往復を`--tap`で機械的に
繰り返す/PCビルド固有のSDL・glibc・LovyanGFX_SDLパネル経路)との違いが原因の可能性があり、
**実機で同じ「同一画面の往復を10回以上」というシナリオを踏んだことは無い**(実機計測は
「シーンを跨いだ複数回の遷移」であり、「同じ2画面の往復」ではなかった)。

**手がかり(2026-09-24)**: 増え方は**往復の回数ではなく、シーンに滞在したフレーム数に比例する**
(1フレームあたり約1.3〜1.4KB)。同じ`InputTestScene`で「戻る」を押すまでを40フレーム→400フレームに
延ばすと、残留が約49KB→約523KBになった。`GameBoyScene`でも、ROMを読まずにROM選択のダイアログを
開いたままにしているだけで同じ割合で増える。**どの画面でも毎フレーム何かが確保されて返っていない**
(= シーン遷移そのものではなく、毎フレームの処理のどこか)と見てよい。

**未着手(2026-09-24時点)**: 原因の特定(候補: LovyanGFXのフォント/グリフキャッシュ、Task_Functions、
Network_Functionsの再接続チェック、SDL側のイベント処理)、実機での再現確認、修正。
  `Label::lines`の件は上記のとおり対処済みで、残る`std::function`のDelegate化も単体では5%程度の効果しかない
  (上記)。

**原因特定・修正(2026-09-28)**: 「候補」に挙げていた**「SDL側のイベント処理」が的中**。
`valgrind --tool=massif`で400フレーム分のヒープ成長を追跡したところ、増加分の一部が
`lgfx::v1::Panel_sdl::sdl_create() <- sdl_update() <- _update_proc() <- loop()`という、
**毎フレーム呼ばれる経路から確保されたまま残っている**ことが分かった(`PICO_GFX::Setup()`
からの起動時1回きりの確保とは別の経路として計上されていたのが決め手)。

原因は**pico-osではなくLovyanGFX側(`src/lgfx/v1/platforms/sdl/Panel_sdl.cpp`)のバグ**:
`sdl_update()`は`monitor.renderer == nullptr`の間ずっと`sdl_create()`を呼び直すが、
`sdl_create()`は`SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)`
の戻り値をチェックしていない。**この再現コマンドが使っている`SDL_VIDEODRIVER=dummy`は
この組み合わせのレンダラーを一切提供できない**(検証用の最小SDLプログラムで確認:
`dummy`は`opengl`/`opengles2`/`software`の3種を持つが、`index=-1`の自動選択では
先に列挙される`opengl`/`opengles2`の失敗で終わり、`software`まで辿り着かない。
`SDL_RENDERER_SOFTWARE`を明示すれば`dummy`でも成功する)。そのため`monitor.renderer`が
永遠に`nullptr`のままになり、`sdl_create()`が毎フレーム(実測: 60フレームで510回)
呼ばれ続け、**呼ばれるたびに`SDL_CreateWindow()`で新しいウィンドウを作り、直前の
`m->window`を`SDL_DestroyWindow()`せずに上書きして捨てる**(=毎フレーム1つ
SDL_Windowがリークする)、という壊れ方だった。

**この問題はSDL_VIDEODRIVER=dummyというヘッドレス実行条件に固有**で、実際のウィンドウ
表示環境(通常のPCビルド)・Webビルド(Emscripten、別経路)・実機(RP2350、SDL不使用)
のいずれでも再現しない。上の「実機での20回計測と矛盾しているように見える点」は、
実機がそもそもSDLを使わないことを踏まえれば矛盾ではなかったことになる。

対応:
1. **`pc/patches/0001-panel_sdl-renderer-fallback-and-cleanup.patch`**を新設し、
   `pc/CMakeLists.txt`のFetchContentへ`PATCH_COMMAND`として組み込んだ(取得した
   LovyanGFXへ自動適用される。詳細は`pc/patches/README.md`)。`sdl_create()`の
   呼び直しに備えて前回分を必ず`SDL_Destroy*`してから作り直すようにし、
   `SDL_RENDERER_SOFTWARE`へのフォールバックも追加した。これで`dummy`環境でも
   毎フレームのリークが止まり(検証: 上記`--tap`再現コマンドで
   `ヒープ下限(9回): 初回=276832B 最新=282880B 差+6048B`まで縮小。修正前は
   `差+523872B`)、`dummy`のままでも正しく描画できるようになった。
2. **`pc/README.md`/`CLAUDE.md`のヘッドレス確認コマンドは`SDL_VIDEODRIVER=dummy`
   ではなく`SDL_VIDEODRIVER=offscreen`を使うよう変更した**(`cli-tool/picoos`の
   `pc shot`サブコマンドも同様)。`offscreen`は最初からレンダラーを作れるので
   `sdl_create()`は起動時の1回だけで済み、パッチが無くても問題を踏まない
   (パッチは主に「`dummy`をどうしても使う場合の保険」「本当にレンダラーが
   1つも無い環境でもクラッシュしない安全策」という位置づけになった)。
3. 修正後に残る微小な残留(**+6048B/9回、1往復あたり1KB未満**)は、実機計測
   (2026-09-12、「9回時点で+248B」)と同程度のオーダーで、通常のシーン滞在中の
   一時確保(Wi-Fi再接続チェック等)が拾われているだけと見てよい。この規模の残留を
   追う投資対効果は低いと判断し、追加調査は行っていない。

### ✅ 解決済み: 同じ`pc/build`を2回目以降configureすると必ず失敗する(2026-09-28発見・修正)

`cli-tool/picoos`へ`midi2mml`サブコマンドを配線した直後、ユーザーが`picoos pc build`を実行したところ
(=既に一度ビルド済みの`pc/build`に対して`cmake -S pc -B pc/build`を**2回目として**実行したところ)、
上記パッチの適用(`git apply`)が`error: patch does not apply`で失敗し、configureそのものが止まった。

- **原因は`FetchContent_Declare(lovyangfx ...)`側の設定漏れ**で、パッチの中身とは無関係。
  git系のFetchContentは既定で**「update」ステップ(git fetch + 対象タグへのcheckout)を
  configureのたびに実行する**。取得先のタグは固定なので中身は何も変わらないが、
  update自体が走ったという事実だけでmake/ninjaの依存関係上「patchステップより新しい」
  扱いになり、**既に当たっているパッチを同じ木へもう一度当てようとして失敗する**
  (`git apply`は冪等ではなく、当たり済みのhunkを検出して拒否する)。
  1回目のconfigure(まだ何も取得していない状態)ではupdateとpatchが同時に初めて走るため
  問題が起きず、**2回目以降のconfigureで必ず踏む**という踏み方だった。
- **対処**: `FetchContent_Declare`に`UPDATE_DISCONNECTED TRUE`を追加。これで初回の取得・
  パッチ適用が済んだ後はupdateステップ自体が自動実行されなくなり、同じ`pc/build`への
  再configureが安全になる(`pc/CMakeLists.txt`にコメントで経緯を残してある)。
- 検証: `picoos pc build --clean`(まっさらな状態から初回ビルド)→`picoos pc build`
  (2回目、**修正前ならここで必ず`patch does not apply`が出ていた**)→もう一度`picoos pc build`
  (3回目)の3連続実行が全て成功し、2回目・3回目は`update`/`patch`ステップ自体が
  走らずconfigureが一瞬(0.2秒程度)で終わることを確認。`--shot`での画面描画も
  修正前と変わらないことを確認済み。
- **LovyanGFXの版(`LOVYANGFX_TAG`)を上げたいときは`pc/build`を作り直すこと**
  (`UPDATE_DISCONNECTED`はconfigureのたびの自動update自体を止める設定なので、
  版を変えても既存の`_deps/lovyangfx-src`はそのまま使われてしまう。
  `picoos pc build --clean`か`rm -rf pc/build`で対応する)。

## Luaバインディング (`src/lua/LuaEngine`) (2026-09-19着手)

Lua<->C++を繋ぐ実行エンジン。**1インスタンス=1つのlua_State=1つのLuaアプリ**という対応で、
既存の受け皿(`WidgetFactory`/`WidgetRegistry`/`WidgetProperty`/`ErrorFunctions`)を
薄く橋渡しするだけの設計にしてある(新規に持つ状態は「pico.*」というAPI表と、
コールバック中継用の小さな対応表だけ)。**まだSDからスクリプトを読んで実行する
`LuaScene`は無く、`LuaEngine`単体をホストテスト(`script/host_test/lua_engine_test.cpp`)
から動かして検証した段階。**

### ライフサイクルとメモリ

- コンストラクタで`lua_newstate()`にカスタムアロケータ(`BudgetAlloc`)を渡し、
  確保量に`budget_bytes`の上限を課す。超過時は`lua_newstate`自体がnullptrを返すか、
  `LUA_ERRMEM`として安全に失敗する(`lua_alloc_budget_test.cpp`で確認済みの挙動)。
  **`luaL_openlibs()`とAPI登録は`InitTrampoline`という1つのC関数へまとめ、必ず`lua_pcall`
  越しに呼ぶ**。保護せずに直接呼ぶと、初期化中のOOMがLuaの仕様上そのまま`abort()`する
  ため(これも`lua_alloc_budget_test.cpp`で踏んで学んだ)。
- `valid()`が`false`の場合、構築失敗(予算不足)なので呼び出し側はアプリの起動自体を諦める。
- `Run(script, chunkname)`: 読み込み(`luaL_loadbuffer`)と実行(`lua_pcall`)の両方を1関数でこなし、
  構文エラー・実行時エラーはどちらも捕捉して`ErrorFunctions::ShowFatal()`へ渡してから`false`を返す。
  呼び出し側は追加のエラー処理をしなくてよい。

### `pico.*` API(Lua側から見える面)

| 関数 | 内容 |
|---|---|
| `pico.create(type_name)` | `WidgetFactory::TypeFromName()`→`Create()`。生成物は即`WidgetFunctions::Add()`で登録し、`WidgetId`(整数)を返す |
| `pico.destroy(id)` | コールバック登録を`PruneCallbacksFor()`で外してから`WidgetFunctions::DestroyLater()`(フレーム境界での遅延削除) |
| `pico.set(id, name, value)` / `pico.get(id, name)` | `WidgetProperty::IdFromName()`→`Set()`/`Get()`。プロパティ名は`snake_case`の文字列 |
| (追加分) | 2026-10-05に `off` `text_set` `set_dots` `set_name/find/parent/children/get_rect` `bring_to_front/send_to_back` `scroll_to` `list_*` `tab_*` `show_choice/date/time/number/progress` `draw_text_wrapped` `measure_text` `get_pixel` `image_create/target/clear` `app_dir` `path_join` `time` `wifi_status` `url_*` `base64_*` `settings_*` `memory_info` `toast` `on_back/go_back` `encrypt/decrypt/is_encrypted/hash/random_bytes` を足した。一覧は下記「Lua APIの追加(2026-10-05 その2)」 |
| `pico.on(id, event_name, fn)` | 4種の共通イベント(`press_start`/`press_end`/`press_move`/`press_out`。**`fn(id, x, y, lx, ly, dx, dy)`でタッチ座標も届く**、下記「Lua APIの追加(2026-10-05)」)+`render`(`Canvas`限定、下記「直接描画」参照)+ウィジェット固有4種(`checked_changed`/`value_changed`/`select_item`/`tab_changed`、下記「ウィジェット固有イベント」参照)+`closed`(ダイアログ限定)に対応(下記) |
| `pico.add_child(container_id, child_id)` | `LayoutContainer`/`GridContainer`/`ScrollContainer`のみ対応 |
| `pico.remove_child(container_id, child_id)` | `add_child`の逆。破棄せず取り外す。取り外した子はフラットリストへ独立したルートとして戻る(下記「コンテナからの取り外し」参照)(2026-09-21追加) |
| `pico.list_add(id, text)` / `pico.list_clear(id)` | `ScrollList`/`DropdownMenu`へ項目を足す/全消しする(下記「リストへの項目追加」参照)(2026-09-21追加) |
| `pico.tab_add(id, label)` | `TabBar`へタブを足す。`kMaxTabs`(4)超過なら`false`(下記「リストへの項目追加」参照)(2026-09-21追加) |
| `pico.log(msg)` | `LOG_APP_MSG` |
| `pico.show_error(msg)` | `ErrorFunctions::ShowFatal()` |
| `pico.pop([result])` | `SceneFunctions::Pop()`。`LuaScene`から起動されたアプリがランチャへ戻るためのもの。`result`は`push_scene`した親の`on_result(result)`に届く(2026-09-19追加) |
| `pico.content_rect()` | `Scene::contentRect()`を`x,y,w,h`の4値で返す。ステータスバー分を避けた配置に使う(2026-09-19追加) |
| `pico.draw_pixel(x,y,color)` / `draw_line(x0,y0,x1,y1,color)` / `draw_rect(x,y,w,h,color)` / `fill_rect(...)` / `draw_circle(x,y,r,color)` / `fill_circle(...)` / `clear_rect(x,y,w,h[,color])` / `draw_text(x,y,text[,color[,font_size]])` | `OSData::frame`へ直接描く。**`Canvas`の`render`コールバック内で使うこと**(下記「直接描画」参照)(2026-09-20追加) |
| `pico.invalidate(id)` | 対象ウィジェットの画面矩形を`needsRender()`でdirty化(次のFlushDirty()で`render()`が呼ばれる)。`Canvas`に限らず任意のウィジェットに使える汎用API(2026-09-20追加) |
| `pico.mark_dirty(x,y,w,h)` | `PICO_GFX::MarkDirty()`の生の下請け。任意の矩形を直接dirty化したいとき向けの低レベルAPI(2026-09-20追加) |
| `pico.set_palette(index,r,g,b)` / `pico.get_palette(index)` / `pico.reset_palette()` | パレットの1〜14番(黒0・白15は固定)の色を差し替える(2026-10-07)。実体は`PICO_GFX::COLORS`(RGB565、可変)+`paletteRevision`で、`FlushDirty()`がframeのパレットを合わせて全画面を描き直す(行ハッシュも無効化)。`CanvasRaster`/`ImageView`は`render()`で、Luaの画像は`set_palette`時に追従。**`LuaEngine`のデストラクタが既定へ戻す**(アプリ終了・画面遷移)。ホストテストは`lua_ext_test`、PCビルドの`--shot`で確認、実機は未確認 |
| `pico.get_draw_area()` | 今のクリップ矩形を`x,y,w,h`で返す(無ければw/hが0)。`render`の中では「Canvasとdirty矩形の重なり」なので、部品の多い絵で描き直しが要る部分だけを描ける(テトリスの盤面)(2026-09-26追加) |
| `pico.set_draw_area(x,y,w,h)` / `pico.clear_draw_area()` | `OSData::frame->setClipRect()`/`clearClipRect()`。以降の`pico.draw_*`をこの矩形の内側だけに制限する/解除する(下記「直接描画エリア」参照)(2026-09-20追加) |
| `pico.sd_exists(path)` | `OSData::SD.exists()`。`bool`を返す(2026-09-20追加) |
| `pico.sd_read(path)` | ファイル全体を文字列で返す。無い/開けない/上限超過は`nil`(下記「SDカードアクセス」参照)(2026-09-20追加) |
| `pico.sd_write(path, content[, append])` | 新規作成+上書き(既定)、または`append=true`で追記。成否を`bool`で返す(2026-09-20追加) |
| `pico.sd_remove(path)` | ファイルなら`SD.remove()`、ディレクトリなら`PICO_IO::removeRecursive()`(`FileExplorer`の削除と同じ判断)(2026-09-20追加) |
| `pico.sd_mkdir(path)` | `OSData::SD.mkdir()`(2026-09-20追加) |
| `pico.sd_list(path)` | ディレクトリを列挙し`{ {name=..., is_dir=...}, ... }`の配列を返す。パスが無い/ディレクトリでないなら`nil`(2026-09-20追加) |
| `pico.config_read(path)` / `pico.config_get(path, key)` / `pico.config_write(path, key, value)` | `key=value`形式の設定ファイル(`PICO_Config`と同じ書式)の読み書き。値は文字列で返す。書き込みは`PICO_Config::SetValue()`(1キー差し替え)。**`app.cfg`へは書けない**(下記「設定ファイルとapp.cfgの書き込み制限」参照)(2026-09-30追加) |
| `pico.image_load(path)` | `.pimg`をデコードして整数ハンドルを返す。失敗(SD無し/パス不正/不正な`.pimg`/上限超過)は`nil`(下記「画像」参照)(2026-09-21追加) |
| `pico.image_size(handle)` | 読み込んだ画像の`width, height`を返す。無効なハンドルはエラー(2026-09-21追加) |
| `pico.draw_image(handle, x, y)` | 画像を描く。他の`pico.draw_*`と同じく**`Canvas`の`render`コールバック内で使うこと**。無効なハンドルはエラー(2026-09-21追加) |
| `pico.image_free(handle)` | 画像を明示的に解放する。無効/解放済みハンドルは`pico.destroy`と同じく黙って無視(2026-09-21追加) |
| `pico.draw_image_part(handle, x, y, sx, sy, w, h)` | 画像の一部だけを描く(スプライトシートからの切り出し。画像は4枚までなので部品の多い絵は1枚にまとめる)。今のクリップと描き先の重なりへクリップを一時的に狭めてから画像全体をずらして`pushSprite()`し、クリップは元へ戻す(2026-09-26追加) |
| `pico.canvas_clear(id)` | `CanvasRaster`(`pico.create("CanvasRaster")`)を白紙(`PICO_WHITE`)へ戻す。対象がCanvasRaster以外/無効なIDはエラー(下記「ラスタキャンバスの保存/読み込み」参照)(2026-09-23追加) |
| `pico.canvas_save(id, path)` | `CanvasRaster`の中身を`.pimg`としてSDへ書き出す。成否を`bool`で返す(SD無し/権限外/書き込み失敗はfalse。対象種別/IDが不正ならエラー)(2026-09-23追加) |
| `pico.canvas_load(id, path[, keep_size])` | `.pimg`を読み込み`CanvasRaster`へ反映する。**読み込んだ画像のサイズへキャンバス自体もリサイズされる**(内容は消える)。`keep_size=true`なら大きさを変えず白紙にしてから左上に合わせて読む(2026-09-24追加)。成否を`bool`で返す(2026-09-23追加) |
| `pico.canvas_undo(id)` | 1段だけの「元に戻す」(もう一度でやり直し)。`undo_enabled`が有効なときだけ効く(下記「ペイント」参照)(2026-09-24追加) |
| `pico.push_scene(path [, args])` / `pico.change_scene(path [, args])` | 別のLuaスクリプトへ`SceneFunctions::Push/Change`する。`args`は子の`pico.args()`で受け取る(下記「シーン制御」「Lua APIの追加(2026-10-05)」参照)(2026-09-21追加) |
| `pico.launch_app(name)` | `AppFunctions::LaunchByName()`経由で登録簿の任意のアプリ(C++製含む)へ`Push`する。見つかれば`true`、無ければ`false`(下記「シーン制御」参照)(2026-09-21追加) |
| `pico.show_message(text, cancel_text, ok_text)` | `MsgDialog`を表示する。閉じた結果は`pico.on(id,"closed",fn)`で受ける(下記「ダイアログ」参照)(2026-09-21追加) |
| `pico.show_input(label, initial_text, is_single_line)` | `InputDialog`を表示する。入力文字列は`pico.get(id,"text")`で読む(2026-09-21追加) |
| `pico.show_file_save(start_dir[, default_name])` / `pico.show_file_select(start_dir)` | `FileSaveDialog`/`FileSelectDialog`を表示する。選択パスは`pico.get(id,"path")`で読む(未選択は`nil`)(2026-09-21追加) |
| `pico.show_color()` | `ColorDialog`(4×4パレット)を表示する。選択色は`pico.get(id,"value")`で読む(未選択は`-1`)(2026-09-21追加) |
| `pico.http_request(method, url, body, content_type, callback [, opts])` | 非同期HTTPリクエスト(GET/POST/PUT/PATCH/DELETE)。走るのは1本、2本目以降は順番待ち(最大4本)。戻り値はリクエストID。`opts={headers=, save_to=}`、`callback(ok, status_code, body_or_nil, error_or_nil, headers, info)`(下記「ネットワーク」と「Lua APIの追加(2026-10-05)」参照)(2026-09-21追加、2026-10-05拡張) |
| `pico.http_cancel([id])` | `pico.http_request()`を取り消す(idなしは走っているものも順番待ちも全部)(2026-09-21追加) |
| `pico.get_time()` | `TimeFunctions::timeinfo`を`{year, month, day, hour, min, sec, wday}`のテーブルで返す(下記「時刻取得」参照)(2026-09-21追加) |
| `pico.get_touch()` | 現在(直近)のタッチ位置を`x, y, is_touched`の3値で返す。`pico.draw_*`と同じ絶対スクリーン座標(下記「タップ位置の取得」参照)(2026-09-21追加) |

- **プロパティ名・種別名は文字列(snake_case/PascalCase)にした**(数値定数にしなかった)。
  Lua側の書きやすさを優先した判断で、毎回文字列比較が挟まるが、UI操作程度の頻度なら実害は無いはず。
  対応表は`WidgetProperty::IdFromName()`/`WidgetFactory::TypeFromName()`に集約してあるので、
  プロパティ/種別を増やす際はそこへ1行足すだけでよい(片方だけ更新すると「Luaから見えない」
  というずれ方をするので、`WidgetProperty.cpp`のコメントで注意喚起してある)。
- **数値プロパティはLua側の1/1.0の書き分けを吸収する**(`l_set`)。`WidgetProperty`側は
  プロパティごとに期待する型(Int/Float)が固定だが、Lua側に「整数リテラルで書け」
  「浮動小数点数で書け」を強制するとミスの元になるため、整数値に見えるならまずIntとして試し、
  ダメならFloatとして試す。
- **未対応の値(型不一致・非対応プロパティ)は`pico.set`側はエラー、`pico.get`側はプロパティ名が
  有効ならnil**(名前自体が無効ならどちらもエラー)。「setは黙って失敗させない」
  「getは無ければnilというLuaの慣習に合わせる」を使い分けてある。

### 権限(LuaPermissions、2026-09-21実装)

Luaアプリの権限管理の第一歩として、`network`/`sd_outside_app_dir`の2値(`src/lua/LuaPermissions.hpp`)
だけを見る粗い実装にした。パスのホワイトリストやホスト単位の制限のような細かい制御はまだ無い。

- **`LuaEngine`のコンストラクタが`LuaPermissions`と`app_dir`(文字列)を追加で受け取る**
  (どちらもデフォルト引数があるので、権限を意識しない既存の呼び出し元
  (ホストテスト等)は今まで通り`LuaEngine(budget)`のままで良い)。`app_dir`は
  `PICO_IO::normalize()`して`app_dir_`へ持つ。**既定値の`"/"`は「制限なし」に相当する**
  (ルート配下=あらゆる絶対パスが該当するため)。`LuaScene`を介さず`LuaEngine`を
  直接使う場面(ホストテスト等)はこの既定のままなので、今回の変更で挙動は変わらない。
- **実際に効くのは`LuaScene`経由の場合だけ**。`LuaScene::onEnter()`が
  `PICO_IO::parent(script_path)`でスクリプト自身の親ディレクトリを毎回計算し直して
  `app_dir`として渡す(`push_scene`/`change_scene`で別ファイルへ移った場合、
  そのファイル自身の場所を見るのが正しいため、`LuaScene`のメンバへ固定して
  持ち回したりはしない)。
- **ゲートしているのは`pico.http_request`(network)と、`pico.sd_exists/read/write/remove/mkdir/list`
  +`pico.image_load`(sd_outside_app_dir)**。いずれも拒否時は`luaL_error`にはせず
  `false`/`nil`を返すだけ(SD無し等、既存の「実行時の状態」枠と同じ扱い。
  プログラマの書き間違いだけでなく、想定通り動くスクリプトが試しうる経路でもあるため)。
  ただし`LOG_APP_WARN`は出す(SD無しのような日常的な状態とは違い、権限の壁に
  当たったことは開発者が気づけるようにしておきたいため)。
  判定は`LuaEngine::SdPathAllowed()`の1箇所に集約してある:
  `sd_outside_app_dir==true`なら常に許可、`false`なら`path`を`PICO_IO::normalize()`した
  上で`app_dir_`自身か`app_dir_+"/"`始まりかを見る(`normalize()`が`..`によるルート越え
  自体は既に防いでいるので、ここでは「どのディレクトリ配下か」だけを見ればよい)。
- **権限は「スクリプトファイル単位」ではなく「アプリ単位」で決まるモデルにした**。
  `pico.push_scene()`/`pico.change_scene()`は呼び出し元の`LuaEngine`が持つ
  `LuaPermissions`をそのまま新しい`LuaScene`へ引き継ぐ(`LuaEngine::permissions()`)。
  複数画面のLuaアプリで2画面目以降だけ権限が既定値(最小権限)へ落ちてしまうと
  分かりにくいバグの元になるため。一方`pico.launch_app()`は対象アプリ自身の
  `AppEntry::create()`(=その`Register()`呼び出しが決めた権限)へ委ねるので、
  ここでは何も引き継がない(別アプリへの遷移なので独立した権限であるべき)。
- **権限の割り当ては現状、アプリ登録側(`App_List.cpp`)が`Register()`呼び出しごとに
  手書きする**。`AppFunctions::MakeSceneWithArg<LuaScene>`は`entry.arg`(パス)しか
  `LuaScene`へ渡さない汎用テンプレートのままにしてあり(他の`MakeSceneWithArg<T>`
  利用者に影響を与えたくないため)、権限が必要なアプリは専用の生成関数を書く
  (`App_List.cpp`の`MakeLuaHelloScene()`が実例。同梱デモ`hello.lua`は`/lua/`の外
  (`/img/hello.pimg`)を読むため`sd_outside_app_dir=true`を明示的に与えている)。
  **Luaアプリが増えて権限の組み合わせも増えたら、`AppEntry`へ権限フィールドを
  持たせる形へ一般化することを検討する**(今は登録されているLuaアプリが1つだけなので、
  汎用化は時期尚早と判断した)。**SDを走査して動的にLuaアプリを登録する仕組み
  (下記「Lua着手前の受け皿の状態」参照)ができた際は、その時点でこの割り当て方法を
  再設計する必要がある**(スキャンで見つけたスクリプトに対し、誰が何を根拠に
  権限を決めるかがまだ無い)。

### Love2D比較で足した拡張API (2026-10-04)

Love2Dにあってpico-osのLua APIに無かったもののうち、C++側にほぼ実装があり橋渡しだけで済むものをまとめて足した。
ドキュメントは`lua-api-doc/content/api/`(drawing/images/sound/sdcard/misc)。**ハード制約で難しいもの
(半透明・Shader・Mesh・物理エンジン・スレッド・マルチタッチ)と、変換/座標変換のスタック(`translate/rotate/scale/push/pop`)・
`require`・OGG/MP3・複数WAVの同時再生・Quad/SpriteBatch・フォント差し替えは対象外のまま**。

- **図形**: `pico.draw_ellipse/fill_ellipse`、`draw_triangle/fill_triangle`、`draw_polygon/fill_polygon`(`{x1,y1,...}`の平らな配列、3〜32点。
  塗りは偶奇規則のスキャンラインで凹んだ形も塗れる)、`draw_arc/fill_arc`(ラジアン、0=右・時計回り。扇形は中心+円弧の多角形)。
  `draw_line`/`draw_triangle`/`draw_polygon`/`draw_arc`の末尾引数`width`(1〜64)は`CanvasRaster::DrawThickLine()`を再利用
  (**`drawWideLine()`は4bppパレットでクラッシュするので使わない**)。楕円は`drawEllipse/fillEllipse`をそのまま使う。
- **画像**: `pico.draw_image_ex(handle, x, y, r, sx, sy, ox, oy)`(Love2Dの`draw`と同じ並び。負の倍率で反転)。描き先の画素ごとに
  元画像を逆変換で引く最近傍で、**クリップ(`getClipRect()`)の内側だけ**を`writePixel()`する(実機のクリップ未設定時は全面を返す。
  ホストのスタブは未設定だと0を返すので、テストでは`setClipRect()`してから呼ぶ)。透過画像はindex0を飛ばす。
  **2026-10-06に速い道を足した**: 元と描き先がどちらも4bpp(回転0)なら`getBuffer()`を直接読み書きし、各行で「元画像の中に入る区間」を
  割り算で先に求めて外接矩形の空きを回らない(1画素ごとは32bitの加算だけ)。PCビルドで回転・2.5倍の描画が約2.3倍速く、出力は同じ。
  ホストテストのスタブ(1画素1バイト、`getColorDepth()`=8)は従来の`readPixelValue()`/`drawFastHLine()`の道を通る
- **回転済みのコマ**(2026-10-06): `pico.image_rotate(handle, frames, {sx,sy,ox,oy,start})`が`frames`通りの角度に回した絵を
  1枚の透過画像(一辺`cell`の正方形のコマを格子に並べたもの、スロットを1つ使う)へ先に焼き、`pico.draw_rotated(sheet, x, y, r)`が
  一番近い角度のコマを中心が(x,y)に来るよう写す(角度は丸まる。品質より速さ)。コマの情報は`ImageSlot::rot_frames/rot_cols/rot_cell`
  (解放で0へ戻す)。**写すのはLovyanGFXの透過つき`pushSprite()`ではなく4bppバッファどうしの直接コピー**
  (透過つきの`pushSprite()`は1画素ごとに色変換と判定を通り、PCビルドで同じ大きさの不透明な画像の約40倍遅かった)。
  PCビルドで1.5倍・16コマを180回: `draw_rotated` 2.4ms / 同じ大きさの透過画像の`draw_image` 8.9ms / `draw_image_ex` 5.5ms。
  出力はpushSpriteの道と同じ(PCビルドで比べた)。焼いた画像は0番が透過になる(元が不透明でも黒が抜ける)。
  実機では未計測
- **画像の描画は全部4bppの直接コピー**(2026-10-06): `IconRender::Blit4bpp(src, sx, sy, w, h, dx, dy, transparent, flip_x, flip_y)`が
  frameの今のクリップの内側だけを`getBuffer()`どうしで写す(元と描き先の画素の上位/下位が揃えば不透明は`memmove`・透過は1バイト=2画素ずつ、
  奇数ぶんずれていても隣の2バイトの4bitを組み合わせて1バイトずつ。反転は1画素ずつ)。`DrawPimgSprite()`がこれを先に試すので、
  `draw_image`・`Image`ウィジェット・`draw_tilemap`(タイルごとにクリップを触らず直接)・`draw_image_part`(反転も)・`draw_rotated`が全部これを通る。
  4bppでない(ホストテストのスタブ)ときはfalseで従来の`pushSprite()`へ落ちる。PCビルドで200回: 不透明`draw_image` 1.71→0.58ms・
  透過`draw_image` 1.57→0.50ms・`draw_image_part`(反転込み) 3.17→0.85ms・`draw_rotated` 5.74→1.71ms。出力は従来の道と全画素一致(PCビルドで比べた)。
  `ImageView::render()`も同じ`Blit4bpp`で写す(背景の塗りは下の`Fill4bpp::FillRect`)。実機では未計測
- **図形の塗りも4bppへ直接**(2026-10-06、`src/gui/Fill4bpp.hpp`、ヘッダのみ): `Fill4bpp::Span`がframeのバッファ・1行のバイト数・クリップを図形1つにつき
  1回だけ求め、横線は「端の半端な画素+間は`memset`」で書く。`FillRect/HLine/FillCircle/FillEllipse/FillTriangle`は**LovyanGFXの
  `fillRect`/`fillCircle`(+`fillCircleHelper`)/`fillEllipse`/`fillTriangle`と同じ手順を写した**もの(どの横線を塗るかが同じなので画素は変わらない。
  3点が一直線の三角形だけLovyanGFXの`drawLine`へ任せる)。`pico.fill_rect/clear_rect/fill_circle/fill_ellipse/fill_triangle`と`FillPoly`
  (`fill_polygon`/`fill_arc`)の横線が通る。4bppでない(ホストテストのスタブ)ときはfalseを返し、呼び出し側が従来のLovyanGFXの関数で描く。
  PCビルドで大きい図形を300回: 円1.5→1.1ms・楕円1.6→1.2ms・三角形3.3→2.1ms・多角形/扇形は約1.15倍(交点の浮動小数点の計算が大半)・
  長方形は同等(LovyanGFXの4bppの`fillRect`は元々バイト単位。`rect()`も同じく列ごとに端→`memset`→端の順にしてある)。小さい図形はLuaの呼び出しが大半で差は小さい。
  出力は従来の道と全画素一致(PCビルドで比べた)。実機では未計測
- **文字幅**: `pico.text_width(text[, font_size])`(`Label::GetTextWidth()`を新設。`DrawPlain()`と同じフォント設定で`textWidth()`)。
- **WAV**: `wav_pause/wav_paused/wav_position/wav_duration/wav_seek`(`SoundFunctions`にあったものを出しただけ)。
- **システム**: `pico.millis()`(単調)、`pico.battery()`(残量・電圧・USB給電。読めていなければnil。`LuaEngine.cpp`が`Battery_Functions.hpp`を取り込む)。
- **ファイル**: `pico.sd_stat(path)`→`{size,is_dir}`、`pico.sd_read_part(path, offset, length)`(`sd_read`と同じ16KiB上限)、`sd_list`の要素へ`size`を追加。
  **更新日時は持たない**(PCビルドの`FsFile`互換層に`getModifyDateTime`が無いため。要るなら互換層から足す)。
- **物理キーボード**: `pico.on_key(fn)`。`LuaScene::onKey()`→`LuaEngine::DispatchKey()`が`fn(key, mods)`を呼び(文字はUTF-8、特殊キーは名前、
  `mods={ctrl,alt,shift}`)、**真を返せば取った扱い、偽ならオンスクリーンキーボードの入力へ回る**。エラーになったらダイアログを1回出して以降は呼ばない
  (`loop()`と同じ安全弁。その打鍵は消費した扱い)。`ProtectedCall()`は戻り値を1つ受けるため`nresults`引数を足した。
  配り順は「画面の`onKey()`→開いているキー盤」(「物理キーボード」参照)で、日本語キー盤が読みを入力中のときはキー盤が先。
- 動作確認アプリ「描画API確認」(`pc/sdcard/lua/apps/描画API確認/`。図形・太線・画像の拡大/回転/反転・扇形・右寄せ文字・`on_key`・電池を1画面に描く。PCビルドの`--tap`+標準入力の`key`行で確認済み)。
- 検証: `lua_engine_test`(run.sh。dirty矩形・引数エラー・塗りのピクセル(正方形/L字/扇形)・画像の等倍/拡大/反転/回転/原点/透過・
  文字幅・WAV・battery・sd_stat/sd_read_part・on_keyの登録/解除/エラー)。PCビルドは通る。**実機・PCビルドでの見た目は未確認**
  (`fill_polygon`の1画素ごとの`drawFastHLine`、`draw_image_ex`の全画素ループの速さは実機で見ること。クリップが全面だと最大240x320回)。

### コールバック中継の設計(ヒープを使わない理由)

`Widget::on_press_start`等は`std::function<void()>`のままシグネチャを変えていない
(前回のセッションでは再設計を見送ると決めていた箇所)。素朴に`lua_State*`とLuaの
registry ref(関数への参照)をラムダへキャプチャすると16Bを超え、`std::function`の
小バッファ最適化(SBO)からあふれてヒープ確保が起きる。

代わりに、`LuaEngine`が「`WidgetId`+イベント種別 → Lua registry ref」という対応表
(`callbacks_`、単純な`std::vector`の線形探索。1ウィジェットあたり高々4イベントなので
十分速い)を自分で持ち、ウィジェット側へ設定するラムダは**`this`(`LuaEngine*`)と
`WidgetId`(4B)だけをキャプチャする**ようにした。これなら合計12B程度でSBOに収まり、
`pico.on()`を何回呼んでもヒープ確保は増えない。実際に鳴らす際は
`Dispatch(id, kind)`が対応表からrefを引き、`lua_pcall`越しにLua関数を呼ぶ
(失敗時は`ErrorFunctions::ShowFatal()`)。

### 直接描画(2026-09-20実装、設計を1度やり直した)

**最初の実装(`pico.draw_*`をloop()/コールバックから素で呼ぶだけ)は動かなかった。**
PCビルドの`--shot`で実際に確認したところ、`pico.fill_rect()`で塗った矩形が
跡形もなく消えた。原因は`PICO_GFX::FlushDirty()`(`src/functions/GFX_Functions.cpp`)の
合成方式: dirty矩形ごとに「それを覆うウィジェットが無ければ背景色で塗りつぶし
(`clear_bg`)、そこに重なるウィジェットだけを`renderForce()`で再描画する」。
つまり`OSData::frame`はウィジェットが毎回描き直す前提の合成先であって、単純な
永続キャンバスではない。ウィジェットに属さない場所への直接描画は、次にその領域が
dirtyになった瞬間(シーン遷移時の全画面dirty化を含め、ほぼ必ず起きる)に消え、
誰も描き直さないので二度と戻らない。しかも自分自身の`MarkDirty()`呼び出しが
その場でこの消去を引き起こすため、「1フレームだけ映って消える」のではなく
**最初から一切映らない**。

**正しい実装: `LuaCanvas`ウィジェット(`src/gui/widgets/LuaCanvas.hpp/.cpp`)。**
中身を持たない最小限のウィジェットで、`render()`はLua側が`pico.on(id,"render",fn)`で
登録したコールバックを呼ぶだけ。`render()`は`FlushDirty()`の合成サイクルの**中**
(`clear_bg`の後、`pushSprite`の前)で呼ばれるので、そこで`pico.draw_*`を使えば
正しく合成に参加できる(`CanvasRaster`が自前スプライト+`render()`内`pushSprite`で
同じ問題を解決しているのと同じ理屈。こちらは私有スプライトを持たず直接
`OSData::frame`へ描く分だけ軽い)。`RenderMode::OPAQUE`にしてあるので、
`FlushDirty()`が`render()`を呼ぶ前に自分の矩形を`background_color`で塗りつぶして
くれる(前景だけ描けばよい)。

- **クラス名は`Canvas`ではなく`LuaCanvas`**(`CanvasRaster.hpp`が既に
  `namespace Canvas`(`Canvas::Mode`)を使っており衝突するため)。Lua側からは
  `WidgetFactory::TypeFromName()`で`"Canvas"`として見せている
  (`pico.create("Canvas")`)。`WidgetType`の一覧・`WidgetFactory`(`Create`/`IsCreatable`/
  `TypeFromName`)・`WidgetProperty::Set()`(`w`/`h`)の3箇所に登録してある
  (他の汎用ウィジェットと同じ追加パターン)。
- **`pico.on(id, "render", fn)`は`Canvas`にしか登録できない**(`l_on()`が
  `w->getWidgetType() != WidgetType::LuaCanvas`なら`pico.set`と同様エラーにする)。
  コールバックの配線自体は既存の`press_start`等と同じ「`LuaEngine*`+`WidgetId`だけ
  キャプチャ」方式(`EventKind::Render`を追加しただけ)。
- **再描画のリクエストは2段構え**:
  - `pico.invalidate(id)` — そのウィジェットの`needsRender()`を呼ぶ(画面矩形を
    まるごとdirty化)。`Canvas`に限らず任意のウィジェットに使える汎用API。
  - `pico.mark_dirty(x,y,w,h)` — `PICO_GFX::MarkDirty()`の生の下請け。`Canvas`の
    一部だけ再描画したい等、細かい制御が要る場合向けの低レベルAPI。
  静的な内容は**生成直後のウィジェットが初期状態でdirty**なので、追加の
  呼び出し無しで次の`FlushDirty()`に自動で1回`render()`が呼ばれる。アニメーション等
  毎フレーム描き直したい場合だけ`loop(dt)`から`pico.invalidate()`すればよい
  (「変化が無ければ再描画しない」という他ウィジェットと同じ省エネ方針)。
- **座標は`pico.content_rect()`と同じ絶対スクリーン座標、色は既存プロパティ
  (`border_color`等)と同じPICO 4bitパレット番号(0〜15)**。範囲チェックはせず
  `int8_t`へキャストするだけ(`WidgetProperty::Set()`の色プロパティと同じ割り切り)。
- `draw_text`だけは幅が要る(dirty矩形の計算とはみ出し防止のため)。専用の幅計算APIを
  足す代わりに、**残りスクリーン幅(`SCREEN_WIDTH - x`)へ自動で収める**簡易な実装にした
  (`AppGrid::drawName()`のmaxWidthクリップと同じ考え方)。
- **`pico.draw_*`系が呼ぶ`MarkDirty()`は、`render`コールバック内では実質no-op**
  (`FlushDirty()`が合成中`isDirtyDeactivates=true`にしているため)。無害だが
  意味も無いので、`Canvas`の外(loop()等)から`pico.draw_*`を単独で呼んでも
  表示は持続しない点は変わらない——**`pico.draw_*`は必ず`render`コールバックの
  中で使うこと**。
- ホストテストは`lua_engine_test.cpp`。`OSData::frame`はホストテスト用スタブ
  (実際には描画しないダミー実装)で、`canvas->renderForce()`を直接呼んで
  `FlushDirty()`の呼び出しを模している。確認できるのは「クラッシュしないこと」
  「`render`コールバックが実際に呼ばれること」「`pico.invalidate`/`mark_dirty`が
  期待通りの矩形を`PICO_GFX::MarkDirty()`へ渡すこと」まで(見た目の確認は
  PCビルドの`--shot`で行った。`pc/sdcard/lua/hello.lua`に`Canvas`で顔アイコンを
  描く実例がある)。

### 直接描画エリア(2026-09-20実装)

`pico.set_draw_area(x,y,w,h)`/`pico.clear_draw_area()`は`OSData::frame`の
クリップ矩形(`setClipRect`/`clearClipRect`、`Label`/`ScrollList`/`MarkdownView`の
表描画等が既に使っている仕組み)をそのままLuaへ橋渡ししただけの薄いラッパー。
用途は「`Canvas`の`render`コールバック内で、ウィジェット自身の矩形からはみ出す
描画を防ぐ」こと(例: 円グラフの角度計算やスクロールする描画内容が、意図せず
`Canvas`の外まで塗ってしまうのを防ぐガード)。

**`OSData::frame`は全ウィジェット共有の1枚のスプライトで、クリップ矩形も1個しか
持たない。** そのため`set_draw_area()`を呼んだままLua側の`render`コールバックを
抜けると、次にそのクリップ矩形が有効なまま他のウィジェットの`render()`が呼ばれ、
**そのCanvas以外の描画まで巻き込んで切り詰められてしまう**(1フレームだけでなく
`clearClipRect()`されるまでずっと)。これを防ぐため、`LuaCanvas::render()`が
`on_render()`(=Luaのrenderコールバック)から戻った直後に無条件で`clearClipRect()`
する安全弁を入れてある(`LuaCanvas.cpp`)。スクリプト側が`clear_draw_area()`を
呼び忘れても、「そのフレーム内で他のウィジェットまで巻き込む」事故だけは防げる
(呼び忘れたスクリプト自身の後続描画がその場で変な形に切り詰まることまでは
面倒を見ない——そこはスクリプトの責任)。

### SDカードアクセス(2026-09-20実装)

`pico.sd_exists/read/write/remove/mkdir/list`は`SD_Functions`/`FileExplorer`/
`PICO_IO`が既に持っている操作をLuaへ薄く橋渡ししただけで、新しい判断はほぼ無い。

- **`OSData::SD_usable == false`の間はどれも例外にせず失敗値(`false`/`nil`)を返す**。
  SD無しは配線の状態であってスクリプトの書き方の誤りではないため、
  `pico.create`の未知種別のような「プログラマの誤り」枠(`luaL_error`)には入れなかった。
- **`pico.sd_read`には上限(`kMaxSdReadBytes` = 16KiB、`LuaScene::kMaxScriptBytes`と
  同じ値)がある。** スクリプト読み込み(`LuaScene`)は上限超過時に「切り詰めて使う」
  判断をしているが、任意のデータファイルを同じように黙って切り詰めると、
  スクリプトが壊れたJSON/セーブデータを気付かずに使ってしまう恐れがあるため、
  **`sd_read`は切り詰めずに`nil`を返して失敗させる**設計にした(スクリプト読み込みとは
  意図的に判断を変えた点)。
  読み込み自体は`MarkdownView::load()`/`LuaScene::loadAndRun()`と同じく、
  ファイル全体ぶんの一時バッファをヒープへ一度に確保せず、256Bのスタックチャンクで
  `luaL_Buffer`へ読み進める(Lua文字列自体はLua側の確保になるので、
  `LuaEngine`のメモリ予算(既定200KB)がそのまま上限としても効く)。
- **`sd_remove`はディレクトリと判明したら`PICO_IO::removeRecursive()`を使う**
  (`FileExplorer::on_press_delete()`と同じ判断)。
- **`sd_list`は`{name=..., is_dir=...}`の配列を返す**(`FileExplorer::update_list()`と
  同じ`openNext()`の走査。ファイル名バッファも同じ128B)。
  ディレクトリという概念を持たないホストテストのSdFatスタブでは空配列しか返らない
  ため、`lua_engine_test.cpp`では「クラッシュしないこと」までしか確認できておらず、
  実際の列挙結果はPCビルド(`pc/compat/SdFat.hは実ファイルシステム`)の`--shot`で
  確認した(`sd_write`→`sd_read`→`sd_exists`→追記→`sd_list`→`sd_remove`→
  再度`sd_exists`の一連が期待通りに動くことを確認済み)。
- **`sd_read`/`sd_write`等はFsFileを開いたままLuaのC API(`luaL_Buffer`/テーブル構築)を
  呼ぶ。** その最中にLua側のメモリ予算超過でエラー(`longjmp`。ANSI Cのsetjmp/longjmp
  ベースなのでC++デストラクタは呼ばれない)が起きると、開いたままの`FsFile`の
  `close()`が飛ばされ得る。ごく小さな読み書きの最中に限られる稀なエッジケースであり、
  「Lua着手前の受け皿の状態」表にある**OS内部90箇所のOOM未対応と同じ割り切りで
  対象外**とした(そこまで手を入れる投資対効果は低いと判断)。

### 設定ファイルとapp.cfgの書き込み制限(2026-09-30実装)

`pico.config_read/config_get/config_write`。セーブデータや設定を`pico.sd_read`+自前の文字列解析で扱っていたのを、
OSと同じ`key=value`書式(`Config_Functions`)で読み書きできるようにした。読みは`PICO_Config::ParseFile()`、
書きは`PICO_Config::SetValue()`(一時ファイル経由で1キーだけ差し替え)をそのまま使う。

- 値は常に**文字列**で返す(型の解釈はスクリプト側)。書くときは文字列/数値/真偽値を受け、`AsInt/AsFloat/AsBool`で
  読み戻せる表記にする(小数は`%.6f`から末尾の0を削る。`AsFloat()`が指数表記を拒むので`%g`は使えない)。
- **改行を含む値は`false`**(次の行として別のキー=権限等を差し込めるため)。不正なキー(`=`・改行・前後の空白・`#`始まり)は`luaL_error`。
- 読みは`pico.sd_read`と同じ16KiB上限。ファイルが無い/ディレクトリならnil。

**app.cfgの書き込み制限**: `app.cfg`は`permission_network`/`permission_sd_outside_app_dir`を持つので、
スクリプトが書き換えられると自分の権限を上げられてしまう(次回起動のスキャンで効く)。書き込み系の共通ガード
`LuaEngine::SdWriteAllowed()`(`sd_write`/`sd_remove`/`sd_mkdir`/`canvas_save`/`config_write`が通る)で、
権限に関わらず以下を拒否する(`false`+`LOG_APP_WARN`):

- 自分の`app_dir`直下の`app.cfg`、および`/lua/apps/<名前>/app.cfg`(どのアプリのものでも)。後者は
  `sd_outside_app_dir`を持つアプリが他のアプリの権限を上げる・新しいアプリを`app.cfg`付きで作るのを防ぐため。
- `sd_remove`では、それらを含むディレクトリ(自分の`app_dir`とその祖先、`/lua/apps/<名前>`とその祖先)も。
- パスは`normalize()`の後、**FATと同じく大小を区別せず、各セグメントの先頭の空白・末尾の空白と`.`を捨てて**比べる
  (`APP.CFG`や`app.cfg.`も同じファイルを指すため)。
- 読むのは自由(`sd_read`/`config_read`)。
- ホストテストは`lua_engine_test.cpp`(読み書きの往復・後勝ち・型ごとの書式・改行の拒否・各経路でのapp.cfg拒否・
  大文字/末尾の`.`/`..`・`sd_outside_app_dir=true`でも他アプリの`app.cfg`は拒否)。

### 画像(2026-09-21実装)

`pico.image_load/image_size/draw_image/image_free`。ウィジェット(`Image`)を介さず、
`.pimg`(`script/generate_pimg.py`生成の4bpp+RLE独自形式。詳細は上の「MarkdownView実装詳細」
「文書内の参照(画像)」参照)をLuaスクリプトが直接デコードして持ち、描いて、解放できるようにした。

- **デコードは新規実装せず、既存の`IconRender::LoadPimgToSprite()`/`DrawPimgSprite()`
  (`src/gui/icons/icon_render.h/.cpp`)をそのまま呼ぶ。** `Image`ウィジェットの
  `onRAM=true`のとき(`Image::updateSprite()`)と全く同じ経路で、`.pimg`以外の画像形式
  (PNG/BMP等)は最初から対象外(このOSでは`.pimg`だけが唯一の画像形式)。
- **ウィジェットではないので`WidgetRegistry`は使わず、`LuaEngine`インスタンスごとの
  固定長スロット配列(`images_`、上限`kMaxLuaImages=4`)を持つ。** MarkdownViewの
  `labelPool`/`imagePool`と同じ「固定長配列」志向で、任意個数の`std::vector<Widget*>`的な
  管理はしていない。ハンドルは`WidgetId`と同じ発想の
  「generation(上位)+index(下位、1始まり)」パック整数だが、この配列専用のスコープなので
  `WidgetId`のような32bit全体を型ビットまで使う配分は真似ていない。
- **generationは`WidgetRegistry`と同じく、解放(`pico.image_free`)のたびに1つ進める
  (使用中かどうかは別の`ImageSlot::used`フラグで見る)。** 実装時に一度、
  「解放時にgenerationを0(未使用)へ戻し、次のロード時に1から数え直す」という誤った
  設計で書いてしまい、解放直後に同じスロットを再利用すると**前回発行分と全く同じ
  ハンドル値を再発行してしまう**(解放済みの古いハンドルが新しい画像を指してしまう
  use-after-free相当のバグ)ことに気づいて直した。`WidgetRegistry::Unregister()`が
  「`widget=nullptr`にするがgenerationは戻さず進める」設計にしている理由と同じ。
  `script/host_test/lua_engine_test.cpp`に「解放→再利用→古いハンドルがエラーのまま」
  の回帰テストを入れてある。
- **デコード後のピクセルバッファ(`LGFX_Sprite::createSprite()`)は`LuaEngine`の
  `budget_bytes`(Lua自体のアロケータ予算)には乗らない、素のOSヒープ確保**
  (`WidgetFactory::Create()`が作るウィジェット本体と同じ扱い)。そのため画像専用に
  別枠の上限を設けた: 同時に保持できる枚数(`kMaxLuaImages=4`)と合計バイト数
  (`kMaxLuaImageBytes=64KiB`、4bppなので`width*height/2`で概算)の両方。
  超過時は`pico.image_load`が`nil`を返すだけ(`pico.sd_read`等と同じく、SD絡みの
  失敗は`luaL_error`にしない方針を踏襲)。
- **`LGFX_Sprite`は`images_`配列の値メンバなので、`pico.image_free()`を呼び忘れて
  スクリプトが終了しても、`LuaEngine`自体の破棄(`LuaScene::onExit()`)で
  デストラクタが確保分を回収する(リークしない)。** `pico.image_free()`はそれを
  待たず即座に解放したい場合向けの明示API(`pc/sdcard/lua/hello.lua`の「戻る」
  ボタンでの呼び出しがその実例)。
- **`draw_image`は他の`pico.draw_*`と同じ「直接描画」の一種**で、`Canvas`
  (`pico.create("Canvas")`)の`render`コールバック内で使うこと(そうしないと
  次にその領域がdirtyになった瞬間に消える。詳細は「直接描画」参照)。描画後は
  画像サイズぶんを`PICO_GFX::MarkDirty()`する。
- ホストテストは`lua_engine_test.cpp`に追加(SD無し時のnil、正常系のハンドル発行・
  サイズ取得・描画・dirty矩形、存在しないパス/壊れたヘッダのnil、スロット枯渇、
  解放→再利用→古いハンドルの無効化、二重解放の無害化、合計バイト数上限)。
  実際の見た目は`pc/sdcard/lua/hello.lua`に画像描画のデモを追加し、PCビルドの
  `--shot`で`.pimg`(`pc/sdcard/img/hello.pimg`、`examples/img/sample.pimg`と同じ
  48x24の色帯サンプル)が実際に描けることを確認済み。

### ラスタキャンバスの保存/読み込み(CanvasRasterのリサイズ・`pico.canvas_clear/save/load`、2026-09-23実装)

「スクラッチパッド」(黒/青ペン+消しゴムの手書きメモアプリ。`pc/sdcard/lua/apps/スクラッチパッド/main.lua`)を
作る過程で見つかった2つの穴を埋めた。`CanvasRaster`(`pico.create("CanvasRaster")`)は自分専用の
`LGFX_Sprite`を持ち続け、`causeOnPressMove()`がタッチのドラッグをそのまま線として焼き込む
ウィジェットで、手書き入力の土台としては元から使えた。しかし:

1. **`WidgetFactory::Create()`が100×100固定で、`w`/`h`をリサイズする手段が無かった**
   (`WidgetProperty::Set()`のCanvasRaster caseにW/Hが無かった)。全画面に近いスクラッチパッドを
   作ろうとして初めて踏んだ。
2. **クリア・保存・読み込みの手段が無かった**(`CanvasRaster::canvasClear()`自体はC++側に
   元からあったが、Luaへ橋渡ししていなかった。保存/読み込みは影も形も無かった)。

対応:

- **`CanvasRaster::setW/setH`**(内部の`resize(int16_t,int16_t)`)が`LGFX_Sprite::createSprite()`を
  呼び直してサイズを変える。**既存の描画内容は消える**(コンストラクタと同じ手順をやり直す
  ため)ので、生成直後に一度だけ呼ぶ使い方を想定している。`WidgetProperty::Set()`のCanvasRaster
  caseへ`Id::W`/`Id::H`を足しただけで、`pico.set(id,"w"/"h",...)`から使える(`Id::W`/`Id::H`の
  **取得**は元々`GetCommon()`が全ウィジェット共通で処理しているので、Get側の変更は不要だった)。
- **`pico.canvas_clear(id)`**: `CanvasRaster::canvasClear()`を橋渡しするだけ。
- **`pico.canvas_save(id, path)` / `pico.canvas_load(id, path)`**: `CanvasRaster`のスプライトを
  `.pimg`(4bpp+RLE、`script/generate_pimg.py`と同じ形式。上の「画像」参照)としてSDへ書き出す/
  読み込む。**`.pimg`は元々デコード専任(C++側にエンコーダが無かった)**だったが、
  実行中に描いた内容をその場で保存する用途はデコードだけでは足りないため、
  `IconRender::EncodePimg()`(`src/gui/icons/icon_render.h/.cpp`)を新設した。
  行優先(ラスタスキャン)で`sprite.readPixelValue(x,y)`を読みRLE符号化する、
  `generate_pimg.py`のC++版エンコーダ。読み込み側も`LoadPimgToSprite()`(新規に
  スプライトを確保する)とは別に`IconRender::DecodePimgBody()`を新設し、**呼び出し側が
  既にサイズを合わせたスプライトへ直接デコードする**(`pico.canvas_load()`が
  `CanvasRaster`自前のスプライトへそのまま読み込むための版)。
- **`pico.canvas_load()`は読み込んだ`.pimg`のwidth/heightへ`CanvasRaster::resize()`で
  合わせ直す。** 保存時と現在のキャンバスサイズが食い違っていても読み込める
  (=`w`/`h`も画像のサイズへ変わる)。**不正/悪意のあるファイルが画面サイズを超える
  width/heightを名乗っていても、`resize()`を呼ぶ前(=大量確保が起きる前)に
  `SCREEN_WIDTH`/`SCREEN_HEIGHT`基準の上限チェックで弾く**安全策を入れてある。
- 3関数とも対象が`CanvasRaster`以外(または無効なID)だと`luaL_error`
  (`pico.on(render/closed)`と同じ「対応する種別以外はエラー」という約束)。
  `canvas_save`/`canvas_load`のSD絡みの失敗(SD無し・`sd_outside_app_dir`権限無しで
  app_dir外・ファイル不正)は`pico.sd_*`と同じく`false`を返すだけでエラーにはしない。
- **ホストテスト(`lua_engine_test.cpp`)でRLEバイト列の完全一致まで検証するため、
  `script/host_test/stubs/LovyanGFX.h`の`LGFX_Sprite`スタブを「実際にピクセルを
  読み書きする」よう拡張した**(以前は`writePixel`/`clear`が完全な無描画no-opで、
  `readPixelValue`自体が無かった)。他のメソッド(`drawRect`等)は従来どおり無描画のまま。
- ホストテストは`lua_engine_test.cpp`に追加(生成直後100×100・リサイズの往復、
  SD無しの失敗、保存したバイト列が`script/generate_pimg.py`と同じRLE形式になることの
  完全一致比較、クリア後に白へ戻ること、保存→クリア→読み込みの往復、保存時と異なる
  サイズの`.pimg`を読み込むとキャンバス自体がリサイズされること、画面サイズ超過の
  width/heightを拒否すること、対象種別/ID検証、`app_dir`配下への閉じ込め)。
  実際の見た目・操作性はPCビルドの`--shot`(`--tap`でボタンを押しての動作確認)で
  確認した。

### 実機未検証だったCanvasRasterの潜在クラッシュ(`drawWideLine`、2026-09-23発見・修正)

スクラッチパッドを実際にドラッグ操作(`--tap`を連続で細かく打ち、フレーム間で
座標が変わることで`isTouchMove`を発生させる。CLAUDE.md「PC / Web実行環境」の
`--tap`仕様参照)で試して**初めて踏んだクラッシュ**。`CanvasRaster`自体は
Luaバインディング着手より前から存在したコードだが、実際に触れて描画するアプリが
これまで一つも無く(`widget_factory_test.cpp`は生成のみ、`lua_engine_test.cpp`は
`writePixel()`で直接ピクセルを書くだけ)、`causeOnPressMove()`の描画経路が
**本物のLovyanGFXバックエンドに対して一度も実行されたことが無かった**。

- **原因**: `causeOnPressMove()`(Lineモード)と`drawArrow()`(Arrowモードの軸線)が
  `LGFX_Sprite::drawWideLine()`を呼んでいたが、これは内部で`draw_gradient_wedgeline()`
  →`fillRectAlpha()`→`readRect()`という、**アンチエイリアスのため既存ピクセルを
  読み戻すアルファブレンド経路**を通る。`CanvasRaster`のスプライトは4bppの
  パレットモード(`setColorDepth(4)`)で、この読み戻し(`copy_palette_affine`→
  `bgr888_t::get()`)がSEGVした。**AnalogClockの針が`drawWideLine()`を避けて
  `fillTriangle()`で描いている注記(「4bitパレットに無い中間色をアンチエイリアスで
  要求するので使えない」)と全く同じ制約**だが、AnalogClockでは単なる色化けとして
  現れていたのに対し、こちらは実際のクラッシュとして表面化した(用途・入力経路の
  違いによるものと見られ、根本原因は同一)。
- **対処**: `drawWideLine()`を使わず、`CanvasRaster::DrawThickLine()`が
  「両端の`fillCircle()` + 胴体の`fillTriangle()`2枚」で太線を塗る(アルファブレンド無し。
  単純な塗りつぶしのみで`readRect()`を経由しない)。`causeOnPressMove()`と
  `drawArrow()`の両方の呼び出し箇所を差し替えた(後者は`Canvas::Mode::Arrow`の
  軸線部分。矢じり自体は元々`fillTriangle()`でアルファブレンド無しだったので無傷)。
  `Rect`/`Ellipse`モード(`drawRect()`/`drawEllipse()`/`fillEllipse()`)は
  `readRect()`を経由しないため元から安全で、変更していない。
  (2026-09-23追記: 当初この節は「`fillCircle()`を半径の半分間隔で重ね塗りする`drawThickLine()`へ
  置き換えた」と書いていたが、**実際にリポジトリへ入ったコードは`drawWideLine()`のまま**だった。
  下の「ドラッグ描画が重い」の修正で実際に置き換えた)
- **見つけ方**: ホストテスト(ASan)はここを検出できない
  (`script/host_test/stubs/LovyanGFX.h`の`drawWideLine`相当は元から未実装/no-opで、
  実際のLovyanGFXコードパスを一切通らないため)。PCビルドの`--shot`で実際に
  ドラッグを模した`--tap`連打を試して初めて再現できた。**「ホストテストが全部
  通る」ことと「実機/PCビルドで実際に動く」ことは別物**という教訓を、この機能の
  実装で改めて踏んだ形になる。
- 修正後、スクラッチパッドで実際に描画→保存→クリア→読み込みの一連が
  PCビルドの`--shot`で正しく動くことを確認済み。

### CanvasRasterのドラッグ描画が重い(線がカクカク・短い線が引けない、2026-09-23修正)

実機のスクラッチパッドで「線がカクカクで滑らかでなく、ある程度長く引かないと線にならない」と
報告された。原因は描画1回あたりの仕事量で、タッチは`loop()`1周につき1回しか読まないため、
1周が重いほど線は少ない点を結んだ折れ線になる。

- **指が動くたびにキャンバス全体(スクラッチパッドで約234x235px)を作り直していた**:
  `causeOnPressMove()`が`needsRender()`(=画面矩形まるごとdirty)を呼び、さらに`render()`が
  毎回`markdirty(g_rect)`を積み直していたので、**同じ全体矩形が1フレームに2枚**積まれた。
  結果、1回の移動ごとに「キャンバス→`frame`の複製」が3回(`update()`内の`render()`+
  `FlushDirty()`の2枚ぶん)、**液晶への転送(4bpp→RGB565変換+SPI)が画面ほぼ1枚×2回**走っていた。
  → 線分の外接矩形(ブラシ半径ぶん広げる)だけを`MarkDirty()`する(`strokeTo()`)。
  `render()`は移動したときしか`g_rect`を積まない。1回の移動の転送量は数百px程度まで減る。
- **`drawWideLine()`**: アンチエイリアスのため既存ピクセルを`readRect()`で読み戻してアルファ合成する
  重い経路(しかも4bppパレットではPCビルドでSEGVする。上の節参照)。`DrawThickLine()`へ置き換えた。
- **CLEARだった**: `FlushDirty()`が毎回背景の白塗り+下の枠線`Rect`の再描画をしてから上書きしていた。
  スプライトで自分の矩形を隙間なく覆うので`OPAQUE`にした。
- **短い線が消えていた理由**: 触れた瞬間には何も描かず、2px以上動いて初めて線を引いていた。
  さらに**指を離したフレームは`WidgetFunctions::UpdateAll()`が`update()`より先に`causeOnPressEnd()`を
  呼んで`is_pressing`を下ろすため、そのフレームの`causeOnPressMove()`は来ない**(最後の区間が落ちる)。
  → 触れた瞬間に点を打ち、1pxでも動けば繋ぎ、離した瞬間にも最後の区間を繋ぐ。
- ~~`Rect`/`Ellipse`/`Arrow`モードのプレビューは今も移動ごとに全体を`needsRender()`する~~ →
  ペイントで解消(2026-09-24)。「前回+今回のプレビューの外接矩形」だけをdirtyにする(下記「ペイント」参照)。
- ホストテストは`lua_engine_test.cpp`(点・線分・離した瞬間のdirty矩形がキャンバス全体ではなく
  線分の周りだけになること)。見た目はPCビルドの`--tap`連打で確認した。**実機での速度は未計測**
  (`FlushDirty()`が5秒ごとにシリアルへ出す`fps`/`push average`で確かめられる)。

### ペイント(`pc/sdcard/lua/apps/ペイント/`、2026-09-24実装)

SUMMARY.md #8の「ペイント」。**Luaアプリ**で、描画の中身は全て`CanvasRaster`(C++)に足した
(Luaにはピクセルを読む口が無く、塗りつぶしをLuaで書くと遅すぎるため)。スクリプトは道具の切り替えと
ダイアログ(色=`ColorDialog`、保存=`FileSaveDialog`+上書き確認`MsgDialog`、開く=`FileSelectDialog`、
新規/未保存での終了=`MsgDialog`)の配線だけ。ツールバーはアイコンボタン2段
(道具: 戻る/ペン/消しゴム/直線/四角/楕円/バケツ、操作: 色/太さ/元に戻す/新規/開く/保存)。
**四角/楕円は選択中にもう一度押すと輪郭⇔塗りつぶし**(アイコンも塗りつぶし版に変わる)。

`CanvasRaster`へ足したもの:

- **`Canvas::Mode`に`Straight`(4)と`Fill`(5)を末尾へ追加**(Luaは数値で指定するので既存値は動かさない。
  `WidgetProperty`は範囲外の`canvas_mode`をエラーにする)。
- **図形は`drawShape()`の1箇所で描く**(ドラッグ中の`frame`へのプレビューと、離したときのスプライトへの
  焼き込みが同じ関数)。四角形は左上/右下へ揃えるので**どちら向きにドラッグしても同じ**(以前は右下へ
  ドラッグした場合しか正しくなかった)。輪郭は`brush_radius`の太さで、四角は4辺を`DrawThickLine()`、
  楕円は周を折れ線で近似して各辺を`DrawThickLine()`(`drawEllipse()`を半径をずらして重ねると縞状の
  隙間が残る)。`filled`プロパティで`fillRect()`/`fillEllipse()`。
- **プレビューのdirtyは「前回+今回の外接矩形」だけ**(`preview_rect`)。以前は動くたびに全体を
  `needsRender()`していた。プレビューは`frame`へ直接描くので、今のクリップ(FlushDirty()が掛けた
  dirty矩形)と自分の矩形の重なりへ絞ってから描く(`getClipRect()`で退避・復元)。
- **塗りつぶし(`floodFill()`)**: スキャンライン方式。種(シード)の置き場は固定長256件で、溢れたら
  「塗った画素に隣接する、まだ塗っていない同色の画素」を全面走査して拾い直す。**塗ったかどうかは色では
  判定できない**(元から塗り色だった画素と区別できない)ので1bit/画素の印を持つ。種と印は塗る間だけの
  一時確保(全面キャンバスで約7KB)。塗った範囲だけをdirtyにする。
  溢れる形(格子状の点等)は全面走査を何周かするので重い。実機での速度は未計測。
- **元に戻す(1段)**: `undo_enabled=true`の間だけ、スプライトと同じ大きさ(`bufferLength()`。4bppで
  `w*h/2`、ペイントの234x194で約23KB)の控えを`malloc`で持つ。描き込む直前(ペンは触れた瞬間、図形は
  離した瞬間、塗りつぶしは実際に色が変わるときだけ、`canvasClear()`、`keep_size`の読み込み)に
  `memcpy`で控え、`undo()`は**控えと中身を入れ替える**(2回目はやり直しになる。2枚持たずに済む)。
  既定は無効(スクラッチパッド等に負担させない)。`resize()`で控えは捨てて取り直す。
- **`pico.canvas_load(id, path, keep_size)`**: `keep_size=true`なら`resize()`せず白紙にしてから
  `DecodePimgBody()`する。**`writePixel()`はクリップされるので、大きい画像は右/下が切れるだけ**。
- `pico.show_file_save(start_dir, default_name)`(`FileSaveDialog::setFileName()`)。
- アイコン10種(pencil/line/circle/square-filled/circle-filled/bucket-droplet/palette/arrow-back-up/
  folder-open/file-plus)を**`ICONS`の末尾へ**足した(IconID 70〜79)。四角形の道具は既存の
  `CheckboxOff`(35、tablerの`square`)を流用している。

気づいたこと:

- **`LuaScene`が読むスクリプトは16KiBまで**(超えると切り詰めて構文エラー)。日本語コメントは1文字3Bで、
  ペイントは最初16.3KBあって踏んだ。`main.lua`冒頭に注意書きを残してある。
- **`Label`では`*`がマークアップとして消える**ので、未保存の印は「(未保存)」と書いている。
- ダイアログを閉じた直後に次のダイアログを開く流れ(保存→上書き確認、等)は、`closed`の中で直接開かず
  `loop()`で2フレーム待つ(`MarkdownScene::Pending`と同じ理由)。
- ホストテストのスタブ(`stubs/LovyanGFX.h`)は`fillRect`/`drawFastHLine`も実際に書き込むようにし、
  `bufferLength()`を足した。塗りつぶし(種あふれを含む)・元に戻す・塗りつぶしの四角形・`keep_size`は
  `lua_engine_test.cpp`で確認。輪郭の図形は`fillCircle`/`fillTriangle`がスタブで無描画なので、見た目は
  PCビルドの`--tap`で確認した。

### テトリス(`pc/sdcard/lua/apps/テトリス/`、2026-09-26実装、2026-10-07に`pico.game`へ移行)

SUMMARY.md #8の「テトリス風」。**本体はLuaアプリ**。作りは下記「ボードゲーム4本のpico.gameへの移行」(盤面=タイルマップ、落ちるミノ=スプライト)。

- ファイル: `main.lua`(規則・状態・見た目への写し方)/ `lib.lua`(ミノの形・SRSの壁蹴りの表・操作ボタンの絵`drawBtn`・HOLD/NEXTのミノの絵`mini`。
  LuaSceneが本体より先に実行しグローバル変数`LIB`で渡る。`main.lua`の16KiB制限は`pico.game`移行でゆとりができた)/
  `blocks.pimg` / `bgm.mml` / `hiscore.txt`(書き出し)/ `app.cfg` / `icon.pimg`。
- **ミノの絵は画像**: 12x12のタイルを横に10枚(I O T S Z J L ゴースト 消えるラインの白 空きマスの黒)並べた`blocks.pimg`(120x12)。
  盤面のタイルマップ・スプライト・HOLD/NEXTの`pico.draw_image_part()`の全部で同じ画像を使う。`script/generate_tetris_blocks.py`がパレット番号で
  直接描く(PIL不要)。パレットに橙が無いのでLは灰。
- 規則: SRSの回転と壁蹴り(左回転は「1つ前の向きからの右回転」の候補の符号反転で引く)、7種1巡、NEXT3つ、HOLD(1個につき1回)、ゴースト、
  接地から500msで固定(動かすと猶予が戻る、15回まで)、左右の長押しは170ms後に50msごと、ソフトドロップ30ms/段(+1点)、ハードドロップ(+2点/段)、
  得点は100/300/500/800×レベル、10ラインごとにレベルが上がり落下はガイドラインの式(`(0.8-(lv-1)*0.007)^(lv-1)`秒)。消える行は200ms白く光る。
- **盤面**: 論理の盤面`grid`(見えない上2段を含む22段)を、`syncBoard()`が`map:set()`でタイルへ写す(`board_dirty`の間だけ。変わったタイルだけエンジンが描く。
  空きマスは黒のタイル(10)、消える行は白のタイル(9))。**落ちているミノとゴースト**は4枚ずつのスプライトで、`syncPiece()`が毎フレーム位置・コマ・表示を書く。
  HOLD/NEXT/点数は`g:on_draw`で描き、変わったとき(`side_dirty`)だけ`g:dirty()`。描き直す範囲に重ならなければ描かない(`pico.get_draw_area()`)。
- **画面**は`g:state`の5つ: title / play / pause / clear(ライン消しの光る200ms) / over。「停止」「戻る」と下の6つの操作ボタンは`g:button`
  (操作ボタンは`draw`で自前の絵。**名前をコントローラーのボタン名(`left` `down` `right` `up` `b` `a`)と同じにした**ので、画面のボタンでも十字キーでも同じ
  `g:down`/`g:pressed`で読める。押したまま指を滑らせると隣のボタンへ移るのもエンジンの機能)。
- 入力: タッチ(下の6ボタン、盤面タップ=右回転/開始/再開、HOLD枠・BGM枠のタップ)とコントローラー。十字キー=移動/ソフト(下)/ハード(上)、A・X=右回転、
  B・Y=左回転、L/R/ZL/ZR=HOLD、START=一時停止/開始、HOME・Esc=戻る(`pico.on_back`)。
  **HOLD/BGMのタップは左の欄(x<SX+54)だけ**(移行前は右の欄全体のキャンバスが反応していて、NEXTの絵をタップしてもHOLDが働いた)。
- 音: BGMは`bgm.mml`(コロベイニキ、ロシア民謡でパブリックドメイン)。効果音は**チャンネル2**(BGMが使わないチャンネル)で鳴らすので曲を借りない。BGM枠のタップで入り切り。
- 検証: `tetris_test`(run.sh。`script/host_test/tetris_test.lua`を`lua_script_test`(vendorしたLuaで.luaを動かすだけの下請け)で実行する。
  **`pico.game`は本物(`LuaBuiltinModules.hpp`の`kGame`)で`pico.*`だけ偽物**。`main.lua`の末尾へ`TEST`(ローカル変数を覗く口)を足して読む。ライン消し・テトリス・
  壁蹴り・HOLD・長押し・ゲームオーバーとハイスコア保存・一時停止・タッチの操作ボタン(滑らせる)・重力・盤面のタイルとスプライトの位置・16KiB以内)、
  PCビルドの`--tap`で盤面・ミノ・ゴースト・HOLD/NEXTの絵(`PICOOS_VERIFY_LCD=1`で食い違い0)。**実機では未確認**。

### ブロック崩し / マインスイーパーの効果音・コントローラー(2026-09-26、2026-10-07に`pico.game`へ移行)

(盤面・画面の作りは下記「ボードゲーム4本のpico.gameへの移行」。ここは音とコントローラーの決まり)
- **ブロック崩し**(`pc/sdcard/lua/apps/ブロック崩し/`): 効果音はチャンネル2で**1フレームに1回、優先度の高いものだけ**鳴らす
  (`se(pri, ...)`で候補を覚え、`g:on_update()`の最後で1回`pico.sound_play`。ボールが16個あると命令の列(32件)が溢れるため)。
  壁<パドル/壊せないブロック<ブロック(色で高さが変わる)<アイテム。ミス/ステージクリア/全クリア/ゲームオーバーは
  `pico.music_play_text()`の短いジングル(チャンネル1なので効果音とぶつからない)。
  コントローラー: 左右=パドル(220px/秒)、A/START/上=発射・次へ・もう一度(ダイアログをやめて`g:state`の枠にしたので、タッチでもコントローラーでも同じ`goKey()`)。HOME・Esc=戻る。
- **マインスイーパー**: 開く(1マス=クリック/0マスで広く開く=低い音)・旗の立て外し・旗モードの切り替え・爆発(ノイズ)・クリアのジングル。
  効果音はチャンネル2(クリアのジングルの頭の音を食わないように)。

### Buttonのアイコン化(`pico.set(id,"icon_id"/"icon_size",...)`、2026-09-23実装)

スクラッチパッドのツールバーをテキストボタンからアイコンボタンへ差し替えて
描画領域を広げる過程で見つけた穴。`Button::setIcon(IconID, IconSize)`
(文字の代わりにアイコンを描く。`getIconId()`はLua側から既に読めていた)自体は
C++側に元からあったが、**`WidgetProperty::Set()`のButtonケースに`Id::IconId`/
`Id::IconSize`が無く、Luaからは`icon_id`が読み取り専用だった**
(`Id::IconSize`はGetすら無かった)。

- `Button::getIconSize()`を追加(既存の`getIconId()`と対になる読み出し口)。
- `WidgetProperty.cpp`のButtonケースへ`Id::IconId`/`Id::IconSize`のGet/Setを追加。
  Setはどちらも`b->setIcon(id, size)`を呼び直す(もう片方は現在値のまま渡す)だけの
  薄い橋渡しで、C++側に新しいロジックは無い。
- アイコンの色は`text_color`(既存プロパティ)に従う(`Button::drawContent()`が
  `IconRender::DrawIcon()`へ渡す前景色がそのまま`text_color`のため)。ボタンの
  背景色を使った状態表現(ペンの色を背景色そのものにする、等)と組み合わせやすい。
- **アイコンボタンにしても`text`プロパティは無効化されない**(`has_icon`が
  trueの間`drawContent()`がtextを無視して読まないだけ)。`icon_id`を設定した後に
  `text`を読み書きしても実害は無いが、画面には出ない。
- `pico.set(id,"icon_id",N)`のNは`src/gui/icons/icons_data.h`の`IconID`の並び順
  そのまま(明示値なしの連番)。**この並びへ新しいアイコンを追加する際は必ず
  末尾(`script/generate_icons.py`の`ICONS`リストの末尾)へ足すこと** —
  途中へ挿入すると、既存のLuaスクリプトが数値で指定しているアイコンが黙って
  別物にすり替わる(この制約自体は`generate_icons.py`にコメントで明記した。
  今回`Save`/`StackPop`/`StackPush`/`Brush`/`Eraser`の5種が追加されているが、
  この時点ではまだLua側が数値のIconIDに依存していなかったため実害は無かった。
  以降は要注意)。ordinal一覧は`lua-api-doc/content/reference/limits.md`の
  「icon_id(IconID)」に載せてある(スナップショットである旨を明記)。
- ホストテストは`lua_engine_test.cpp`の「細部のプロパティ」ブロックへ追加
  (icon_id/icon_sizeのget/set往復、`setIcon()`経由で`has_icon=true`になること、
  `w`/`h`未指定なら箱の大きさが`icon_size`へ追従すること)。実際の見た目は
  スクラッチパッドのPCビルド`--shot`で確認した。

### シーン制御(2026-09-21実装)

`pico.push_scene/change_scene/launch_app`。それまで`pico.pop()`(`SceneFunctions::Pop()`)
しか無く、Luaスクリプトは「自分を起動した画面へ戻る」以外の画面遷移ができなかった。
C++側の`SceneFunctions::Change/Push/Pop`に相当する3つを揃え、Lua同士の複数画面アプリと、
Luaから既存アプリ(C++製含む)へ飛ぶことの両方をカバーした。

- **`pico.push_scene(path)` / `pico.change_scene(path)`**: `new LuaScene(path)`を
  `SceneFunctions::Push()`/`Change()`へそのまま渡すだけ。Lua側が構築できるScene型は
  `LuaScene`(パス文字列1つのコンストラクタ)だけなので、この2つは**Lua同士の画面遷移**
  (複数画面のLuaアプリを組む)専用になる。`LuaScene(path)`のコンストラクタは
  `FixedString`へパスをコピーするだけで失敗し得ないため、`pico.pop()`と同じく戻り値なし
  (要求を登録するだけで、実際の遷移・エラー表示(ファイル不在等)は次のフレーム境界の
  `LuaScene::onEnter()`まで保留される。呼び出し中の今のLuaEngine自身がその場で
  破棄されることはない)。
- **`pico.launch_app(name)`**: 新設した`AppFunctions::LaunchByName(name)`
  (`App_Functions.hpp/.cpp`、完全一致で登録簿を線形探索して見つかれば`Launch(index)`を
  呼ぶだけの薄いラッパー)経由で、ランチャの登録簿にある**任意のアプリ(C++製含む)**へ
  `Push`する。名前が見つからなければ`false`(呼び出し元がスクリプト側のtypoに気づける
  ようにするための戻り値。他は`AppFunctions::Launch()`をC++コードが直接呼ぶ場合と同じで、
  実際のPush自体が成功するか(スタック上限等)までは見ていない)。
- **`push_scene`は新しいシーンをスタックへ退避するのでPop()で戻れる。`change_scene`は
  スタックを消費せず現在のシーンを置き換えるだけ**(`SceneFunctions::Change`と同じ)。
  そのため`change_scene`で入った画面から`pop()`すると、`change_scene`を呼んだ側の画面を
  飛び越して、**その手前**(スタックの先頭)へ直接戻る。PCビルドの`--shot`で
  「push_scene→change_scene→pop」の一連を実際に確認済み(下記サンプル参照)。
- **LuaSceneはPop()で戻ってきたときスクリプトを最初から実行し直す**(既存の仕様、上の
  「`LuaScene`」参照)。そのため`pico.launch_app()`をスクリプトのトップレベルで
  無条件に呼ぶと、そのシーンへPop()で戻ってくるたびに再度発火してしまう
  (実装時にホストテストでこれを踏んだ。ボタンの`press_start`等のイベント越しに
  呼ぶ分には問題ない。`push_scene`/`change_scene`も同じ理由でボタン経由が無難)。
- ホストテストは`lua_scene_test.cpp`に追加(`lua_engine_test.cpp`ではなくこちら。
  実際にシーン遷移まで起こす必要があるため、既存の`FakeLauncherScene`+
  `SceneFunctions::Setup/Update`の結合テスト環境にそのまま乗せた)。
  push_scene/change_sceneは別のLuaスクリプトへ実際に遷移すること・要求がフレーム境界まで
  保留されること・スタック深さの増減(push_sceneは+1、change_sceneは変化なし)、
  launch_appは登録簿のC++製アプリ(テスト用の`OtherAppScene`)へ実際に遷移すること・
  未登録名は`false`を返すことを確認している。
  実際の見た目はPCビルドの`--shot`で確認した:
  `pc/sdcard/lua/hello.lua`に追加した「サブ画面へ」ボタンから`pc/sdcard/lua/hello_sub.lua`
  (`pico.push_scene()`の飛び先。「電卓を開く」で`pico.launch_app("電卓")`、
  「置き換えへ」で`pico.change_scene()`)→`pc/sdcard/lua/hello_sub2.lua`
  (`pico.change_scene()`の飛び先)という3ファイル構成のデモ一式を作り、
  push_scene→launch_app(電卓が実際に開く)、push_scene→change_scene→pop
  (hello_sub.luaを飛び越してhello.luaへ直接戻る)の両方を確認した。

### ダイアログ(2026-09-21実装)

`pico.show_message/show_input/show_file_save/show_file_select/show_color`。
`MsgDialog`/`InputDialog`/`FileSaveDialog`/`FileSelectDialog`/`ColorDialog`を
Luaスクリプトから表示できるようにした(`SearchDialog`はMarkdownブラウザの検索フロー
専用の状態を前提にしているため対象外)。

- **`WidgetFactory`は経由しない。** 上記5種は`WidgetFactory::Create()`が対応する
  汎用部品15種の対象外で(コンストラクタが型ごとに必須の引数を取り、「位置0,0・
  空文字列」といった無難な既定値では作れない)、代わりに各ダイアログ専用の
  `pico.show_xxx()`を用意した。中身は`new Xxx(...)`→`WidgetFunctions::AddDialog()`
  →`setVisible(true)`→`WireDialogClosed()`という同じ手順で、生成した`WidgetId`を返す。
  `WidgetId`の発行自体(`Widget::getId()`の初回呼び出しで`WidgetRegistry::Register()`)は
  どの`Widget`サブクラスでも汎用に効くため、`WidgetFactory`を経由しなくても
  `pico.on()`/`pico.get()`から安全に参照できる。
- **閉じた通知は共通の`pico.on(id, "closed", function(id, is_ok) ... end)`で受ける。**
  新設した`EventKind::Closed`は既存の`callbacks_`(WidgetId+種別→Lua registry ref)に
  そのまま乗せたが、**実際のC++側コールバック配線(`setOnClosed`/`setOnClose`)は
  生成時点(`WireDialogClosed()`)で済ませてしまう**点が他の4イベントと違う。
  理由: ダイアログはモーダルなので、`pico.on()`を呼び忘れても画面に居座り続けては
  いけない。生成時に必ず配線しておくことで、**`pico.on()`の有無に関わらず閉じたら
  `WidgetFunctions::DestroyLater()`されることを保証**し、`pico.on()`は「あれば
  追加でLuaへも通知する」という上乗せの位置づけにした(`BindCallback()`の
  `EventKind::Closed`ケースは`callbacks_`への登録だけ行い、ウィジェット側の配線はしない)。
  `DispatchClosed(id, is_ok)`は`Dispatch(id, kind)`と別メソッドにしてある
  (`is_ok`を2つ目のLua引数として渡す必要があり、既存の「WidgetId 1引数固定」の
  `Dispatch()`とは呼び出し規約が違うため)。
- **`MsgDialog`/`InputDialog`は`setOnClosed()`、`FileSaveDialog`/`FileSelectDialog`/
  `ColorDialog`は`setOnClose()`と、綴りが割れている**(実装時に気づいた既存コードの
  不統一。直さず`WireDialogClosed()`側の`switch`で吸収した)。
- **`InputDialog`の入力文字列/`FileSaveDialog`・`FileSelectDialog`の選択パス/
  `ColorDialog`の選択色は、`Dispatch`の引数に積まず`WidgetProperty`経由で読む設計**
  にした(`pico.get(id, "text"/"path"/"value")`)。`Dispatch`をダイアログの具象型に
  依存させたくなかったため。`WidgetProperty::Get/Set()`はこれまで
  `WidgetFactory::IsCreatable()`の15種だけが対象だったが、この4種(`MsgDialog`は
  `is_ok`だけで完結するので追加のプロパティ無し)を相乗りさせた。`text`プロパティは
  既存のButton/Label/Textbox等と同じId(`InputDialog::getInput()`/`setInput()`に
  マップ)、`path`は既存のImage用Id(`FileSaveDialog::getSavePath()`/
  `FileSelectDialog::getSelectedPath()`。後者は未選択なら`nullptr`→Lua側は`nil`)、
  `value`は既存のNumberSlider用Id(`ColorDialog::getSelectedColor()`。未選択は`-1`、
  型はNumberSliderと違いInt)を流用しており、新規のプロパティId追加は無し。
- ホストテストは`lua_engine_test.cpp`に追加(ハンドル発行・`dialog_roots`登録・
  `closed`のis_ok往復・`pico.get()`での結果読み出し・`pico.on()`を呼ばなくても
  自動的に破棄されること・`closed`イベントがダイアログ以外だとエラーになること)。
  `FileSaveDialog`/`FileSelectDialog`はホストテストのSdFatスタブがディレクトリの
  実体を持たない(パス→内容のフラットな`map`)ため`getSavePath()`/`getSelectedPath()`の
  中身までは確認できず、生成・キャンセル・自動破棄までに留めた。実際の選択結果と
  見た目はPCビルドの`--shot`で確認済み(`show_message`/`show_input`(初期値の
  プレフィル込み)/`show_color`(実際にスワイプ相当のタップでスウォッチ→OKを押し、
  `pico.get(id,"value")`で選択色が読めることまで)。
  なお、この作業でFileExplorerを初めてホストテストへリンクすることになり、
  `script/host_test/stubs/SdFat.h`に`isDirectory()`(`isDir()`とは別綴りのSdFat API。
  `pc/compat/SdFat.h`には両方あるがスタブは`isDir()`だけだった)が無いことに気づいて追加した。

### ネットワーク(2026-09-21実装)

`pico.http_request(method, url, body, content_type, callback)` / `pico.http_cancel()`。
既存の`Http_Get`(`src/task/Http_Get.hpp`)はMarkdownブラウザのキャッシュ用途に
特化していて汎用のHTTPクライアントとしては使えなかった(GET専用でメソッドが
`sendRequestLine()`にリテラル`"GET "`で埋め込まれている/送信ボディの概念が無い/
`HttpBodyGate`が`statusCode()==200`以外の本文を黙って捨てる/200・304以外を
一律`FAILED`扱いにする)ため、新設の`HttpRequest`(`src/task/Http_Request.hpp/.cpp`)を
別クラスとして用意した。

- **`HttpGet`とコード骨格(接続/送信/受信ループ・タイムアウト・リダイレクト追跡)は
  ほぼ同じにしてある**(実績のある形をそのまま踏襲。差分だけ抜き出す共通基底への
  リファクタは、Markdownブラウザの動いているHTTP経路を壊すリスクの方が大きいと
  判断して見送った)。**違うのは以下3点だけ**:
  1. メソッドが`enum class Method { GET, POST, PUT, PATCH, Delete }`で選べる
  2. 送信ボディ+`Content-Type`を指定でき、`Content-Length`ヘッダを自分で付けて
     ボディを送る(`HttpResponse`が読む*応答*の`Content-Length`とは別物)
  3. **`HttpBodyGate`を挟まない**(`res.reset(sink_)`で直結)ため、
     ステータスコードによらず本文がsinkへ渡る。Task自体の成功/失敗も「応答を
     最後まで読めたか」だけで判定し、ステータスコードの意味(2xx/4xx/5xx)は
     呼び出し側(Luaスクリプト)へ渡す。**Markdownブラウザ側の判断(200/304だけ
     成功、他は本文を捨てる)はそのまま`HttpGet`に残しており、この変更の影響は
     受けない**
- **GETだけリダイレクトを自動で追う。** POST等はボディを送り直すべきか
  (RFCの解釈も一律ではない)を決め打ちせず、3xxが返ってきてもそのまま
  呼び出し側(Luaスクリプト)へ渡す判断にした。
- **`HttpGet`/`HttpRequest`はどちらも`PICO_Task`の全体リストには登録されない。**
  実際の利用者(`DocFetch`/`DocSearch`)は`HttpGet`を値メンバとして持ち、
  自分の`update()`から毎フレーム`http.update()`を呼ぶ設計になっている
  (`NetworkScan`だけが`PICO_Task::Add()`でグローバルリストに乗る例外)。
  同じ流儀で、`LuaEngine`が`HttpRequest`を1本(`http_`、後述の理由で遅延`new`)
  保持し、**`LuaScene::onUpdate()`から毎フレーム`engine->UpdateHttp()`を呼んで
  進める**(`setup()`/`loop()`の定義有無に関わらず無条件に呼ぶ。`CallLoop()`
  とは別の独立した呼び出しにしてあり、`pico.*`のネットワーク進行を
  Arduino風`loop(dt)`の意味論と混ぜていない)。
- **同時に走るリクエストは1本まで(2026-10-05からは2本目以降を最大4本まで順番待ちにする。下記「Lua APIの追加(2026-10-05)」)。** `HttpState::callback_ref`が
  `LUA_NOREF`かどうかで「進行中か」を判定する。進行中に`pico.http_request()`を
  呼んでも`luaL_error`にはせず`false`を返すだけ(SD無し等と同じ「実行時の状態」
  枠)。完了時は**Lua側コールバックを呼ぶ前に`callback_ref`を`LUA_NOREF`へ戻す**
  ことで、コールバック自身の中から次の`pico.http_request()`を呼べるようにしてある
  (チェイン可能)。
- **`http_`(`HttpRequest`+受信バッファ+送信ボディの控え。受信/送信とも
  `kMaxHttpResponseBytes`/`kMaxHttpBodyBytes`=16KiBで頭打ち)は初回の
  `pico.http_request()`呼び出しまで`new`しない。** ネットワークを使わない
  Lua アプリ(大半を占める見込み)に32KiB(16KiB×2)の固定バッファを常時
  負担させたくないための遅延生成(`struct HttpState`はヘッダでは前方宣言のみ)。
- **送信ボディ/Content-Typeは呼び出し時にLuaEngine側の`FixedString`へコピーする。**
  `HttpRequest::begin()`は`const void* body`/`const char* content_type`を
  非所有ポインタ(`IHttpSink*`と同じ約束)で受け取るが、Luaスタック上の一時的な
  文字列をそのまま渡すと、`Connecting`フェーズが次のフレームへ回る(`HttpGet`と
  同じ設計。フレーム時間をならすため)間にポインタが無効になりかねない。
  `HttpState::body_buf`/`content_type_buf`へ一度コピーしてから渡すことで解決した。
- **受信本文はNULを含み得るため、Luaへ渡す際は`lua_pushlstring()`(長さ明示)を使い、
  `lua_pushstring()`(strlen前提)にしていない。**
- ホストテストは`lua_engine_test.cpp`に追加。**実ソケットに一切触れない範囲
  (未知のメソッド/不正なURL/送信ボディの上限超過/同時実行数の上限)での
  早期拒否がすべて`false`(またはpcall経由でエラー)になることだけを確認**しており
  (`HttpGet`と同様、実際の通信を伴う検証は`run.sh`(ASan、ネットワーク無し)の
  対象外。`run_net.sh`と同じ「本物のソケット」区分に属する)、送信ボディの上限
  テストで16KiB超の文字列を作る都合上、それまでの全テストで積み上がった共有
  `engine`の予算と衝突しないよう専用の`LuaEngine`を使っている。
  **実際の通信はPCビルドで、使い捨てのローカルHTTPサーバ(`http.server`)を相手に
  動作確認した**(このリポジトリには含めていない検証用スクリプト): GET成功
  (status=200、本文が読める)、POST(送信ボディ+Content-Typeが実際にサーバへ届き、
  エコーされた応答本文が読める)、**404応答でも本文が読めること**
  (`HttpGet`の`HttpBodyGate`なら本文が捨てられていたはずの経路で、`HttpRequest`が
  正しく本文を渡せていることの確認)の3パターンを確認済み。

### ウィジェット固有イベント(2026-09-21実装)

`pico.on(id, event_name, fn)`の共通4種(press_start/end/move/out)+`render`(Canvas限定)+
`closed`(ダイアログ限定)に加え、一部のウィジェットが元々持っていた専用コールバックも
Luaから使えるようにした:

| イベント名 | 対象ウィジェット | 元のC++コールバック |
|---|---|---|
| `checked_changed` | `Checkbox` | `setOnChangeChecked()` |
| `value_changed` | `NumberSlider` | `setOnValueChanged()` |
| `select_item` | `ScrollList` | `setOnSelectItem()` |
| `tab_changed` | `TabBar` | `setOnChanged()` |
| `dropdown_changed` | `DropdownMenu` | `setOnChanged()`(2026-09-21追加。細部の穴埋め) |
| `text_changed` | `Textbox` | `setOnTextChanged()`(2026-09-21追加。細部の穴埋め) |

- **対応するウィジェット種別以外へ登録しようとすると`"render"`/`"closed"`と同じく
  `luaL_error`になる**(`l_on()`側で`getWidgetType()`を見て弾く)。
- **`checked_changed`/`value_changed`/`tab_changed`の3つは、変わった後の値そのものを
  コールバック引数として渡さず、既存の共通`Dispatch(id, kind)`(idのみ渡す)にそのまま乗せた。**
  Checked/Value/TabSelectedはいずれも`pico.get(id, "checked"/"value"/"tab_selected")`で
  読める永続プロパティ(`WidgetProperty`)なので、Lua側はコールバック内でそれを読めば足りる。
  値の型・個数をイベントごとに変える専用Dispatchを増やすより、既存の「値はpico.getで読む」
  という約束に寄せた方が単純だと判断した。
- **`select_item`だけは例外で、`already_selected`(同じ項目を2回連続でタップしたか。
  `SearchDialog`等の「2回タップで開く」判定に使う値)をコールバックの第2引数として渡す
  専用の`DispatchSelectItem(id, already_selected)`を用意した。** これは`ScrollList::on_selectitem`が
  渡す一時的な値で、`selected_index`のような永続プロパティとして持てないため
  (`DispatchClosed(id, is_ok)`と同じ理由づけ)。選択後のindex自体は"select_item"の中で
  `pico.get(id, "selected_index")`を読めばよい。
- コールバックの配線方式(`this`(`LuaEngine*`)+`WidgetId`だけをキャプチャしてヒープ確保を
  起こさない)は共通4種と同じ(`BindCallback()`参照)。
- ホストテストは`lua_engine_test.cpp`に追加。6イベントそれぞれについて、対応するC++側の
  トリガ(`Checkbox::causeOnPressStart()`でのタップ相当/`NumberSlider::setValue()`/
  `ScrollList::causeOnSelectItem()`/`TabBar::setSelected(index, true)`/
  内部`ScrollList`経由の`DropdownMenu`選択/`Textbox::onHide()`)を直接呼んで
  Luaコールバックが実際に発火すること、`pico.get()`で変更後の値が読めること、
  `select_item`の`already_selected`が引数で渡ること、対応外のウィジェットへの登録が
  エラーになることを確認している。

**追加分(2026-09-21、細部の穴埋め): `dropdown_changed`(DropdownMenu)と
`text_changed`(Textbox)。** どちらも元々「作れるのに変化を知る手段が無い」状態だった:
- `DropdownMenu`は元々選択されたことを外へ知らせる仕組み自体が無く(内部で
  `value`ラベルの表示更新だけを完結させていた)、`DropdownMenu::on_changed`
  (`std::function<void()>`。`TabBar::on_changed`と同じ「引数無し、値は
  `pico.get(id,"selected_index")`で読む」形)を新設して`setOnSelectItem()`の
  ラムダ内(表示更新の直後)から呼ぶようにした。`ScrollList`と違い「同じ項目の
  選び直し」がそのまま2回目の確定として飛ぶことは無い(開き直しにしかならない)ため、
  `already_selected`に相当する概念自体が要らず、共通Dispatch組にそのまま入る。
- `Textbox::on_text_changed`(`std::function<void()>`)は**以前からヘッダに宣言だけ
  あったが、setterも発火する場所も無い死んだメンバだった**(実装時に見つけた)。
  `setOnTextChanged()`を追加し、`onHide()`(オンスクリーンキーボードを閉じて
  入力が確定したタイミング。`onTextChanged()`が「入力途中は背景を更新しない」
  方針なのに合わせ、1文字ごとには発火させない)から呼ぶよう配線した。
  ホストテストは実機のオンスクリーンキーボードを介さず、最小限の`ITextInputWidget`
  実装(`FakeKeyboard`)を使って`Textbox::onHide()`を直接呼ぶことで検証している。

### コンテナからの取り外し / リストへの項目追加(2026-09-21実装、細部の穴埋め)

`pico.remove_child(container_id, child_id)`: `pico.add_child`の逆。`pico.destroy(child)`は
子ごと破棄する経路しか無く、「親から外して別のコンテナへ移す」「一旦フリーにして後で
作り直す」といった用途に使えなかった。

- `LayoutContainer`/`GridContainer`/`ScrollContainer`のみ対応(それ以外は`luaL_error`)。
  指定した`child`が実際にその`container`の子でない場合もエラーにする。
- 手順は`WidgetFunctions::Remove(child)`相当: `container->removeChild(child)`(仮想関数、
  各コンテナのoverrideがそのまま呼ばれる)→フラットリスト(`WidgetFunctions::widgets`)へ
  `Add()`し直す。取り外した子は次フレームから独立したルートウィジェットとして
  描画・当たり判定の対象になる。
- **`Widget::removeChild()`の3つのoverride(`LayoutContainer`/`GridContainer`/
  `ScrollContainer`)全てに`child->setParent(nullptr)`を足した(実装の副産物)。**
  以前は`children_`から外すだけで親ポインタが残ったままになっており、
  `WidgetFunctions::Destroy()`経由(直後に`delete`するので実害が無い)でしか
  呼ばれていなかったため問題が顕在化していなかった。`pico.remove_child`で
  子を**生かしたまま**取り外す経路ができたことで、親ポインタを残すと
  取り外し後も旧コンテナ基準で`getScreenRect()`等を計算してしまうバグになるため、
  必須の修正として一緒に入れた。
- **取り外し後もx/y座標はコンテナ内での相対値のまま残る**(コンテナが管理していたのは
  位置決めだけで、子自身の`l_rect`はコンテナ座標系の値を持ち続ける)。`pico.create()`
  直後と同じく、呼び出し側が`pico.set(id,"x"/"y",...)`で置き直す前提。
- ホストテストは`lua_engine_test.cpp`(既存の`add_child`テストの続きとして追加)。
  取り外し後に親が`nullptr`になること・コンテナの`children_`から外れること・
  破棄されずフラットリストへ独立したルートとして戻ること・`WidgetId`がまだ有効な
  ことを確認している。

`pico.list_add(id, text)` / `pico.list_clear(id)` / `pico.tab_add(id, label)`:
`ScrollList`/`DropdownMenu`/`TabBar`は`pico.create()`で生成できるのに、中身を
増やす手段が無かった(C++側は`ScrollList::add()`/`DropdownMenu::add()`/
`TabBar::addTab()`を直接呼べるが、Luaから叩く経路が無かった)。

- `pico.list_add`はアイコンを指定できず既定(`IconID::AppBox`)固定
  (`ScrollList`は`enable_icon`がfalseの間そもそも描かれない)。名前→`IconID`の
  変換表を足す話は将来の拡張として残し、今回は「文字列を足せる」ことを優先した。
- `pico.list_clear`で`DropdownMenu`を空にする場合、項目を消すだけでなく表示ラベルも
  プレースホルダへ戻す(`DropdownMenu::clear()`新設)。選択済みの表示だけが
  残ってしまわないようにするため。
- `pico.tab_add`は`TabBar::addTab()`の戻り値(`kMaxTabs=4`超過で`false`)をそのまま返す。
  呼び出し側がタブ数の上限に達したことを検知できるよう、`luaL_error`にはしていない。
- 副産物として`ScrollList::getItemCount()`/`DropdownMenu::getItemCount()`を追加し、
  `WidgetProperty`へ`item_count`プロパティ(`pico.get(id,"item_count")`)として
  乗せた(読み取り専用。`list_add`/`list_clear`後の件数確認に使う)。
- ホストテストは`lua_engine_test.cpp`。2種のウィジェットへの追加・全消し・件数確認、
  `TabBar`の上限到達、対応外ウィジェットへの呼び出しがエラーになることを確認している。

### 時刻取得(2026-09-21実装)

`pico.get_time()`。`TimeFunctions::timeinfo`(NTP同期後に妥当な値になる。`Setup()`呼び出し
自体はLuaEngineの責務ではなく、main.cppが起動時に済ませている)をLuaへ橋渡しするだけの
薄いAPI。

- 年/月は`TimeFunctions::year`/`month`(`tm_year`/`tm_mon`から1900年オフセット/0始まり月を
  補正済みの値。`ClocksScene`等と同じものを使う)をそのまま使い、残り(日/時/分/秒/曜日)は
  `struct tm`のフィールドをそのまま渡す。`wday`は`tm_wday`そのまま(0=日曜〜6=土曜)。
- **戻り値は`pico.content_rect()`のような複数戻り値ではなく、フィールド名付きの1個の
  テーブル**(`{year=.., month=.., day=.., hour=.., min=.., sec=.., wday=..}`)にした。
  `pico.sd_list()`が`{name=.., is_dir=..}`の配列を返すのと同じ「複数の名前付き値は
  テーブルで返す」という使い分け(`content_rect`はx/y/w/hの4値のみで意味も自明なため
  複数戻り値のままにしてある)。
- **NTP未同期の場合の値の妥当性はこのAPI側では保証しない**(`ClocksScene`等、既存の
  `TimeFunctions`利用箇所と同じ割り切り。`main.cpp`起動時点、あるいはWi-Fi未接続のままの
  場合は同期前の初期値がそのまま返る)。
- ホストテストは`lua_engine_test.cpp`に追加。`TimeFunctions::timeinfo`/`year`/`month`を
  テストから直接書き換え、`pico.get_time()`が返すテーブルの各フィールドと一致することを
  確認している(`Setup()`/`Update()`はNTP同期や`millis()`に依存するため呼ばず、`struct tm`を
  直接埋める)。

### タップ位置の取得(pico.get_touch、2026-09-21実装)

Luaでオセロアプリ(`pc/sdcard/lua/apps/オセロ/main.lua`)を作った際に踏んだ穴を埋めた。
`pico.on(id, "press_start", fn)`等の共通4イベントはWidgetIdしか渡さない(「コールバックの
引数は多くの場合WidgetIdのみ」という既存の設計方針。イベントごとに引数の型・個数を
変える複雑さを避けるための意図的な判断で、これ自体は変えていない)。そのため「盤面全体を
1枚の`Canvas`にして、押された座標からマス目を逆算する」というC++側の`AppGrid`/
`ColorDialog`と同じパターンがLuaからは組めず、オセロの初版はマスの数(8×8=64個)だけ
小さな`Canvas`を敷き詰めて、それぞれの`press_start`で「どのマスが押されたか」を判定する
遠回りをしていた。

- **`pico.get_touch() -> x, y, is_touched`を追加した。** 中身は`OSData::touchX/touchY/
  isTouched`をそのまま返すだけの薄いAPI(`pico.get_time()`と同じく、既存のグローバル状態を
  橋渡しするだけで新しい状態は持たない)。`WidgetFunctions::HitTest()`
  (`src/functions/Widget_Functions.cpp`)が当たり判定に使っているのと同じ値なので、
  `press_start`等のコールバックの中で読めば「そのタップが当たったウィジェットの外側から見た
  絶対スクリーン座標」と一致する(`pico.draw_*`/`pico.content_rect()`と同じ座標系)。
- **戻り値は`pico.get_time()`のようなテーブルではなく、`pico.content_rect()`と同じ複数戻り値**
  (`x, y, is_touched`)にした。名前付きのフィールドが3つだけで意味も自明なため
  (`get_time`がテーブルにした基準「複数の名前付き値」に該当するほど多くない)。
- **`isTouchEnd`(離した瞬間)でも座標はリセットされず最後の値を保持したまま**なので
  (`Touch_Functions.hpp`/`Touch_Functions_PC.hpp`の`Update()`、`if(!touched){ ... }`の枝を
  参照。`touched=false`の間`OSData::touchX/touchY`へは一切書き込まない)、`press_end`の中で
  呼んでも問題なく使える。
- **イベント引数のシグネチャ自体(`Dispatch(id, kind)`)には一切手を入れていない。**
  座標を「その場で起きたイベントの引数」として渡す設計にはせず、「今の状態を問い合わせる
  別関数」にしたことで、既存の8種類のイベント(共通4種+ウィジェット固有4種+render+closed)の
  ディスパッチ経路・コールバック中継の仕組み(`LuaEngine*`+`WidgetId`だけをキャプチャする
  設計、上記「コールバック中継の設計」参照)を一切変更せずに済んだ。
- ホストテストは`lua_engine_test.cpp`。`OSData::touchX/Y/isTouched`を直接書き換えてから
  `Widget::causeOnPressStart()`(実際のタップ相当)を発火させ、`press_start`コールバックの
  中で`pico.get_touch()`が同じ値を読めることを確認している。
- ドキュメント(`lua-api-doc/`)にも[タッチ](../../api/touch/)ページを新設し、
  [イベント](../../guide/events/)と[Canvasと直接描画](../../guide/drawing/)の両方から
  相互参照するようにした。オセロアプリ自体は(既に動いていたため)このAPI追加に合わせた
  書き換えはしていない——マス目ごとに`Canvas`を敷き詰める方式も引き続き有効で、
  マスごとに全く違う描画をしたい場合はむしろこちらのほうが素直なことがある。

### 実装中に見つけて直した既存のバグ2件

Luaバインディングを実際に動かして初めて踏んだ、`LayoutContainer`/`GridContainer`が
「Luaアプリ向けに用意したが実コードでは一度も使われていなかった」ことに起因する
潜在バグ。どちらも`lua_engine_test.cpp`がASanで検出し、修正して回帰確認済み。

- **`pico.add_child`の重なり順**: `WidgetFunctions::widgets`は追加順=描画順(後が上)のフラットな
  配列で、`Widget::needs_children_update`が立った次のフレームにコンテナ経由で子孫を再スイープする
  仕組みは既にあった(`Widget_Functions.cpp::UpdateAll()`)。しかし生成順が「子→親」だと、
  子が配列内で親より手前(=下)の位置に残ったままになり、親の描画に隠れてしまう。
  対策: `pico.add_child()`は`container->add(child)`する前に一度`WidgetFunctions::Remove(child)`で
  フラットリストから外す。こうすると次フレームの再スイープで「まだ登録されていない子」として
  親の直後(=上)へ正しく入り直す。
- **`ScrollContainer`に`removeChild()`が無かった**: `LayoutContainer`/`GridContainer`は
  `removeChild()`をoverrideして`children_`から取り除くが、`ScrollContainer`だけ無く、
  基底の空実装のままだった。そのため子を`WidgetFunctions::Destroy()`等で個別に破棄すると、
  `children_`に残った破棄済みポインタを`~ScrollContainer()`がもう一度`delete`し二重解放になる。
  `pico.destroy(child_of_scroll_container)`で実際に踏める経路だったため、
  `LayoutContainer`と同じ形の`removeChild()`を追加した。

### `LuaScene`(`src/gui/scenes/LuaScene.hpp/.cpp`)(2026-09-19実装)

SD上のLuaスクリプトを1本読んで実行する画面。`AppEntry`の`MakeSceneWithArg<LuaScene>`パターンで
スクリプトパス("/lua/hello.lua"のようなSD絶対パス)を`arg`に渡して登録する
(同じ`LuaScene`型を別のargで何個でも登録できるので「Luaスクリプトごとに1タイル」が作れる)。

- **ライフサイクル**: `onEnter()`で`LuaEngine`を`new`(予算`kLuaBudgetBytes=200KB`)、
  スクリプトをSDから読んで`Run()`する。`onExit()`で`delete`。**Push()で背後へ退避される場合も
  `onExit()`は呼ばれる**(`Scene`のレイヤ説明通り)ので、Lua stateもウィジェットと同じく
  「シーンがアクティブな間だけ」の寿命にした。他のシーンのように状態をメンバへ退避して
  `onEnter()`で復元する形にはできない(Luaアプリの状態はスクリプト内のLua変数にあり、
  C++側から見えないため)ので、**`Pop()`で戻ってきたらスクリプトを最初から実行し直す**。
- **スクリプトの読み込み**は`MarkdownView::load()`と全く同じ手順(256Bのスタックチャンクで
  読み進めてメンバの`FixedString`へ追記。ヒープを使わない)。上限は`kMaxScriptBytes=16KiB`
  (`MarkdownView`の8KiBより大きくしてあるのは、`pico.*`呼び出しの羅列は文書より冗長になりがちなため)。
  超過分は警告ログを出して打ち切る。このバッファは`LuaScene`のメンバなので、`LuaScene`自体が
  `MarkdownScene`と同じく「シーン本体は数十バイト」の例外になる。
- **ランチャへ戻る手段はスクリプト側が自前で用意する**(`pico.create("Button")`+
  `pico.on(id, "press_start", ...)`で`pico.pop()`を呼ぶ。`ClocksScene`/`CalculatorScene`等、
  他のアプリの「戻る」ボタンと同じ考え方)。これに伴い`LuaEngine`へ2関数を追加した:
  - `pico.pop()`: `SceneFunctions::Pop()`を呼ぶだけ
  - `pico.content_rect()`: `Scene::contentRect()`を`x,y,w,h`の4値で返す。
    Luaアプリだけステータスバーの下に潜り込む、といったズレを防ぐ
- **動作サンプル**: `pc/sdcard/lua/hello.lua`(ボタンでカウンタが増える最小限の画面)を
  `App_List.cpp`に`Register("Lua Hello", IconID::AppBox, &MakeSceneWithArg<LuaScene>, "/lua/hello.lua")`
  として登録済み。PCビルドで`--tap`により実際にタップ→カウンタ更新→戻るまで動作確認済み
  (`pc/build/picoos_pc`)。
- ホストテストは`script/host_test/lua_scene_test.cpp`。`Scene_Functions.cpp`と組み合わせた
  結合テストで、SDからの読み込み・`pico.pop()`での実際のランチャ復帰・ファイル不在時の
  ダイアログ表示・大きすぎるスクリプトの打ち切り警告を確認している
  (`script/host_test/stubs/SdFat.h`の`HostSd::files`にスクリプトを登録して読ませる)。

### Luaは32bit(`LUA_32BITS`、2026-10-09)

`lib/lua/src/luaconf.h`の`LUA_32BITS`を1にした(vendorしたLuaへの唯一の改造。`lib/lua/README-pico-os.md`)。整数は32bit、小数は単精度。
RP2350は単精度しかハードウェアで計算できず、実機のゾンビTDで1フレームのLuaが5〜9ms(PCの約30倍。PCで測るとごみ集めはほぼ0で、
Luaの計算そのものが一様に遅かった)かかり、メモリも足りなかったため。実機では値1つが16→8バイト。
- 合わせて直したもの: `pico.json_decode`は32bitを超える整数を小数にする(以前は`strtoll`の値をそのまま詰めて化けた)、
  `json_encode`は単精度の小数を`%.7g`で書く、`pico.micros()`の差は`b - a`でよい(ゾンビTDの`us_since`)。
- Luaから見える違いはAPIドキュメントの`reference/limits.md`「数の範囲」。`WidgetId`(上位6bitが種類)は負の整数になりうるが、C++側は
  `(WidgetId)luaL_checkinteger()`でビット列のまま戻すので往復できる。
- 確認: ホストテスト全部(`lua_engine_test`に32bitとJSONの項目)、PCビルドのゾンビTD、実機ファームのビルド(`pio run`)。
- 実機の結果(ゾンビTD): Luaのメモリは約176KB → 約131〜159KB、ゾンビ0匹の始めのLuaは約5ms → 約0.4ms。ただ遊んでいる間は
  0匹でも4〜7ms、12〜18匹で8〜12ms(PCの約30倍のまま)。PCで区間ごとに測ると突出した所は無く、Luaの実行そのものの速さと見て、
  **ゾンビTDのゲームの計算を1秒に30回(`SIM_MS`=33ms)にまとめた**(描き直しと操作は毎フレーム)。PCで1フレーム0.37ms → 0.12ms。
- 同じときに見つけたメモリ: **曲の置き場を曲の大きさだけ確保するようにした**(`Sound_Functions.cpp`の`music_slots`。以前は最初に鳴らしたときに
  6KiB×2+読み取り係 約3.5KBを確保して持ち続け、ジングルを鳴らすだけのゾンビTDが約15KBを抱えていた。読み取り係と6KiBの作業場所は
  読む間だけ確保する)。`pico.iso`の「ごみを集めてやり直す」は1秒に1回まで(全部集めるのは実機で数十msかかり、兵士の数だけ続いて止まった)。

### Luaの確保がヒープの残りを守る(`LuaEngine::Alloc` / `MemFunctions::HeapTopRoom()`、2026-10-10)

実機のゾンビTDが「ウェーブ4以降にランダムで再起動する」報告への対策。**Luaの予算(200KB)はヒープ全体の空きより大きい**ので、
Luaが予算の内側でもヒープを使い切ることがあり、そのとき次のC++側の確保(`new`/`std::vector`/`std::string`/SD/Wi-Fi)が失敗する。
Luaの確保の失敗はエラーで済むが、C++の`new`の失敗はabortで本体ごと落ちる(再起動の候補として一番濃い。クラッシュダンプは未確認)。
- `MemFunctions::HeapTopRoom()`(`Mem_Functions.hpp`、inline): ヒープの末尾`sbrk(0)`から`__HeapLimit`(arduino-picoのリンカスクリプト。
  SRAMの終わり=0x20080000。弱いシンボル)までの未使用の量。O(1)。PC/ホストは`kRoomUnknown`で、テストは`heap_room_hook`で偽る。
  `Mem_Functions`の`stack_headroom`(= `pico.memory_info().heap_headroom`)もこれで測るようにした(以前はスタックポインタまでで、
  実機のスタックはSCRATCHにあるので大きく見えていた)。
- `LuaEngine::Alloc()`: 増やす確保で、残りが`kHeapReserveBytes`(20KB)+確保量を割りそうなときだけ`malloc`+コピーで確保し、
  前後で末尾が伸びて残りが20KBを割っていたら返して断る(空きブロックから取れたなら通す)。断るとLua本体はごみを集めて1回だけ頼み直し
  (lmem.cの`tryagain`)、それでも足りなければ「not enough memory」のエラー(再起動はしない)。断った回数は`pico.memory_info().heap_refused`、
  残りは`heap_room`。ゾンビTDの5秒ごとの`[TD]`の行に`refused=`を足した。
- 検証: `lua_engine_test`(差し替え口で「末尾を伸ばさない確保は通す」「伸ばして割る確保は断りエラーで止まる」「余裕が戻れば続く」)、
  実機ファームのビルド(maxgerhardtのplatform-raspberrypiで`pio run`。`__HeapLimit`が0x20080000に解決される)。**実機での効果は未確認**。

### pico.* の関数の置き方(Luaのメモリを減らす、2026-10-09)

ゾンビTDにコントローラーの操作を足してLuaの予算が足りなくなったのを受けて、全部のLuaアプリに効く形で減らした(PCで起動直後に約23KB減)。
- **`pico.*`は上位値の無い素のCの関数**(`lua_pushcfunction`)。`this`は`lua_getextraspace(L)`に置き(`lua_newstate`の直後)、各ファイルの`Self(L)`がそこから読む。
  コルーチンへは`lua_newthread()`が写す。以前は`this`を上位値に持つCのクロージャ約320個で、クロージャだけでPCで約11KBだった。
  `pcall`の包み・`coroutine.wrap`等、自分で`lua_pushcclosure`しているものは上位値のまま(上位値1が`this`)。
- **`pico`/`pico.iso`のテーブルは空で始まり、`__index`(`l_api_index`)が初めて使われた関数だけをテーブルへ入れる**。関数の一覧は
  `registerFn()`が`api_pico_`/`api_iso_`(`std::vector<luaL_Reg>`。名前は文字列リテラルを指すだけ。Luaの予算の外、実機で約2.5KB)へ積み、
  `finishApiTable()`が名前順に並べて(同じ名前は後から足したもの=差し替えを残す)メタテーブルを付ける。**`pairs(pico)`では使ったことのある関数しか見えない**。
  `json_null`・`iso`のような関数でない値は今までどおりテーブルに直接入る。
- **先読み(`preloadModules`)は読み込み済み(`pico_loaded`)のモジュールを飛ばす**。以前は`Run()`のたびに`require`しているモジュールを全部コンパイルし直して
  `pico_preload`へ置き、使われないまま残っていた(アプリは`Run()`1回なので実害は小さいが、テストや`pico.require`を書いた`Run()`を重ねると積もった)。
- `pico.game`の`g:tilemap{rows=}`は1行ぶんの表を使い回す(行ごとに作っていて、100x40のマップで一時的に約80KBのごみが出てテストの予算160KBに当たった)。

### Lua APIの追加(2026-10-05): require・タッチ座標・画面の受け渡し・HTTP/JSON・タイマー

「ウィジェットを使うLuaアプリを書くときに足りない機能」を洗い出した(`pico.*`の不足リスト)うち、実害が出ていた5項目をまとめて入れた。
ドキュメントは`lua-api-doc/content/api/`(`modules.md`/`json.md`/`timers.md`/`scenes.md`/`network.md`)、動作確認アプリは`pc/sdcard/lua/apps/ウィジェットAPI確認/`。
**実機では未確認**(PCのホストテストとPCビルドのみ)。

1. **`require(name)` / `pico.require(name)`**(`LuaEngine::l_require`、`preloadModules`)
   - アプリのフォルダ(`app_dir_`)の`<名前>.lua`→`<名前>/init.lua`を読む。`.`はフォルダ区切り、`..`や`/`は不可、`sd_outside_app_dir`があっても外は探さない。
     1ファイル32KiBまで(`main.lua`は`LuaScene::kMaxScriptBytes`=16KiBのまま)。結果は`registry.pico_loaded`に覚え、2回目は同じ値。循環はエラー。
   - **実行中にコンパイルしない**のが肝(`LuaScene.hpp`のコメント: 実機のコア0スタックは4KiBで、Luaの実行中にパーサーを重ねて溢れた事故がある)。
     `Run()`がソースの`require("名前")`/`require "名前"`/`require('名前')`を**文字列として拾い**(`ScanRequires`)、本体より前に`PreloadTrampoline`(`ProtectedCall`越し)で
     読み込んで`registry.pico_preload[名前]`に関数(構文エラーなら文字列)で置く。読んだモジュールの`require`も辿る(最大16個)。実行時の`require`はその関数を呼ぶだけ。
     先読みで拾えない名前(変数で組み立てたもの)は、`lua_getstack(L, 6)`が無い=呼び出しが浅いときだけ実行中に読む。深ければ「関数の奥から呼ばれています」でエラー。
   - `package`ライブラリは引き続き無い。グローバル`require`は`pico.require`と**同じ関数**。サンドボックスのテストは「標準のrequireは無い」から「requireは自前のもの」へ書き換えた。
     モジュールの実行は`lua_pcall`で包むが、失敗は必ず再度`lua_error`で投げ直す(打ち切り=`aborting_`を握り潰さない)。
2. **タッチのイベント引数**: `press_start/move/end/out`は`fn(id, x, y, lx, ly, dx, dy)`(`Dispatch()`で組み立てる)。`x,y`=画面座標、`lx,ly`=`getScreenRect()`の左上からの座標、
   `dx,dy`=前のタッチのイベントからの移動量(`last_touch_x_/y_`。`press_start`は0)。`fn(id)`の書き方は従来どおり動く。`pico.get_touch()`は残してある。
3. **画面をまたぐ受け渡し**(`LuaScene`+`LuaEngine`)
   - `push_scene/change_scene(path, args)` → 子の`pico.args()`。`pico.pop(result)` → 親の`on_result(result)`。値はJSONにして**1KiB未満**(`EncodeSceneValue`、超過・JSON化不能は`luaL_error`)。
   - `LuaScene`は`launch_args`/`parent_script`/`saved_state`(2KiB)をメンバに持つ(`setLaunchArgs()`)。`LuaEngine`は`SetScriptPath()`/`SetSceneArgs()`で受ける。
     `change_scene`は親を引き継ぐ(置き換えた先の`pop(result)`は元の親へ届く)。
   - **結果の待ち箱**は`LuaScene::PostResult(target, json)`/`TakeResult(target, out)`(宛先=親のスクリプトパス、1件、`kResultTtlMs`=3秒で失効、別の画面宛ては取らない)。
     通知の起動理由(`TakeLaunchReason`)と同じ作り。親は`Pop`で戻って`onEnter()`からやり直すので、`setup()`の後に受け取る。
   - **状態の持ち越し**: `onExit()`で`engine->CallSuspend()`(グローバル`on_suspend()`が返したテーブルをJSON化して`saved_state`へ。エラーはダイアログを出さずログだけ)→
     次の`onEnter()`で`setup()` → `on_resume(state)` → `on_result(result)`の順に`CallWithJson()`で呼ぶ。`Push`で退避中もシーンのオブジェクトは残るのでメンバで持てる。
   - **`pico.store_load()`/`store_save(tbl)`**: `<app_dir>/store.json`(JSON、16KiBまで、一時ファイル→差し替え)。アプリを閉じても残る。`.gitignore`済み。
4. **HTTPとJSON**
   - `http_request(..., callback, opts)`: `opts.headers`(`HttpRequest::addExtraHeader()`で足す。`Host`/`Content-Length`/`Content-Type`/`Connection`/`Transfer-Encoding`等は不可、
     CR/LF・制御文字は不可、合計480B)、`opts.save_to`(`<path>.part`へ直接書いて最後まで受け取れたら差し替え。上限8MiB、`SdWriteAllowed`を通る=`app.cfg`や`app_dir`の外は不可。
     保存中は`setIdleTimeout(true)`で「10秒無通信」まで続け、`UpdateHttp()`が1フレームに最大16回・6msまで`update()`を回す)。
     `callback(ok, status, body, err, headers, info)`: `headers`は小文字キー(`content-type`/`content-length`/`etag`/`last-modified`/`location`。`HttpResponse::contentType()`を足した)、
     `info={size=, saved=}`。
   - **同時に走るのは1本のまま**(RAMとTLSの約40KBのため。本物の並列にはしていない)。2本目以降は`PendingHttp`(本文・ヘッダを`std::string`で持つ)として最大`kMaxHttpQueue`=4本待たせ、
     終わるたび`StartQueuedHttp()`が先頭から始める。戻り値は`true`ではなくリクエストID(整数)に**変えた**(旧: 進行中に呼ぶと`false`、今: 順番待ち)。`http_cancel(id)`/`http_cancel()`(全部)。
     始められなかった待ちは失敗としてコールバックへ知らせる。コールバックの中から次の`http_request`を呼んでも待ちの後ろに並ぶ。
   - **`pico.json_decode(text [, keep_null])` / `pico.json_encode(v)` / `pico.json_null`**(`src/lua/LuaJson.hpp`、ヘッダのみ=ホストテストのビルド一覧を増やさないため)。
     Decodeは再帰下降でLuaの値を直接作る(深さ16・入力64KiB、整数は整数、`null`は既定でnil)。Encodeは確保しない関数だけで読み`std::string`へ出す(longjmpしない)。
     空テーブルは`[]`、穴のある配列は`null`で埋める、まばらな整数キー/文字列以外のキー/NaN/循環は失敗(`nil, 理由`)。
     C++側の`Json_Reader`(SAX、値256B・キー48Bの上限)は使わなかった(汎用の変換には小さすぎる)。
5. **タイマー**: `pico.after(ms, fn)`/`pico.every(ms, fn)`/`pico.cancel(handle)`。固定16個(`kMaxTimers`)、ハンドルは`(世代<<8 | 添字+1)`で、解放済みハンドルが別のタイマーを巻き込まない。
   `LuaScene::onUpdate()`が毎フレーム`UpdateTimers(dt)`(`setup()/loop()`の有無に関わらず)。`every`は遅れても溜めずに1回(次は「今からms後」)。
   コールバックのエラーはそのタイマーを止める。別の画面へ`push_scene`している間は動かない(シーンごと`LuaEngine`が作り直されるため)。

- 検証: `lua_engine_test`(JSON・タイマー・タッチ引数・受け渡し・`require`・**127.0.0.1に立てた小さなサーバ(スレッド)相手の実HTTP**: ヘッダ送信・応答ヘッダ・順番待ち・POST・`save_to`で40000バイト・取り消し・権限)、
  `lua_scene_test`(実際のシーン遷移で`args`→`pop(result)`→`on_result`、`on_suspend`→`on_resume`)、`lua_sandbox_test`(`require`の扱いを更新)。
  `lua_engine_test`は`-pthread`でビルドする。環境変数`LUA_TEST_VERBOSE=1`でテストのログ(Luaのエラー等)が標準エラーへ出る。PCビルドでは「ウィジェットAPI確認」アプリで
  盤面のタップ→マス目、タイマー、`push_scene`→`pop(result)`、`store_save`を`--tap`/`--shot`で確認した。
- **既知の限界/未確認**: 実機でのスタック(`ScanRequires`と先読みのコンパイルが想定どおり浅いか)・`save_to`の速度(SDとTLS)・`http_request`の戻り値の変更で`== true`と比べていた既存アプリが無いこと
  (リポジトリ内のLuaアプリは`http_request`を使っていない)。OOM時に`PendingHttp`/`std::string`が漏れうる(`luaL_error`のlongjmpはデストラクタを飛ばす。アプリを閉じる状況なので許容)。
- 洗い出したうえで見送った項目は、下の「Lua APIの追加(2026-10-05 その2)」で入れた。

### Lua APIの追加(2026-10-05 その2): 見送ったウィジェット・○項目・暗号化

前節の洗い出しで「見送った」としたものと、洗い出しの○(あると明らかに楽)の項目を入れた。ドキュメントは `lua-api-doc/content/`
(`api/crypto.md` `api/stdlib.md` を新設、`api/widgets` `dialogs` `drawing` `images` `misc` `scenes` `canvas` と `reference/*` `guide/events.md` を更新)、
動作確認アプリは `pc/sdcard/lua/apps/ウィジェット追加確認/`。**実機では未確認**(PCのホストテストとPCビルドのみ)。

**ソースの置き場**: `LuaEngine.cpp` が4400行を超えたので、追加分は `src/lua/LuaEngine_Ext.cpp`(ウィジェット補助・イベント・ジェスチャー・リスト/タブ・ダイアログ・
描画の補助・ユーティリティ)と `LuaEngine_Crypto.cpp`(暗号API)に置いた。`LuaEngine` の private へは `friend struct LuaEngineExt` / `LuaEngineCrypto` 経由で触る。
登録は `registerApi()` の末尾の `RegisterExtApi()`(同名は後勝ちなので `list_add` は差し替え)。**ホストテストのリンクの一覧にこの2本と `util/Secret_Aead.cpp` と
`lib/monocypher`(`lua_obj/monocypher.o`)を足した**(`LuaEngine.cpp` をリンクする4つの塊すべて)。

1. **Luaから作れるウィジェットを7種足した**(合計27種): `ProgressBar`(新設、タップ素通り)・`TextView`・`ImageView`・`MarkdownView`・`AnalogClock`・`DurationPicker`・`MonthGrid`。
   `WidgetFactory`/`WidgetProperty` の表に足した(新しい `Id`: `ScrollX/Y` `MaxScrollX/Y` `ImageW/H` `RowCount` `Hour/Minute/Second` `HandColor` `SecondHandColor` `TotalMs`
   `Editable` `Year/Month/Today/Selected` `HitTransparent` `Enabled`)。足りなかった口を元のウィジェットへ足した: `TextView::setOwnedText()`(Lua用に文書を自分で持つ。16KiB・確保は更新のときだけ)・
   `setW()`/`setScrollY()`、`MonthGrid::setW/H`、`ImageView::setSize()`(窓を取り直す)、`MarkdownView::loadText()`/`setSize()`(組み直す)。
   - 255バイトを超える文章は `pico.set(id,"text")` に入らない(`Value` の文字列が255B)ので、`pico.text_set(id, text)` を用意した(TextView 16KiB / MarkdownView 8KiB)。
   - **`MarkdownView` は約40KB**使うので1アプリに1つまで(生成を数えて断ってはいない。ドキュメントで注意)。
   - **`Image`/`ImageView`/`MarkdownView` の `path` は `sd_outside_app_dir` の権限で縛った**(`l_set`)。これまで `Image` の `path` が権限を迂回できる穴だった。
2. **新しいイベント**: `duration_changed`(DurationPicker)・`day_selected`(MonthGrid)・`link_tap`(MarkdownView)・`text_tap`(TextView)・`text_input`(Textbox。入力中も `text` が最新)・`scrolled`(ScrollContainer)。
   引数付きのイベントは `BeginDispatch(id, kind)` → 値を積む → `EndDispatch(n)` の口で呼ぶ。`pico.off(id, event)` で解除。
   **ジェスチャー**(`long_press` / `double_tap` / `swipe`)はウィジェットへ配線せず、`LuaScene::onUpdate()` が毎フレーム呼ぶ `LuaEngine::UpdateGestures()` が
   `OSData` のタッチの状態から判定する(`press_*` が同じウィジェットで使われていても干渉しない。登録が1つも無ければ何もしない)。しきい値: 長押し500ms・
   タップ400ms以内・ダブルタップは400ms/24px・スワイプは24px以上を700ms以内。発火先は押し始めの位置を矩形に含む、表示中で有効なウィジェットだけ。
   ホストテストのために `script/host_test/stubs/Arduino.h` の `millis()` を `PicoHostClock::now` で進められるようにした(既定は0のまま)。
3. **ウィジェット操作**: `enabled`(`Widget` 基底に追加。`WidgetFunctions::UpdateAll()` が無効のウィジェットへのタップを受け止めて何も起こさない。`Button` は文字が灰色)・`hit_transparent`・
   `pico.set_name/find/parent/children/get_rect`・`bring_to_front/send_to_back`(`WidgetFunctions::BringToFrontTree/SendToBackTree`。部分木ごと動かす。コンテナの子には使えない)・
   `scroll_to`・`show_keyboard/hide_keyboard`(`causeOnPressStart()` を呼ぶだけ)。
   リストは `list_insert/remove/get/select/scroll_to` と `list_add` のオプション(`icon`/`color`)(`ScrollList` に `insertAt/removeAt/scrollToIndex`、`DropdownMenu` に `removeAt`)。**インデックスは0始まり**(`selected_index` と同じ)。
   タブは `kMaxTabs` を4→8にして `tab_label/set_label/remove/clear`、**`pico.tab_link(tab, index, widget)`**(タブが選ばれている間だけ表示。`TabBar::setOnChanged` を `OnTabChanged()` に差し、`pico.on(tab,"tab_changed")` の有無に関わらず動く)。
4. **ダイアログ**: `closed` の3番目の引数に結果(`DispatchClosed()` が閉じる前に読む)。新設の `PickerDialog`(`dialogs/PickerDialog.hpp/.cpp`。Choice/Date/Time/Number/Progress の5モードを1クラスで)を
   `pico.show_choice/show_date/show_time/show_number/show_progress` で出す。Choiceは1タップで選んで閉じる。Dateは `MonthGrid`+`[<][>]`。Timeは `DurationPicker`。Numberは `NumberInput`。
   Progressは自動では閉じず、`pico.set(id,"value",0〜100)` で進めて `pico.destroy` で閉じる。`WidgetType::PickerDialog` を足した(Countは64未満のまま)。
5. **描画**: `draw_text` の第6引数 `align`・`draw_text_wrapped`・`measure_text`(`WrapLineLength` が空白と文字で折る)・`get_pixel`・`canvas_get_pixel`。
   **オフスクリーン画像**: `pico.image_create(w,h[,transparent])` / `image_target(handle|nil)` / `image_clear`。`image_target` は `OSData::frame` を画像のスプライトへ差し替え、
   `LuaOffscreen::active` の間は `pico.draw_*` が画面のdirtyを積まない(`LuaMarkDirty()`)。**`ProtectedCall()` の一番外を抜けると必ず画面へ戻す**(`EndImageTarget()`。戻し忘れで他の描画が吸い込まれない)。
   画像スロットは4→8枚・64→96KiBに増やした。
6. **ユーティリティ**: `app_dir/path_join/time/wifi_status/url_encode/url_decode/base64_encode/base64_decode/settings_get/set/all/memory_info/toast`、`pico.on_back(fn)`/`pico.go_back()`
   (`LuaScene::onKey()` がEscを、`onUpdate()` がコントローラーのHOMEを「戻る」として渡す。登録が無ければ何もしない)。`util/Base64.hpp` を新設。
   `settings_*` はアプリのフォルダの `settings.cfg`(`PICO_Config`)。`toast` は `NotificationFunctions::Post()` の音なし(履歴にも残る。300msの間隔を空ける)。
7. **OS同梱のLuaモジュール**(`src/lua/LuaBuiltinModules.hpp`。ソースはフラッシュに置くだけで、`require` されたときだけコンパイルする): `pico.ui`(宣言的なUIの組み立て)・
   `pico.async`(`async.run/await/sleep/http/message/input/choice/date/time/number/...` でコルーチン連携)・`pico.tween`。`preloadModules()` が `pico.` で始まる名前を
   アプリのフォルダより先に同梱ソースから読む(実行中のコンパイルを避ける「先読み」の仕組みに乗せた)。**静的な `require("pico.ui")` の書き方だけ**先読みされる。
8. **暗号化**(`util/Secret_Aead.hpp/.cpp`、`LuaEngine_Crypto.cpp`): `pico.encrypt(plain[, password])` / `decrypt` / `is_encrypted` / `hash`(BLAKE2b-256・鍵付き可)/ `random_bytes`、
   `pico.store_save(tbl, {encrypt=, password=})` / `pico.store_load({password=})`。**XChaCha20-Poly1305(Monocypher)+パスワードならArgon2id(64KiB・3パス)**。形式は
   `"enc2:"`+base64url(`version(1)`[+`passes(1)`+`blocks(2)`+`salt(16)`]+`nonce(24)`+暗号文+`MAC(16)`)で、ヘッダもAADに入れて認証する。
   - **パスワード無し**は鍵が「`PICO_Secret::kKey`(ファームに焼かれた固定鍵)+アプリのフォルダ名」なので、守れるのは**SDだけを盗まれる場合と、別のアプリが復号すること**だけ
     (`Secret_Cipher.hpp` と同じ限界)。**パスワード付き**はファームを吸い出されても読めない(強いパスフレーズなら)。どちらも実行中のメモリを覗く相手と暗号文の巻き戻しは防げない。
   - 復号側は壊れた/悪意のあるヘッダで大量のメモリを使わないよう、Argon2のブロック数(8〜256)とパス数(1〜10)に上限を設けてある。平文は12KiBまで。
   - Argon2は64KiBを `malloc` して使い終わったら消す(Luaの予算の外)。**実機での時間とRAMは未計測**(RP2350で数十ms〜の見込み)。
   - 乱数は `SecretAead::Random()`(実機は `rp2040.hwrand32()`、PCは `/dev/urandom`。`SshUtil::Random()` と同じ中身を、SSHに依存しないよう複製した)。
9. **開発体験**: `script/host_test/lua/pico_mock.lua`(Luaアプリのテスト用の `pico.*` の偽物。ウィジェット・イベント・タイマー・ダイアログ・HTTP・SD・保存・画面遷移を記録し、
   `mock.fire/advance/close_dialog/respond` で動かす。`pico_mock_test.lua` が自身のテスト)。`pico.memory_info()` で予算の残りが見える。

**検証**: `lua_ext_test`(新設のグループ `lua-ext`。新ウィジェットのプロパティ・イベント(MonthGrid/TextViewは実際のタップ)・`pico.off`・無効のウィジェットがタップを受けないこと(`UpdateAll()`)・
ジェスチャー(スワイプ/長押し/ダブルタップ/遅い動き/矩形の外)・スクロール・リスト/タブ/連動・5種のダイアログと `closed` の3番目の引数・オフスクリーン画像(画面のdirtyを積まないこと、抜けると戻ること)・
描画の補助・ユーティリティ・`on_back`・暗号(往復・改ざん・パスワード違い・別のアプリ・12KiB・BLAKE2bの既知の値・store)・`pico.ui`/`pico.async`/`pico.tween`)、`widget_factory_test`/`widget_property_test`、
`lua_engine_test`(タブ8個・画像8枚に更新)、`pico_mock_test`。ASan/UBSanで通る。

### 2Dゲームの簡易エンジン(`pico.game`、2026-10-06)

`require("pico.game")`。**本体はLuaの同梱モジュール**(`LuaBuiltinModules.hpp`の`kGame`)で、C++へ足したのは重い描画の2つだけ:
`pico.draw_tilemap(handle, tw, th, data, cols, x, y)`(`LuaEngine_Ext.cpp`。1バイト1マスの文字列を今のクリップにかかるマスだけ
`DrawPimgSprite()`で描く。`draw_image_part`と同じ「クリップを狭めて画像全体をずらしてpushSprite」)と、
`pico.draw_image_part`の8・9番目の引数`flip_x/flip_y`(反転はpushSpriteでできないので1画素ずつ。`draw_image_ex`と同じ書き方)。
ドキュメントは`lua-api-doc/content/api/game.md`、サンプルは`pc/sdcard/lua/apps/ジャンプアクション/`(絵は`script/generate_platformer_sheet.py`)。

- **構成**: `game.new{...}`がCanvasを1枚作り、`pico.every(1, ...)`で毎フレーム`g:step()`(`manual=true`なら自分で呼ぶ)。
  1回のdtは0.05秒で頭打ち(遅いフレームで壁をすり抜けないため)。順番: 入力 → ゲーム内タイマー → 状態の`update` → `g:on_update` →
  各スプライト(アニメ → `on_update` → 重力と移動)→ `g:collide`の規則 → カメラ → 描き直す範囲。
- **タイルとの当たり**: 軸ごとに動かし、動いた向きに**新しく入ったマスだけ**を近い順に見て押し戻す(すり抜けない・壁の中から始めても
  反対側へ飛ばない)。結果は`on_ground`/`hit_ceiling`/`hit_wall`(そのフレームだけ。`on_update`では前のフレームの値が読める)。
  坂は無い(すり抜け床は下の「軽い物理」)。スプライト同士はAABB(`hitbox`)の総当たり(`g:collide(tagA, tagB, fn)`)。
  **押し戻しの位置は「タイルの端 - 箱のずれ」で決める**(差し引きで直すと小数の誤差が溜まり、床の上でyが56.0000…01になって`==`が外れた)。
- **描き直し**: カメラが動いた/状態が変わった/重なり順が変わった → `pico.invalidate`。それ以外は見た目(位置・コマ・反転・色・表示)の
  変わったスプライトの前後の矩形だけを`pico.mark_dirty`(24個を超えたら全体)。`render`は`pico.get_draw_area()`の範囲にかかるものだけ描く
  (FlushDirty()はdirty矩形ごとに`render`を呼ぶため)。HUDは`g:on_draw`で描き、中身を変えたら`g:dirty()`で知らせる決まり。
- **入力**: `g:down/pressed/released(name)`はコントローラー(`pico.pad_*`)と画面ボタン(`g:button`、`pad=true`で下56pxに← ↑ ↓ → B A)をOR。
  画面ボタンは`press_start`で`latch`も立てて1フレームより短いタップを取りこぼさない(テトリスと同じ)。押したまま滑らせると隣へ移る。
- **同梱モジュールのデバッグ情報を落とすようにした**(`LuaEngine::preloadModules`→`StripFunction()`。`lua_dump(strip=1)`して`"b"`で読み直す。
  自分でdumpしたものなので安全)。pico.gameのrequireは約61KB→約47KB(PCの64bit。`lua_ext_test`が表示する。軽い物理を足して約55KB)。
  代わりに同梱モジュールの中で起きたエラーは行番号が「?」になる(`error(msg, 2)`の引数の誤りはアプリの行を指す)。
  pico.ui/async/tweenも同じく小さくなった。**実機(32bit)での値は未計測**(ポインタが半分なので少し小さいはず)。
- 検証: `lua_ext_test`(反転の画素・`draw_tilemap`の引数とクリップの復元・rows/legend/spawn・重力と床・壁・すり抜け・天井・hitbox・
  collideとremove・アニメ・状態・ゲーム内タイマーと一時停止・画面ボタン(押す/滑らせる/離す/短いタップ)・タイルの書き換え・
  描き直す矩形が動いたスプライトの前後だけ・カメラの追従と端・bounded・renderのon_draw/draw)。PCビルドの`--tap`でサンプルの
  タイトル・歩く・スクロール・ジャンプ・ミスを`--shot`で確認。**実機では未確認**(Luaで毎フレーム回す量・反転描画・タイル描画の速さ)。
- **軽い物理(2026-10-06)**: 全部Lua(C++は触っていない)。スプライトに`ax/ay`・`drag`(1/秒)・`friction`(床に乗っている間だけ、
  前のフレームの`on_ground`で判定)・`max_vx`・`bounce`/`bounce_min`(壁で止まる所は全部`stop_v()`を通る)・`mass`・`static`・`drop_through`、
  `s:impulse()`、`game.new{gravity=, iterations=}`(世界の重力は`gravity`未指定かつ`static`でないものだけ)。タイルマップの`oneway`
  (下へ動いて、動く前の足元がマスの上端以上だったときだけ乗る)。**`g:solid(tagA, tagB, fn|{oneway, on_hit})`がスプライト同士を押し合う**
  (`_physics()`、移動の後・`collide`の前): 動く前の位置(`_px/_py`、step()が各スプライトの更新前に覚える)から入ってきた軸を決め、
  押し戻しは逆質量の比。**押される向きが塞がっている側(hit_wall/on_ground/hit_ceiling)は動かないものとして扱う**(これが無いと
  「押す→箱→壁」が2周では収束せず、押している側が箱にめり込んだ)。速度は近づく成分だけ撃力で交換(bounceの大きいほう)。
  上に乗ったら`_ride`を覚え、次のフレームの頭で足場の移動量だけ`move()`する(動く足場)。規則はタグ別の配列を作って総当たり、
  `iterations`(既定2)周。コールバックは1周目だけ。検証は`lua_ext_test`の「pico.game: 物理」(跳ね返り・摩擦・抵抗・impulse・
  oneway・押す/壁/積み重ね・動く足場・速度の分け合い/入れ替わり/質量比・世界の重力)。**実機では未確認**(総当たりの命令数)。
  **上がる足場に乗ったものは`_ride`で運んだ後、足が足場の上面から0.5px以内なら`on_ground`/`_ride`を立て直す**
  (運ぶと重なりが生まれず、1フレームおきに`on_ground`が外れてジャンプが効かなくなった)。
- **サンプル「おしてのぼれ」(`pc/sdcard/lua/apps/おしてのぼれ/`、2026-10-07)**: 物理を使うパズルアクション3ステージ
  (`stages.lua`。1=木箱を押して踏み台・横の足場・すり抜け床、2=重い鉄箱(mass 4)でスイッチを押さえて扉を開ける・ばね、
  3=縦の足場で棚へ上がり、棚の箱を落として2段に積む)。絵は`script/generate_physics_sheet.py`(地面・主人公・旗は
  `generate_platformer_sheet.py`から借りる)。スイッチは押さえている間だけ扉が開き、扉のマスに何かが重なっていれば閉じない。
  ばねは`g:solid(t, "spring", fn)`で`ny==-1`のとき打ち上げる(Aを離して低く跳ぶ処理は自分のジャンプのときだけ。
  ばねの勢いまで切っていた)。押す速さは箱の`friction`で決まる(木箱400で約55px/秒、鉄箱150で約30px/秒)。
  足場は端で0.8秒止まる(止まらないと乗り込めなかった)。検証は`oshite_test`(run.sh。`lua_script_test`で本物のpico.game
  (`LuaBuiltinModules.hpp`の`kGame`を読む)と偽物のpico.*を使い、操作を流し込んで3ステージを実際に解く。箱が無いと/1つでは
  壁に届かないことも見る)とPCビルドの`--tap`/`--shot`。**実機では未確認**。
- 未: 坂・スプライトの回転/拡大(`draw_image_ex`を`s.draw`で使えばできる)・パーティクル・効果音の補助・
  タッチパネルでの同時押し(XPT2046が1点しか取れない)。

### ボードゲーム4本の`pico.game`への移行(リバーシ・マインスイーパー・テトリス・ブロック崩し、2026-10-07)

4本とも、ウィジェット(`Button`/`Label`/`Canvas`/`Rect`/`Ellipse`…)を自分で作る書き方をやめ、`game.new`が作る`Canvas`1枚の中で
**画面ボタン(`g:button`)・状態(`g:state`)・タイルマップ・スプライト**だけで組み立て直した。ゲームの規則(盤面の判定・SRS・速さの段・ステージ定義等)は
そのまま。**盤面はどれもタイルマップ**で、「1マス=1タイル、石/旗/ブロック/ミノの違いはタイルの値」にしたので、変えたいマスを`map:set()`するだけで
エンジンがそのマスだけ描き直す(マインスイーパーの0マスの連鎖も、ブロック崩しのブロックを壊すのも同じ)。絵は`script/generate_*.py`が
パレット番号で直接描く(PIL不要、`generate_pimg.py`と同じ.pimg形式)。

| アプリ | 盤面 | 動くもの | 画面(`g:state`) | 画像 |
|---|---|---|---|---|
| リバーシ | タイルマップ8x8(24px。1=空 2=黒 3=白 4=打てる印) | なし | play / pass / over | `tiles.pimg`(96x24) |
| マインスイーパー | タイルマップ(難易度ごとに20/16/14px+隙間。1=閉じ 2=空き 3〜10=数字1〜8 11=旗 12=地雷) | なし | menu / play / done | `tiles20/16/14.pimg` |
| テトリス | タイルマップ10x20(12px) | ミノ・ゴースト=スプライト4枚ずつ | title / play / pause / clear / over | `blocks.pimg`(120x12) |
| ブロック崩し | タイルマップ8列(29x14px。1〜6=速さの段 7=壊せない灰) | ボール・パドル・アイテム=スプライト | ready / play / clear / over / win | `tiles.pimg`(203x14) |

- **エンジンへ足したもの(`LuaBuiltinModules.hpp`の`kGame`)**: 画面ボタンの`visible = false`(描かず、**タッチも受けない**。画面ごとに見せるボタンを入れ替えるのに。
  それまで`button_at()`は`visible`を見ていなかった)と、ボタンの`draw = function(b, x, y, w, h, on)`(自前の絵。テトリスの操作ボタン・ブロック崩しの「自動」)。
  `pico.game`のドキュメント(`lua-api-doc/content/api/game.md`)と、4本の解説(`examples/tetris.md`・`examples/board-games.md`)を更新した。
- **ダイアログをやめて`g:state`の枠にした**: リバーシ(パス・結果)とブロック崩し(ステージクリア・ゲームオーバー・全クリア)は`pico.show_message`だった。
  今は盤面の真ん中へ`draw`で枠を出し、タップ/A/STARTで進む(パスは1.8秒でも進む)。コントローラーでも同じ操作になり、`closed`の自前処理(`pico.destroy`して自分でfnを呼ぶ)が要らなくなった。
- **マインスイーパーの難易度**は`TabBar`から、最初の`menu`の状態(大きな3ボタン)と、上の「難度」ボタンで`menu`へ戻る形に変えた。選び直すたびに`g:clear()`してタイルマップを作り直す
  (マスの大きさが違うため。画像は難易度ごとに1枚。`digits/flag/mine.pimg`は`script/generate_minesweeper_tiles.py`に文字で埋め込んで消した)。
  上級が300px(コンテンツ領域)にちょうど収まるよう、見出しの高さは58px。
- **ブロック崩しのボール**の位置と速さは自前(`cx` `cy` `dx` `dy`。**`vx`/`vy`にするとエンジンが勝手に1回で動かす**)。1フレームを6px以下に刻んで壁・ブロック・パドルへ当てる
  (エンジンの1回の移動だとブロックをすり抜ける)。ブロックとの当たりは外接矩形がかかるマスだけ`map:get(c, r)`で引く(以前は全ブロックを回していた)。
  アイテムは`vy`だけ持たせてエンジンが落とし、パドルとの重なりは`g:collide("paddle", "item", fn)`。ステージの切り替えは`g:clear()`で全部消して作り直す。
  「自動」(バッテリー計測用の放置プレイ)は残してある。`lib.lua`の`itemShapes`/`makeAutoToggle`/`autoControl`は不要になり消した(`autoTargetX`だけ残る)。
- **罠(ブロック崩しで踏んだ)**: **スプライトの`draw`は`w`x`h`の外へはみ出させない**。ボールを`pico.fill_circle(x+4, y+4, 4, 0)`(直径9画素)で描きながらスプライトを8x8にしたら、
  はみ出した1画素が消去の範囲(スプライトの矩形)に入らず、毎フレーム残って軌跡になった(`PICOOS_VERIFY_LCD=1`は「液晶=frame」を比べるだけなので、frameの時点で
  残っているこの種の汚れは検出できない。`--shot`で動かして目で見ること)。`fill_circle(x, y, r)`は直径`2r+1`画素なので、枠を9x9(ボール)・11x11(アイテム)にした。
- **`g.touch`の座標はキャンバスの中**(`x`/`y`)で、画面ボタンの上のタッチは含まない(ボタンは別に`g:pressed(名前)`)。キャンバスをステータスバーの下の全面
  (`pico.content_rect()`)にしたので、リバーシ等の座標はステータスバーの高さ(20px)を引いた値で書いてある。盤面の外のタップは各アプリが範囲を見て捨てる。
- **HUD(点数・状態の行)は`g:on_draw`で描き**、中身が変わったときだけ`g:dirty(x, y, w, h)`。キャンバスは背景色で塗ってから描き直すので、前の文字は自動で消える。
- 検証: **`reversi_test`/`minesweeper_test`/`breakout_test`を新設し、`tetris_test`を書き直した**(どれも`run.sh`。`pico.game`は本物、`pico.*`だけ偽物。
  `main.lua`の末尾へ`TEST`を足して中身を覗く。乱数は`math.random`を差し替えて地雷やアイテムを決め打つ)。
  リバーシ=打つ・裏返す・パス(タップ/時間)・終局・リセット中のパス、マインスイーパー=最初の1手は安全・連鎖・旗・地雷・3難易度・難度ボタン、
  ブロック崩し=壁/ブロック/パドルの反射・段ごとの速さ・点・アイテム3種・残機・ステージクリア・全20ステージ・全クリア・自動プレイ。
  PCビルドの`--tap`/`--shot`で4本とも動かして見た目を確認した(`PICOOS_VERIFY_LCD=1`で食い違い0)。**実機では未確認**(Luaで毎フレーム回す量・タイルマップの描画の速さ)。

### ブロック(2.5Dマインクラフト風、`pc/sdcard/lua/apps/ブロック/` + C++のエンジン `src/iso/`、2026-10-07)

[TheScienceElf/Blocks-TI-84](https://github.com/TheScienceElf/Blocks-TI-84)(TI-84 CE用、MIT)をLuaアプリへ移したもの。
元は 48x16x48 の世界だが、**チャンク読み込みで 1024x16x1024 にした**(下記)。25種類のブロック(水・松明を含む)・太陽の影・松明の光と昼/夜(下記「松明の光」)・半透明の水・自然/平ら/デモの3種の生成・5つのセーブ枠・
カーソルを9方向+上下に動かして置く/壊す(数字キーの配置も同じ)。元の絵(`TextureMap.png`/`player.png`)とライセンスは
`script/blocks_assets/`。

**2.5Dのエンジン(ワールド・生成・保存・描画・影・引き当て)は C++ にある(2026-10-07に Lua の `view.lua`/`world.lua`/`demo.lua`/`migrate.lua` から移した)**:
`src/iso/Iso_World.hpp/.cpp`(エンジン本体。LovyanGFXを知らず、描き先は関数ポインタの `Sink`)・`src/iso/Iso_Blit.hpp`(面の絵の写し方)・
`src/lua/LuaEngine_Iso.cpp`(Luaの `pico.iso.*`。ドキュメントは `lua-api-doc/content/api/iso.md`)。アプリに残るのは
`main.lua`(`require("game")` だけ)/ `game.lua`(画面の流れ・操作)/ `palette.lua`・`faces.pimg`・`icon.pimg`(`script/generate_blocks_sheet.py` が作る)。

- **移した理由と結果**: Luaでは描画の処理だけでPCで約1.4ms/画面(絵を写す呼び出しを除く)、Luaのメモリがゲーム中に約171〜180KB/200KB
  (モジュールの関数だけで約150KB・チャンクの文字列で最大約38KB)で余裕が無かった。C++では描画の処理が約0.026ms(約55倍)、
  絵を写すところまで入れて約0.07ms(PC)。Luaのメモリは約97KB(チャンクは C++ の置き場 約61KB に移った)。**実機の値は未計測**。
- **ワールド(`Iso::World`)**: 8x8 の柱(高さ16)を1チャンク(1024バイト、(lx,y,lz) は `y*64+lx*8+lz`)とし、置き場
  (最大`kMaxChunks=56`個、1個 約1.35KB)に読み込む。**置き場は読み込むときに1個ずつ`malloc`し、閉じるまで返さない**
  (`slots_[]`。2026-10-09: 以前は56個ぶん約75KBを1回で取っていて、実機のゾンビTDで画像を読んだ後に連続した空きが無く
  「マップを作れません: メモリが足りません」になった)。`setKeepAll(true)`は要る数(K*K)をその場で確保し、足りなければfalse。
  それでも実機では空きの合計が足りなかった(16個で止まり、ヒープの空き約39KB)ので、**`iso.set_image(handle, rows)`で
  使うブロックの段だけを並べた画像を使えるようにし**(`IsoState::rows`/`mapY()`。Worldは知らず、Luaの描き先が y を読み替える)、
  ゾンビTDの`faces.pimg`を13段(224x299、約34KB。前は約67KB)にした(`generate_zombie_td_sheet.py`の`FACE_ROWS`と`game.lua`の`FACE_ROWS`を揃えること)。
  さらに**ARENAのワールドは松明の明るさを持たず、チャンクを柱ごとの3バイトで持つ**(`World::lighting()`/`compactChunks()`、
  `Chunk::no_light`/`compact`)。柱ごとに「地面の高さ h・地面の一番上のブロック・その上(h+1)のブロック」だけを持ち、
  `CompactGet()`が 岩盤/石/土/水/空気 を組み立てる(地形は種から決まり、ゾンビTDが書き換えるのは地面の上の柵だけなので)。
  1チャンク約200バイト、明るさの作業場所(約6KB)も取らない。**書き換えられるのは地面の上の1段と地面の一番上の種類だけ**で、
  それ以外の`set()`は警告を1回出して無視する。松明は置けるが光らない。`get()`と描画は`BlockOf()`を通る。
  作る・読む・書き出すときは`gen_buf_`(1KB)へ一度広げてから`Compress()`する(`iso_td_test`で作った地形と1つも違わないことを確かめる)。
  ゾンビTDの置き場は約82KB → 約13KB(実機のログ: 画像を小さくして明るさを外した後でも39個で止まり、空きは約48KBだった)。チャンクを引くのは
  `(cx&31, cz&31)` の直写しの表(`map_`、1KB)+座標の照合。柱ごとの高さ(`col[64]`)を持ち、描画は柱の一番上までしか回さない。
  - **読み込む範囲**: 視点(`iso.origin`)/表示範囲が変わると、次の `pump()` が表示範囲の u=x-z / s=x+z の範囲に影をたどる分(左へ 2*(H-1))を
    足した範囲を決め直し、そこにかかるチャンクを近い順に予定に入れ、範囲より1チャンクより外は手放す(書き換えたものは書き出す)。
    Lua版と同じ規則で、240x204 の表示を動き回って最大49個(テストで確認)。置き場が足りなければ範囲の外で一番遠いものを手放し、
    それも無ければ読み込まない(空気に見える。ログを1回)。
  - **地形は種と位置だけで決まる**(整数のハッシュ。格子の高さ3〜12の補間・水面5・水辺の砂・鉱石・木はチャンク3つに1本で根元はチャンクの中の2〜5)。
    **Lua版と1バイトも違わない**(既存のセーブの書き換えていないチャンクは種から作り直すため。Lua版で作った1164チャンクと突き合わせ、
    テストにはそのハッシュを残した)。デモの家などの並びは C++ の表(`Demo()`)。
  - **ファイルの形式は Lua 版と同じ**(`worlds/<A〜E>/world.dat` = 見出し17バイト + K*K ビットの「書き出したチャンク」の印、
    `c_<cx>_<cz>.dat` = 上の空気だけの段を落としたチャンク)。ただし **K は128まで**(印を固定長 2KB で持つため。新しいワールドは128)。
    前の版の `worlds/world_X.dat`(48x48の1ファイル)は `iso.migrate()` が K=6 のワールドへ移して元を消す。
  - `create/open/migrate` のディレクトリは `pico.sd_write` と同じ権限の確認(`SdWriteAllowed(dir + "/world.dat")`)を通す。
- **描画(`World::render`)**: 画家のアルゴリズム(s=x+z の大きい順、同じ s の中は y の小さい順。同じ s の柱は横に32pxずつ離れて重ならない)。
  dirty矩形にかかる柱と高さだけを回す(Lua版と同じ範囲の計算)。水は市松模様の透過で半透明、**真上が水でない水(水面)は元の`WATER_HALF`と同じく2px低く見せる**
  (上面を2px下げ、横の面は上2行を抜いた絵=`faces.pimg`の水の段の x=144/160)。
  - **隠れたブロックを描かない**: (x-k, y+k, z-k) は画面のちょうど同じ六角形に重なり後から描かれるので、そこに**透けない**(絵に穴の無い)
    ブロックがあれば描かない。どの画素も、視線を手前からたどって最初に当たる透けないブロックのその面が必ず描かれるため、省いても画素は変わらない。
    **これが成り立つよう、透けないブロックの面は「隣が透けないブロックでなければ」描く**(Lua版は「隣が空気か水なら」で、葉に面した面を描かなかった。
    葉の絵には穴があり、穴から見える所が「たまたまその前に描いた絵」になっていた)。葉の面・水の面の規則は Lua 版のまま。
    透けないブロックは `iso.set_image` のときに絵から調べる(`ComputeOccluders`。今の絵では水・葉・松明以外)。乱数のワールド150個・Lua版の画面で、
    省いても画素が1つも変わらないことをテストで確かめた。表示全体で描く面は約2〜3割減る。
  - **面の写し方(`FaceBlitter`)**: 透けないブロックの面の絵は「上面のひし形(32x15)・左右の平行四辺形(16x23)」の形どおり(内側が全部不透明・外側が全部透過)なので、
    1行を1区間として透過の判定なしに写す(元と描き先の画素の偶奇が揃えば `memcpy`)。形どおりかは絵を読み込んだときに調べる(今の絵で22種×10枚)。
    葉・水・松明・カーソルは1画素ずつ。画面か画像が 4bpp でなければ(ホストテストのスタブ)`pico.draw_image_part` と同じ遅い道。
  - Lua版と同じ規則(葉も透けないとして省略を切る)にすると、描く面・影の絵・順番が Lua 版と全く同じになる(テストに Lua 版の並びのハッシュを残した)。
- **影は元と同じ形**: 日の当たりうる上面・左面を光から見た三角形2つに分け(上面は x+z 一定の線で奥/手前、左面は y=z の線で上/下)、
  三角形の中の1点から太陽(-1,+1,+1)へ向かう直線が通るマスを調べる(1歩4マス。`topShadow`/`leftShadow`)。右面はいつも影。
  読み込んだチャンクの空気でない一番上の高さより上はたどらない。水と松明は影を落とさない(`Casts()`)。絵は面ごとに 上(日なた/影/奥半分/手前半分)・
  左(日なた/影/上半分/下半分)・右(影/日なた。日なたは松明の光が当たったときだけ使う)の10枚を `faces.pimg`(224x598。段=ブロック番号-1、
  24段目=松明、25段目=カーソル)に並べる。置く/壊すときの描き直しは、太陽へ向かう直線がそのマスを通る面のブロック(k歩ごとに8個)を覆う矩形をkごとに積む(`dirtyEdit`)。
- **松明の光(2026-10-07)**: 松明(`TORCH`=25)は光源。2.5Dエンジンの中で、松明のマスを明るさ7(`kTorchLight`)とし、光を通すマス
  (`blocksLight()`=透けないブロック(`occluders_`)以外。空気・水・葉・松明)へ1マスごとに1ずつ減らして広げる(6マス先まで)。
  - **持ち方**: チャンクごとに1マス2bit(`Chunk::light[256]`、0〜3に丸めた値: 距離0〜2=3、3〜4=2、5〜6=1)と松明の数(`torches`)。
    置き場は 56個×約1.35KB + 作業場所 6.4KB = 約80KB(前は約61KB)。**明るさは保存しない**(セーブの形式は変えていない。ブロック番号25が増えただけ)。
  - **計算(`relight(cx, cz)`)**: そのチャンクのまわり6マス(20x20x16 の作業場所 `scratch_`)を「壁(0xFF)/松明(7)/0」で埋め、
    明るさ l=7..2 の順に「l のマスの隣を l-1 へ」を全体に1周ずつ掛ける(キュー無し、6周)。松明から6歩以内の道は全部作業場所の中に収まるので、
    チャンクの中は正しい。まわり3x3のチャンクに松明が1つも無ければ暗くするだけ(普通はこれで終わる)。読み込んでいないチャンクと世界の外は壁扱い。
    いつ計算するか: チャンクを読み込んだとき(自分とまわり8つ。`relightAround`)、`set()`で松明を置いた/取った・光を通すかが変わったとき
    (まわり6マスにかかるチャンク)、`setOccluders()`で透けないブロックが変わったとき(全部)。
  - **描画**: 面の明るさ q は、その面が向いている隣のマス(上面=真上、左面=-x、右面=-z)の明るさ。日の光で決まった絵(昼は影の判定どおり、
    夜は影の絵)に、q=3 なら日なたの絵そのもの、q=1・2 なら日なたの絵を `Sink::dither`(`FaceBlitter::dither`。q=2 は市松、q=1 は4画素に1つ。
    模様は面の絵の左上が基準=視点を動かしてもちらつかない)で重ねる(`DrawLit`)。右面の日なたの絵(x=208)はこのために足した。
    松明そのものは影も明るさも無くいつも同じ絵(`DrawIcon`)。松明は透けないブロックではないので後ろを隠さず、隣の面も描く。
    隠れたブロックを省いても画素が変わらないこと(乱数のワールド150個)は、松明と夜を混ぜても確かめてある。
  - **描き直し**: `set()`が明るさの変わったマスの範囲(`lightChanged()`)を覚え、次の`dirtyEdit()`がそこに面を向けたブロック(下・+x・+z)を覆う
    1枚の矩形を描き直す。チャンクの読み込みはアプリが表示全体を描き直すので覚えない。
  - **昼/夜**: `World::setSunlight(false)`(Luaの`iso.sunlight`)。アプリの「昼へ/夜へ」ボタン・`n`キー・ZL/ZR。夜の空は黒(パレットの0番)。保存しない。
  - 絵: `generate_blocks_sheet.py`の`TORCH_ART`(炎6x8 + 棒4x13、32x31の絵を上面/左面/右面の場所へ分ける)。色はパレットの近い色で、
    減色(k-means)には入れていない(パレット・既存の絵は変わらない)。
  - 選ぶ画面は7列(25種類が4段に収まる)。`l_set_image`は 224x598 以上を求める。
- **人や物(エンティティ、2026-10-08)**: 好きな画像を箱庭の中の小数の位置に立てて置ける(`Iso::Entity`、`World::ents_`。最初は32個の固定長配列。2026-10-08に96個へ広げ、置いた数に合わせて16個ずつ確保する形にした(下の「タワーディフェンス向けの道具」)。
  Luaは`pico.iso.entity_add(image, x, y, z, opts)`/`entity_set`/`entity_move`/`entity_get`/`entity_remove`/`entity_clear`/`entity_at`、
  `pico.iso.ground(x, z[, y])`/`to_screen(x, y, z)`)。(x, y, z) は足元の中心、点 (X,Y,Z) は画面の `(OX+16+16(X-Z), OY+32-8(X+Z)-16Y)`。
  - **前後**: エンティティは「足元を中心にした半径 r の正方形 × 高さ h」の箱を持つ(既定は絵の大きさから r=幅/64・h=高さ/16)。
    `render()`はブロックを全部描いた後(`renderBlocks`)、エンティティを奥から順(中心の x-y+z が大きい順)に描き、そのたびに
    **その箱より手前のブロックだけを、絵の不透明な画素の上にだけ描き直す**(`Masked`の描き先が `Sink::face_px`/`image_px`/`put` で1画素ずつ)。
    画家の順の途中へ挟んだのと同じ結果になる(描き直す絵は前と同じ画素を書くだけなので)。手前かは `World::InFront`: x・z・y のどれかで離れていれば
    見る人の側(-x・-z・+y)が手前、両方の向きで離れていれば中心の奥行き、重なっていれば絵が上(水だけは手前)。小数の誤差に 0.01 の余裕。
    **重なった水の上面は、エンティティの中心より画面で下の所だけ**手前にする(`renderBlocks`の`cut`。水面より上の体は隠れず、沈んだ所だけ水越し)。
    絵が箱の画面の形からはみ出す所(箱より幅の広い絵)は近似。カーソルはエンティティの後に描く。
  - **影**: 足元の四隅と中心の`ground()`(上面が足の裏以下の一番上のブロック。水も地面、水の中なら水の底)の一番高い所に、半径 r+0.08 の円を
    写した楕円(横 16√2・縦 8√2 倍)を市松模様で落とす。高く上がるほど小さく(8段で 35% まで)。その高さの地面の上面(上が空気/松明/水)の画素だけに描き、
    影ごとに地面より手前のブロックを影の画素の上へ描き直す(壁の裏の影は隠れる。水の底の影は水越し)。影は全部の絵より先に描く。
  - **描き直し**: `entityChanged()`が前に頼んだ矩形(`lx..`)と今の矩形(絵∪影)を`sink.dirty`で頼む(Lua側は`entity_move/set/add/remove`が自動で)。
    視点・表示範囲が変わったら矩形を覚え直すだけ(アプリが全体を描き直す)。`dirtyEdit()`は足元がそのマスにかかるエンティティも描き直す(影の高さが変わる)。
    `close()`でエンティティは片付く。
  - 夜でも絵はそのままの明るさ(松明の光・日の影は絵に掛からない)。
  - アプリの例: `mobs.lua`(村人3人と羊2匹が歩き回る。1段なら登る・崖は落ちる・水と壁で向きを変える・タップで跳ねる)。
    絵は `people.pimg`(`script/generate_blocks_people.py`。palette.lua の番号で直接描く。村人 12x22 の2コマ・羊 16x12)。
  - ついでに直したもの: `pico.iso.set_image` が大きさの足りない画像をエラーにする前に差し替えていた(エラーの後も小さい画像のままだった)。
  - **直した不具合(2026-10-08、Webビルドでの報告)**: ①チャンクを手放すと村人/羊が消えた — 読み込んでいない所は空気に見えるので、
    足元が無くなって落ち続けていた。`pico.iso.loaded(x, z)`(`World::loadedAt`)を足し、`mobs.lua`は足元の四隅を読み込んでいない間は
    動かさず落とさない(読み込み直すと続きから歩く)。②段の縁で上と下を高速に行き来した — 登る判定は足元の四隅、落ちる判定は中心の柱だけで
    見ていたので「登る→中心の下が低いので落ちる→また登る」を毎フレーム繰り返していた。落ちる判定も四隅の一番高い地面(`support()`)にした。
    エンジンの影も四隅を箱の角そのもの(以前は 0.9r)で見るよう揃えた。`blocks_test`に両方の再現(修正前は60フレームで60回上下した)を足した。
  - 検証: `iso_world_test`(セルに収まるエンティティを乱数のワールド120個で「奥のブロックだけのワールド → 絵 → 手前のブロックだけのワールド」と
    画素ごとに比べる・壁の裏/手前・1段の段差・セルをまたいでも奥のブロックに隠れない・水・影(高さ・壁・無し)・**頼まれた矩形だけ描き直しても
    全体を描き直したのと同じ(移動・跳ねる・反転・隠す・足す・取り除く・足元の置く/壊すを300回)**・引き当て・ハンドル・反転・前後の判定)、
    `lua_ext_test`(Lua APIと、壁の裏で隠れること)、`blocks_test`(村人と羊: 出る・歩く・壁から出ない・段を登る・タップで跳ねる・閉じると片付く)、
    PCビルドの`--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0)。**実機では未確認**(1画素ずつの描き直しの時間)。
- **経路探索(2026-10-08、`src/iso/Iso_Path.hpp/.cpp`、Luaの`pico.iso.path`/`pico.iso.stand`)**: 2点を結ぶ一番安い道をA*で探す。
  「立てる場所」は足元が空気・松明でないブロックで体のマス(`height`、既定2)が空気/松明(`swim`なら水も)の (x,y,z)。1歩は隣の柱へ
  (`diagonal`で斜めも。斜めは両脇に行けるときだけ)、登り`max_up`(1)・降り`max_down`(2)段まで、登る/降りるときは頭の上が空いていること。
  値段 = `step`(斜め×√2)+ `up_cost`×登った段 + `down_cost`×降りた段 + `block_cost[足元]` + `edge`コールバックの値(負/falseで通れない)。
  `avoid`(ブロックのビット)は足元と体のマスの両方で見る。見積もりは`step`×距離なので、どの1歩も`step`以上なら最短。
  1つの柱に複数の高さの点があり得る(橋の上と下)ので、点は(x,z,y)で持つ。作業場所(点・ハッシュ表・ヒープ、1点約26B)は探す間だけ`malloc`し、
  点の数は`max_nodes`(既定1024、上限4096)。**作業場所は256点から始めて足りなければ倍にする**(2026-10-09。以前は`max_nodes`ぶんを最初に確保していて、ゾンビTDの3200点で約86KB+結果の配列約19KBになり、実機で「pico.iso.path: メモリが足りません」になった。広げられなければ`Limit`/`partial`と同じ扱い。Luaの`pico.iso.path`は`FindPathAlloc()`で道の長さぶんだけ確保する)。**C++側の確保(経路探索・流れの場・人や物の置き場)に失敗したら`lua_gc(LUA_GCCOLLECT)`でLuaのごみを集めて1回やり直す**(`CollectForRetry()`。Luaは自分の確保の失敗でしかごみを集めないため。実機のゾンビTDで空き約17KBのときに失敗した)。それでもだめなら`pico.iso.path`は`nil, "memory"`(エラーにしない)。ゾンビTDの兵士はそのとき1秒探し直さず持ち場へまっすぐ歩き、流れの場の作り直しは2秒後にやり直す。読み込んでいないチャンクは空気に見えるので立てない。
  Luaの`edge`は`lua_pcall`で呼び、エラーなら作業場所を返してから投げ直す(`Aborted`)。結果の配列は一度Luaの文字列へ写してから`free`する(表を作る途中のOOMで漏れないように)。
  検証: `iso_world_test`(段差の上限・頭の上・避けるブロック・値段で道が変わる・斜めの角・edge・partial/上限/誤り・橋の上下・
  **乱数の高さの地形40個で素朴なダイクストラと値段が一致し、道が規則を守ること**)、`lua_ext_test`(Lua APIの引数・回り道・edgeのエラー)。
  **実機での時間は未計測**。アプリ「ブロック」ではまだ使っていない(村人を目的地へ歩かせる等は今後)。
- **タワーディフェンス向けの道具(2026-10-08、`ZOMBIE_TD.md`のゲームのため)**: 使い方は`lua-api-doc/content/api/iso.md`「タワーディフェンス向けの道具」。
  - **流れの場(`src/iso/Iso_Flow`、`pico.iso.flow_build/flow_step/flow_get/flow_info/flow_clear`)**: 目的地の柱(256個まで)から全部の柱へ
    ダイクストラで「値段」と「次の柱」を求める(大勢が1体ずつ経路探索しない)。規則は`PathRules`そのもの。登り/降りの向きは「歩く側から見た向き」
    (柱 m から目的地側の柱 c へ`CanStep`)で、値段は c へ入る値段(経路探索と同じなので、高さの地形なら`FindPath`と値段が一致することをテストで確かめた)。
    **1つの柱に立てる高さは1つだけ**(柱の一番上。橋の下は扱わない)。`step(budget)`で少しずつ作り(柱を調べる/確定するのが1単位)、**作っている間は
    前の結果を答え**、出来上がったら入れ替える(dist/next の2面持ち、立つ高さは出来上がったときに写す)。値段は1/8単位の16bit(斜めは1.375)。
    幅64まで。`begin()`の後に`addGoal()`で目的地を足す。
    **メモリ(2026-10-08に作り直した)**: 最初は1柱12バイトを1回の`malloc`(56x56で約38KB)で取っていて、**実機のゾンビTDで「道を作れません: メモリが足りません」**
    (チャンクの置き場約82KBの後に、その大きさの連続した空きが無かった)。今は結果が1柱3バイト(値段2+「向き4bit・立つ高さ4bit」を詰めた1。56x56で約9.4KB)だけを持ち、
    作っている間だけもう1組と出番待ちの列(値段と柱の4バイトの組。512件から`realloc`で広げ、値段を下げた柱はもう1件積んで古い件は取り出すときに飛ばす=位置の表は持たない)を足し、
    出来上がったら前の結果と列を返す。確保は配列ごとに分けた(一番大きいもので2×W×Wバイト)。途中で確保できなければ止まって`failed()`(前の結果は残る。Luaは`flow_info().failed`)。
    立てない柱の`standY()`は-1(届かない柱でも立てれば高さを返す)。`memoryBytes()`/`flow_info().bytes`で持っている量が分かる。
    `pico.memory_info()`に`heap_headroom`(ヒープの末尾とスタックの間の未使用。PCでは0)を足し、ゾンビTDは道を作る前と遊び始めにログへ出す。
  - **`PathRules`に`pass`と`body_cost`**: `pass`のブロックは体のマスにあってよい(中を通り抜ける)が足元にならない(バリケード)。`body_cost`は行き先の体のマスごとの値段。
    `CanStep`/`EnterCost`を`Iso_Path.hpp`へ出した(流れの場と共有)。
  - **人や物を96個へ**(ハンドルの下位8bitが番号+1なので255まで増やせる)。**置き場はヒープで、置いた数に合わせて`kEntityChunk`(16)個ずつ広げる**
    (`growEntities()`。閉じるまで縮めない。番号=ハンドルは広げても変わらないが、`Entity*`は広げたときに無効になる)。弾の置き場(64個、約4.6KB)も
    最初に撃ったときに確保する。**最初はどちらも`World`の中の固定長配列にしていて、`World`が約6KB→約15KBに増え、実機で「ブロック」のワールドを
    開けなくなった**(約82KBのチャンクの置き場を確保できなかった)。今の`World`は約4.5KB(前より小さい)。並べ替え・近い順・押し合いの作業場所も
    スタックに置かず、置き場と同じ数だけヒープに持つ(`escr_`。実機のコア0のスタックは4KiB)。Luaの`shots_step`は当たりを8個ずつ受け取り、
    `nearby`は32個まで、`flow_build`の目的地は流れの場の作業場所へ直接入れる(どれもスタックに大きな配列を置かないため)。`bar`(HPバー、0〜100)・`mark`(選択の三角)は`renderOverlays()`が
    **ブロックにも他の人や物にも隠れず一番上に**描き、`entityRect()`に入る。`tag`(0〜31)と`World::nearby()`(近い順)。
  - **押し合い(`World::crowdStep`、`pico.iso.crowd`)**: `crowd`(1=動く/2=動かない)の総当たりで、水平の円の重なりを`mass`の逆数の比で押し離す。
    押された先の体のマスにブロックがあればその軸は動かさない。高さ(y)は変えない(地面に合わせるのはアプリ)。
  - **弾(`Iso::Shot`、64個、`pico.iso.shot_add/shots_step/...`)**: 人や物ではない点。狙った人や物の中心(`y+h/2`)を追い、出発点からの割合`p`で進む
    (相手が動いても割合で詰めるので**必ず当たる**)。相手が消えたら最後に見た位置で`lost`。`arc`で山なり。描くのは人や物の後(隠れない)。
  - **視線(`World::lineOfSight`、`pico.iso.sight`)**: 3Dのマスたどり。空気・水・松明・`pass`は通す。出発点と到着点のマスは見ない。
  - **`World::setKeepAll`(`pico.iso.keep_all`)**: 読み込む範囲を世界全体にして手放さない(K*K ≦ kMaxChunks=56、つまり7x7まで)。
  - 描く順: ブロック → 人や物(影・絵・手前のブロックの描き直し)→ 弾 → HPバーと印 → カーソル。
  - 検証: `iso_td_test`(run.sh。流れの場の値段/向き/隙間/pass/届かない/少しずつ/**乱数の地形20個でFindPathと同じ値段**/斜め、視線、近く、押し合い、弾、HPバーと印、全体の読み込み)、
    `lua_ext_test`(Lua APIを一通り)。**実機では未確認**(人や物96個・流れの場の作り直しの時間)。ゲーム本体は下の「ゾンビTD」。
- **ゾンビTD(`pc/sdcard/lua/apps/ゾンビTD/`、2026-10-08〜09、`ZOMBIE_TD.md`の「作る順番」の2〜7)**: 作る順番は全部済み
  (マップ・ゾンビ・兵士・建物・ウェーブ・保存・絵・効果音・説明)とコントローラー/キーボードの操作。未: 実機での速さとメモリの確認。
  - **地形は`Iso::ARENA`(`kind`=4、`Iso_World.cpp`の`arenaLayout`/`arenaHeight`)**: 8マスごとの格子の高さ4〜9を補間し、水面5より下は水(底は砂)、木は無し。
    ベースは z の小さい端の真ん中(`W/2, 4`)で 7x7 を平らな丸石に、出現位置は反対の端(`z = W-3`)に3つで 3x3 を平らな砂利に(半径8/4まで元の高さへなだらかにつなぐ)。
    `create(ARENA)`はベースの中心を返す。Luaは`pico.iso.arena()`→`{base=, spawns=}`。
  - **アプリ(`game.lua`)**: `iso.create(app_dir.."/map", 4, 種, 7)`(56x56。何も書き出さない)→`keep_all`→`pump`を数フレーム→
    ベースの中心から2マスの輪(16柱)を目的地に`flow_build`→`flow_step`で少しずつ→**出現位置のどれかから届かなければ種を1つ進めて作り直す**(8回まで)。
    ベースは人や物(`crowd="fixed"`、HPバー)。カメラはドラッグ/十字/矢印で、画面の真ん中の地面がマップの中に収まるよう抑える。
  - **ゾンビ(`zombies.lua`)**: 種類ごとの表(ノーマル/遠距離/重量級)、順番待ち→同時40匹まで出す(出現位置ごとに0.35秒空ける)。**柱が変わったときだけ**
    `iso.stand`と`iso.flow_get`を引き、次の柱の真ん中へ歩く(毎フレームの仕事は位置の更新と`entity_move`だけ。絵の向きとコマは変わったときだけ`entity_set`)。
    目的地(流れの場の`次`が無い)で近接はベースを叩き、遠距離は流れの場の値段が`reach`以下で`iso.sight`が通れば止まって石(`iso.shot_add`の`target`=ベース、`tag`=ダメージ)を投げる。
    押し合いは`iso.crowd(2, ...)`の後に`iso.entity_pos`で位置を読み戻す。段差は見た目だけ少しずつ上り下りする。
  - **速さを測る画面でもある**: 上の行に fps と1フレームの処理時間(Lua+描画の平均、ms)、5秒ごとにシリアルへ`[TD] fps=.. lua=..ms render=..ms alive=..`。
    計測用に`pico.micros()`(32bit、約71分で一巡)を足した。下のボタン`[+5][+40]`でゾンビを呼ぶ、`[x1/x3]`で速さ、`[作直]`で別の種。
    PCビルド(SPIの待ち込み)で40匹: 約98fps(フレームの上限)、Luaの処理は約0.26ms/フレーム、描画は見えている所次第。**実機は未計測**。
  - **実機で「ゾンビが30体を超えると再起動」(2026-10-08)**: Luaの使い捨ての表(コマ/向きが変わるたびの`entity_set{...}`、毎フレームの空の`shots_step`の表)が
    既定のごみ集め(生きている量の2倍まで溜める)で溜まり、PCで測るとLuaが予算の上限(約200KB)まで膨らんでいた。実機ではその前に本体のヒープが尽きて落ちたと見ている
    (Luaの確保の失敗は本来エラーで済むので、再起動したのは本体側の確保の失敗。クラッシュダンプは未確認)。`entity_set`の表を使い回し、弾が無いときは`shots_step`を呼ばず、
    `collectgarbage("generational")`にした(PCで40体: 約200KB→約140KB)。5秒ごとの`[TD]`の行に`mem lua=.. heap_free=.. headroom=..`を出す。
  - **兵士(`soldiers.lua`、2026-10-08、作る順番の4)**: 近接/回復/弓の3種×Lv1〜4(値は`TYPES`)。お金(最初300、ゾンビを倒すと`money`)で雇い、
    ベースの前に出て持ち場へ歩く。**移動は`iso.path`**(水=ブロック1を`avoid`、斜めあり、`partial`、`max_nodes`3200)で、道の点を`combat.walk`でたどる
    (押されて1秒進めなければ探し直す)。持ち場では射程のゾンビを自動で攻撃し、近接は持ち場から`LEASH`(3)マスまで追って戻る。**移動中は攻撃されたときだけ反撃**
    (`hurt()`が攻撃してきた相手を`tgt`にする。相手が倒れる/間合い+1マスより離れると道の続きへ)。回復兵は自分→範囲2.5の一番減っている兵士(割合)を回復。
    強化はその場で、売却は払った合計の70%。ゾンビは気づく範囲4の兵士を「距離+優先度の順位×1.5」で選んで追い(近接0/回復1/弓3)、
    6マスより離れる・3秒攻撃できないと諦めて流れの場へ戻る(5秒は探さない)。兵士が1人もいなければ探さない(`zombies.hunt`。探すと毎回小さな表ができる)。
    - `combat.lua`: 人や物のハンドル→ユニットの表(`units`)、弾(撃ったユニットとダメージを弾のハンドルで覚え、当たったら`u:hurt(dmg, src)`)、高低差の倍率、
      地形に沿って歩く`walk`(登れない段差・水へは入らず、入れない軸だけ止めて滑る)。ユニットは`hp`が0以下で倒れた扱い(`combat.alive`)。
    - 押し合いは`game.lua`が1回だけ`iso.crowd`して、`zombies.sync()`/`soldiers.sync()`で読み戻す。流れの場と`stand`にも水の`avoid`を足した(仕様の「水には入れない」)。
    - **画面**: 下の欄が2段(情報の行20px=選んだ兵士のLv・耐久・[強化][売] / ボタン34px=[戻る][雇う][選択][x1][中央][他])。雇うと「他」(ゾンビ+5/+40・解除・作り直し)は
      `pico.show_choice`。地図のタップ=兵士を選ぶ(もう一度で外す)/選んでいる兵士をそこへ(複数なら半径3までの立てる柱へ散らばる)。
      **「選択」はトグル**(仕様は「押している間」だが、タッチが1点なのでボタンを押しながらドラッグできない)で、オンの間のドラッグが範囲選択(黄色の枠)。
      キーボード: 1/2/3=雇う、Esc=選択の解除。
  - **Luaのメモリ(2026-10-08)**: 兵士を足したら、PCで40匹+兵士3人の**生きている量**が約198KB(予算200KB)になった(コードが約60KB増え、1匹の表が約1KB)。
    対策: ①**app.cfgの`strip_debug=true`**(新設。requireしたモジュールの行番号・ローカル変数名を落とす。エラーの行番号は「?」になる。PCで約18KB減)、
    ②**1体の表のキーを16個以内**にした(Luaの表は16を超えると32ノード=倍になる。種類ごとに同じ値はメタテーブル、`tgt`は`false`で持つ、
    コマは時計から決める、最大耐久・払った額は計算する。`lua_ext_test`が16個以内を確かめる)。結果: 生きている量は約164KB、ごみを含めた山は約190KB。
    **実機の値は未計測**(32bitなので少し小さいはず)。建物・ウェーブを足すとさらに増えるので、`[TD]`の行の`mem lua`と`heap_free`を実機で見ること。
  - **建物(`buildings.lua`、2026-10-09、作る順番の5)**: 弓塔(射程5.5)・剣塔(台の上から槍、ゾンビの狙う優先度2=囮)・バリケード(柵)。
    建設(8秒/柵4秒)・レベルアップ(その半分)の間は耐久が完成時の20%で、攻撃もしない。修理は「減った割合×払った合計×0.5」で3秒かけて戻る。
    建設中・強化中は売れない(70%)。置ける所は兵士が立てる柱で、ベース(チェビシェフ距離3)・出現位置(同3)の近くと建物のある柱は不可。
    - **タワー**は人や物(`crowd="fixed"`、tag 4)。建設中は足場の絵(units.pimg x=196)、完成で塔の絵。ユニットの`y`は台の上(地面+1)にして、
      撃つ高さと高低差の倍率に効かせる。兵士の道は`iso.path`の`edge`でタワーの柱を通らない。
    - **バリケード**はブロック。**アリーナで使わないブロック番号12/14/15/16を Lv1〜4 の柵にし、ゾンビTD専用の`faces.pimg`でその段を
      穴の開いた絵に差し替えた**(`generate_zombie_td_sheet.py`が「ブロック」の faces.pimg を読んで差し替える。エンジンは無改造で、
      穴があるので透けないブロックにならない)。ゾンビの規則は`pass`(中を通れる)+流れの場の`body_cost`=6、兵士と置く所の規則は`avoid`。
      視線(`iso.sight`)と押し合いもバリケードを通す(`combat.SIGHT_PASS`・`CROWD_RULES`)。置く/壊す/強化で流れの場を作り直す
      (`flow_build`して毎フレーム`flow_step(800)`。作っている間は前の流れのまま)。中のゾンビは速さ×0.5で、毎秒「ダメージ÷間隔×建物の倍率」を削る。
    - ゾンビは兵士とタワーを「距離+優先度×1.5」で狙う。建物へのダメージは種類ごとの`bmul`(重量級3)。
    - 画面: 下のボタンは[戻る][雇う][建設][選択][x1][他](「中央」は「他」へ)。建設で種類を選ぶ→地図をタップした柱へ建てる。
      建物をタップ(柵はブロックをタップ)すると選べ、下の欄に[強][修][売]。選んだ柵にはカーソルを出す。
  - **Luaの予算を64bitで1.33倍にした(`LuaScene::kLuaBudgetBytes`、2026-10-09)**: 建物まで入れると、PCでゾンビ40匹+兵士6人+建物7つの
    生きている量が約203KBになり、PCでだけメモリ不足になった。同じモジュールを32bit/64bitのLua(このリポジトリの`lib/lua`)で読み比べると
    57KB/76KB(1.33倍。ポインタの大きさ)なので、実機(32bit)の予算200KBはそのまま、64bitのときだけ同じ割合で広げた。実機では約155KB相当の見込み。
    **実機での確認はまだ**(以前ゾンビ30体で実機が再起動したのは本体のヒープが先に尽きたため。`[TD]`の行の`heap_free`を実機で見ること)。
    モジュールごとのLuaのメモリ(PC、デバッグ情報を落とした後): OSの分 約43KB、combat 約7KB、zombies 約18KB、soldiers 約31KB、buildings 約23KB、game 約26KB。
    `game.lua`は約31KBで、requireの上限(32KB)に近いので、次に画面を足すときは画面の部品を別のモジュールへ分けること。
  - **ウェーブ・保存(2026-10-09、作る順番の6)**: ファイルを分けた(`game.lua`が32KBの上限に近づいたため): `state.lua`(共有の状態 G。
    お金・選択・モード・Canvas)・`orders.lua`(選ぶ/雇う/建てる/強化/修理/売る/移動の指示)・`ui.lua`(下の欄)・`waves.lua`・`save.lua`。
    - `waves.lua`: 準備時間60秒(2回目から30秒)→ウェーブ。中身は`mix(n)`(合計6+3n、2回目から遠距離、3回目から重量級)、体力×(1+0.08(n-1))・
      攻撃×(1+0.04(n-1))(`zombies.hp_mul/dmg_mul`)、倒したお金×(1+0.05(n-1))、越えると30+10n、「次へ」は残り秒×2。
      全部出て(順番待ちを含む)全部倒れたら越える。準備時間の下の欄に予告(「重量級が来る!」等)。
    - `save.lua`: `store.json`に`{best={w,hp,earned}, game={seed,w,money,earned,hp,b={{種類,x,z,lv,hp}},s={{種類,lv,hp,px,pz}}}}`。
      **保存はウェーブの始めと、準備時間の始め・準備時間中に閉じたとき**。ウェーブ中に閉じると、そのウェーブの始めの保存から準備時間としてやり直す。
      起動時に保存があれば「続きから W? / 新しく始める」(閉じた・キャンセルは続きから)。同じ種でマップを作り直してから建物(完成した状態)と
      兵士(持ち場に立った状態)を戻す(`buildings.restore`/`soldiers.restore`)。ベースが壊れたら途中の保存を消し、最高記録(耐えたウェーブ数→
      ベースの耐久→稼いだ合計)を残す。
    - 上の行: 準備時間は「W?まで残り秒」と[次へ](タップ・A・n)、ウェーブ中は「W? 残?」とfps。速さのボタンは x1 → x3 → 停止(一時停止)。
    - **`pico.keep_awake()`を新設**(C++、`LuaEngine_Ext.cpp`。`PowerFunctions::KeepAwake()`を呼ぶだけ)。ウェーブ中と一時停止中に毎フレーム呼ぶ。
    - ゾンビを呼ぶ「+5/+40」は無くした(ウェーブで来る)。
    - Luaのメモリ(PC): ファイルを分けた分も含め、始めた直後で約192KB(予算は64bitで273KB。実機の32bitでは約145KB相当)。
    - 検証: `lua_ext_test`の「waves/save」(中身・予告・倍率・次へ・start/clear・スコアの比べ方)と、**`game.lua`全体を本物のエンジンで動かす**
      (マップを作って準備時間から→雇う→次へのお金→ウェーブ1と保存→全部倒れて越えたお金と保存→保存の書き出しと戻し→ゲームオーバーで
      保存を消して最高記録)。PCビルドの`--tap`/`--shot`で準備時間・次へ・ウェーブ・閉じて開き直しての続きからを確認。
  - **絵・効果音・説明(2026-10-09、作る順番の7)**:
    - `units.pimg`を40px×4段(256x160)にし、**段 = レベル**で兵士3種と塔2種の Lv1〜4 を描き分けた(近接: 鎧→槍→金の縁の強化鎧、
      弓: 赤い帯→矢筒→兜と金の弓、回復: 明るい頭巾→光る杖→金の頭巾、塔: Lv2 旗・Lv3 石の脚・Lv4 金の手すり)。`entity_set`の`sy = lv*40 - 高さ`。
      ついでに直したもの: 兵士の弓兵の絵の変数名が遠距離ゾンビ(`RANGED_TOP`)を上書きしていて、遠距離ゾンビが弓兵の絵になっていた(`ARCHER_TOP`へ)。
    - `sfx.lua`: 効果音はチャンネル2で1フレームに1つ・優先度の高いものだけ(ブロック崩しと同じ)。矢・叩く・倒す・ベース・完成・お金・倒れた/壊れた。
      ジングル(ウェーブ開始・越えた・ゲームオーバー)は`music_play_text`。「他」で入り切りし、`store.json`の`snd`に覚える。
    - `tutorial.lua`: 初めて遊ぶとき(`store.json`の`tut`が無いとき)だけ、地図の上部に案内を1つずつ出す(雇う→移動→建設→次へ→強化/修理/売却→守り抜こう)。
      言われたことをすると次へ進む。案内(右上の×)をタップすると終える。「他」の「説明をもう一度」で出し直す。
    - **Luaのメモリ**: ゾンビ40匹・兵士8人・建物10個で、生きている量が PC で約237KB(実機の32bit換算で約178KB)。ごみを含めた山を下げるため
      `collectgarbage("generational", 5, 30)`(小さい集めを早める)にして、山が約267KB → 約247KB(実機換算で約186KB。予算は200KB)。
      **余裕は少ない**: これ以上コードを足すなら、先に実機の`[TD]`の行で`mem lua`/`heap_free`を確かめること。
  - **コントローラー・キーボードの操作(`cursor.lua`、2026-10-09)**: 地図にマス1つのカーソル(黄色のひし形。`iso.to_screen`の角から`draw_line`で描く。
    `iso.cursor`は柵を選んだ印に使っているので別)。十字はマス目に沿って斜め(上=+x=右上、右=-z=右下。2つ同時で画面の上下左右)、
    押したままで0.28秒後から0.08秒ごと、画面の端から28/20px以内に来るとカメラが付いてくる。A=カーソルの所の兵士(柱の真ん中から0.75マス以内)・
    建物を選ぶ/選んでいる兵士を動かす/建設中なら建てる(`orders.act_at`。`tap_map`と同じ`toggle`/`move_to`を使う)、B=建設をやめる→選択を外す、
    X/Y=兵士/建物を順に選ぶ、R=ベースへ、START=次へ(終わった画面ではA/STARTで新しく)、L+十字=カメラだけ、SELECT=下の欄のボタンへ
    (左右で選び A で押す。黄色の枠。速さ・強化・修理以外は押すと地図へ戻る)。キーボードは矢印・Enter/Space・Esc・x/y/c/n・Tab・Shift+矢印。
    地図をタッチするとカーソルは隠れる。**選ぶ一覧(雇う/建設/他/始め方/新しく始める確認)はOSのダイアログ(タッチでしか選べない)をやめ、
    `ui.menu()`で地図の上に出す**(上下とA/B、タップも可、枠の外のタップはやめる)。
    Luaのメモリを食わないよう`cursor.lua`は関数を10個に絞った(関数1つでPCで約1KB)。それでもカーソル・メニュー・フォーカスで起動直後のLuaがPCで約18KB増えたので、
    OS側で減らした(下の「pico.* の関数の置き方」): 起動直後のLua はPCで 204.5KB → 200KB(この機能込み)。
    ついでに直したもの: 新しく始めるとき兵士を選んだままだと落ちた(消した後に選択の印を消そうとした。`clear_units()`で先に外す)、
    キーボードのEscが効いていなかった(`on_key`の名前は`"escape"`)。
    検証: `lua_ext_test`の「コントローラーのカーソル」(`pico.pad_*`を偽物に差し替えて`loop()`を回す: 出る・動く・押したまま・SELECT→雇う→メニュー・B・X/Y・
    A で動かす/選ぶ/外す・建設・メニューのB/タップ・カメラが付いてくる・キーボード・START)、PCビルドで標準入力へ`pad`の行を流して`--shot`。
  - 絵は`script/generate_zombie_td_sheet.py`(`units.pimg`=ゾンビ3種x2コマ・ベース・兵士3種x2コマ(x=124〜)・足場・弓塔・剣塔(x=196〜)、`icon.pimg`、
    柵を差し替えた`faces.pimg`。`palette.lua`は「ブロック」から写す)。兵士と塔の絵はレベルで変わらない(作る順番の7で)。
  - 検証: `iso_td_test`(ARENA: 平らな所・丸石と砂利・木が無い・24個の種で道がある・種で決まる)、`lua_ext_test`(`iso.arena`/`entity_pos`/`pico.micros`と、
    **本物の`zombies.lua`を本物のエンジンの上で120秒ぶん動かす**: 40匹の上限と順番待ち・近接と石がベースに当たる・地面に立つ・重ならない・倒すと次が出る)、
    PCビルドの`--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0)。
    兵士は`lua_ext_test`の「soldiers」: 雇う→持ち場で止まる・地面に立つ、指示した所へ着く・散らばる・水/ベースの上は不可、強化/売却の額、
    ゾンビ16匹と60秒戦わせて倒す・近接が持ち場から3マスほどまで・回復する、移動中の反撃(止まって戦い、倒したら道の続きへ)、倒れたら消える、
    ゾンビの狙う優先度、表のキーが16個以内。`strip_debug`も同じファイルで確かめる。PCビルドで雇う・範囲選択・移動の指示・戦いを`--tap`/`--shot`で確認。
    建物は同じファイルの「buildings」: 置けない所、建設中の耐久・売れない、完成で塔の絵、強化中の耐久と終わり、修理の費用と3秒、売却額、
    柵のブロックと流れの場の作り直し、柵の中はゾンビは立てて兵士は立てない、兵士の道がタワーの柱を通らない、柵の中のゾンビは遅く柵が削れる、
    柵の強化でブロックが変わる、壊されると消える、弓塔がゾンビを倒す、ゾンビがタワーを狙う、`clear`で柵のブロックも消える。
- **色**: 元の63色+半分の明るさの影を Lab の k-means で14色にし、起動時に `pico.set_palette` で入れる(1〜14番を既定のパレットの近い番号へ
  並べてあるので、ステータスバー等の色は大きくは変わらない)。アイコンは既定のパレット(彩度を上げてから最近傍)。
- 操作: 画面=左下の9キー(真ん中が置く/壊す)・上へ/下へ・ブロック変更・中央・昼へ/夜へ・終了、ワールドのタップ=その面の手前へ・長押し=そのブロックへ・
  ドラッグ=視点。コントローラー=十字(2つ同時で斜め)・A・B/START・X/R・Y/L・SELECT+十字=視点・HOME=保存して戻る。キーボード=元と同じ
  1〜9・`*` `-`・Enter・矢印。セーブは `worlds/A/`〜`E/`(`.gitignore`済み)。
- 元との違い: 世界の広さ(1024x1024)と地形の作り方(種から決まる)。カーソルの奥側の面、視点の滑らかなスクロールは無い。水は色を混ぜずに市松模様で透かす。
- 検証: `iso_world_test`(run.sh の lua-scene。エンジン: 生成がLua版と同じ・読み書きと境目・読み込みの範囲と手放し・置き場に収まる・保存と読み込みの往復と壊れたファイル・
  前の版からの移し替え・見えない面/影/水/葉/範囲/カーソル・Lua版と同じ描画の並び(カーソルの段だけ読み替えて比べる)・隠れたブロックを省いても画素が同じ・
  引き当て・描き直す範囲・面の写し方(ディザも)が1画素ずつと同じ・松明の光の広がり/壁/葉と水/取ったとき/チャンクの境目と後から読み込んだチャンク/
  影を落とさない/描く絵(昼・夜・右面・ディザ)/描き直す範囲/保存して開き直す)、
  `lua_ext_test`(`pico.iso` をLuaから一通りと権限)、`blocks_test`(`game.lua` の画面の流れと操作。`pico.iso` は偽物)、PCビルドの `--tap`/`--shot`
  (`PICOOS_VERIFY_LCD=1` で食い違い0。同じ種で Lua 版と並べて、違うのは葉の穴の中だけ)。**実機では未確認**
  (描画の時間・チャンクの生成の時間・約80KBの置き場の確保・松明の多い所での明るさの計算の時間・SDへの小さなファイルの読み書き)。

### 実行時間の安全網(暴走防止、2026-09-21実装)

OSは単一スレッドのポーリングループ(`main.cpp`の`loop()`)なので、Luaのコールバック
(`loop(dt)`/`press_start`/…)の中に`while true do end`のような終わらないループが
あると、`lua_pcall()`が戻ってこずOS全体が固まる。C++側の新規コードにはレビューが
あるが、Luaスクリプトは(このあとの「SDを走査したLuaアプリの自動登録」でますます)
書く人を選ばなくなるため、この種の事故を検出できる仕組みを入れた。

- **`lua_sethook(L, InstructionHook, LUA_MASKCOUNT, kHookInstructionInterval)`を
  コンストラクタで1回だけ設定**し、Luaバイトコードを`kHookInstructionInterval`
  (=1000)命令実行するたびに`InstructionHook()`を呼ぶ。外部から見える呼び出し
  (`Run`/`CallSetup`/`CallLoop`/各種`Dispatch`/HTTPコールバック、合計8箇所)は
  共通のprivateヘルパー`ProtectedCall(nargs)`を必ず経由し、そこで
  「この1回の呼び出しで消費してよい命令数」の残高(`instructions_remaining_`)を
  `kMaxInstructionsPerCall`(=200万、暫定値)へ積み直してから`lua_pcall()`する。
  `InstructionHook()`は毎回この残高を減らし、尽きたら`luaL_error()`で
  Luaのエラー機構(内部はlongjmp)経由に処理を戻す。
- **`lua_pcall()`から見れば通常の実行時エラーと区別が付かない**ため、`Run()`等の
  既存のエラーハンドリング(`ErrorFunctions::ShowFatal()`でログ+ダイアログ)を
  そのまま使い回せる。新しい状態は`instructions_remaining_`(1個のuint32_t)だけ。
- **Lua自身のバイトコード実行だけを数える。** `pico.sd_read`等のC関数の中
  (ファイルI/O等)ではフックは発火しない——C関数呼び出し中はインタプリタが
  バイトコードを進めていないため。「うっかり書いた無限ループ」を捕まえる用途に
  対し、C++側の正当な処理を巻き込まない。
- **時間ではなく命令数で打ち切る。** `millis()`ベースの時間打ち切りも検討したが、
  ホストテスト環境の`millis()`スタブが常に0を返すため時間ベースでは検証できず、
  実機の処理速度にも依存して閾値の意味が変わってしまう。命令数ならホストテストでも
  `while true do end`を実際に実行してエラーになることを確認でき(実測: ASan+UBSan
  付きでも約50ms程度で打ち切りに達する)、ハードウェアに依存しない決定的な基準になる。
  `kMaxInstructionsPerCall=200万`は暫定値(実機RP2350での実測は未実施。RAM/Flash予算の
  「200KB」と同種の「後で実機で確かめる」枠)。
- ~~既知の限界: Luaの`pcall`で自前でエラーを握り潰して繰り返す敵対的なスクリプトまでは
  防げない~~ → **解消済み(2026-10-03)**。下の「開発者向けの道具」の「Luaサンドボックスの強化」参照。
- ホストテストは`lua_engine_test.cpp`に追加。終わらないループを含むスクリプトが
  `Run()`/`CallLoop()`をハングさせず`false`で戻ること(テストプロセス自体がハング
  しないことが最大の確認点)、打ち切り時もダイアログが出ること、Lua側の`pcall`で
  捕まえれば普通に続行できること、上限内のループは邪魔されないこと、`loop()`内で
  打ち切られた場合は既存の`loop_broken_`安全弁と重ねて効く(以降`loop()`自体が
  呼ばれなくなる)ことを確認している。

### SDを走査したLuaアプリの自動登録(LuaAppScanner、2026-09-21実装)

`AppFunctions::Register()`はC++からしか呼べず、「SDにスクリプトを置くだけでランチャに
タイルが出る」というLua版の"アプリストア"的な使い方の土台が無かった
(`AppEntry`が`FixedString`で動的な名前/argを持てるようになった時点(2026-09-13)で
下地はできていたが、実際に走査してRegister()を呼ぶ側が無かった)。

- **`src/lua/LuaAppScanner.hpp/.cpp`が新設した唯一の関数は`LuaAppScanner::Scan()`。**
  `/lua/apps/<名前>/main.lua`という「サブディレクトリ1つ=アプリ1つ」の構成で
  `/lua/apps/`直下を走査し、`main.lua`があるサブディレクトリを見つけるたびその
  ディレクトリ名をそのままタイル名として`AppFunctions::Register()`する。
  `App_List.cpp::Setup()`の末尾(静的登録の後)で1回呼ぶだけで配線は完了する。
- **なぜサブディレクトリ単位か(`/lua/apps/`直下へフラットに`*.lua`を置く案は不採用)**:
  `LuaScene::onEnter()`はスクリプト自身の親ディレクトリを`app_dir`(SDアクセスの
  閉じ込め先)として使う。フラットに置くと複数アプリのapp_dirが全部`/lua/apps/`
  自身になり、見つかった複数のアプリが互いのファイルを読み書きできてしまう。
  サブディレクトリ単位にすることで、各アプリが自分の`main.lua`の親ディレクトリ
  (=自分専用のサブディレクトリ)だけに閉じ込められる。
- **権限は既定値(`LuaPermissions{}`、`network`/`sd_outside_app_dir`ともfalse)固定。**
  SDに置かれているだけで中身を検証していないスクリプトへ、走査した側が勝手に
  強い権限を与えないための判断。ネットワークやapp_dir外のSDアクセスがどうしても
  要るLuaアプリは、従来通り`App_List.cpp`へ専用の生成関数(`MakeLuaHelloScene()`と
  同じ形)を書いて手動登録する(このパスはスキャン対象外のまま)。
- **`/lua/`直下(`hello.lua`等の動作サンプル)とは別ディレクトリにしてある。**
  `/lua/`直下を走査すると、`hello_sub.lua`/`hello_sub2.lua`のような
  「他のスクリプトから`push_scene`/`change_scene`で遷移するためだけのサブ画面」まで
  誤って1タイルずつ登録してしまうため。
- **`App_Functions.cpp`自体は変更していない。** `LuaAppScanner.cpp`は`App_List.cpp`と
  同じ立ち位置(`AppFunctions::Register()`の1利用者)で、シーン実装
  (`LuaScene`)に依存するコードを`App_Functions.cpp`へ持ち込まない、という既存の
  分離方針(CLAUDE.md「アプリの枠組み」参照)をそのまま踏襲している。
- **名前の重複チェックはしていない。** 静的登録とスキャンで同名のアプリがあれば
  単純に2タイル並ぶ(後勝ちで上書きはしない)。今のところ登録されるLuaアプリの
  絶対数が少ないため、重複解決の優先度は低いと判断した。
- 動作サンプル: `pc/sdcard/lua/apps/スキャン確認/main.lua`。`App_List.cpp`には
  一切手を加えず、PCビルドの`--shot`でランチャに「スキャン確認」タイルが実際に
  現れてタップで起動できることを確認済み。
- ホストテストは`script/host_test/lua_app_scanner_test.cpp`。**完全な走査結果は
  ホストのSdFatスタブでは検証できない**(パス→内容のフラットな`map`でディレクトリの
  実体も`openNext()`の走査も無いため。`pico.sd_list`/`Doc_Cache::Clear()`と同じ制約)。
  SD無し/ディレクトリが存在しない場合に安全に0件を返すことのみASanで確認し、
  実際の走査結果はPCビルドの`--shot`(上記)で確認する、という役割分担にした。

### 現時点のスコープ外(次回以降)

- ~~コールバックは共通4種(press_start/end/move/out)のみ~~ → **解消済み(2026-09-21)**。
  `Checkbox::on_change_checked`/`NumberSlider::on_value_changed`/`ScrollList::on_selectitem`/
  `TabBar::on_changed`を`checked_changed`/`value_changed`/`select_item`/`tab_changed`として
  `pico.on()`から使えるようにした(詳細は上の「ウィジェット固有イベント」参照)。
- ~~命令単位の実行時間制御(`lua_sethook`)は無い~~ → **解消済み(2026-09-21)**。
  詳細は上の「実行時間の安全網(暴走防止)」参照。`StepBudget`(時間で区切る土台)とは
  別物のまま(こちらはLua命令数、StepBudgetはTaskのフレーム分割用)で、統合はしていない。
- ~~1フレームごとにLua側の「update」関数を呼ぶ仕組みは無い~~ → **解消済み(2026-09-20)**。
  `LuaEngine::CallSetup()`/`CallLoop(dt_ms)`がArduino風の`setup()`/`loop(dt)`を
  呼ぶ(どちらも定義は任意)。`LuaScene::onEnter()`がRun()成功後に`CallSetup()`を
  1回、新設した`LuaScene::onUpdate()`が毎フレーム`CallLoop(dt)`を呼ぶ
  (dtは`millis()`差分。`ClocksScene`と同じ計測方法)。**loop()が一度エラーを
  出すと以降は自動的に呼ばれなくなる**(`LuaEngine`内の`loop_broken_`フラグ。
  毎フレーム同じエラーダイアログが積まれるのを防ぐ安全弁で、setup()側はRun()と
  同じく1回きりなので不要)。ホストテストは`lua_engine_test.cpp`(CallSetup/CallLoop単体)
  と`lua_scene_test.cpp`(LuaScene経由の結合テスト)。サンプル`pc/sdcard/lua/hello.lua`に
  経過秒数を表示するloop()の実例を追加した。
- ~~コンテナからの明示的な子の取り外し(`pico.remove_child`)は無い~~ → **解消済み
  (2026-09-21)**。詳細は上の「コンテナからの取り外し」参照。この節に挙げていた
  Lua APIの既知の穴はこれで全て埋まった(残るのはOS内部90箇所の
  OOM未対応など、コストに見合わないと判断して対象外にしたものだけ)。

## Lua着手前の受け皿の状態 (2026-09-19時点)

Lua向けの土台は「発行側・ファクトリ・プロパティ共通口・実行時間制御の土台・エラー表示導線・
ビルドへの組み込みまでは入って、Luaバインディング本体(`lua_State`を実際に生成してsrc/へ繋ぐ部分)
だけが空」の状態。着手時に必ず当たる穴を列挙しておく。

| 箇所 | 状態 |
|---|---|
| ~~`WidgetRegistry::Resolve()`~~ | **ホストテストで検証済み(2026-09-18)**。発行済みIDからの解決・type改ざん検出・破棄済みID(use-after-free)検出・スロット再利用時のgeneration不一致検出を`script/host_test/widget_factory_test.cpp`で確認。ただし**実コード中の呼び出し元はまだテストのみ**で、Luaバインディングを書いた時点で初めて実利用される |
| ~~ウィジェットのファクトリ~~ | **解消済み(2026-09-18)**。`WidgetFactory::Create(WidgetType)`(`src/gui/widgets/WidgetFactory.hpp/.cpp`)がwidgets/直下の汎用部品15種を生成する。widgets/apps・systems・dialogsの専用ウィジェットは対象外 |
| ~~プロパティのget/set共通口~~ | **解消済み(2026-09-19)**。`WidgetProperty::Get/Set()`(`src/gui/widgets/WidgetProperty.hpp/.cpp`)を参照。Lua側が「WidgetIdを`Resolve()`で引く→`WidgetProperty`で値を読み書きする」という2段構えを、Lua本体無しで既にホストテストまで確認できている |
| ~~`AppEntry`(`App_Functions.hpp`)~~ | **解消済み(2026-09-13、スキャン側も2026-09-21で解消)**。`create`が`Scene* (*)(const AppEntry&)`になり、`name`/`arg`は`FixedString`でコピー保持するようになった。「同じ`LuaScene`型 + 別スクリプトパス」も、寿命の短い文字列からの動的登録も表現できる。**スキャン処理そのもの(`LuaAppScanner::Scan()`)も実装済み**(下記「SDを走査したLuaアプリの自動登録」参照) |
| コールバック | **意図的に見送り(2026-09-19)**。`std::function<void()>`のまま。上記「メモリ計測の結論」が既に出している判断(「関数ポインタ+`void*`のDelegateへ替える効果は単体では5%程度、旨味が出るのはLuaのコールバックを大量に貼るようになってから」)をそのまま踏襲し、今回は手を入れなかった。今のシグネチャには「引数もコンテキストも無い」という実害(誰が押したか・どのウィジェットのIDかをコールバック側へ渡せない)もあるため、**再設計するならLuaバインディング本体を書く回でシグネチャを一度に決める**(引数無しのまま先にDelegate化だけ済ませても、Lua側の要求で結局signature変更が要る可能性が高く、二度手間になるため) |
| ~~実行時間の制御~~ | **解消済み(土台2026-09-19、命令単位の打ち切りは2026-09-21)**。`task/StepBudget.hpp`が「一定時間(マイクロ秒)働いたら次のフレームへ回す」ための時間区切りプリミティブを提供する(`Task::update()`内の作業ループを`StepBudget::ShouldContinue()`で区切る。ホストテストは`script/host_test/step_budget_test.cpp`)。**`LuaEngine`側は`lua_sethook(LUA_MASKCOUNT)`でLuaバイトコード命令数を数え、1回の外部呼び出しあたりの上限を超えたら`luaL_error()`で打ち切る**(`StepBudget`とは別物のまま、統合はしていない。詳細は上の「実行時間の安全網(暴走防止)」参照) |
| ~~確保失敗(OOM)~~ | **Lua向けの経路は解消、内部90箇所は対象外と決定(2026-09-19)**。`WidgetFactory::Create()`はtype非対応時もWidget::operator new失敗時も一貫してnullptrを返す設計になっており(ヘッダのコメントで明記済み)、**Luaが実際に触る唯一の生成経路はこの時点で既に安全**。一方、Scene/Dialog等OS内部の`new Button(...)`等(約90箇所)は個々にnullチェックしていないが、これらは実行時に増減しない固定・既知個数の生成で、「メモリ計測の結論」が示す通り実測で断片化もリークも無く十分な余裕がある。ここへ90箇所分のnullチェックを機械的に足す投資対効果は低いと判断し、**対象外とする**(Luaスクリプトが暴走してウィジェットを大量生成する経路は`WidgetFactory::Create()`1箇所に絞られているため、そこが安全なら実害は無い)。`lua_newstate`のカスタムallocでLuaに上限枠を切る話(下記RAM/Flash予算)は引き続き未着手 |
| ~~エラーの見せ方~~ | **解消済み(2026-09-19)**。`ErrorFunctions::ShowFatal(message)`(`src/functions/Error_Functions.hpp/.cpp`)がログ(`LOG_APP_FAIL`)とMsgDialog表示の両方を1呼び出しでこなす。`FileExplorer::on_press_delete()`等と同じ「生成→`AddDialog`→`setVisible`→`setOnClosed`で`DestroyLater`」の作法を関数内に閉じ込めてあるので、将来Luaの`pcall`エラーを拾った先はこれを呼ぶだけでよい。ホストテストは`script/host_test/error_functions_test.cpp` |
| RAM/Flash予算 | **暫定枠: Lua用に200KBを割り当てる方針(2026-09-19決定、`lua_newstate`のカスタムallocへ渡す上限)**。開発者が実機で計測した「OS側のヒープ使用量はピークでも150KB程度」を根拠に、RP2350の総SRAM 520KBから逆算した(150KB+200KB=350KBでも170KBの余裕)。**ただし2点未確認**: ①その150KBが`Mem_Functions`(mallinfoベースのヒープ)の値かどうか(フレームバッファ`frame`スプライトやWi-Fi/lwIPスタックがヒープ計測に乗らない確保だと実際の総使用量はもう少し上振れし得る)、②このリモート実行環境には実機もPlatformIOのRP2350ボード定義も無く追試できていない(`platform = raspberrypi`のPlatformIO公式パッケージ1.20.0にはrpipico2wのボード定義が同梱されていない)。**実機が使える時に、Wi-Fi接続中+一番重いシーン(Markdown/Dict)を開いた状態で`MemFunctions`のレポートを取り、200KB確保後も安全か確認すること。** Lua本体はflash 100KB超で、**stateだけでRAM 20〜30KBのオーダーという見積もりはPC上で実測して裏付けた**(空のstate+`luaL_openlibs()`一式でピーク約19.5KB。`script/host_test/lua_alloc_budget_test.cpp`のBudgetAlloc計測)。200KB枠はそこにユーザースクリプト+ウィジェットツリー分の余裕を見込んだ値。**カスタムallocによる予算制御そのものの安全性もPCで確認済み**(下記「ビルドの二重管理」参照)。実機での絶対値(mallinfoの150KBが本当に正しいか)はやはり未確認 |
| ~~ビルドの二重管理~~ | **LovyanGFXとは別方式で解消済み(2026-09-19)**。Lua 5.4.7本体(`lua.c`/`luac.c`を除く)を`lib/lua/`へvendorした(`lib/lua/README-pico-os.md`に経緯あり: Lua本体の`src/`には`main()`を持つ`lua.c`/`luac.c`が混在しており、`lib_deps`へ生のgit/tarballを指定するとPlatformIOの自動収集がそれも拾ってArduinoコア自身の`main()`と衝突するため、LovyanGFX方式(`lib_deps`+`pc/CMakeLists.txt`が同じタグをそれぞれ取得)は使えなかった)。vendor後は`lib/`配下がPlatformIOの「プロジェクト専用ライブラリ」として自動的にビルドされる(`platformio.ini`への追記は不要)ので、`pc/CMakeLists.txt`もこの同じ`lib/lua/src/`を参照するようにし、**実質1箇所の情報源**に落ち着いた。PCビルドで実際にリンクし、`lua_newstate`/`luaL_openlibs`/`luaL_dostring`/`lua_close`が動くことまで確認済み(`script/host_test/lua_smoke_test.cpp`、PC実行バイナリ`pc/build/picoos_pc`でも起動確認済み)。**言語機能・標準ライブラリの動作確認(2026-09-19追加)**: `script/host_test/lua_stdlib_test.cpp`で数値/文字列/テーブル/クロージャ/メタテーブル/pcall/コルーチン/GCが一通り動くことをASan/UBSan付きで確認(全てpc/CMakeLists.txtと同じ`LUA_USE_LINUX`無しのANSI構成でビルド)。**カスタムアロケータでの予算制御の安全性も確認済み**: `script/host_test/lua_alloc_budget_test.cpp`が`lua_newstate`へ確保量上限付きの`lua_Alloc`を渡し、予算超過時に(a)`lua_newstate`自体は素直に`nullptr`を返す、(b)`pcall`越しの`luaL_openlibs()`は`abort()`せず`LUA_ERRMEM`を返す(`pcall`で保護しないまま直接呼ぶとLuaの仕様上`abort()`する点に注意)、(c)いずれの場合も`lua_close`後は確保量が必ず0に戻る(リーク無し)ことを確認した。これは上記RAM/Flash予算の「200KBで上限を切る」方式が安全に実現できることの裏付けになる。**PlatformIO側(実機)は引き続き未検証**(このリモート実行環境にRP2350のボード定義が無いため。上記RAM/Flash予算の未確認点と同種の制約) |

**API仕様は「C++で標準アプリを1〜2本書いてみて、必要になったもの」から逆算するのが確実。** ランチャに載っているのは`MarkdownScene`(引数なしなら`network.cfg`の`browser-home`を開く)/ `ClocksScene`(時計・タイマー・ストップウォッチ)/ `InputTestScene`(部品の動作確認用)の3本で、バインディング設計の実例としてはまだ足りていない。

**2026-09-19追記: 上記②④⑤は`src/lua/LuaEngine`として実装・検証済み**(詳細は上の
「Luaバインディング」参照)。コールバックは結局シグネチャを変えずに済み、
`WidgetRegistry::Resolve()`/`WidgetProperty`は`pico.get`/`pico.set`/`pico.on`から
実際に呼ばれ、Luaのエラーは`pcall`で受けて`ErrorFunctions::ShowFatal()`へ渡るところまで
ホストテスト(`lua_engine_test.cpp`)で確認済み。**残っているのは①(`lua_newstate`を
`budget_bytes`付きで呼ぶ場所自体は`LuaEngine`のコンストラクタに決まったが、
「どの`Scene`/`Task`がいつ`LuaEngine`を`new`/`delete`するか」という置き場所はまだ無い
=`LuaScene`が無い)と③(命令単位の実行時間制御)**、および「Luaバインディング」章末尾の
「現時点のスコープ外」に挙げた項目(ウィジェット固有コールバック、毎フレームのupdate呼び出し等)。

**2026-09-21追記: ①③とも解消済み。** ①は`LuaScene`(上記)、③は`LuaEngine`の
`lua_sethook`ベースの安全網(上記「実行時間の安全網(暴走防止)」)。ウィジェット固有
コールバックも同日解消(上記「ウィジェット固有イベント」)。この節が挙げていた
`pico.remove_child`(コンテナからの子の明示的取り外し)も同日の細部穴埋めで追加し、
挙げていた穴は全て埋まった(詳細は上の「コンテナからの取り外し / リストへの項目追加」参照)。

## Claude Codeへの申し送り

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
  特定のアプリのために作ったものは`widgets/apps/`へ。**汎用かどうか迷ったら`apps/`へ置く**
  (後で汎用と分かって上げるのは簡単だが、直下に溜まると分類し直す手間が大きい)。
- 判断に迷ったら `SUMMARY.md`(https://raw.githubusercontent.com/Kimu1109/pico-os/refs/heads/main/SUMMARY.md)と実コードを突き合わせて確認する。
- **`SUMMARY.md`を書き換えるときは体裁を崩さないこと**。「TODO」は**項目名だけ**の一覧に保ち、
  説明を足したくなったら下の「詳細」側へ書く(TODO欄に長文をぶら下げると一覧として読めなくなるため、
  この形へ整理した)。**新しい大項目を足したら冒頭の「全体の進捗」表にも1行足す。**
- **テストは全て手動**。CIはWebビルドの公開(`.github/workflows/web-pages.yml`)だけで、
  **テストを回すワークフローは無い**。`sh script/host_test/run.sh`(ASan、58本。グループ名を渡すとそのグループだけ回す: `run.sh core lua-engine`、一覧は`--list`。全部を並列に回すなら`sh script/host_test/run_parallel.sh [-j N] [グループ名...]`、2026-10-05追加)/
  `sh script/host_test/run_net.sh`(実通信)/ `sh script/host_test/run_mem.sh`(確保回数)/ PCビルドは
  変更のたびに自分で回すこと。
  **`script/host_test/stubs/SdFat.h`は常に`<fcntl.h>`の`O_CREAT`等を使う(2026-09-23)**。以前は「先に取り込まれていれば
  そちら、無ければ自前の値」で、翻訳単位ごとに値が変わり、inlineの`open()`がどちらの値でリンクされるか次第で
  「書き込み用に開けない」ことがあった(TLSのヘッダ経由で`<fcntl.h>`を取り込むファイルが増えて`run_net.sh`で踏んだ)。
  スタブで`#ifndef`して定数を足すときは同じ罠に注意すること。
  **`run.sh`はコンパイル・テスト実行の各ステップに`timeout`を掛けてある(2026-09-21追加)**。
  「まれにrun.shが終わらない」という報告を受けて入れた安全網で、コンパイル1ステップ
  180秒・テスト実行1本60秒を超えると`[FATAL]`ログを出して明示的にexitする
  (`timeout -k 10`でSIGTERM無視にも備え、猶予後SIGKILLする)。**`if ! cmd; then rc=$?`や
  `if cmd; then ... fi; rc=$?`ではPOSIX上`$?`にcmdの本当の終了コードが乗らない
  (前者は`!`による論理反転、後者は「条件が偽で分岐未実行のifは exit status 0」という
  規定のため)ので、`rc=0; cmd || rc=$?`の形を使うこと**(`run.sh`の
  `compile_or_die`/`run_or_die`参照。一度この罠を踏んで学んだ教訓なので、
  同種のラッパーを足す際は再現しないこと)。
