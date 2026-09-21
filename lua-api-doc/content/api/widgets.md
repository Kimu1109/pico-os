---
title: "ウィジェット操作"
weight: 10
description: "create / destroy / set / get / on / add_child / remove_child / list_add / list_clear / tab_add"
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
