# CLAUDE.md — pico-os プロジェクトコンテキスト

> このファイルは `Kimu1109/pico-os` リポジトリ直下に置く、Claude Code向けのプロジェクト背景資料。
> 元はClaude.aiのProject knowledgeとして管理されていた内容(2026-09-06時点情報)を統合したもの。
> **最終同期: 2026-09-13(実コードと突き合わせ済み)。**
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
    scenes/                 Scene基底と各画面(HomeScene/MarkdownScene/InputTestScene)
    widgets/                各ウィジェット実装 (WidgetID.hpp / WidgetRegistryも同居)
      dialogs/              モーダルダイアログ
      interfaces/            ミックスイン的インターフェース
      systems/               Statusbar等システムウィジェット
  ime/                       SKK方式かな漢字変換辞書エンジン
  net/                        HTTPレスポンスの解釈 / 取得〜キャッシュの配線(Doc_Fetch) / サーバ情報(Discovery)
  util/                       Rect(矩形) / FixedString(固定長文字列) / Utf8Byte / Url / Md_Scan(画像参照の走査)
  storage/                    SDカードI/O・パス定数・文書キャッシュ(Doc_Cache)
  task/                       非同期タスク基底 + NetworkScan / HttpGet タスク
  test/                       フォントカバレッジチェック等
script/                       開発補助スクリプト(アイコン生成/SKK辞書変換/pimg生成等, Python)
  tabler_icons/               アイコン元データ(tabler由来のSVG)
  custom_icons/               アイコン元データ(自作SVG)。tablerが16pxで破綻する場合の受け皿
  host_test/                  PCで実コードを動かす検証(run.sh=ASanで解放漏れ検出、scene/label/markdown/config/app/path/cache/http/discoveryの9本 / run_net.sh=参照実装サーバ相手の結合テスト / run_mem.sh=確保回数の計測)
  reference_server.py         PROTOCOL.mdの参照実装サーバ(標準ライブラリのみ)。Markdownブラウザの開発相手
  ppm2png.py                  picoos_pcの--shotが書き出すPPMをPNGへ(標準ライブラリのみ)
pc/                            PC実行用ビルド(CMake + SDL2)。`src/`は実機と同一のまま使う
  compat/                     実機ライブラリの代替ヘッダ(Arduino/SPI/WiFi/SdFat/LGFX設定/タッチ)
  sdcard/                     SDカードとして読まれるディレクトリ
