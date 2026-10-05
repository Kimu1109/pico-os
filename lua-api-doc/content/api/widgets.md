---
title: "ウィジェット操作"
weight: 10
description: "create / destroy / set / get / on / off / add_child / remove_child / リスト・タブ・ツリー・名前・矩形・Z順の操作"
---

## pico.create

<div class="sig">pico.create(type_name: string) <span class="ret">-> id: integer</span></div>

指定した種別のウィジェットを1つ生成し、`WidgetId`(整数)を返します。生成直後の位置・大きさは仮の値です。

- `type_name` が未知の種別名の場合、エラー(`luaL_error`)。
- メモリ不足でウィジェット本体の確保に失敗した場合も、エラー。
- 生成できる `type_name` の一覧は [ウィジェット種別一覧](../../reference/widget-types/) を参照。

## pico.destroy

<div class="sig">pico.destroy(id: integer) <span class="ret">-> (なし)</span></div>

ウィジェットを破棄します。実際の解放はフレーム境界でまとめて行われます。登録済みの `pico.on()` コールバックも同時に解除されます。

- `id` が既に無効(未割り当て・破棄済み)な場合は**エラーにならず黙って無視**されます(二重destroyを許容)。

## pico.set

<div class="sig">pico.set(id: integer, name: string, value: any) <span class="ret">-> (なし)</span></div>

プロパティを書き込みます。`value` は文字列・真偽値・数値のいずれかです(数値は整数として書き込みを試し、失敗したら浮動小数点数として再試行します)。

- `id` が無効: エラー。
- `name` が未知のプロパティ名: エラー。
- そのウィジェット種別が `name` に対応していない、または `value` の型が合わない: エラー。
- 対応プロパティの一覧は [プロパティ対応表](../../reference/widget-properties/) を参照。

## pico.get

<div class="sig">pico.get(id: integer, name: string) <span class="ret">-> value: any | nil</span></div>

プロパティを読み取ります。

- `id` が無効、または `name` が未知のプロパティ名: エラー。
- `name` 自体は有効だが、そのウィジェット種別が対応していない組み合わせ: **エラーにはならず `nil`** を返します。

## pico.on

<div class="sig">pico.on(id: integer, event_name: string, callback: function) <span class="ret">-> (なし)</span></div>

イベントコールバックを登録します。同じ `id` + `event_name` へ再度呼ぶと、古いコールバックを解除して差し替えます。

- `event_name` が未知: エラー。
- 対象ウィジェットがそのイベントに対応していない(例: `Button` に `"checked_changed"`): エラー。
- イベント名の全一覧・引数は [イベント](../../guide/events/) を参照。

## pico.add_child

<div class="sig">pico.add_child(container_id: integer, child_id: integer) <span class="ret">-> (なし)</span></div>

`child_id` を `container_id` の子として追加します。`container_id` は `LayoutContainer` / `GridContainer` / `ScrollContainer` のいずれかである必要があります(それ以外はエラー)。

## pico.remove_child

<div class="sig">pico.remove_child(container_id: integer, child_id: integer) <span class="ret">-> (なし)</span></div>

`add_child` の逆。子を**破棄せずに**取り外します。

- `container_id` がコンテナ種別でない: エラー。
- `child_id` が実際にそのコンテナの子でない: エラー。
- 取り外し後、`child_id` は独立したルートウィジェットとして描画・タップ判定の対象に戻ります。座標はコンテナ内での相対値のまま残ります。

## pico.list_add

<div class="sig">pico.list_add(id: integer, text: string) <span class="ret">-> (なし)</span></div>

`ScrollList` または `DropdownMenu` に項目を1つ追加します。アイコンは指定できません(既定アイコン固定)。対応外の種別はエラー。

## pico.list_clear

<div class="sig">pico.list_clear(id: integer) <span class="ret">-> (なし)</span></div>

`ScrollList` または `DropdownMenu` の項目を全て消します。`DropdownMenu` は表示ラベルもプレースホルダへ戻ります。対応外の種別はエラー。

## pico.tab_add

<div class="sig">pico.tab_add(id: integer, label: string) <span class="ret">-> ok: boolean</span></div>

`TabBar` にタブを1つ追加します。最大4個までで、上限に達している場合は `false` を返します(エラーにはなりません)。`TabBar` 以外を渡すとエラー。

## pico.off

<div class="sig">pico.off(id: integer, event_name: string) <span class="ret">-> removed: boolean</span></div>

`pico.on` で登録したコールバックを解除します。登録されていなければ `false`。`event_name` が未知ならエラー。

## pico.text_set

<div class="sig">pico.text_set(id: integer, text: string) <span class="ret">-> whole: boolean</span></div>

`TextView`(16KiBまで)と `MarkdownView`(8KiBまで)に**長い文章**を渡します。`pico.set(id, "text", ...)` は255バイトまでなので、それより長いものはこちらを使います。上限を超えた分は切り捨てられ、`false` を返します。`MarkdownView` に渡した文書の中の相対パス(画像など)は、SDのルートが基準になります(ファイルから読むなら `pico.set(id, "path", ...)`)。

## pico.set_dots

<div class="sig">pico.set_dots(id: integer, dots: table) <span class="ret">-> (なし)</span></div>

