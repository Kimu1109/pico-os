# CLAUDE.md — pico-os プロジェクトコンテキスト

> 相談の前提を素早く掴むための**地図**。一次情報源はコードと `SUMMARY.md`(TODOの有無と進捗)で、食い違えばコードを信じる。
> 機能ごとの設計の背景・経緯・調査は `docs/design/`(下の「設計記録の索引」)。**このファイルは毎回読み込まれるので、
> 足すのは地図と決まりだけにし、長い経緯・手順・調査は `docs/design/` 等へ書くこと。**(2026-10-20に整理して短くした)

## プロジェクト概要

Raspberry Pi Pico 2 W (RP2350, `rpipico2w`) 上で動く自作タッチGUI OS。PlatformIO + Arduino の C++。
SerenityOS的な設計を参考に、タッチ操作の組み込みGUIフレームワークを自前実装している。

**組み込み環境(RAM/Flash制約あり)前提で考えること。PC向けC++の常識(動的確保多用・例外多用など)を持ち込まない。**

## ハードウェア構成

| 項目 | 内容 |
|---|---|
| ディスプレイ / タッチ | 240x320 TFT(LovyanGFX、80MHz要求=実際75MHz) / XPT2046(IRQあり) |
| SPI0(TFT+タッチ共有) | SCK=18, MOSI=19, MISO=16 / TFT: CS=17, DC=20, RST=21 / TOUCH: CS=13, IRQ=9 |
| SPI1(SD専用、SdFat) | CS=15, SCK=10, MOSI=11, MISO=12, 10MHz |
| バックライト | TFT_LED=22(PWM調光。`Light_PWM`、スライス3のA) |
| 音声出力(I2S) | MAX98357A: BCLK=2, LRCLK=3, DIN=4 / 検出=5(内部プルアップ) / 休止=6(任意) |
| イヤホン(任意) | PCM5102Aを同じI2Sの3本へ並べる / XSMT=14 / ジャックの差し込み検出=8(内部プルアップ、LOW=差してある) |
| マイク(I2S) | SPH0645: BCLK=26, LRCLK=27, DOUT=28(48kHz・32bitスロット) |
| I2C0 | SDA=0, SCL=1。Wiiクラシックコントローラー(0x52、実物は未確認)とCardKB2 |
| CardKB2のUART(任意) | RX=7(SerialPIO。`/sys/cardkb.cfg` の `mode = uart` のときだけ) |

画面/パレット定数は `src/consts.hpp`(PICO-8風16色、既定 `PICO_BACKGROUND=15` / `PICO_FORECOLOR=0`)。

## ビルド

- `platformio.ini` の `[env:rpipico2w]`。`extra_scripts` がファームの版を埋め込み、`-t fwpack`/`-t fwpush` を足す。
- 画面の大きさと液晶の種類はビルド時に選ぶ(`SCREEN_WIDTH/HEIGHT`・`PICO_PANEL_*`、env `rpipico2w_320x480` が例)。**配置は `Scene::contentRect()` からの相対で書き、240/320を直書きしない**(`docs/design/gui.md`「画面の大きさ」)。
- 音声・マイク・電池は `-DPICO_HW_SOUND=0` 等で外せる(`config/Hardware_Config.hpp`、例は env `rpipico2w_minimal`。液晶・タッチ・SDは必須)。
  外すと空の実装(`hal/*_Off.cpp`)が入る。**`hal/Sound_Functions.hpp` 等へ関数を足したら空の実装にも足す。`#if PICO_HW_*` を読むファイルはそのヘッダを自分で include する**(`docs/design/devtools.md`)。
- **LovyanGFXの版は `lib_deps` の完全固定の1行が唯一の情報源**(`pc/CMakeLists.txt` がこれを読む。範囲指定に戻すとPCビルドのconfigureが止まる)。
- PC/Web実行は `pc/`(詳細・`--tap`/`--shot`・Wi-Fiの偽装・SPI待ち・`src/`側の決まりは `pc/README.md`、Webは `docs/design/pc-web.md`):
  ```sh
  cmake -S pc -B pc/build && cmake --build pc/build -j
  SDL_VIDEODRIVER=offscreen ./pc/build/picoos_pc --tap 61,65@30:5 --shot md.ppm 250   # FRAMEで(X,Y)をHOLDフレーム押して撮る
  python3 script/ppm2png.py md.ppm md.png 2
  ```
  `src/` は実機と同一。実機ライブラリだけ `pc/compat/` で差し替える(例外は `config/LGFX_Config.hpp` と `hal/Touch_Functions.hpp` の `#if defined(PICOOS_PC)`)。
  **`src/` へ実機依存を足すときは `pc/compat/` にも代替を足す。** GUIの確認は実機の前にPCビルドで。

