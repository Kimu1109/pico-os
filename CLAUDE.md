# CLAUDE.md — pico-os プロジェクトコンテキスト

> このファイルは `Kimu1109/pico-os` リポジトリ直下に置く、Claude Code向けのプロジェクト背景資料。
> 元はClaude.aiのProject knowledgeとして管理されていた内容(2026-09-06時点情報)を統合したもの。
> **最終同期: 2026-09-21(実コードと突き合わせ済み)。**
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
    scenes/                 Scene基底と各画面(HomeScene/MarkdownScene/ClocksScene/InputTestScene/LuaScene)
    widgets/                汎用ウィジェット + 基底 (Widget / WidgetID / WidgetRegistry)
      apps/                 特定のアプリ専用のウィジェット(MarkdownView/FileExplorer/AnalogClock/DurationPicker)
      dialogs/              モーダルダイアログ
      interfaces/            ミックスイン的インターフェース
      systems/               OSのシェル部品(Statusbar / AppGrid)
  ime/                       SKK方式かな漢字変換辞書エンジン
  lua/                        Lua<->C++バインディング本体(LuaEngine)
  net/                        HTTPレスポンスの解釈 / 取得〜キャッシュの配線(Doc_Fetch) / サーバ情報(Discovery) / 検索(Doc_Search) / マニフェスト(Manifest)
  util/                       Rect(矩形) / FixedString(固定長文字列) / Utf8Byte / Url / Md_Scan(画像参照の走査)
  storage/                    SDカードI/O・パス定数・文書キャッシュ(Doc_Cache)
  task/                       非同期タスク基底 + NetworkScan / HttpGet タスク + StepBudget(実行時間の区切り)
  test/                       フォントカバレッジチェック等
script/                       開発補助スクリプト(アイコン生成/SKK辞書変換/pimg生成等, Python)
  tabler_icons/               アイコン元データ(tabler由来のSVG)
  custom_icons/               アイコン元データ(自作SVG)。tablerが16pxで破綻する場合の受け皿
  host_test/                  PCで実コードを動かす検証(run.sh=ASanで解放漏れ検出、scene/label/markdown/config/app/path/cache/http/discovery/calc_eval/calculator/dict/dict_scene/widget_factory/widget_property/step_budget/error_functions/lua_smoke/lua_stdlib/lua_alloc_budget/lua_engine/lua_sceneの22本 / run_net.sh=参照実装サーバ相手の結合テスト / run_mem.sh=確保回数の計測)
  reference_server.py         PROTOCOL.mdの参照実装サーバ(標準ライブラリのみ)。Markdownブラウザの開発相手
  ppm2png.py                  picoos_pcの--shotが書き出すPPMをPNGへ(標準ライブラリのみ)
lib/lua/                       vendorしたLua 5.4.7本体(lua.c/luac.cを除く)。詳細はlib/lua/README-pico-os.md
pc/                            PC/Web実行用ビルド(CMake + SDL2 / Emscripten)。`src/`は実機と同一のまま使う
  compat/                     実機ライブラリの代替ヘッダ(Arduino/SPI/WiFi/SdFat/LGFX設定/タッチ)
  web/shell.html              Webビルドのページの外枠(canvas + ログ + デバッグ用ボタン)
  sdcard/                     SDカードとして読まれるディレクトリ
    lua/hello.lua             LuaEngine/LuaSceneの動作サンプル(ランチャに「Lua Hello」タイルあり)
examples/doc.md                MarkdownView動作確認用サンプル文書
PROTOCOL.md                    ドキュメントサーバとの通信仕様(v1は一通り実装済み)
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
| Error_Functions | 「ユーザーへ見せるべき失敗」をログ+MsgDialogの両方へ出す共通口(`ShowFatal()`)。Lua着手前の受け皿の1つ |

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

### ウィジェットの置き場所
**`widgets/`直下に置けるのは「どのアプリからでも使える汎用部品」だけ**。専用のものはサブフォルダへ入れる。