`MonthGrid` の日付の下に点(予定の印)を出します。`dots[日]` に色の番号の配列(最大3つ)か、色の番号1つを入れます。

```lua
pico.set_dots(grid, { [3] = {12, 9}, [14] = 10 })  -- 3日に赤と青、14日に緑
```

## pico.set_name / pico.find

<div class="sig">pico.set_name(id: integer, name: string) <span class="ret">-> (なし)</span></div>
<div class="sig">pico.find(name: string) <span class="ret">-> id: integer | nil</span></div>

ウィジェットに名前を付けて、後から名前でidを引きます。IDを全部変数で持ち回らずに済みます。名前は1〜23文字で、アプリの中で一意です(同じ名前を付け直すと付け替わります)。最大128個。ウィジェットを `pico.destroy` すると名前も消えます。

## pico.parent / pico.children

<div class="sig">pico.parent(id: integer) <span class="ret">-> parent_id: integer | nil</span></div>
<div class="sig">pico.children(id: integer) <span class="ret">-> ids: table</span></div>

親と(直接の)子のidです。親がない(一番上の)ウィジェットは `nil`。

## pico.get_rect

<div class="sig">pico.get_rect(id: integer) <span class="ret">-> x, y, w, h</span></div>

画面座標での矩形です(親のコンテナの位置を含んだ実際の位置)。

## pico.bring_to_front / pico.send_to_back

<div class="sig">pico.bring_to_front(id: integer) <span class="ret">-> ok: boolean</span></div>
<div class="sig">pico.send_to_back(id: integer) <span class="ret">-> ok: boolean</span></div>

重なり順を変えます(子孫も一緒に動きます)。コンテナの子には使えず `false` を返します(コンテナの中の並びは `add_child` した順です)。

## pico.scroll_to

<div class="sig">pico.scroll_to(container_id: integer, child_id: integer) <span class="ret">-> ok: boolean</span></div>

`ScrollContainer` を、子が見える位置(上端)までスクロールします。子でなければ `false`。位置の読み書きは `scroll_y` プロパティ、変化は `scrolled` イベントです。

## pico.show_keyboard / pico.hide_keyboard

<div class="sig">pico.show_keyboard(id: integer) <span class="ret">-> (なし)</span></div>
<div class="sig">pico.hide_keyboard() <span class="ret">-> (なし)</span></div>

`Textbox` / `NumberInput` のオンスクリーンキーボードを、タップしなくても開く/閉じます。

## リストとドロップダウン

`ScrollList` と `DropdownMenu` の項目の操作です。**番号は0始まり**で、`selected_index` と同じです。

<div class="sig">pico.list_add(id: integer, text: string, opts?: table) <span class="ret">-> (なし)</span></div>

末尾に追加します。`opts` は `ScrollList` だけに効き、`{icon = IconID, color = パレット番号}`(項目ごとのアイコンと文字色)。

<div class="sig">pico.list_insert(id: integer, index: integer, text: string, opts?: table) <span class="ret">-> (なし)</span></div>

`index` 番目へ挿入します(`ScrollList` のみ。範囲外は末尾)。選択中の項目はずれた分だけ追従します。

<div class="sig">pico.list_remove(id: integer, index: integer) <span class="ret">-> ok: boolean</span></div>

取り除きます(範囲外は `false`)。選択中の項目を消すと選択は外れます。

<div class="sig">pico.list_get(id: integer, index: integer) <span class="ret">-> text: string, color: integer | nil, icon: integer</span></div>

範囲外は `nil`。

<div class="sig">pico.list_select(id: integer, index: integer) <span class="ret">-> (なし)</span></div>

選択します(`-1` で選択なし)。コールバックは呼ばれません。範囲外はエラー。

<div class="sig">pico.list_scroll_to(id: integer, index: integer) <span class="ret">-> (なし)</span></div>

`ScrollList` を、その項目が先頭に来る位置までスクロールします。

## タブ

`TabBar` は最大8個のタブを持てます。番号は0始まりです。

<div class="sig">pico.tab_label(id, index) <span class="ret">-> label: string | nil</span></div>
<div class="sig">pico.tab_set_label(id, index, label) <span class="ret">-> ok: boolean</span></div>
<div class="sig">pico.tab_remove(id, index) <span class="ret">-> ok: boolean</span></div>
<div class="sig">pico.tab_clear(id) <span class="ret">-> (なし)</span></div>

<div class="sig">pico.tab_link(tab_id: integer, index: integer, widget_id: integer) <span class="ret">-> (なし)</span></div>
<div class="sig">pico.tab_unlink(tab_id: integer, index?: integer) <span class="ret">-> (なし)</span></div>

**タブの中身の切り替えを自動化**します。`tab_link` した `widget_id` は、そのタブが選ばれている間だけ表示されます(タブが切り替わるたびに自動で `visible` を切り替えます。`tab_changed` イベントとは別に動きます)。1つのタブに複数のウィジェットを連動できます(全部で32個まで)。`tab_unlink` は連動を外します(`index` を省くとそのTabBarの全部)。

```lua
local tabs = pico.create("TabBar")
for _, name in ipairs({"設定", "履歴", "情報"}) do pico.tab_add(tabs, name) end
pico.tab_link(tabs, 0, settings_panel)
pico.tab_link(tabs, 1, history_panel)
pico.tab_link(tabs, 2, about_panel)
```
