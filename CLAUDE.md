# CLAUDE.md — pico-os プロジェクトコンテキスト

> このファイルは `Kimu1109/pico-os` リポジトリ直下に置く、Claude Code向けのプロジェクト背景資料。
> 元はClaude.aiのProject knowledgeとして管理されていた内容(2026-09-06時点情報)を統合したもの。
> **一次情報源は常にこのリポジトリのコードと `SUMMARY.md`。このファイルは「相談の前提を素早く掴むための地図」であり、
> 実装と乖離があれば実コード側を信じること。**

## プロジェクト概要

Raspberry Pi Pico 2 W (RP2350, `rpipico2w`) 上で動く自作タッチGUI OS。PlatformIO + Arduinoフレームワークで書かれたC++プロジェクト。
過去に一度スクラップ&リビルドしており、現行版はSerenityOS的な設計思想を参考にしつつ、タッチ操作の組み込みGUIフレームワークを自前実装している。

- リポジトリ: https://github.com/Kimu1109/pico-os/tree/main
- TODO一次情報源(チェックボックス形式): https://raw.githubusercontent.com/Kimu1109/pico-os/refs/heads/main/SUMMARY.md
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
| バックライト | TFT_LED=22 |
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
    lovyan03/LovyanGFX@^1.2.26
    https://github.com/PaulStoffregen/XPT2046_Touchscreen.git#v1.4
monitor_speed = 115200
```
モニタはUTF-8。

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
    widgets/                各ウィジェット実装
      dialogs/              モーダルダイアログ
      interfaces/            ミックスイン的インターフェース
      systems/               Statusbar等システムウィジェット
  ime/                       SKK方式かな漢字変換辞書エンジン
  model/Rect.hpp              矩形構造体
  storage/                    SDカードI/O・パス定数
  task/                       非同期タスク基底 + NetworkScanタスク
  test/                       フォントカバレッジチェック等
script/                       開発補助スクリプト(アイコン生成/SKK辞書変換/pimg生成等, Python)
examples/doc.md                MarkdownView動作確認用サンプル文書
```
`include/`, `lib/`, `test/` はPlatformIO標準雛形ディレクトリで未使用(README以外中身なし)。

## コアアーキテクチャ

### OSData (`src/OS_Data.hpp`)
グローバル状態ハブ。`inline`変数として: タッチ状態(touchX/Y/Z, isTouched, isTouchStart/End/Move)、グラフィック(`LGFX* lcd`, `LGFX_Sprite* frame`)、SD(`SdFat SD`, SD_usable)、日本語/英語キーボードへのポインタ(keyboard_jpn, keyboard_eng)を保持。

### `Xxx_Functions` サブシステム (`src/functions/`)
各機能は名前空間+`inline`変数/関数のシングルトン的パターン(クラス化しない)。

| モジュール | 役割 |
|---|---|
| GFX_Functions | LovyanGFX初期化、ダーティリージョン管理(`dirtyRects`)、`FlushDirty()`で差分描画 |
| Widget_Functions | ウィジェット/ダイアログの登録・削除・毎フレーム更新・当たり判定の中枢 |
| Touch_Functions | XPT2046からのタッチ座標取得 |
| Task_Functions | `Task`のリスト管理・毎フレームupdate |
| Network_Functions | Wi-Fi非ブロッキング接続・スキャン(Task化)・NTP同期・電波強度アイコン |
| IME_Functions | SKK辞書ベース変換候補検索 |
| Keyboard_Functions | 日/英オンスクリーンキーボードの入力ルーティング |
| Font_Functions | U8g2フォントサイズ切替(Small16px/Normal24px/Big32px/Bigger48px) |
| SD_Functions | SDカード初期化 |
| Config_Functions | `key=value`形式の設定ファイルパーサ |
| Log_Functions | システムログ(LOG_SYS_OK/WARN/FAIL/MSG) |
| Time_Functions | 時刻管理(NTP同期後) |

### 起動・ループ (`main.cpp`)
`setup()`: GFX→SD→Log→Touch→Task→Network→Keyboard→IME→Time→Testの順にSetup()を呼び、Statusbar・FileExplorer・MarkdownView・各種ダイアログを生成して`WidgetFunctions`へ登録。

`loop()`: Touch更新 → `WidgetFunctions::UpdateAll()` → `GFX::FlushDirty()` → Task/Log/Time/Network更新、という単純なポーリングループ。**画面遷移やシーン管理の仕組みは無く、全ウィジェットをフラットにmain.cppで直接newして常駐させている。**

## Widgetシステム