examples/doc.md                MarkdownView動作確認用サンプル文書
PROTOCOL.md                    ドキュメントサーバとの通信仕様(Markdownブラウザのネットワーク対応用。実装は未着手)
```
`include/`, `lib/`, `test/` はPlatformIO標準雛形ディレクトリで未使用(README以外中身なし)。

## コアアーキテクチャ

### OSData (`src/OS_Data.hpp`)
グローバル状態ハブ。`inline`変数として: タッチ状態(touchX/Y/Z, isTouched, isTouchStart/End/Move)、グラフィック(`LGFX* lcd`, `LGFX_Sprite* frame`)、SD(`SdFat SD`, SD_usable)、キーボード3種へのポインタ(keyboard_jpn / keyboard_eng / keyboard_num)を保持。

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
| Config_Functions | `key=value`形式の設定ファイルパーサ + 書き込み(`SetValue()`は一時ファイル経由で1キーだけ差し替え) |
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
- `hit_transparent`: **当たり判定を素通りさせるフラグ**。`WidgetFunctions::Add()`は`visitAll()`で**子孫も全て`widgets`へ積む**ため、子は親とは別の「根」として`HitTest()`の対象になる。つまり**親が自分でタップを処理したい場合、表示のために置いただけの子がタップを奪う**。`MarkdownView`のプール(Label/Image/Icon)がこれで、リンクのタップが一切反応しなかった。表示専用の子にはこれを立てる。

### ウィジェットカタログ
Button / Label / Textbox(Labelを継承、単一行/複数行対応の入力欄) / NumberInput(数字キーボード専用の1行入力欄) / Checkbox / Icon(tabler_icons由来、`IconSize`指定) / Image / NumberSlider / ScrollContainer / ScrollList / CanvasRaster(ピクセル単位描画) / LayoutContainer(縦横1方向の自動整列) / GridContainer(列数固定の2次元流し込み) / AppGrid(ランチャのアプリタイル) / DropdownMenu / FileExplorer(SDのファイル一覧・作成/削除/選択、`currentPath`は`FixedString<PICO_PATH_LEN>`) / MarkdownView(最も作り込まれたウィジェット) / Statusbar。

`LayoutContainer` / `GridContainer` は**Luaアプリが子を動的に積むこと**を想定して足したコンテナ。`add()`で所有権を引き取りデストラクタで`delete`する。子の位置(x/y)だけを面倒見てサイズは子自身に委ねる(`Widget`基底に`setW`/`setH`が無いため)。コンストラクタの`reserve_hint`は上限ではなく単なるヒントで、超えても`std::vector`の再確保で動き続ける。

### ウィジェットID (`src/gui/widgets/WidgetID.hpp` / `WidgetRegistry.hpp`)
Lua等の外部から安全にウィジェットを指すための32bit ID。**発行側は実装済み、消費側はまだ空。**

- ビット配分は `[31:26] type(6bit, WidgetType)` / `[25:10] generation(16bit)` / `[9:0] index(10bit)`。`generation==0`は未割り当ての予約値なので**ID 0は常に無効**。
- 具象ウィジェットは`getWidgetType()`の実装が必須(純粋仮想)。種類を足すときは`WidgetType`へ追記する(64種を超えると`static_assert`で落ちる)。
- **IDは`getId()`の初回呼び出し時に遅延発行**する。外部から触られないウィジェットはスロットを消費しない。解放は`~Widget()`が`WidgetRegistry::Unregister()`を呼び、スロットのgenerationを進める。
- `WidgetRegistry::Resolve(id)`はindex範囲・generation・typeの3点を検証して`Widget*`を返す(不一致ならnullptr)。**破棄済みIDの誤参照(use-after-free)はここで弾かれる。**
- **ただし`Resolve()`の呼び出し元はまだコード中に1つも無く、ホストテストも無い。** 実際に使われるのはLua統合から。
- `WidgetType`から実体を作るファクトリ(`WidgetType` → `new Xxx`)も未整備で、外部からウィジェットを生成する口はまだ存在しない。

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
- 実装例: `HomeScene`(ランチャ) / `MarkdownScene`(Markdownブラウザ。下記) / `InputTestScene`。
- ホスト側の検証: `sh script/host_test/run.sh`(実コードをPCのg+++ASanで動かし解放漏れを検出。実機ビルドとは独立)。

### ダイアログ (`src/gui/widgets/dialogs/`)
`WidgetFunctions`内で`dialog_roots`という独立リストで管理(当たり判定・描画順ともに最優先)。共通の骨格: 「`children_`ベクタで子を保持」「`setOnClosed(std::function<void(bool is_ok)>)`で結果通知」「`setVisible(false)`で自身を隠して終了」。**新規ダイアログを提案する際はこの型に合わせる。**

- `MsgDialog`: メッセージ+アイコン+OK/キャンセル。`RenderMode::TRANSLUCENT`。
- `InputDialog`: ラベル+テキスト入力+決定/キャンセル(単一行/複数行切替可)。
- `FileSaveDialog`: `FileExplorer`+ファイル名`Textbox`+OK/キャンセル。**保存専用**。
- `FileSelectDialog`: `FileExplorer`+OK/キャンセルのみ。**選択専用**(ファイル名欄なし)。
  - ※旧設計では1クラスで兼用予定だったが、実装では保存/選択で別クラスに分離された。
- `ColorDialog`: 実装済み(直近コミット)。4×4=16色グリッド(`getIndexToColor(x,y)=x+y*4`)+OK/キャンセル。`selected_color`(未選択-1)、`getSelectedColor()`。
- `Keyboard` / `KeyboardEng` / `KeyboardNum`: オンスクリーンキーボード3種。いずれも`KeyboardFunctions::Setup()`が`AddOverlay()`でOS常駐させる。
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
`PROTOCOL.md` の平文HTTPを喋る側。3つに割ってあるのは**テストできる形にするため**。

- **`Url`**: `http://host:port/path?query` を分解して持つ型。`"http://"` を各所で`strncmp`しないための入れ物で、**schemeをここに閉じ込めてある**ので将来HTTPS対応で触るのは`Http_Get`の接続処理だけで済む。パスとクエリを分けて持つのは、相対解決(`PICO_IO::resolve`を再利用)がクエリ内の`/`まで畳んでしまわないようにするため。
- **`HttpResponse`**: **ソケットを持たない**増分パーサ。受信したバイト列を`feed()`へ渡すだけなので、ネットワーク無しに全経路をホストテストできる(`http_test.cpp`は1バイトずつ食わせた場合も同じ結果になることまで見ている)。見るヘッダは`Content-Length`/`ETag`/`Last-Modified`/`Location`/`Transfer-Encoding`だけ。**chunkedは検出したらエラー**にする(黙って本文として書くと壊れたファイルが正常なキャッシュとして残るため)。本文の行き先は`IHttpSink`で差し替える。
- **`HttpGet`**: `Task`派生。`Connection: close`を送り、`Accept-Encoding`は送らない。リダイレクト最大3回、全体10秒で打ち切り。手元の検証子を渡すと条件付きGETになる(`GMT`を含むかで`If-None-Match`と`If-Modified-Since`を出し分ける — 目録が「どちらのヘッダで来たか」を覚えていないための割り切り)。**3xx/4xxの本文はシンクへ流さない**(`BodyGate`が200を見てから開く)のでキャッシュが汚れない。
- **接続(`connect`)だけは同期的**。到達しない相手を指すと最大`kConnectTimeoutMs=3000`ぶん画面が止まる。受信は全てポーリングなので、繋がってしまえばフレームは止まらない。非同期接続にはlwIPを直に叩く必要があり、別の段の仕事。
- PC側の`WiFiClient`は`pc/compat/WiFiClient_PC.h`にある。**Wi-Fiの「状態」(`pc/compat/WiFi.h`)は偽物のままだが、通信そのものは本物のソケット**。SDにもLovyanGFXにも依存しないので、ホストテスト(`script/host_test/stubs/WiFi.h`)からも**同じ実装**を使う(通信経路のテストで別物を使っては意味が無いため)。

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

