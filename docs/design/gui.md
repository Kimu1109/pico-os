# GUIの仕組み(ウィジェット・アプリ・シーン・ダイアログ・キーボード)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## ウィジェットカタログ
Button / Label / Textbox(Labelを継承、単一行/複数行対応の入力欄) / NumberInput(数字キーボード専用の1行入力欄) / Checkbox / Icon(tabler_icons由来、`IconSize`指定) / Image / NumberSlider / ScrollContainer / ScrollList / CanvasRaster(ピクセル単位描画) / LayoutContainer(縦横1方向の自動整列) / GridContainer(列数固定の2次元流し込み) / AppGrid(ランチャのアプリタイル) / TabBar(横並びのタブ) / AnalogClock(アナログ時計の文字盤) / DurationPicker(「時:分:秒」の表示/入力欄) / DropdownMenu / FileExplorer(SDのファイル一覧・作成/削除/選択、`currentPath`は`FixedString<PICO_PATH_LEN>`) / MarkdownView(最も作り込まれたウィジェット) / Statusbar / TextView(見えている行だけを描くプレーンテキストの表示欄。任意でカーソル) / ImageView(.pimgを1回だけ解いて持ち、ドラッグでスクロール) / LuaCanvas(中身を持たず`render()`でLua側コールバックを呼ぶだけ。Lua側からは`"Canvas"`。詳細は下記「直接描画」参照) / ProgressBar(表示専用の進捗バー。タップ素通り) / QRCodeView(QRコードの表示。タップ素通り。Lua側からは`"QRCode"`。下記「QRコード」)。
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

## ウィジェットID (`src/gui/widgets/WidgetID.hpp` / `WidgetRegistry.hpp` / `WidgetFactory.hpp`)
Lua等の外部から安全にウィジェットを指すための32bit ID。**発行側・ファクトリ・`Resolve()`の実利用(Lua統合本体)まで実装済み**(2026-09-19、下記「Luaバインディング」参照)。

- ビット配分は `[31:26] type(6bit, WidgetType)` / `[25:10] generation(16bit)` / `[9:0] index(10bit)`。`generation==0`は未割り当ての予約値なので**ID 0は常に無効**。
- 具象ウィジェットは`getWidgetType()`の実装が必須(純粋仮想)。種類を足すときは`WidgetType`へ追記する(64種を超えると`static_assert`で落ちる)。
- **IDは`getId()`の初回呼び出し時に遅延発行**する。外部から触られないウィジェットはスロットを消費しない。解放は`~Widget()`が`WidgetRegistry::Unregister()`を呼び、スロットのgenerationを進める。
- `WidgetRegistry::Resolve(id)`はindex範囲・generation・typeの3点を検証して`Widget*`を返す(不一致ならnullptr)。**破棄済みIDの誤参照(use-after-free)はここで弾かれる。**
- **`Resolve()`はホストテスト(`script/host_test/widget_factory_test.cpp`)で初めて検証された**: 発行済みIDからの解決、type改ざんの検出、破棄済みID(use-after-free)の検出、スロット再利用時のgeneration不一致検出を確認済み。(2026-09-19追記: 実コード側の呼び出し元も`LuaEngine`(`pico.set/get/on/destroy/add_child`等)として実装済み)
- **種類ごとの情報は `src/gui/widgets/WidgetTypes.cpp` の表(`kTypes`)1か所**(2026-10-19)。1行が「種類・Luaでの名前・作り方・種類固有のプロパティの読み方/書き方」で、
  `WidgetFactory`(`Create`/`IsCreatable`/`TypeFromName`)と `WidgetProperty`(`Get`/`Set`。X/Y/Visible等の共通のものは先に見る)はこの表を引くだけ。
  **種類を足すときに直すのは `WidgetType`(WidgetID.hpp)とこの表(関数と1行)だけ**。以下の節の「Factoryの3か所・WidgetPropertyのswitch」は当時の記録。