### 基底クラス (`src/gui/widgets/Widget.hpp`)
- `l_rect`(ローカル矩形)/ `prev_l_rect` を保持。親子は生ポインタ(`Widget* parent`)+ 仮想関数 `getChildren()`。
- `visitAll(F&&)` で自分+全子孫を再帰走査。
- 描画モード `WidgetTools::RenderMode { OPAQUE, CLEAR, TRANSLUCENT }`。
- 座標系: `getLocalRect()` / `getScreenRect()` / `getScreenClipRect()`。
- タッチ: `hitTest(px,py)`、`causeOnPress{Start,Move,End,Out}` コールバック(`std::function<void()>`)。
- 再描画: `needsRender()` / `markdirty(Rect)`。
- `disable_markdirty`: 親が描画反映を一括保証する場合の子markdirty無効化フラグ(**乱用厳禁、バグりやすい**)。

### ウィジェットカタログ
Button / Label / Textbox(Labelを継承、単一行/複数行対応の入力欄) / Checkbox / Icon(tabler_icons由来、`IconSize`指定) / Image / NumberSlider / ScrollContainer / ScrollList / CanvasRaster(ピクセル単位描画) / DropdownMenu / FileExplorer(SDのファイル一覧・作成/削除/選択、`currentPath`はchar[128]) / MarkdownView(最も作り込まれたウィジェット) / Statusbar。

### ダイアログ (`src/gui/widgets/dialogs/`)
`WidgetFunctions`内で`dialog_roots`という独立リストで管理(当たり判定・描画順ともに最優先)。共通の骨格: 「`children_`ベクタで子を保持」「`setOnClose(std::function<void(bool is_ok)>)`で結果通知」「`setVisible(false)`で自身を隠して終了」。**新規ダイアログを提案する際はこの型に合わせる。**

- `MsgDialog`: メッセージ+アイコン+OK/キャンセル。`RenderMode::TRANSLUCENT`。
- `InputDialog`: ラベル+テキスト入力+決定/キャンセル(単一行/複数行切替可)。
- `FileSaveDialog`: `FileExplorer`+ファイル名`Textbox`+OK/キャンセル。**保存専用**。
- `FileSelectDialog`: `FileExplorer`+OK/キャンセルのみ。**選択専用**(ファイル名欄なし)。
  - ※旧設計では1クラスで兼用予定だったが、実装では保存/選択で別クラスに分離された。
- `ColorDialog`: 実装済み(直近コミット)。4×4=16色グリッド(`getIndexToColor(x,y)=x+y*4`)+OK/キャンセル。`selected_color`(未選択-1)、`getSelectedColor()`。
- `Keyboard` / `KeyboardEng`: オンスクリーンキーボード。**数字専用(電卓用)キーボードは未実装**。

### メモリ管理方針(重要・相談時の大前提)
- **基本は各ウィジェットが`new`で子生成、デストラクタで`delete`する素朴な方式。**
- **例外: `MarkdownView`だけは既にオブジェクトプール方式**: `labelPool`/`imagePool`/`checkboxIconPool`という固定長配列を起動時に一度だけ確保し、`boundXxxBlock[]`でスロット使用状況(-1=未使用)を管理、スクロールに応じて使い回す。→ 「事前確保→使い回し、全消去時は中身クリアのみ」構想の**実例プロトタイプ**。
- `WidgetFunctions::DestroyLater()` + `pending_deletes`: 非表示化→次フレーム末尾で`ProcessPendingDeletes()`によりまとめてdelete(フレーム途中delete事故防止)。SUMMARY.md「ウィジェットのメモリ解放」チェック済み項目に相当。
- **開発者はRAM断片化回避のため固定長バッファ/オブジェクトプールを志向している。新規実装で`new`/`delete`を安易に増やす提案より、MarkdownViewのプールパターンに寄せた提案を優先すること。**
- ウィジェットIDベース管理(32bit: 種別enum/generation/index)は未実装。現状は`std::vector<Widget*>`+生ポインタ直接参照。

### MarkdownView 実装詳細
`MdBlockType`: H1/H2/H3/Paragraph/Image/Link/CodeBlock/ListItem/HorizontalRule/Quote/TableRow。`MdBlock`はオフセット/長さ参照方式(`srcOffset`/`srcLength`、Stringをコピーせず範囲参照)。固定上限: `kMaxBlocks=128`, `kMaxSourceBytes=16384`, `kLabelPoolSize=16`, `kImagePoolSize=2`, `kMaxListLevels=6`, テーブル最大列`kMdTableMaxCols=4`。テーブル/水平線/引用バーは`Label`を介さず`frame`へ直接描画(`renderDecorations()`)。リンクタップ用`on_link_tap`あり。フロントマターは`skipFrontMatter()`で読み飛ばし。**ヘッダー/フッター機能は現状なし。**

### 文字列の扱い
`Label`/`Textbox`/`MarkdownView::doc_text`はArduino `String`(可変長)。一方`Network_Functions::currentSSID`(char[33])、`IME_Functions::candidates[][IME_MAX_CAND_BYTES]`、`FileExplorer::currentPath`(char[128])はCスタイル固定長`char`配列。**共通の`FixedString`的クラスはまだ存在しない**(テンプレートで長さ指定、`strncpy`ベースの安全な代入/比較演算子などが構想段階)。