## PC実行環境 (`pc/`)

実機に書き込まずにPC上のウィンドウでpico-osを動かせる。詳細は `pc/README.md`。

```sh
sudo apt-get install libsdl2-dev      # 前提: SDL2開発パッケージ
cmake -S pc -B pc/build && cmake --build pc/build -j
./pc/build/picoos_pc                  # マウス左ドラッグ = タッチ
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc --shot shot.ppm 40   # ヘッドレス確認

# ヘッドレスではSDLへマウスが来ないので、撮りたい画面まで --tap で操作を進める
#   --tap X,Y@FRAME[:HOLD]   FRAMEフレーム目に(X,Y)をHOLDフレーム押す(既定3、最大16件)
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc \
    --tap 61,65@30:5 --shot md.ppm 250        # ランチャの1枚目のアプリを開いて撮る

python3 script/ppm2png.py md.ppm md.png 2     # PPMは見づらいのでPNGへ(2倍)
```

**画面の確認はこの2つで完結する。** `--tap`はヘッドレスでの動作確認のために用意した
もので、実際にこれで「リンクをタップしても反応しない」「短い文書を開き直すと前の内容が
下部に残る」という2つのバグが見つかっている(いずれもホストテストでは出ない類のもの)。

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

## ロードマップ・TODO状況(2026-09-13時点)

相談が来た際はまず本表を見て、「既存機能の拡張」か「ゼロから設計する新機能」かを見分けること。**都度 `SUMMARY.md` をfetchして最新状況を確認するのが望ましい。**