| 置き場所 | 何を入れるか | 中身 |
|---|---|---|
| `widgets/` | 汎用部品と基底 | `Widget` / `WidgetID` / `WidgetRegistry` + 下のカタログのうち専用でないもの |
| `widgets/apps/` | **特定のアプリ専用**のウィジェット | `MarkdownView` / `FileExplorer` / `AnalogClock` / `DurationPicker` |
| `widgets/systems/` | **OSのシェル部品**(特定アプリのものではない) | `Statusbar`(常駐オーバーレイ) / `AppGrid`(ランチャのタイル) |
| `widgets/dialogs/` | モーダルダイアログ + オンスクリーンキーボード3種 | 下記「ダイアログ」参照 |
| `widgets/interfaces/` | ミックスイン的インターフェース | `IBorderColor` / `IFontImplementation` / `ITextColor` / `ITextInputTarget` |

- **includeは常に`src/`起点の絶対パス**(`#include "gui/widgets/apps/MarkdownView.hpp"`)。
  相対includeは使っていないので、フォルダを移してもファイル自身の中身は書き換え不要。
- **PlatformIOもPCビルドも`src/**.cpp`を再帰的に拾う**ので、ファイルを移動してもビルド定義に触る必要はない。
  ただし**`script/host_test/*.sh`はソースを1本ずつ明示列挙している**ので、移動したらここだけ直すこと。
- `FileExplorer`が`apps/`なのは、`FileSaveDialog`/`FileSelectDialog`から使われていても
  **「SD上のファイルを見せる」という用途に特化した部品**だから。汎用部品の定義は「役割が特定の
  画面に紐付いていないこと」で、「複数箇所から使われていること」ではない。

### ウィジェットカタログ
Button / Label / Textbox(Labelを継承、単一行/複数行対応の入力欄) / NumberInput(数字キーボード専用の1行入力欄) / Checkbox / Icon(tabler_icons由来、`IconSize`指定) / Image / NumberSlider / ScrollContainer / ScrollList / CanvasRaster(ピクセル単位描画) / LayoutContainer(縦横1方向の自動整列) / GridContainer(列数固定の2次元流し込み) / AppGrid(ランチャのアプリタイル) / TabBar(横並びのタブ) / AnalogClock(アナログ時計の文字盤) / DurationPicker(「時:分:秒」の表示/入力欄) / DropdownMenu / FileExplorer(SDのファイル一覧・作成/削除/選択、`currentPath`は`FixedString<PICO_PATH_LEN>`) / MarkdownView(最も作り込まれたウィジェット) / Statusbar / LuaCanvas(中身を持たず`render()`でLua側コールバックを呼ぶだけ。Lua側からは`"Canvas"`。詳細は下記「直接描画」参照)。

`LayoutContainer` / `GridContainer` は**Luaアプリが子を動的に積むこと**を想定して足したコンテナ。`add()`で所有権を引き取りデストラクタで`delete`する。子の位置(x/y)だけを面倒見てサイズは子自身に委ねる(`Widget`基底に`setW`/`setH`が無いため)。コンストラクタの`reserve_hint`は上限ではなく単なるヒントで、超えても`std::vector`の再確保で動き続ける。

**「子を持たず`render()`で直接描き、タップ位置から逆算する」型のウィジェット**が増えている:
`AppGrid` / `ColorDialog` / `KeyboardNum` / `TabBar` / `AnalogClock` / `DurationPicker`。
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
- `DurationPicker`は総ミリ秒だけを保持し、時/分/秒は描くときに割り出す(タイマーの「設定値」と「残り時間」を
  同じウィジェットで見せるため)。カウントダウン中は`setEditable(false)`で▲▼が消える。
  ▲▼は**長押しで連続加算**(450ms後に110ms間隔)— 25分を1タップずつ積むのは現実的でないため。
  桁は繰り上がらず、その桁だけが巡回する。上限は`23:59:59`。

### ウィジェットID (`src/gui/widgets/WidgetID.hpp` / `WidgetRegistry.hpp` / `WidgetFactory.hpp`)
Lua等の外部から安全にウィジェットを指すための32bit ID。**発行側・ファクトリは実装済み、Resolve()の実利用(Lua統合本体)だけが残っている。**