## ディレクトリ(要点)

```
src/
  main.cpp / OS_Data.hpp(グローバル状態) / consts.hpp / config/
  system/    土台の Xxx_Functions(ログ・設定・時刻・メモリ・入力の列KeyInput/Pad・通知の中身・バックグラウンドの登録簿・クリップボード)
  hal/       ハードの窓口(タッチ・SD・電池・音声・マイク・Wi-Fi・I2C)
  services/  OSの機能(アプリ登録簿・アラーム・タイマー・電源・ロック・明るさ・スクショ・ファーム更新・USBドライブ・端末の鍵・スマホからのWi-Fi設定)
  devtools/  プロファイラ・クラッシュダンプ・debug.cfg・自己診断
  boot/      つなぎ(App_List / Background_List / KeyInput_Dispatch / Notification_Sources)
  gui/core/  GFX(合成と転送)・Font・Widget・Focus・Scene・Keyboard・Error
  gui/widgets/  汎用部品と基底。apps/ dialogs/ keyboards/ interfaces/ systems/ は下の「ウィジェットの置き場所」
  gui/scenes/   機能の中身を持たない画面(設定・時計・ファイル・Lua等)
  apps/<機能>/  1フォルダ=1機能(browser calculator calendar chat dict firmware gb home mail music notes reader ssh store texteditor todo video)。
                直下=中身、ui/=その機能専用の部品、*Scene=画面
  lua/       Lua<->C++バインディング(LuaEngine*、builtin/*.lua、LuaAppScanner、デバッガ)
  iso/ sound/ net/ crypto/ ime/ storage/ task/ util/   機能のライブラリ(util/ は機能を持たない部品だけ)
lib/         vendor: lua(5.4.7) / monocypher / peanut_gb(各 README-pico-os.md)
pc/          PC/Webビルド。pc/sdcard/ がSDの中身(lua/apps/<名前>/main.lua が自動登録されるLuaアプリ)
script/      開発補助(cli-tool/picoos が名前で呼び分ける)。host_test/(ホストテスト)・lua_test/(picoos lua-test)・check_layers.py・generate_icons.py・dict/ 等
docs/        仕様(PROTOCOL / CHAT_PROTOCOL / APP_REPO / MUSIC_FORMAT / VIDEO_FORMAT / FIRMWARE_UPDATE / ZOMBIE_TD / HARDWARE_CHECKLIST)と design/
lua-api-doc/ Lua APIの利用者向けの説明。server/chat/ 自前チャットサーバ
store/       移行中の古いストアの目録(更新しない)。ストアの配布元は別リポジトリ kimu1109/pico-os-apps(lua-apps.md「アプリストアの投稿」)
```

## コアアーキテクチャ

### 層 (`script/check_layers.py`、CIでも回る)

**ファイルは自分と同じ段か下の段のヘッダだけを `#include` してよい。** 段の割り当てと理由付きの例外はスクリプトの中。

| 段 | 置き場所 |
|---|---|
| 0 base | `consts.hpp` `OS_Data.hpp` `config/` `util/`(+少数の個別ファイル) |
| 1 system | `system/` |
| 2 lib | `hal/` `storage` `net` `crypto` `task` `sound` `iso` `ime`、`apps/<機能>/` 直下 |
| 3 ui | `gui/`(`core/` `icons/` `widgets/`)、`apps/*/ui/` |
| 4 service | `services/` `devtools/` `gui/widgets/systems/` `apps/home/`(+`apps/todo/Todo_Watch`) |
| 5 app | `gui/scenes/` `lua/` `apps/store/`、`apps/*/*Scene.*`・`*Scene_*.cpp` |
| 6 boot | `boot/` `main.cpp` |

- 下の段から上を呼びたいときは、下に関数ポインタの差し込み口を置いて `boot/` で差す(`KeyInputFunctions::intercept` 等)か、
  下で宣言した関数を `boot/` で定義する(`AppFunctions::Setup()` → `boot/App_List.cpp` 等)。中身とつなぎを分けるので中身だけでホストテストできる。
- `OS_Data.hpp` は `Widget` を前方宣言で持つだけ。中身が要るファイルは自分で `gui/widgets/Widget.hpp` を読む。
- 例外を足すときは理由を書く。段の割り当てを変えてもホストテストのCMakeは `--sources` を読むので直さなくてよい。
- **新しいアプリの中身(.cpp)は `apps/<機能>/` に作る。**