| # | 旧TODO大項目 | 状況 |
|---|---|---|
| 1 | ダイアログ(ファイル選択・保存・色選択) | **全て実装済み(betaレベル)**。上記ダイアログカタログ参照。数字専用(電卓用)キーボード`KeyboardNum`も実装済み。 |
| 2 | スクリーン管理 | メモリ解放(`DestroyLater`)・パネル/グリッドレイアウト(`LayoutContainer`/`GridContainer`)・**シーン遷移+画面スタック(`Scene`/`SceneFunctions`)は実装済み**。**メモリプール化(汎用)は計測の結果いったん保留**(下記「メモリ計測の結論」参照)。 |
| 3 | Wi-Fi管理強化 | **実装済み**。非ブロッキング接続・スキャン・NTP同期・電波強度アイコンに加え、`SUCCESS`中は`HEALTH_CHECK_INTERVAL=5000ms`ごとに`WiFi.status()`を確認し、切断を検知したら`ConnectWiFiAsync()`を呼び直す(`currentPassword`を再接続用に保持)。 |
| 4 | Luaアプリ/API | **未着手**(Lua本体のコードは皆無)。ただし受け皿の一部は先行して入っている: ウィジェットID発行(`WidgetID`/`WidgetRegistry`)、`LayoutContainer`/`GridContainer`、**PC実行環境(`pc/`)**。残っている穴は下記「Lua着手前の受け皿の状態」を参照。 |
| 5 | 標準/セカンダリアプリ開発 | **未着手**。設定アプリ・時計・辞書・電卓・チャット・オセロ/テトリス風・シューティング・ブロック崩し・リマインダー・カレンダー等、アプリ本体コードなし(部品は存在)。 |
| 6 | GBエミュ | **未着手**。 |
| 7 | 外部コントローラー | **未着手**。GPIO/UART連携コードなし(タッチのみ)。 |
| 8 | Chiptune音声再生 | **未着手**。音声出力・PWM/I2S関連コードなし。 |

## 未実装の設計アイデア(旧pico-osからの持ち越し議論)

- **ウィジェットのメモリプール化(汎用)**: 実測の結果、現時点では保留と判断した(下記「メモリ計測の結論」)。再開する場合は`Widget::operator new/delete`をアリーナへ差し替えるところから。
- **Markdownブラウザのブラウザ化**: 仕様は `PROTOCOL.md`(HTTP/行指向TSV/SDをキャッシュにする方式)、サーバの参照実装は `script/reference_server.py`。
  **第1段(リンク追従・履歴・ナビゲーションヘッダー)と第2段(キャッシュ層)は実装済み。**
  第1段: `MarkdownScene`が履歴を自前で持ち、`MarkdownView::setOnLinkTap()`から`PICO_IO::resolve()`で相対パスを解決して同じシーンのまま開き直す。
  第2段: `storage/Doc_Cache.hpp`(下記)。
  第3段(HTTPクライアント): `util/Url.hpp` / `net/Http_Response` / `task/Http_Get`(下記)。
  第4段(取得→キャッシュ→表示の配線): `net/Doc_Fetch`(下記)。**ここまでで「サーバ上の文書を読む」が成立している。**
  `network.cfg` の `browser-home` にURLを書くと、Markdownアプリがそこを開く。
  第5段(画像の解決と先読み)・第6段(discovery)も実装済み。
  **残りは検索のみ。**
  - **リンクごとに`SceneFunctions::Push`してはいけない**。スタック上限が`kMaxSceneDepth=4`しかなく4回で詰む。履歴はシーンが持つ(`kMaxHistory=8`、パス+スクロール位置)。
  - `MarkdownScene`の履歴に載るのは**「場所」でSDパスとURLのどちらもあり得る**。見分けは`UrlTools::Parse()`が通るかどうかの**1箇所だけ**で、`"http://"`の判定を各所へ撒いていない。
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
- **LuaでのウィジェットID管理**: 32bit整数IDの**発行側は実装済み**(`WidgetID.hpp`/`WidgetRegistry`)。残るのは消費側 — `Resolve()`を叩くバインディング、`WidgetType`→実体のファクトリ、プロパティのget/setをLuaへ通す共通の口。Lua組み込み設計と一緒に決める部分。

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
    **単体では旨味が薄い。** Luaのコールバック(`lua_State*`+registry refのキャプチャは16B超え=貼るたびに
    ヒープ確保)を大量に貼るようになって初めて費用対効果が出る。