- **`WidgetType` → `new Xxx` のファクトリを追加した**(`src/gui/widgets/WidgetFactory.hpp/.cpp`)。`WidgetFactory::Create(WidgetType)`がwidgets/直下の汎用部品15種(Button/Label/Textbox/NumberInput/Checkbox/Icon/Image/NumberSlider/ScrollContainer/ScrollList/CanvasRaster/LayoutContainer/GridContainer/TabBar/DropdownMenu)を生成する。widgets/apps・systems・dialogsの専用ウィジェットは対象外(SD走査やシーン固有状態への依存が強いため)。生成直後は仮の位置・大きさなので、呼び出し側がsetX/setY/setW/setH等で整える前提。Textboxは`Textbox.cpp`が明示インスタンス化済みの`N`(`PICO_STR_LL`)に合わせてあり、任意のNは使えない(未使用の組み合わせを増やすには明示インスタンス化をもう1行足す必要がある)。
- **プロパティのget/setをLuaへ通す共通口(`WidgetProperty.hpp/.cpp`)を追加した(2026-09-19)**。`WidgetProperty::Get/Set(Widget*, Id, Value)`が`WidgetFactory::Create()`対応15種それぞれの代表的なプロパティ(Text/Value/Checked/FontSize/BorderColor等)を読み書きする。Widget基底の仮想関数にはしていない(`setW()`/`setH()`が基底に無く型ごとに意味が違うため。`WidgetFactory`と同じ「WidgetType→switch」形式)。値は`Value{type, i, f, b, FixedString<PICO_PATH_LEN> s}`という固定長のタグ付き共用体もどきで、ヒープを使わない。ホストテストは`script/host_test/widget_property_test.cpp`。
  (2026-09-19追記: この時点では「共通口」自体は揃ったが、Lua側からこれを叩くバインディング本体は
  まだ無かった。`LuaEngine`実装により`pico.set/get`から実際に呼ばれるようになった)
  (2026-09-21追記: 当初「NumberInputの入力値、Icon::opaqueのget、GridContainerのHAlign/VAlignのget等はウィジェット側に対応するgetter/setterが元々無いため未対応」だったが、Lua APIの細部の穴埋めで`NumberInput::getNum/setNum()`・`Icon::getOpaque()`・`GridContainer::getHAlign/getVAlign()`を追加し全て解消した。`ScrollList`/`DropdownMenu`には新たに`item_count`プロパティ(読み取り専用)も足した。詳細は「Luaバインディング」の「コンテナからの取り外し / リストへの項目追加」参照)
- `Widget::operator new`が確保失敗(nullptr)した際、以前は無言で失敗していたが、唯一の確保入口である`Widget.cpp`側でLOG_SYS_FAILを出すようにした。個々の`new Xxx(...)`呼び出し元がnullチェックしていない問題そのものは残っている(下記「Lua着手前の受け皿の状態」の「確保失敗(OOM)」参照)。

## アプリの枠組み (`src/services/App_Functions.hpp`)
`AppEntry`(名前/アイコン/引数/シーン生成関数)の固定長テーブルに登録し、`HomeScene`の`AppGrid`がそれを並べる。