### Xxx_Functions とグローバル状態

- 機能単位は「`XxxFunctions` 名前空間 + `inline` 変数/関数」(クラス化しないシングルトン的パターン)。役割はファイル名と各 `docs/design/` で引く。
- `OSData`(`OS_Data.hpp`)はタッチ・`lcd`/`frame`・`SD`・キーボード3種などのハブ。**`lcd` と `SD` だけ `extern` で `OS_Data.cpp` に定義**
  (作る処理の要る `inline` 変数は、読む `.cpp` ごとに初期化コードが数百バイトずつフラッシュへ入るため。同種の変数を増やすときも同じ注意)。
- よく使う共通口: 失敗をユーザーへ見せる `ErrorFunctions::ShowFatal()` / 設定の1キー書き換え `PICO_Config::SetValue()` /
  シーン遷移 `SceneFunctions` の Change/Push/Pop(フレーム境界まで保留) / ウィジェットの遅延削除 `WidgetFunctions::DestroyLater()`。

### ループと2コア

- `setup()` で各 `Setup()` を呼び、Statusbar と最初のシーンだけ `new`。画面ごとのウィジェットは各 `Scene::onEnter()` が作る。
- `loop()` は単純なポーリング: タッチ → I2C/Pad → 明るさ/電源 → `SceneFunctions::Update()` → `KeyInputFunctions::Update()` →
  `WidgetFunctions::UpdateAll()` → `GFX::FlushDirty()` → 各サービスの更新。区間ごとの `ProfilerFunctions::Mark()` はプロファイラとクラッシュダンプのパンくず。
- **2コア目(`loop1()`)は音声専用**(音源・マイク)。1コア目とは `std::atomic` とロック無しの列だけでやり取りする。
  **2コア目からログ・ウィジェット・SD・`OSData` に触らないこと。**

## Widgetシステム

- 基底 `src/gui/widgets/Widget.hpp`: `l_rect`、親は生ポインタ+仮想 `getChildren()`、`visitAll()`、`needsRender()`/`markdirty()`、
  `causeOnPress{Start,Move,End,Out}`。詳細とカタログは `docs/design/gui.md`。
- `disable_markdirty`: 親が描画を一括保証する場合の子のmarkdirty無効化(**乱用厳禁**)。
- `hit_transparent`: `WidgetFunctions::Add()` は子孫も全部積むので、**表示用に置いただけの子が親のタップを奪う**。表示専用の子にはこれを立てる。
- 格子状・多ボタンのUIは部品ごとに `Button` を `new` せず、`render()` で直接描いてタップ位置から逆算する(`AppGrid`/`KeyboardNum`/`TabBar` 等)。
- 新しい画面は `Scene` を継承して `onEnter()` で生成。常駐ウィジェットは `AddOverlay()`。新規ダイアログは既存の骨格
  (`children_` 保持、`setOnClose`、`setVisible(false)` で終了)に合わせる。ファイル選択/保存/色選択のダイアログは既存のものを拡張する。

### ウィジェットの置き場所

| 置き場所 | 中身 |
|---|---|
| `gui/widgets/` 直下 | どのアプリからでも使える汎用部品と基底(`Widget`/`WidgetID`/`WidgetRegistry`) |
| `gui/widgets/apps/` | 用途は決まっているが複数のアプリ・Luaから使う部品(`MarkdownView`/`FileExplorer`/`SeekBar` 等) |
| `src/apps/<機能>/ui/` | **1つのアプリ専用**の部品・ダイアログ。**迷ったらここ** |
| `gui/widgets/systems/` | OSのシェル部品(`Statusbar`/`PerfOverlay`/`NotificationToast`/`LockScreen`) |
| `gui/widgets/dialogs/` `keyboards/` `interfaces/` | モーダルダイアログ / キー盤3種と基底 / ミックスイン |

- 汎用の基準は「役割が特定の画面に紐付いていないこと」(使う箇所の数ではない)。
- **include は常に `src/` 起点の絶対パス。** PlatformIO・PCビルド・ホストテストは `src/**.cpp` を拾うので移動してもビルド定義は不要。
  **`run_net.sh`/`run_mem.sh` だけはソースを1本ずつ並べている**ので、そこに載るファイルを移したら直す。移したら `check_layers.py` を回す。

### メモリ管理(相談時の大前提)

- 基本は各ウィジェットが子を `new`、デストラクタで `delete`。**子の解放は親のデストラクタの責任**
  (`ClearSceneWidgets()` はルートしか消さないので、デストラクタが無いと丸ごとリークする。過去に複数回発生)。
