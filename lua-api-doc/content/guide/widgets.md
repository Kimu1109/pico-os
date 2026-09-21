---
title: "ウィジェットの生成・操作・破棄"
weight: 10
description: "pico.create/destroy/set/getの基本と、WidgetIdの考え方"
---

## WidgetIdとは

`pico.create()` はウィジェットを1つ生成し、それを指す**32bit整数のID**(WidgetId)を返します。以降、そのウィジェットへの操作(`pico.set` / `pico.get` / `pico.on` / `pico.destroy` など)はすべてこのIDで行います。C++側のポインタをLuaへ直接渡さないための仕組みで、内部的には「種別 + 世代 + スロット番号」がパックされています。

- **破棄済みのIDを使い回すことはできません。** `pico.destroy()` されたウィジェットのIDを後から操作しようとすると、`pico.set`/`pico.get`/`pico.on` はエラーになります(誤って解放済みのウィジェットを触る事故を検出できます)。
- IDが無効なとき、`pico.destroy()` だけは黙って無視します(二重destroyを許容)。それ以外(`set`/`get`/`on`/`add_child` など)はエラーになります。

## ウィジェットを作る

```lua
local id = pico.create(type_name)
```

`type_name` は次のいずれかの文字列です(大文字小文字を区別します)。

`Button` / `Label` / `Textbox` / `NumberInput` / `Checkbox` / `Icon` / `Image` / `NumberSlider` / `ScrollContainer` / `ScrollList` / `CanvasRaster` / `LayoutContainer` / `GridContainer` / `TabBar` / `DropdownMenu` / `Canvas`

全種別の詳細(生成直後の初期値・対応プロパティ)は [ウィジェット種別一覧](../../reference/widget-types/) を参照してください。

未知の種別名を渡す、またはメモリ不足でウィジェット本体の確保に失敗すると `pico.create()` はエラー(`luaL_error`)になります。

> **生成直後の位置・大きさは仮の値です。** 必ず `pico.set()` で `x` / `y` / `w` / `h` を設定してから使ってください。

```lua
local button = pico.create("Button")
pico.set(button, "x", 10)
pico.set(button, "y", 10)
pico.set(button, "w", 80)
pico.set(button, "h", 30)
pico.set(button, "text", "OK")
```

## プロパティの読み書き

```lua
pico.set(id, name, value)
local value = pico.get(id, name)
```

- `name` はプロパティ名の文字列(`snake_case`)です。例: `"x"`, `"text"`, `"font_size"`, `"checked"`。
- 全ウィジェット共通の名前(`x` / `y` / `w` / `h` / `visible` / `background_color`)と、ウィジェットごとの固有名(`text` / `checked` / `value` など)があります。完全な対応表は [プロパティ対応表](../../reference/widget-properties/) を参照してください。
- `pico.set()` に**そのウィジェットが対応していない名前**や**型の合わない値**を渡すとエラーになります。
- `pico.get()` は**非対応の名前**なら `nil` を返します(存在しないプロパティを問い合わせても落ちません)。ただし**プロパティ名自体が未知**(綴りミス等)の場合はどちらもエラーになります。

### 数値の型について

Lua には整数と浮動小数点数の書き分けがありませんが、内部のプロパティは `Int` か `Float` のどちらかに固定されています。`pico.set()` は数値を渡されると、まず整数として書き込みを試し、失敗したら浮動小数点数として試します。呼び出し側が `1` と書くか `1.0` と書くかを気にする必要はありません。

```lua
pico.set(slider, "value", 3.5)  -- NumberSlider.value は Float
pico.set(label, "font_size", 1) -- Label.font_size は Int (FontFn::FontSize)
```

## 破棄する

```lua
pico.destroy(id)
```

その場では消えず、フレーム境界でまとめて破棄されます(`WidgetFunctions::DestroyLater()` と同じ仕組み)。破棄と同時に、そのIDに登録していた `pico.on()` のコールバックもすべて解除されます。

## コンテナへの出し入れ

`LayoutContainer` / `GridContainer` / `ScrollContainer` は子ウィジェットを持てます。

```lua
pico.add_child(container_id, child_id)
pico.remove_child(container_id, child_id)
```

- `pico.add_child()` は子の**所有権をコンテナへ移します**。以降、子の位置はコンテナのレイアウトロジック(方向・整列・列数など)に従います。
- `pico.remove_child()` は子を**破棄せずに**取り外します。取り外した子は次のフレームから独立したルートウィジェットとして描画・タップ判定の対象に戻ります。**x/y座標はコンテナ内での相対値のまま**残るので、必要なら `pico.set(id, "x"/"y", ...)` で置き直してください。
- 対応していないウィジェット種別(コンテナ以外)を渡すとエラーになります。
- 指定した子が実際にそのコンテナの子でない場合、`pico.remove_child()` はエラーになります。

コンテナの詳しい使い方(方向・整列・列数などのプロパティ)は [レイアウトコンテナ](../containers/) を参照してください。

## リスト系ウィジェットへの項目追加

`ScrollList` / `DropdownMenu` / `TabBar` は生成時点では空です。項目を追加する専用APIがあります。

```lua
pico.list_add(id, text)     -- ScrollList / DropdownMenu
pico.list_clear(id)         -- ScrollList / DropdownMenu
pico.tab_add(id, label)     -- TabBar (最大4個。上限超過ならfalseを返す)
```

`pico.list_add()` で追加した `ScrollList` の項目にアイコンは付けられません(既定アイコン固定)。件数は `pico.get(id, "item_count")` で読めます(読み取り専用)。