- **アプリを増やすときに触るのは `src/boot/App_List.cpp` の `Setup()` に1行足すだけ**。シーン側にも`HomeScene`にも手を入れない。
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
- **ランチャの並び・非表示(2026-10-11、`/sys/launcher.cfg`)**: 登録簿のindex(`Get`/`Launch`/`LaunchByName`/`NameForDir`)は登録した順のまま変えず、
  **見せる順番(`launcher_order[]`、登録簿のindexの並べ替え)と隠した印(`launcher_hidden`、32bit)だけを別に持つ**(RAMは36バイト)。
  ランチャ(`AppGrid`)は`VisibleCount()`/`VisibleAt(pos)`で見えているものだけを並べ、`hitTile()`は登録簿のindexを返す(内部は位置=`hitPos()`)。
  - `launcher.cfg`は上から順に`app = 名前`/`hidden = 名前`(`SaveLayout()`が一時ファイル→差し替えで丸ごと書く)。`App_List.cpp`の最後で`LoadLayout()`。
    載っていないアプリ(新しく入れたもの)は後ろへ登録順に、消えたアプリ・知らないキーの行は捨てる。名前が重なれば登録の若いものから当てる。
    **隠したアプリは並びの中の位置を覚えている**(表示し直すとそこへ戻る)。`MoveApp(index, ±n)`は見えているものだけを数えて動かす。
  - 操作: **ランチャのタイルの長押し**(`AppGrid::takeLongPress()`を`HomeScene::onUpdate()`が毎フレーム見る。600ms。長押ししたタイルは離しても起動しない。
    メニューは次のフレームで開く=押し込みの絵がダイアログの下に残らないように)、コントローラーのX/Y・キーの`m`(選んでいるタイル)で
    「先頭へ/前へ/後ろへ移動・ランチャから隠す」。**設定アプリと、見えている最後の1つは隠せない**(戻す手段が設定アプリにあるため)。
    戻すのは設定アプリの「他」タブの1行目([編集]→隠したアプリを選ぶ/並びを元に戻す=`ResetLayout()`でファイルを消す)。
  - 隠したアプリも`pico.launch_app`・通知からは起動できる。
  - 検証: `app_test`(移動・先頭へ・隠したものを飛び越す・書いて読み直す・新しいアプリは最後・知らない名前/キー・元に戻す・AppGridが隠したものを並べない)、
    PCビルドの`--tap`/`--shot`(長押し→移動/隠す→再起動しても残る→設定アプリで表示し直す、`PICOOS_VERIFY_LCD=1`で食い違い0)。