- ビット配分は `[31:26] type(6bit, WidgetType)` / `[25:10] generation(16bit)` / `[9:0] index(10bit)`。`generation==0`は未割り当ての予約値なので**ID 0は常に無効**。
- 具象ウィジェットは`getWidgetType()`の実装が必須(純粋仮想)。種類を足すときは`WidgetType`へ追記する(64種を超えると`static_assert`で落ちる)。
- **IDは`getId()`の初回呼び出し時に遅延発行**する。外部から触られないウィジェットはスロットを消費しない。解放は`~Widget()`が`WidgetRegistry::Unregister()`を呼び、スロットのgenerationを進める。
- `WidgetRegistry::Resolve(id)`はindex範囲・generation・typeの3点を検証して`Widget*`を返す(不一致ならnullptr)。**破棄済みIDの誤参照(use-after-free)はここで弾かれる。**
- **`Resolve()`はホストテスト(`script/host_test/widget_factory_test.cpp`)で初めて検証された**: 発行済みIDからの解決、type改ざんの検出、破棄済みID(use-after-free)の検出、スロット再利用時のgeneration不一致検出を確認済み。ただし**実コード中の呼び出し元はまだテストのみ**で、実際に使われるのはLua統合から。
- **`WidgetType` → `new Xxx` のファクトリを追加した**(`src/gui/widgets/WidgetFactory.hpp/.cpp`)。`WidgetFactory::Create(WidgetType)`がwidgets/直下の汎用部品15種(Button/Label/Textbox/NumberInput/Checkbox/Icon/Image/NumberSlider/ScrollContainer/ScrollList/CanvasRaster/LayoutContainer/GridContainer/TabBar/DropdownMenu)を生成する。widgets/apps・systems・dialogsの専用ウィジェットは対象外(SD走査やシーン固有状態への依存が強いため)。生成直後は仮の位置・大きさなので、呼び出し側がsetX/setY/setW/setH等で整える前提。Textboxは`Textbox.cpp`が明示インスタンス化済みの`N`(`PICO_STR_LL`)に合わせてあり、任意のNは使えない(未使用の組み合わせを増やすには明示インスタンス化をもう1行足す必要がある)。
- **プロパティのget/setをLuaへ通す共通口(`WidgetProperty.hpp/.cpp`)を追加した(2026-09-19)**。`WidgetProperty::Get/Set(Widget*, Id, Value)`が`WidgetFactory::Create()`対応15種それぞれの代表的なプロパティ(Text/Value/Checked/FontSize/BorderColor等)を読み書きする。Widget基底の仮想関数にはしていない(`setW()`/`setH()`が基底に無く型ごとに意味が違うため。`WidgetFactory`と同じ「WidgetType→switch」形式)。値は`Value{type, i, f, b, FixedString<PICO_PATH_LEN> s}`という固定長のタグ付き共用体もどきで、ヒープを使わない。一部のプロパティ(NumberInputの入力値、Icon::opaqueのget、GridContainerのHAlign/VAlignのget等)はウィジェット側に対応するgetter/setterが元々無いため未対応(該当箇所にコメントで明記)。ホストテストは`script/host_test/widget_property_test.cpp`。**ここまでで「共通口」自体は揃ったが、Lua側からこれを叩くバインディング本体はまだ無い。**
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

- `MsgDialog`: メッセージ+アイコン+OK/キャンセル。`RenderMode::TRANSLUCENT`。
- `InputDialog`: ラベル+テキスト入力+決定/キャンセル(単一行/複数行切替可)。
  決定/キャンセルの時点で`KeyboardFunctions::HideAll()`を呼ぶ(入力対象がこの後消えるため)。
- `FileSaveDialog`: `FileExplorer`+ファイル名`Textbox`+OK/キャンセル。**保存専用**。
- `FileSelectDialog`: `FileExplorer`+OK/キャンセルのみ。**選択専用**(ファイル名欄なし)。
  - ※旧設計では1クラスで兼用予定だったが、実装では保存/選択で別クラスに分離された。
- `SearchDialog`: Markdownブラウザの検索結果。状態1行 + `ScrollList` + 再検索/次へ/閉じる。
  **通信はしない**(判断は`MarkdownScene`側)。結果は2回タップで開く。
