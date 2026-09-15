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
    scenes/                 Scene基底と各画面(HomeScene/MarkdownScene/InputTestScene)
    widgets/                各ウィジェット実装
      dialogs/              モーダルダイアログ
      interfaces/            ミックスイン的インターフェース
      systems/               Statusbar等システムウィジェット
  ime/                       SKK方式かな漢字変換辞書エンジン
  model/Rect.hpp              矩形構造体
  util/                       FixedString(固定長文字列) / Utf8Byte(UTF-8リードバイト判定)
  storage/                    SDカードI/O・パス定数
  task/                       非同期タスク基底 + NetworkScanタスク
  test/                       フォントカバレッジチェック等
script/                       開発補助スクリプト(アイコン生成/SKK辞書変換/pimg生成等, Python)
  tabler_icons/               アイコン元データ(tabler由来のSVG)
  custom_icons/               アイコン元データ(自作SVG)。tablerが16pxで破綻する場合の受け皿
  host_test/                  PCで実コードを動かす検証(run.sh=ASanで解放漏れ検出 / run_mem.sh=確保回数の計測)
pc/                            PC/Web実行用ビルド(CMake + SDL2 / Emscripten)。`src/`は実機と同一のまま使う
  compat/                     実機ライブラリの代替ヘッダ(Arduino/SPI/WiFi/SdFat/LGFX設定/タッチ)
  web/shell.html              Webビルドのページの外枠(canvas + ログ + デバッグ用ボタン)
  sdcard/                     SDカードとして読まれるディレクトリ
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
| Scene_Functions | シーン(画面)の遷移管理。Change/Push/Popをフレーム境界まで保留して適用 |
| Config_Functions | `key=value`形式の設定ファイルパーサ |
| Log_Functions | システムログ(LOG_SYS_OK/WARN/FAIL/MSG) |
| Time_Functions | 時刻管理(NTP同期後) |
| App_Functions | アプリ登録簿(`App_List.cpp`が一覧、`App_Functions.cpp`が仕組み) |
| Mem_Functions | ヒープ計測(`mallinfo`ベース)。シーンごとの使用量レポート |
| UTF8_Functions | UTF-8のエンコード/デコード(文字列操作は`FixedString`側の担当) |
| HitBox_Functions | 当たり判定のヘルパ |
| Test_Functions | フォントカバレッジ等の起動時セルフチェック |

### 起動・ループ (`main.cpp`)
`setup()`: GFX→SD→Log→Touch→Task→Network→Keyboard→IME→Time→Testの順にSetup()を呼び、Statusbar・FileExplorer・MarkdownView・各種ダイアログを生成して`WidgetFunctions`へ登録。

`loop()`: Touch更新 → `SceneFunctions::Update()`(保留中のシーン遷移の適用) → `WidgetFunctions::UpdateAll()` → `GFX::FlushDirty()` → Task/Log/Time/Network更新、という単純なポーリングループ。

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

### ウィジェットカタログ
Button / Label / Textbox(Labelを継承、単一行/複数行対応の入力欄) / Checkbox / Icon(tabler_icons由来、`IconSize`指定) / Image / NumberSlider / ScrollContainer / ScrollList / CanvasRaster(ピクセル単位描画) / AppGrid(ランチャのアプリタイル) / DropdownMenu / FileExplorer(SDのファイル一覧・作成/削除/選択、`currentPath`はchar[128]) / MarkdownView(最も作り込まれたウィジェット) / Statusbar。

### アプリの枠組み (`src/functions/App_Functions.hpp`)
`AppEntry`(名前/アイコン/シーン生成関数)の固定長テーブルに登録し、`HomeScene`の`AppGrid`がそれを並べる。

- **アプリを増やすときに触るのは `src/functions/App_List.cpp` の `Setup()` に1行足すだけ**。シーン側にも`HomeScene`にも手を入れない。
- 仕組み(`Register`/`Launch`/`Get`)は`App_Functions.cpp`、載せるアプリの一覧は`App_List.cpp`に分けてある(前者はシーン実装に依存しないのでホストテストが軽い)。
- 生成関数は`std::function`ではなく素の関数ポインタ。`&AppFunctions::MakeScene<XxxScene>`の形で渡す(登録簿を確保ゼロの静的テーブルに保つため)。
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
- 実装例: `HomeScene`(ランチャ) / `MarkdownScene` / `InputTestScene`。
- ホスト側の検証: `sh script/host_test/run.sh`(実コードをPCのg+++ASanで動かし解放漏れを検出。実機ビルドとは独立)。

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
- **`Widget::operator new/delete`が全ウィジェットの確保の唯一の入口**(`src/gui/widgets/Widget.cpp`)。現状は`malloc`を呼ぶだけで`MemFunctions`へ量を通知する。将来アリーナを入れる場合はここの実装を差し替えるだけで済み、`new Button(...)`のような既存コードは書き換え不要。
- **子ウィジェットの解放は親のデストラクタの責任**。`WidgetFunctions::ClearSceneWidgets()`は親を持たないルートしか`delete`しないので、子を`new`するウィジェットにデストラクタが無いと丸ごとリークする(過去に`MarkdownView`/`ScrollList`/`CanvasRaster`で発生)。
- ウィジェットIDベース管理(32bit: 種別enum/generation/index)は未実装。現状は`std::vector<Widget*>`+生ポインタ直接参照。