- **ランチャのフォルダ(2026-10-21)**: 方針は利用者と相談して決めた: **自分で作るフォルダ**(自動のカテゴリ分けではない)/
  押すと**グリッドがフォルダの中身に入れ替わる**(ポップアップではない)/ タイルは**中の先頭4つのアイコンを縮めて**描く。
  - データは`AppFunctions`(`launcher_folder[]`=アプリごとのフォルダの番号、`folder_names[8]`・`folder_used`。約430B)。
    **フォルダの中のアプリは`launcher_order`の中で続けて並ぶ**(フォルダを変える操作と読み込みの`Normalize()`が保つ)ので、
    並びは「フォルダの外のアプリ1つ」か「フォルダ1つぶん」の塊の列になり、移動(`MoveApp`/`MoveFolder`)は塊ごとに回す(`Rotate`)。
    トップに出るのは見えている塊(`TopCount()`/`TopAt()`/`TopPosition()`)。フォルダの位置は中の先頭のアプリの位置で、別に持たない。
    中を全部隠したフォルダはトップに出ない(消えはしない)。中が空になったフォルダは消える(最後の1つを出す・登録簿から消える)。
    フォルダの番号は作ったときに決まり、他が消えても変わらない(ホームのウィジェットの付け直しで使うため)。フォルダの中にフォルダは無い。
    `VisibleCount()`/`VisibleAt()`は今までどおりフォルダの中も数えた平らな数え方(設定アプリ・隠せるかの判定)。
  - `launcher.cfg`: `folder = 名前`の行の後に中身を`in = 名前`/`in-hidden = 名前`で書く(`app`/`hidden`の行が来たらフォルダの外)。
    フォルダが無ければ今までの形と同じ。中身の無い`folder`の行は捨て、8個を超えたフォルダの中身は外に並べる。
  - ホームのウィジェットの anchor は「トップの項目(アプリとフォルダ)の何番目の前か」に変えた(フォルダが無ければ今までと同じ値)。
    フォルダを作る/入れる/出す/解除・中のアプリを隠すは`HomeLayout`の同じ名前の関数を通し、`Reanchor()`が
    **ウィジェットが前と同じ項目の前に残るよう**付け直す(消えた項目なら後ろで残っているものの前。作ったときは入れたアプリ→そのフォルダ)。
  - `AppGrid`: `openFolder()`/`closeFolder()`(閉じるとトップのそのフォルダを選ぶ)。開いている間は`HomeLayout::ComputeFolder()`
    (中の見えているアプリだけ、全部1x1、ウィジェットは置かない)。中が空になれば`ensureLayout()`がトップへ戻す。
    タイルは48pxの角の丸い灰色の枠に16pxで2x2(`.pimg`は`IconRender::DrawImageRLE4bppScaled()`で間引き、組み込みは16px)。
  - `HomeScene`: フォルダを開いている間はページ送りの行を`[戻る] n/m 名前 [<][>]`にする(ページ番号を先に出すのは名前が長くても見えるように)。
    閉じるのは[戻る]・コントローラーのB・Esc/BackSpace・フォーカスの戻る。アプリを開いて戻ってきたら、開いていたフォルダとページへ戻す。
    長押しのメニュー: トップのアプリ=「フォルダへ入れる」(新しいフォルダ/今あるフォルダ。新しいものは`InputDialog`で名前、空なら「フォルダ」)、
    フォルダ=先頭へ/前へ/後ろへ・名前を変える・フォルダを解除(中身はその場に出る)・ウィジェット追加、
    フォルダの中のアプリ=フォルダの中での移動・別のフォルダへ・フォルダから出す(フォルダのすぐ後ろへ)・隠す。フォルダの中の空いた所の長押しは何もしない。
  - 設定アプリの「並びを元に戻す」(`ResetLayout()`)はフォルダも無くす。
  - 検証: `app_test`(作る・入れる・中での移動・塊ごとの移動・書いて読み直す・全部隠す・出す/空で消える・解除・上限・登録簿から消える・
    手で書いたファイル)、`home_test`(並びにフォルダが1マスで出る・ウィジェットが同じ項目の前に残る・中だけを並べる・AppGridで開く/起動/戻る/
    長押し/空になったらトップへ)、PCビルドの`--tap`/`--shot`(フォルダのタイル・開く・中のアプリを開いて戻るとフォルダのまま・[戻る]・
    長押し→フォルダへ入れる→今あるフォルダ/新しいフォルダ・長い名前とページ送り、`PICOOS_VERIFY_LCD=1`で食い違い0)。
- `AppGrid`はタイルごとに子ウィジェットを作らず、`render()`で直接描いてタップ位置から逆算する(`ColorDialog`の色グリッドと同じ方式)。`WidgetFunctions::HitTest()`は子から先に判定するため、タイルをIcon+Labelの親として作ると子がタップを奪ってしまう。
- レイアウトは2列×3行=6個/ページ(タイル111x78px)。3列だとタイル幅72px=日本語4文字しか入らず大半のアプリ名がはみ出したため2列にした。名前は`drawName()`がUTF-8の文字境界で切って最大2行へ折り返す(`DrawPlain()`は折り返さないため自前)。

## シーン (`src/gui/scenes/`)
`Scene`基底クラス(`getName()`/`onEnter()`/`onExit()`/`onUpdate()`/`contentRect()`)と`SceneFunctions`による画面遷移。