- **判断: 断片化もリークも観測されていない以上、64KBを常時占有する対価に見合わないため保留**。アプリが増えて断片化が実際に観測された時点で再検討する。
  `Label::lines`の件は上記のとおり対処済みで、残る`std::function`のDelegate化も単体では5%程度の効果しかない
  (上記)。**次に手を入れる価値が出るのはLuaのコールバックを大量に貼るようになってから。**

## Lua着手前の受け皿の状態 (2026-09-13時点)

Lua向けの土台は「発行側だけ入って消費側が空」の状態。着手時に必ず当たる穴を列挙しておく。

| 箇所 | 状態 |
|---|---|
| `WidgetRegistry::Resolve()` | 実装済みだが**呼び出し元ゼロ・テストゼロ**。実際に使った時点で仕様の穴が出る想定 |
| ウィジェットのファクトリ | **無い**。`WidgetType` enumはあるが `WidgetType` → `new Xxx` の対応表が無く、Luaから生成する口が存在しない |
| ~~`AppEntry`(`App_Functions.hpp`)~~ | **解消済み(2026-09-13)**。`create`が`Scene* (*)(const AppEntry&)`になり、`name`/`arg`は`FixedString`でコピー保持するようになった。「同じ`LuaScene`型 + 別スクリプトパス」も、寿命の短い文字列からの動的登録も表現できる。残りは**SDを走査してLuaアプリを見つける側**(スキャン処理そのもの)だけ |
| コールバック | `std::function<void()>` で引数もコンテキストも無し。Lua側は `lua_State*` + registry ref を持たせる必要があり、そのキャプチャは16B超え=貼るたびにヒープ確保になる |
| 実行時間の制御 | **無い**。`loop()`は単純ポーリングなので、重い/無限ループのLuaはタッチごと固める。`lua_sethook`での命令数バジェットか、`Task`へ載せてコルーチン化するかの判断が要る(`Task`基盤は既にある) |
| 確保失敗(OOM) | `Widget::operator new`はnullptrを返す仕様だが、**呼び出し側は誰もnullチェックしていない**。Luaは「ユーザーのコードがRAMを食う」世界なので、`lua_newstate`のカスタムallocで**Luaに上限枠を切る**必要がある。※シーンアリーナ不要の結論(上記)とは別の話 |
| エラーの見せ方 | Luaのエラーを`pcall`で拾った後に出す先が無い(`LOG_SYS_FAIL`止まり)。「アプリが落ちた」をMsgDialogで見せる導線が要る |
| RAM/Flash予算 | **現状の空きRAMの絶対値を実機で測っていない**(`MemFunctions`のレポートは差分中心)。Lua本体はflash 100KB超・stateだけでRAM 20〜30KBのオーダーなので、入れる前に一度測っておくと判断が早い |
| ビルドの二重管理 | `platformio.ini` と `pc/CMakeLists.txt` の両方にLuaを足す必要がある(LovyanGFXの版追随が既に手動なのと同じ状況) |

**API仕様は「C++で標準アプリを1〜2本書いてみて、必要になったもの」から逆算するのが確実。** 現状アプリは`MarkdownScene`(開く文書が`tmp/doc.md`固定)と`InputTestScene`(部品の動作確認用)しかなく、バインディング設計の実例が足りていない。

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
  足すときは `pc/compat/` 側にも代替を用意すること(PCビルドが壊れる)。
- 判断に迷ったら `SUMMARY.md`(https://raw.githubusercontent.com/Kimu1109/pico-os/refs/heads/main/SUMMARY.md)と実コードを突き合わせて確認する。
- **テストは全て手動**。`.github/`が無くCIは存在しないので、`sh script/host_test/run.sh`(ASan、9本)/ `sh script/host_test/run_net.sh`(実通信)/ `sh script/host_test/run_mem.sh`(確保回数)/ PCビルドは変更のたびに自分で回すこと。