## コーディング上の慣習

- コメント・ログメッセージは日本語、コードは標準的な英語命名。
- 機能単位は「`XxxFunctions`」名前空間+`inline`変数/関数(クラス化せずシングルトン的に扱う)。
- get/setアクセサ+`needsRender()`呼び出しの定型パターンが各ウィジェットで繰り返される。
- 定数は「クラス内`constexpr static int`」と「`consts.hpp`に`#define`集約」の二系統が混在。
- アイコンは`script/generate_icons.py`でtabler_iconsから`icons_data.h`を事前生成(ビルド前処理)。
- 日本語IMEはSKK辞書方式、`script/convert_skk_dict.py`で辞書データ(`skk_body.tsv`/`skk_index.tsv`)をSD収録用に変換。

## ロードマップ・TODO状況(2026-09-06時点)

相談が来た際はまず本表を見て、「既存機能の拡張」か「ゼロから設計する新機能」かを見分けること。**都度 `SUMMARY.md` をfetchして最新状況を確認するのが望ましい。**

| # | 旧TODO大項目 | 状況 |
|---|---|---|
| 1 | ダイアログ(ファイル選択・保存・色選択) | **全て実装済み(betaレベル)**。上記ダイアログカタログ参照。数字専用キーボードのみ別TODOとして未着手。 |
| 2 | スクリーン管理 | 一部進展(`DestroyLater`等のメモリ解放は実装済み)。**シーン遷移・画面スタック・パネル/グリッドレイアウト・メモリプール化は未着手**。ゼロから設計相談になる。 |
| 3 | Wi-Fi管理強化 | 基礎は実装済み(非ブロッキング接続・スキャン・NTP同期・電波強度アイコン)。**定期的再接続交渉・確実な時刻同期の強化は未着手**。 |
| 4 | Luaアプリ/API | **未着手**。Lua関連コード皆無。ゼロから統合方針(実装選定、C++バインディング設計)を相談する必要あり。PCエミュレーション環境(LovyanGFX/タッチ操作代替)も未着手。 |
| 5 | 標準/セカンダリアプリ開発 | **未着手**。設定アプリ・時計・辞書・電卓・チャット・オセロ/テトリス風・シューティング・ブロック崩し・リマインダー・カレンダー等、アプリ本体コードなし(部品は存在)。 |
| 6 | GBエミュ | **未着手**。 |
| 7 | 外部コントローラー | **未着手**。GPIO/UART連携コードなし(タッチのみ)。 |
| 8 | Chiptune音声再生 | **未着手**。音声出力・PWM/I2S関連コードなし。 |

## 未実装の設計アイデア(旧pico-osからの持ち越し議論)

- **固定長文字列クラス**: `char[N]`をラップし`strncpy`ベースの安全な代入/比較演算子を持つテンプレートクラス。既存の生`char[]`箇所(SSID/パス/辞書候補等)を置き換える用途。
- **ウィジェットのメモリプール化(汎用)**: `MarkdownView`のプール実装を`WidgetFunctions`本体や他ダイアログにも一般化する方向性。placement newベースの汎用プールアロケータが今後のテーマ。
- **Markdownブラウザのヘッダー/フッター**: `l_rect`内でのヘッダー/フッター分の高さ控除、スクロール対象外の固定描画領域追加が論点。
- **LuaでのウィジェットID管理**: 32bit整数ID(上位バイトから ウィジェット種類(enum)/generation/index)でLua側から実体へ安全アクセス。Lua統合自体が未着手のため、種類enum整理・generationカウンタ追加・index⇔ポインタ変換テーブルの新設が必要。Lua組み込み設計と合わせて相談されることが多い。

## Claude Codeへの申し送り

- 組み込み制約(RAM/Flash)を常に意識し、PC向けC++の常識をそのまま持ち込まない。
- 固定長バッファ/オブジェクトプール志向を優先し、安易な`new`/`delete`追加は避ける(MarkdownViewパターンを参照)。
- ダイアログ系(ファイル選択/保存/色選択)は実装済みなので車輪の再発明をせず、既存クラス(`FileSaveDialog`/`FileSelectDialog`/`FileExplorer`)を拡張する形で提案する。
- Lua組み込み・スクリーン管理・GBエミュ・外部コントローラ・Chiptune再生は土台が無いため、ゼロから設計相談する前提で臨む。
- 新規ダイアログ/ウィジェットは既存の骨格(`children_`保持、`setOnClose`コールバック、`setVisible(false)`終了)にトーンを合わせる。
- コメント・ログは日本語、識別子は英語という言語使い分けを踏襲する。
- 判断に迷ったら `SUMMARY.md`(https://raw.githubusercontent.com/Kimu1109/pico-os/refs/heads/main/SUMMARY.md)と実コードを突き合わせて確認する。