- **`MarkdownView` はオブジェクトプール方式**(起動時に固定長配列を確保し、スロットを使い回す)。
  **開発者は断片化回避のため固定長バッファ/プールを志向している。安易な `new`/`delete` 追加よりこのパターンに寄せた提案を優先する。**
- `Widget::operator new/delete`(`Widget.cpp`)が全ウィジェットの確保の唯一の入口(今は `malloc` + `MemFunctions` への通知)。
- ウィジェットIDはLua等の外部向け。OS内部の参照は生ポインタ。

### 文字列

- **Arduino `String` はどこでも使っていない。復活させないこと。** 文字列は `util/FixedString.hpp` の `FixedString<N>`(ヒープ非使用)。
- `assign`/`append` は切り詰めを `bool` で返す(無視しない)。`buf_` を変えるメソッドは必ず `len_` を追従更新する(不変条件)。
- UTF-8の文字単位APIは FixedString 側(`charCount()`/`insertAtChar()` 等)。`system/UTF8_Functions.hpp` はエンコード/デコードだけ。
  **`FixedString.hpp` から `UTF8_Functions.hpp` を include しない**(循環回避。`util/Utf8Byte.hpp` だけ使う)。
- 長さは `consts.hpp` のプリセット(`PICO_STR_S/M/L/LL`、`256B`〜`32KiB`、`PICO_PATH_LEN=255`)から選ぶ。
- パスは `storage/SD_IO.hpp` の `join()`/`parent()`/`filename()`/`normalize()`/`resolve()`(ルートを超える `..` は捨てる)。

## コーディング上の慣習

- **コメント・ログは日本語、識別子は英語。**
- 定数は「クラス内 `constexpr static`」と「`consts.hpp` の `#define`」が混在。
- アイコンは `script/generate_icons.py` が `tabler_icons/` と `custom_icons/` から `gui/icons/icons_data.h` を生成。
  **tablerは16pxで細い線が消えることがあるので生成後に目視**し、破綻したら `custom_icons/` へ描き起こす(基準は `script/custom_icons/README.md`)。
- 「状態の否定」は専用アイコンを作らず、基底アイコンに `IconID::X` を `PICO_RED` で重ねる(SD無し・Wi-Fi圏外)。
- ライセンスは MIT(`LICENSE`、著作権者 Kimu1109)。**他者のコード・データ・素材を同梱したら `THIRD_PARTY_LICENSES.md` に1行足す**(GPL等の継承型はファームに組み込まずSDのデータに留める)。
- 物理キーボードを扱う画面は `Scene::onKey()` を実装する。外部コントローラーの入力元は `PadFunctions` に `Source` を足す(`docs/design/input.md`)。

## テストとCI

- `sh script/host_test/run.sh`(CMake + CTest、ASan、約80本。`run.sh core lua-engine` でグループ指定、`--list`、`-j N`)。
  テストを足すときは `script/host_test/CMakeLists.txt` の一覧に `host_test(<名前> GROUP <グループ>)` を1行。説明と落とし穴は `script/host_test/README.md`。
- `run_net.sh`(実通信)/ `run_mem.sh`(確保回数)/ PCビルドはCIに無いので、**変更のたびに自分で回す**。
- PR時のCI: ホストテスト(`host-tests.yml`。**グループを足したらmatrixにも足す**、ずれると `groups-in-sync` が落ちる)・層の決まり・
  実機ファームのビルド(`firmware-build.yml`)。他にWebビルドの公開(`web-pages.yml`)と、毎晩mainの実機ファームをリリースへ載せるナイトリー(`nightly.yml`、50本まで)。
- シェルで終了コードを取るときは `rc=0; cmd || rc=$?` の形にする(`if ! cmd; then rc=$?` 等は値が化ける)。

## 設計記録の索引 (`docs/design/`)

ここで足りない話は該当ファイルを開く。コードや文書中の「CLAUDE.md「節の名前」」「下記「…」」という参照は、
`grep -n '^#.*節の名前' docs/design/*.md` で引ける(節の名前は移したときのまま)。