- `ColorDialog`: 実装済み(直近コミット)。4×4=16色グリッド(`getIndexToColor(x,y)=x+y*4`)+OK/キャンセル。`selected_color`(未選択-1)、`getSelectedColor()`。
- `Keyboard` / `KeyboardEng` / `KeyboardNum`: オンスクリーンキーボード3種。いずれも`KeyboardFunctions::Setup()`が`AddOverlay()`でOS常駐させる。
  - **入力位置は3種ともカーソル基準**(末尾への追記ではない)。挿入も削除(1文字戻し)もカーソルの位置で起きる。
  - `Keyboard`/`KeyboardEng`では、カーソル位置の**真を持つのはキーボード側**(自分のテキスト上の
    文字インデックス)で、`input_label`へは`Label::setCursorToByteOffset()`でバイト位置として渡す。
    **Labelのカーソルスロット番号を位置として使ってはいけない** — `**`や`~`のマークアップ記号は
    描画されずスロットも持たないため、元テキストの文字数とスロット番号は一致しない。
    (`KeyboardNum`だけは位置をラベルのスロット番号のまま持っている。打てる記号に
    マークアップ文字が無いのでずれないが、記号を足すときはここを先に直すこと)
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
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc --shot shot.ppm 40   # ヘッドレス確認

# ヘッドレスではSDLへマウスが来ないので、撮りたい画面まで --tap で操作を進める
#   --tap X,Y@FRAME[:HOLD]   FRAMEフレーム目に(X,Y)をHOLDフレーム押す(既定3、最大16件)
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc \
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

## ロードマップ・TODO状況(2026-09-21時点)

相談が来た際はまず本表を見て、「既存機能の拡張」か「ゼロから設計する新機能」かを見分けること。
**進捗の一次情報源は`SUMMARY.md`**(番号は同ファイルの大項目と揃えてある)。
本表は**そこへ判断のための一言を足しただけ**なので、**都度 `SUMMARY.md` をfetchして最新状況を確認すること。**

| # | 大項目 | 状況 |
|---|---|---|
| 1 | ダイアログ系統 | **全て実装済み(betaレベル)**。上記ダイアログカタログ参照。数字専用(電卓用)キーボード`KeyboardNum`も実装済み。 |
| 2 | 汎用基盤 | **実装済み**。ウィジェットIDはファクトリ・`Resolve()`ともに実装され、`Resolve()`は`LuaEngine`(`pico.set/get/on/destroy/add_child`等)から実際に呼ばれている。 |
| 3 | スクリーン管理 | メモリ解放(`DestroyLater`)・パネル/グリッドレイアウト(`LayoutContainer`/`GridContainer`)・**シーン遷移+画面スタック(`Scene`/`SceneFunctions`)は実装済み**。**メモリプール化(汎用)は計測の結果いったん保留**(下記「メモリ計測の結論」参照)。**⚠ PCビルドでシーン遷移を繰り返すとヒープ下限が際限なく増える未解決の問題あり**(下記「メモリ計測の結論」内の該当節参照)。 |
| 4 | Wi-Fi管理強化 | **実装済み**。非ブロッキング接続・スキャン・NTP同期・電波強度アイコンに加え、`SUCCESS`中は`HEALTH_CHECK_INTERVAL=5000ms`ごとに`WiFi.status()`を確認し、切断を検知したら`ConnectWiFiAsync()`を呼び直す(`currentPassword`を再接続用に保持)。 |
| 5 | Luaアプリ/API | **`LuaEngine`+`LuaScene`が動き、ランチャから実際にLuaアプリを起動できる(2026-09-19着手)**。ウィジェット操作(生成/破棄/プロパティ/共通コールバック+ウィジェット固有コールバック)・直接描画(Canvas)・SDカードアクセス・画像(.pimg)・シーン制御(push_scene/change_scene/launch_app)・ダイアログ・ネットワーク(HTTPリクエスト)・時刻取得・**権限管理(network/sd_outside_app_dirの粗いフラグ、2026-09-21追加)**まで実装済み。**残っているのは命令単位の実行時間制御、SDを走査してLuaアプリを見つける処理**。詳細は下記「Luaバインディング」「Lua着手前の受け皿の状態」を参照。 |
| 6 | PC/Web動作対応 | **実装済み**(`pc/`)。上記「PC / Web実行環境」参照。 |
| 7 | 標準アプリ開発 | **実装済み**。Markdownブラウザ(`PROTOCOL.md` v1を一通り)・時計(`ClocksScene`)・電卓(`CalculatorScene`)・ファイルエクスプローラー(`FileExplorerScene`)・辞書(`DictScene`)・設定(`SettingsScene`)の6本。詳細は`SUMMARY.md`「7. 標準アプリ開発」参照。 |
| 8 | セカンダリアプリ開発 | **未着手**。チャット・オセロ/テトリス風・シューティング・ブロック崩し・リマインダー・カレンダー等、アプリ本体コードなし。 |
| 9 | GBエミュ | **未着手**。 |
| 10 | 外部コントローラー | **未着手**。GPIO/UART連携コードなし(タッチのみ)。 |
| 11 | Chiptune音声再生 | **未着手**。音声出力・PWM/I2S関連コードなし。 |

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
  - **`MarkdownScene`のオブジェクトは数KBある**(`DocFetch`2.7KB + `DocSearch`2.9KB + 履歴1.6KB等)。
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