- **レイヤとシーンの対応**: `widgets`(通常レイヤ)と`dialog_roots`(ダイアログ層)は**シーンの所有物**で遷移時に一括破棄。`overlays`(Statusbar/キーボード3種)はOS常駐でシーンをまたいで生き続ける。→ 「常駐させたいものは`AddOverlay()`に置く」が唯一のルール。
- **遷移API**: `SceneFunctions::Change/Push/Pop`。いずれも要求を登録するだけで、実際の遷移は`SceneFunctions::Update()`(フレーム境界)で実行される。ボタンのコールバック内から呼んでも自分自身をdeleteしない(`DestroyLater`と同じ発想)。1フレーム1遷移で、2件目以降の要求は却下して即delete。
- **ウィジェットの寿命**: シーンがアクティブな間のみ。`Push`でスタックへ退避されたシーンもウィジェットは解放済みで、`Pop`で戻った時に`onEnter()`から作り直される(シーンオブジェクト本体は数十バイト)。スタック上限は`kMaxSceneDepth=4`の固定長配列。
- **`onExit()`でdeleteしてはいけない**: ウィジェット本体の破棄は`WidgetFunctions::ClearSceneWidgets()`が行う。`onExit()`は自分の生ポインタのnull化と、次回復元したい状態の退避のみ。
- 遷移時は`isDirtyDeactivates`で破棄/生成中のdirtyを抑止し、最後に全画面1枚だけを`MarkDirty`する。
- 実装例: `HomeScene`(ランチャ) / `MarkdownScene`(Markdownブラウザ。下記) / `ClocksScene`(時計。下記) / `InputTestScene`。
- ホスト側の検証: `sh script/host_test/run.sh`(実コードをPCのg+++ASanで動かし解放漏れを検出。実機ビルドとは独立)。

## 画面の大きさ (`consts.hpp` の `SCREEN_WIDTH/HEIGHT`) (2026-10-25)

- **ビルド時に決める**(動いている最中には変えない)。frame・行ハッシュの配列・`contentRect()` 等が `constexpr` のままで済み、
  RAMの見積もりもビルドごとに固定できるため。実機は env(`rpipico2w_320x480` が例)、PCは `cmake -DPICOOS_SCREEN_W/H`(`pc/README.md`)。
- 液晶の種類は `PICO_PANEL_ST7796` / `ILI9488` / `ST7789`(既定はILI9341。`config/LGFX_Config.hpp`)。`setRotation()` はしていないので、
  `SCREEN_WIDTH/HEIGHT` はパネルの縦向きの大きさに合わせる。タッチは XPT2046 のまま(補正の既定は画面の大きさから作る)。
- 決まり: **幅は偶数・幅も高さも240以上**(`static_assert`)。文字・ボタンの大きさは変わらないので、広い画面は余白が増え、狭い画面は入りきらない。
  配置は `Scene::contentRect()` からの相対で書き、240/320を直書きしない。幅240前提で作った部品(ゲームボーイの画面とパッド)は真ん中に置く。
- frame は4bppなので 幅×高さ÷2 バイト(240x320で37.5KB、320x480で75KB、480x800で188KB)。全画面の転送時間も画素数に比例する。
- PCビルドで 240x320 / 320x480 / 320x240 / 480x800 の全アプリを撮って確かめた(2026-10-25)。**縦長(高さ320以上)はおおむね崩れない。**
  横長(320x240)はネイティブの画面も動くが、電卓のキー・本体の更新・ゲームボーイ(パッドが入らない)は高さが足りず詰まる。
- **Lua**: 画面の大きさは定数 `pico.SCREEN_WIDTH/HEIGHT`、アプリが使える範囲は `pico.content_rect()`(どちらも前からある)。
  `pico.game` は `game.new{ w = 240, h = 300 }` のように x/y を省いて w/h だけ渡すと content_rect の真ん中に置く(2026-10-25)。
  240x320向けの固定の配置を大きい画面でもそのまま使うため。同梱アプリで固定の配置だったのはテトリス(これで真ん中へ)と
  マインスイーパーの右上のボタン(右端に寄せた)だけで、ほかは content_rect から配置していた。横長(320x240)では
  テトリス(高さ300が要る)・電池テスト・Spotify・ペイント等が詰まる。
- 実機の別パネル(ST7796等)では未確認(Todoist参照)。