### MarkdownView 実装詳細
`MdBlockType`: H1/H2/H3/Paragraph/Image/Link/CodeBlock/ListItem/HorizontalRule/Quote/TableRow。`MdBlock`はオフセット/長さ参照方式(`srcOffset`/`srcLength`、`doc_text`をコピーせず範囲参照)。固定上限: `kMaxBlocks=128`, `kMdMaxSourceBytes=8192`, `kMdBlockTextBytes=512`(1ブロックの表示テキスト上限。日本語で約170文字), `kLabelPoolSize=16`, `kImagePoolSize=2`, `kMaxListLevels=6`, テーブル最大列`kMdTableMaxCols=4`。`kMdBlockTextBytes`と`kMdMaxSourceBytes`はクラス外定義(クラス外に書くメンバ関数定義の戻り値型はクラススコープより前に解決されるため)。上限に当たった場合は`load()`が警告ログを出す。画像は`onRAM=false`でSDからストリーミング描画する(RAMに載せると占有量が開いた文書次第で青天井になるため)。テーブル/水平線/引用バーは`Label`を介さず`frame`へ直接描画(`renderDecorations()`)。リンクタップ用`on_link_tap`あり。フロントマターは`skipFrontMatter()`で読み飛ばし。**ヘッダー/フッター機能は現状なし。**

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
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc --shot shot.ppm 40   # ヘッドレス確認