| ファイル | 中身 |
|---|---|
| `gui.md` | ウィジェットカタログ・ウィジェットID・アプリの枠組み・シーン・ダイアログ・オンスクリーンキーボード |
| `input.md` | 外部コントローラー・物理キーボード・フォーカス・かな漢字変換・CardKB2・USBドライブ |
| `power-display.md` | 画面の明るさと自動調光・スリープ・時刻の手動設定・電源メニュー |
| `os-services.md` | アラーム・通知・ホーム画面のウィジェット・バックグラウンド実行・スクリーンショット・タイマー・ダークテーマ・画面ロック |
| `battery-touch.md` | バッテリー残量・音割れ対策・タッチの補正とノイズ抑制 |
| `security-wifi.md` | 認証情報の暗号化・端末ごとの鍵(OTP)・保存済みWi-Fi・新規接続・固定IP/DNS・mDNS・キャプティブポータル・AP設定モード |
| `browser-net.md` | MarkdownView・文書キャッシュ・HTTP/HTTPS・取得〜表示・サーバ情報・検索・マニフェスト |
| `apps-online.md` | カレンダー(iCalendar)・チャット・TODO(Todoist)・メール(IMAP)・Spotify・ニュース(XML) |
| `apps-local.md` | テキストエディタ・メモ(タグ・全文検索・最近開いたファイル)・SSH・ファイルビューワー・読書(青空文庫)・電卓・時計 |
| `sound.md` | 音声出力・マイク・ステレオ・曲データ(MML)・チップチューン・ゲームボーイの音・WAV・出力周波数・ミュージック・動画(.pvid) |
| `gameboy.md` | ゲームボーイ |
| `lua.md` | Luaバインディングの土台(ライフサイクル・権限・コールバック中継・LuaScene・メモリ・暴走防止・自動登録) |
| `lua-api.md` | `pico.*` APIの設計記録(描画・SD・画像・キャンバス・ダイアログ・ネットワーク・イベント等) |
| `lua-apps.md` | Luaアプリと `pico.game`(ペイント・テトリス等・アプリストア・動作確認アプリ) |
| `iso.md` | 2.5Dエンジン(`pico.iso`)・ブロック・ゾンビTD |
| `devtools.md` | プロファイラ・Luaデバッガ・クラッシュダンプ・サンドボックス・実機確認の道具・ファームの更新・ビルド時に選ぶハードウェア機能・ナイトリービルド |
| `performance.md` | 最適化とメモリ計測の記録・解決済みの不具合 |
| `pc-web.md` | Webビルド(WebAssembly) |
| `roadmap-ideas.md` | ロードマップと未実装の設計アイデア |

## Claude Codeへの申し送り

### 人にしかできない確認はTodoistで管理する(2026-10-19に利用者と決めた運用)

実機・実物のハードを人が操作しないと確かめられない確認が生じたら、**確認を取らずに** Todoist の MCP(`mcp__Todoist__*`)で登録する。
**未完了かどうかの唯一の情報源はTodoist**。`docs/HARDWARE_CHECKLIST.md` は手順・見方・結果の記録用(進捗の印は付けない)。
コネクタが無ければ、チェックリストへ手順を足し、報告で項目名を挙げる。

- **対象**: 実機ビルド、I2S/PWM/OTP/USB、Wii・CardKB2・マイクの実物、実機の速さ・メモリ・消費電流、本物のサーバ/証明書への接続。
  ホストテストやPCビルドの `--tap`/`--shot` で自分が確かめられるものは入れない。
- **置き先**: プロジェクト「pico-os 実機確認」(ID `6hp9GRmfPXH65cqM`。無ければ `find-projects` で探し、無ければ作る)。
  入れる前に `find-tasks`(`projectId`+`searchText`)で重複を調べ、関連があれば `update-tasks` で説明に追記。
- **形**: 題名は確認内容を短く(例: `マイク(SPH0645)録音確認`)。説明に ①手順と期待結果 ②出典(チェックリストの章や `docs/design/` の節)
  ③関連PRのURL ④クラウドセッションなら `セッション: https://claude.ai/code/<session_id>`(`get_session` で取る。ローカルなら省く)。
  ラベルは分野(ビルド/クラッシュダンプ/画面・電源/入力/ネットワーク/音/計測/見た目・その他)。優先度は先に通す必要があるものだけp2、期限なし。
- 設計記録には「実機では未確認(短く)(Todoist参照)」と書いてよい。長い確認項目はTodoistとチェックリストにだけ置く。
- **完了**: 利用者が確認できたと言ったときだけ、①タスクを完了 ②チェックリストの `→` 欄へ日付つきで結果 ③`docs/design/` の該当を「実機で確認済み(日付)」へ。

### 文書の書き方

- **新しい機能の設計記録は `docs/design/` の該当ファイルへ**(合うものが無ければ足して上の索引へ1行)。このファイルには地図と決まりだけ。
- **`SUMMARY.md` は項目名だけの一覧(説明は `docs/design/` へ)。新しい大項目を足したら冒頭の「全体の進捗」表にも1行。**