### ⚠️ 未解決: PCビルドでシーン遷移を繰り返すと「ヒープ下限」が際限なく増える(2026-09-19発見)

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

**未着手**: 原因の特定(候補: LovyanGFXのフォント/グリフキャッシュ、Task_Functions、
Network_Functionsの再接続チェック、SDL側のイベント処理)、実機での再現確認、修正。
次にこの周辺(Scene/Widget基盤、PCビルド)を触る回で必ず引き継ぐこと。
再現用の`--tap`コマンドは上記の通りなので、まずそれで実機/PCの両方を確認するのが早い。
  `Label::lines`の件は上記のとおり対処済みで、残る`std::function`のDelegate化も単体では5%程度の効果しかない
  (上記)。

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
| `pico.on(id, event_name, fn)` | 4種の共通イベント(`press_start`/`press_end`/`press_move`/`press_out`)+`render`(`Canvas`限定、下記「直接描画」参照)+ウィジェット固有4種(`checked_changed`/`value_changed`/`select_item`/`tab_changed`、下記「ウィジェット固有イベント」参照)+`closed`(ダイアログ限定)に対応(下記) |
| `pico.add_child(container_id, child_id)` | `LayoutContainer`/`GridContainer`/`ScrollContainer`のみ対応 |
| `pico.log(msg)` | `LOG_APP_MSG` |
| `pico.show_error(msg)` | `ErrorFunctions::ShowFatal()` |
| `pico.pop()` | `SceneFunctions::Pop()`。`LuaScene`から起動されたアプリがランチャへ戻るためのもの(2026-09-19追加) |
| `pico.content_rect()` | `Scene::contentRect()`を`x,y,w,h`の4値で返す。ステータスバー分を避けた配置に使う(2026-09-19追加) |
| `pico.draw_pixel(x,y,color)` / `draw_line(x0,y0,x1,y1,color)` / `draw_rect(x,y,w,h,color)` / `fill_rect(...)` / `draw_circle(x,y,r,color)` / `fill_circle(...)` / `clear_rect(x,y,w,h[,color])` / `draw_text(x,y,text[,color[,font_size]])` | `OSData::frame`へ直接描く。**`Canvas`の`render`コールバック内で使うこと**(下記「直接描画」参照)(2026-09-20追加) |
| `pico.invalidate(id)` | 対象ウィジェットの画面矩形を`needsRender()`でdirty化(次のFlushDirty()で`render()`が呼ばれる)。`Canvas`に限らず任意のウィジェットに使える汎用API(2026-09-20追加) |
| `pico.mark_dirty(x,y,w,h)` | `PICO_GFX::MarkDirty()`の生の下請け。任意の矩形を直接dirty化したいとき向けの低レベルAPI(2026-09-20追加) |
| `pico.set_draw_area(x,y,w,h)` / `pico.clear_draw_area()` | `OSData::frame->setClipRect()`/`clearClipRect()`。以降の`pico.draw_*`をこの矩形の内側だけに制限する/解除する(下記「直接描画エリア」参照)(2026-09-20追加) |
| `pico.sd_exists(path)` | `OSData::SD.exists()`。`bool`を返す(2026-09-20追加) |
| `pico.sd_read(path)` | ファイル全体を文字列で返す。無い/開けない/上限超過は`nil`(下記「SDカードアクセス」参照)(2026-09-20追加) |
| `pico.sd_write(path, content[, append])` | 新規作成+上書き(既定)、または`append=true`で追記。成否を`bool`で返す(2026-09-20追加) |
| `pico.sd_remove(path)` | ファイルなら`SD.remove()`、ディレクトリなら`PICO_IO::removeRecursive()`(`FileExplorer`の削除と同じ判断)(2026-09-20追加) |
| `pico.sd_mkdir(path)` | `OSData::SD.mkdir()`(2026-09-20追加) |
| `pico.sd_list(path)` | ディレクトリを列挙し`{ {name=..., is_dir=...}, ... }`の配列を返す。パスが無い/ディレクトリでないなら`nil`(2026-09-20追加) |
| `pico.image_load(path)` | `.pimg`をデコードして整数ハンドルを返す。失敗(SD無し/パス不正/不正な`.pimg`/上限超過)は`nil`(下記「画像」参照)(2026-09-21追加) |
| `pico.image_size(handle)` | 読み込んだ画像の`width, height`を返す。無効なハンドルはエラー(2026-09-21追加) |
| `pico.draw_image(handle, x, y)` | 画像を描く。他の`pico.draw_*`と同じく**`Canvas`の`render`コールバック内で使うこと**。無効なハンドルはエラー(2026-09-21追加) |
| `pico.image_free(handle)` | 画像を明示的に解放する。無効/解放済みハンドルは`pico.destroy`と同じく黙って無視(2026-09-21追加) |
| `pico.push_scene(path)` / `pico.change_scene(path)` | 別のLuaスクリプトへ`SceneFunctions::Push/Change`する(下記「シーン制御」参照)(2026-09-21追加) |
| `pico.launch_app(name)` | `AppFunctions::LaunchByName()`経由で登録簿の任意のアプリ(C++製含む)へ`Push`する。見つかれば`true`、無ければ`false`(下記「シーン制御」参照)(2026-09-21追加) |
| `pico.show_message(text, cancel_text, ok_text)` | `MsgDialog`を表示する。閉じた結果は`pico.on(id,"closed",fn)`で受ける(下記「ダイアログ」参照)(2026-09-21追加) |
| `pico.show_input(label, initial_text, is_single_line)` | `InputDialog`を表示する。入力文字列は`pico.get(id,"text")`で読む(2026-09-21追加) |
| `pico.show_file_save(start_dir)` / `pico.show_file_select(start_dir)` | `FileSaveDialog`/`FileSelectDialog`を表示する。選択パスは`pico.get(id,"path")`で読む(未選択は`nil`)(2026-09-21追加) |
| `pico.show_color()` | `ColorDialog`(4×4パレット)を表示する。選択色は`pico.get(id,"value")`で読む(未選択は`-1`)(2026-09-21追加) |
| `pico.http_request(method, url, body, content_type, callback)` | 非同期HTTPリクエスト(GET/POST/PUT/PATCH/DELETE)。同時に1本まで。`callback(ok, status_code, body_or_nil, error_or_nil)`(下記「ネットワーク」参照)(2026-09-21追加) |
| `pico.http_cancel()` | 進行中の`pico.http_request()`を取り消す(2026-09-21追加) |
| `pico.get_time()` | `TimeFunctions::timeinfo`を`{year, month, day, hour, min, sec, wday}`のテーブルで返す(下記「時刻取得」参照)(2026-09-21追加) |

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
- **同時に実行できるリクエストは1本まで。** `HttpState::callback_ref`が
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
  (未知のメソッド/不正なURL/https/送信ボディの上限超過/同時実行数の上限)での
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
- ホストテストは`lua_engine_test.cpp`に追加。4イベントそれぞれについて、対応するC++側の
  トリガ(`Checkbox::causeOnPressStart()`でのタップ相当/`NumberSlider::setValue()`/
  `ScrollList::causeOnSelectItem()`/`TabBar::setSelected(index, true)`)を直接呼んで
  Luaコールバックが実際に発火すること、`pico.get()`で変更後の値が読めること、
  `select_item`の`already_selected`が引数で渡ること、対応外のウィジェットへの登録が
  エラーになることを確認している。

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