## ダイアログ (`src/gui/widgets/dialogs/`)
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
## オンスクリーンキーボード (`src/gui/widgets/keyboards/` / `dialogs/KeyboardDialog` / `gui/core/Keyboard_Functions`) (2026-09-29)

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
  足したら使われるものをスタブへも足すこと(足さないとリンカが本物の`Keyboard_Functions.o`を拾い、スタブと重複定義になる)。

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

## QRコード (`src/gui/widgets/QRCodeView` / `lib/qrcodegen`) (2026-10-25)

- **符号化は vendor した Nayuki の qrcodegen 1.8.0(C版、MIT)**(`lib/qrcodegen/README-pico-os.md`)。動的確保をせず、呼び出し側が渡す
  2本のバッファだけで符号化する。誤り訂正・8種のマスクの評価まで規格どおりで、自前で書くより確か。PlatformIOは `lib/` を自動で拾い、
  `pc/CMakeLists.txt` と `script/host_test/CMakeLists.txt`(`host_qrcodegen`)が同じコピーを見る。
- `QRCodeView` は**符号化した升目だけを固定長で持つ**(`kMaxVersion = 17` = 85x85升で905B。文字列256Bと合わせて1つ約1.2KB)。
  作業用のバッファ(同じ905B)は全部のQRで1本を使い回す(`QRCodeView.cpp` の `g_temp`。1コア目だけが触る)。
  版17は「255バイトなら誤り訂正Highでも入る」ところから決めた。Luaの `pico.set` の文字列(254B)は必ず入る。
- 文字は `qrcodegen_encodeText()` に任せる(数字だけ・英大文字と一部の記号だけは詰めた形、他はUTF-8のバイト列)。
  誤り訂正は升目の数が増えない範囲で自動で強める(`boostEcl`)。`getEcc()` は頼んだ段を返す。
- **長すぎる文字は切らずに空にして `false`**(途中で切れたQRは別のURL等を指してしまう)。途中にNULのある文字も断る。
  Luaの `pico.set(qr, "text", …)` は `WidgetProperty::Value` の器で黙って切れるので、長さの分からない文字は `pico.text_set` で入ったかを見る。
- 描き方: 升の大きさは「升目+余白1升ずつ」が短い辺に入る最大の整数倍を先に決め、その大きさのまま入る範囲で余白を規格の4升へ近づける
  (小さい升は読みにくい。余白の外も地の色なら読み取り機は困らない)。横に続く黒い升は1回の `fillRect` にまとめる。
- **色はパレットで明るい方を地にする**(`color` と `background_color` のRGB565のおおよその明るさを比べて入れ替える)。ダークテーマは0と15の
  明暗を入れ替えるので、そのままだと反転したQR(暗い地に明るい升目)になり、読めない読み取り機がある。
- 使っている所: Wi-Fiの[スマホ](`WifiSetupScene`)に、読むとAPへ繋がるQR(`WIFI:T:WPA;S:…;P:…;;`、値の中の `\ ; , : "` は前に `\`)。
  名前とパスワードの右に93px(29升なら1升3px)。名前がQRの手前で1行に入らなければ名前だけ小さい字にする。
  Luaアプリ「QRコード」(`pc/sdcard/lua/apps/QRコード/`。サイト・連絡先・Wi-Fi等のQRを保存して一覧から出す。`lua-apps.md`「QRコード」)。
- 検証: `qrcode_test`(widget。版の決まり方・形式情報のBCHと誤り訂正の段・切り出し記号・タイミング・升目どおりの描画と余白・
  ダークテーマで地が明るい方・長すぎ/NULで空・プロパティ)、`lua_ext_test`(`pico.create("QRCode")`・`text_set`・`ecc`・定数)。
  PCビルドの画面写真(等倍)をOpenCVの `QRCodeDetector` で読み、[スマホ]の画面(明るい/ダーク)とLuaアプリ(誤り訂正M/H)が
  元の文字に戻ることを確かめた(OpenCVは手元で入れただけでリポジトリのテストには入れていない)。**実機の液晶をスマホで読むのは未確認(Todoist参照)**。
