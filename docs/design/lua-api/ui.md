# Lua API の設計記録(pico.*): ウィジェット・シーン・ダイアログ・イベント・タッチ・時刻・定数

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `lua-api.md` から節ごとに分けた(目次は同じフォルダの `README.md`)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

## Buttonのアイコン化(`pico.set(id,"icon_id"/"icon_size",...)`、2026-09-23実装)

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
  末尾(`script/codegen/generate_icons.py`の`ICONS`リストの末尾)へ足すこと** —
  途中へ挿入すると、既存のLuaスクリプトが数値で指定しているアイコンが黙って
  別物にすり替わる(この制約自体は`generate_icons.py`にコメントで明記した。
  今回`Save`/`StackPop`/`StackPush`/`Brush`/`Eraser`の5種が追加されているが、
  この時点ではまだLua側が数値のIconIDに依存していなかったため実害は無かった。
  以降は要注意)。名前の一覧は`lua-api-doc/content/reference/constants.md`の
  「アイコン」に載せてある(Luaからは `pico.ICON_<名前>` で引くので番号は載せていない)。
- ホストテストは`lua_engine_test.cpp`の「細部のプロパティ」ブロックへ追加
  (icon_id/icon_sizeのget/set往復、`setIcon()`経由で`has_icon=true`になること、
  `w`/`h`未指定なら箱の大きさが`icon_size`へ追従すること)。実際の見た目は
  スクラッチパッドのPCビルド`--shot`で確認した。

## シーン制御(2026-09-21実装)

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
  `sdcard/lua/hello.lua`に追加した「サブ画面へ」ボタンから`sdcard/lua/hello_sub.lua`
  (`pico.push_scene()`の飛び先。「電卓を開く」で`pico.launch_app("電卓")`、
  「置き換えへ」で`pico.change_scene()`)→`sdcard/lua/hello_sub2.lua`
  (`pico.change_scene()`の飛び先)という3ファイル構成のデモ一式を作り、
  push_scene→launch_app(電卓が実際に開く)、push_scene→change_scene→pop
  (hello_sub.luaを飛び越してhello.luaへ直接戻る)の両方を確認した。

## ダイアログ(2026-09-21実装)

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

## ウィジェット固有イベント(2026-09-21実装)

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

## コンテナからの取り外し / リストへの項目追加(2026-09-21実装、細部の穴埋め)

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

## タップ位置の取得(pico.get_touch、2026-09-21実装)

Luaでオセロアプリ(`sdcard/lua/apps/オセロ/main.lua`)を作った際に踏んだ穴を埋めた。
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
  (`src/gui/core/Widget_Functions.cpp`)が当たり判定に使っているのと同じ値なので、
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

## 時刻取得(2026-09-21実装)

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

## 数値定数(2026-10-20)

色・`font_size`・`icon_size`・`icon_id`・`text_align`・`direction`・各種 align・`canvas_mode`・曜日・画面の大きさ・`pico.iso` のブロック/地形の種類を、
`pico.COLOR_RED` / `pico.ICON_Home` / `pico.iso.BLOCK_TORCH` のように名前で書けるようにした(`src/lua/LuaConstants.hpp`、使い方は `lua-api-doc/content/reference/constants.md`)。

- **値はC++の定義(`consts.hpp`・各enum・`Iso_World.hpp`)を直接引く**ので食い違わない。数値を直接書く書き方もそのまま使える。
- **関数と同じ遅延**: テーブルへは入れず、`l_api_index` が関数の表に無かった名前を定数の表から引いて初めて写す(約150個の定数でLuaの予算を使わない)。
- **アイコンは `icons_data.h` の `kIconNames`**(`script/codegen/generate_icons.py` が `IconID` と同じ並びで出す。`static_assert` で個数を縛る)を引く。
  `pico.ICON_<IconIDの名前>` で、並び順が変わっても追従する。アイコンを足すと自動で使える。
- 定数を足したら `LuaConstants.hpp` の表と、`api/constants.md`・`reference/limits.md` を直す。テストは `lua_engine_test.cpp` の「数値定数」。
- 実機では未確認(PCビルドとホストテストのみ)。