### 現時点のスコープ外(次回以降)

- ~~コールバックは共通4種(press_start/end/move/out)のみ~~ → **解消済み(2026-09-21)**。
  `Checkbox::on_change_checked`/`NumberSlider::on_value_changed`/`ScrollList::on_selectitem`/
  `TabBar::on_changed`を`checked_changed`/`value_changed`/`select_item`/`tab_changed`として
  `pico.on()`から使えるようにした(詳細は上の「ウィジェット固有イベント」参照)。
- **命令単位の実行時間制御(`lua_sethook`)は無い**。`StepBudget`との組み合わせ含め、
  実際に重い/無限ループするLuaアプリを書いてみてから決める。
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
- コンテナからの明示的な子の取り外し(`pico.remove_child`)は無い(`pico.destroy`で
  子ごと破棄する経路しか無い)。

## Lua着手前の受け皿の状態 (2026-09-19時点)

Lua向けの土台は「発行側・ファクトリ・プロパティ共通口・実行時間制御の土台・エラー表示導線・
ビルドへの組み込みまでは入って、Luaバインディング本体(`lua_State`を実際に生成してsrc/へ繋ぐ部分)
だけが空」の状態。着手時に必ず当たる穴を列挙しておく。

| 箇所 | 状態 |
|---|---|
| ~~`WidgetRegistry::Resolve()`~~ | **ホストテストで検証済み(2026-09-18)**。発行済みIDからの解決・type改ざん検出・破棄済みID(use-after-free)検出・スロット再利用時のgeneration不一致検出を`script/host_test/widget_factory_test.cpp`で確認。ただし**実コード中の呼び出し元はまだテストのみ**で、Luaバインディングを書いた時点で初めて実利用される |
| ~~ウィジェットのファクトリ~~ | **解消済み(2026-09-18)**。`WidgetFactory::Create(WidgetType)`(`src/gui/widgets/WidgetFactory.hpp/.cpp`)がwidgets/直下の汎用部品15種を生成する。widgets/apps・systems・dialogsの専用ウィジェットは対象外 |
| ~~プロパティのget/set共通口~~ | **解消済み(2026-09-19)**。`WidgetProperty::Get/Set()`(`src/gui/widgets/WidgetProperty.hpp/.cpp`)を参照。Lua側が「WidgetIdを`Resolve()`で引く→`WidgetProperty`で値を読み書きする」という2段構えを、Lua本体無しで既にホストテストまで確認できている |
| ~~`AppEntry`(`App_Functions.hpp`)~~ | **解消済み(2026-09-13)**。`create`が`Scene* (*)(const AppEntry&)`になり、`name`/`arg`は`FixedString`でコピー保持するようになった。「同じ`LuaScene`型 + 別スクリプトパス」も、寿命の短い文字列からの動的登録も表現できる。残りは**SDを走査してLuaアプリを見つける側**(スキャン処理そのもの)だけ |
| コールバック | **意図的に見送り(2026-09-19)**。`std::function<void()>`のまま。上記「メモリ計測の結論」が既に出している判断(「関数ポインタ+`void*`のDelegateへ替える効果は単体では5%程度、旨味が出るのはLuaのコールバックを大量に貼るようになってから」)をそのまま踏襲し、今回は手を入れなかった。今のシグネチャには「引数もコンテキストも無い」という実害(誰が押したか・どのウィジェットのIDかをコールバック側へ渡せない)もあるため、**再設計するならLuaバインディング本体を書く回でシグネチャを一度に決める**(引数無しのまま先にDelegate化だけ済ませても、Lua側の要求で結局signature変更が要る可能性が高く、二度手間になるため) |
| ~~実行時間の制御~~ | **土台のみ解消(2026-09-19)**。`task/StepBudget.hpp`が「一定時間(マイクロ秒)働いたら次のフレームへ回す」ための時間区切りプリミティブを提供する(`Task::update()`内の作業ループを`StepBudget::ShouldContinue()`で区切る)。ホストテストは`script/host_test/step_budget_test.cpp`(`pc/compat/`の実時間`micros()`を使う点が他と違う)。**命令単位の制御(`lua_sethook`でNバイトコードごとに打ち切る等)はまだ無く**、実際にLuaスクリプトをTask化する回で、このStepBudgetと組み合わせるか独自のフック粒度を足すかを決める |
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
  **テストを回すワークフローは無い**。`sh script/host_test/run.sh`(ASan、9本)/
  `sh script/host_test/run_net.sh`(実通信)/ `sh script/host_test/run_mem.sh`(確保回数)/ PCビルドは
  変更のたびに自分で回すこと。