# Web(WebAssembly)。前提: emsdk(SDL2はemscriptenのportsが持つので不要)
emcmake cmake -S pc -B pc/build-web -DCMAKE_BUILD_TYPE=Release
cmake --build pc/build-web -j
emrun --no_browser --port 8080 pc/build-web    # → http://localhost:8080/index.html
```

- **`src/` のコードは実機とまったく同じものを使う**。差し替えているのは実機ライブラリだけで、
  `pc/compat/` をインクルードパスの先頭に置いて `Arduino.h`/`SPI.h`/`WiFi.h`/`SdFat.h`/
  `XPT2046_Touchscreen.h` を置き換える(`script/host_test/stubs` と同じ考え方)。
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
  | `Serial.*` | 5 | 標準出力へ |
  | `pinMode()` | 4 | 空実装 |
  | `map()` | 2 | そのまま計算 |
  | `digitalWrite()`/`digitalRead()` | 各1 | 空実装 / 常に`HIGH` |

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
- GPIO/SPIは空実装。
- LovyanGFXはCMakeが取得する(1.2.28)。`-DLOVYANGFX_DIR=...` で手元のソースも使える。
  **`platformio.ini` の版を上げたら `pc/CMakeLists.txt` の `GIT_TAG` も追随させること。**
- `pc/build/` は `.gitignore` 済み。
- **漏れはビルドで検出できる**。`src/*.cpp` を全部リンクするので、代替を用意し忘れた実機APIが
  あれば未定義参照になる。逆に言えば、`src/`へ新しい実機依存(`analogRead`/I2C等)を足すと
  PCビルドが即座に壊れて気づける。

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
- **Wi-Fiの疎通判定**: ソケットが無いので `navigator.onLine` を見る(`compat/WiFi.h`)。
- **`delay()`**: 待つとタブが固まるのでWebでは即座に戻る(`src/` は使っていない)。
- **ページの外枠**: `pc/web/shell.html`(emscriptenの `--shell-file`)。canvas・ログ欄・
  Wi-Fi状態の切替・画面のPNG保存ボタンを持つ。デバッグ用の道具を足すならここ。
- **`src/`へ新しい依存を足すときはWebビルドも通すこと**。ネイティブが通ってもemscriptenで
  落ちる依存(生ソケット/スレッド/ブロッキング待ち)があるため。
- **公開**: `.github/workflows/web-pages.yml` が `main` へのpushで
  https://kimu1109.github.io/pico-os/ へ自動デプロイする(プルリクではビルド確認のみ)。
  emsdkの版はワークフローの `EMSDK_VERSION` で固定。公開中のコミットはページのログ先頭の
  `[WEB] pico-os build: <hash>` で分かる。

## ロードマップ・TODO状況(2026-09-13時点)

相談が来た際はまず本表を見て、「既存機能の拡張」か「ゼロから設計する新機能」かを見分けること。**都度 `SUMMARY.md` をfetchして最新状況を確認するのが望ましい。**

| # | 旧TODO大項目 | 状況 |
|---|---|---|
| 1 | ダイアログ(ファイル選択・保存・色選択) | **全て実装済み(betaレベル)**。上記ダイアログカタログ参照。数字専用キーボードのみ別TODOとして未着手。 |
| 2 | スクリーン管理 | メモリ解放(`DestroyLater`)・パネル/グリッドレイアウト(`LayoutContainer`/`GridContainer`)・**シーン遷移+画面スタック(`Scene`/`SceneFunctions`)は実装済み**。**メモリプール化(汎用)は計測の結果いったん保留**(下記「メモリ計測の結論」参照)。 |
| 3 | Wi-Fi管理強化 | **実装済み**。非ブロッキング接続・スキャン・NTP同期・電波強度アイコンに加え、`SUCCESS`中は`HEALTH_CHECK_INTERVAL=5000ms`ごとに`WiFi.status()`を確認し、切断を検知したら`ConnectWiFiAsync()`を呼び直す(`currentPassword`を再接続用に保持)。 |
| 4 | Luaアプリ/API | **未着手**。Lua関連コード皆無。ゼロから統合方針(実装選定、C++バインディング設計)を相談する必要あり。**PC実行環境は実装済み(`pc/`、下記参照)**。 |
| 5 | 標準/セカンダリアプリ開発 | **未着手**。設定アプリ・時計・辞書・電卓・チャット・オセロ/テトリス風・シューティング・ブロック崩し・リマインダー・カレンダー等、アプリ本体コードなし(部品は存在)。 |
| 6 | GBエミュ | **未着手**。 |
| 7 | 外部コントローラー | **未着手**。GPIO/UART連携コードなし(タッチのみ)。 |
| 8 | Chiptune音声再生 | **未着手**。音声出力・PWM/I2S関連コードなし。 |

## 未実装の設計アイデア(旧pico-osからの持ち越し議論)

- **ウィジェットのメモリプール化(汎用)**: 実測の結果、現時点では保留と判断した(下記「メモリ計測の結論」)。再開する場合は`Widget::operator new/delete`をアリーナへ差し替えるところから。
- **Markdownブラウザのヘッダー/フッター**: `l_rect`内でのヘッダー/フッター分の高さ控除、スクロール対象外の固定描画領域追加が論点。
- **LuaでのウィジェットID管理**: 32bit整数ID(上位バイトから ウィジェット種類(enum)/generation/index)でLua側から実体へ安全アクセス。Lua統合自体が未着手のため、種類enum整理・generationカウンタ追加・index⇔ポインタ変換テーブルの新設が必要。Lua組み込み設計と合わせて相談されることが多い。

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
- アリーナが捕まえるのは Markdown の全841回の確保のうち**36回(バイトでは79%、回数では4.3%)**。残り805回は`Label`の行データ(`vector<vector<TextRun>>`)と`Widget`基底の`std::function`×4で、これはアリーナでは消えない。
  - **※この805回のうち`Label`ぶんはその後の改修で潰した**(行データを`vector<vector<TextRun>>`から
    `runs_flat`+`TextRun::line`へ平坦化し、レイアウトを`needs_relayout`で遅延評価に変更)。
    現在は `MarkdownView + load()` で**88回**(`sh script/host_test/run_mem.sh` で再現できる)。
    残りは`Widget`基底の`std::function`×4。
- **判断: 断片化もリークも観測されていない以上、64KBを常時占有する対価に見合わないため保留**。アプリが増えて断片化が実際に観測された時点で再検討する。
  先に効くのは`std::function`の自前Delegate化(固定枠を払わずに確保回数とピークを下げられる)。
  `Label::lines`の件は上記のとおり対処済み。

## Claude Codeへの申し送り

- 組み込み制約(RAM/Flash)を常に意識し、PC向けC++の常識をそのまま持ち込まない。
- 固定長バッファ/オブジェクトプール志向を優先し、安易な`new`/`delete`追加は避ける(MarkdownViewパターンを参照)。
- ダイアログ系(ファイル選択/保存/色選択)は実装済みなので車輪の再発明をせず、既存クラス(`FileSaveDialog`/`FileSelectDialog`/`FileExplorer`)を拡張する形で提案する。
- Lua組み込み・GBエミュ・外部コントローラ・Chiptune再生は土台が無いため、ゼロから設計相談する前提で臨む。
- 新しい画面を追加する話は`Scene`を継承して`onEnter()`でウィジェットを生成する形に寄せる。常駐させたいウィジェットは`AddOverlay()`。
- 新規ダイアログ/ウィジェットは既存の骨格(`children_`保持、`setOnClose`コールバック、`setVisible(false)`終了)にトーンを合わせる。
- コメント・ログは日本語、識別子は英語という言語使い分けを踏襲する。
- 文字列は`FixedString<N>`を使う。**Arduino `String`は現在どこでも使っていないので復活させないこと。**
- GUIの挙動を確かめたいときは実機ビルドの前にPCビルド(`pc/`)で回すのが速い。`src/`へ実機ライブラリ依存を
  足すときは `pc/compat/` 側にも代替を用意すること(PC/Webビルドが壊れる)。ブラウザで動かす場合は
  スレッド・生ソケット・ブロッキング待ちが使えない点にも注意。
- 判断に迷ったら `SUMMARY.md`(https://raw.githubusercontent.com/Kimu1109/pico-os/refs/heads/main/SUMMARY.md)と実コードを突き合わせて確認する。
