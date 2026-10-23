---
aliases: ["/guide/widgets/"]
title: "ウィジェットの生成・操作・破棄"
weight: 10
description: "pico.create / set / get / destroy の基本と、WidgetIdの考え方"
---

## ウィジェットを作る

```lua
local button = pico.create("Button")
pico.set(button, "x", 10)
pico.set(button, "y", 10)
pico.set(button, "w", 80)
pico.set(button, "h", 30)
pico.set(button, "text", "OK")
```

`pico.create(種別名)` はウィジェットを1つ作り、それを指す整数の **WidgetId** を返します。以降の操作(`pico.set` / `pico.get` / `pico.on` / `pico.destroy` など)はすべてこのIDで指定します。

- 種別名は `"Button"` `"Label"` `"Textbox"` `"Canvas"` などの文字列で、大文字小文字を区別します。作れる種別の一覧は [ウィジェット種別一覧](../../../reference/widget-types/) にあります。
- 知らない種別名を渡したときと、メモリが足りなくて作れなかったときはエラーになります。
- **作った直後の位置と大きさは仮の値です。** 必ず `x` / `y` / `w` / `h` を設定してください。

ウィジェットを作る・プロパティを並べる・イベントを付ける、をまとめて1つの表で書ける [`pico.ui`](../../../api/system/stdlib/#picoui--宣言的なuiの組み立て) もあります。

### Button の大きさ

`Button` の `w` / `h` は、枠と立体表示を含めた全体の大きさです。文字やアイコンはその内側(縦横とも9px小さい範囲)に収まります。`text` を差し替えても箱は縮まず、それまでで一番大きかった文字に合わせた大きさのままです。

## プロパティの読み書き

```lua
pico.set(id, name, value)
local value = pico.get(id, name)
```

- `name` はプロパティ名の文字列(`snake_case`)です。例: `"x"` `"text"` `"font_size"` `"checked"`。
- どのウィジェットにもある名前(`x` `y` `w` `h` `visible` `enabled` `background_color` など)と、種別ごとの名前(`text` `checked` `value` など)があります。全部の対応は [プロパティ対応表](../../../reference/widget-properties/) にあります。
- `pico.set()` に、そのウィジェットが持たない名前や型の合わない値を渡すとエラーになります。
- `pico.get()` は、そのウィジェットが持たない名前なら `nil` を返します。ただし綴りの間違いのように**どのウィジェットにも無い名前**は、`set` も `get` もエラーになります。
- 数値は `1` と書いても `1.0` と書いてもかまいません(プロパティの型に合わせて変換されます)。
- 色・文字の大きさ・アイコンなどは、数値の代わりに [定数](../../../reference/constants/)(`pico.COLOR_RED` `pico.FONT_BIG` `pico.ICON_Home` …)で書けます。

## 破棄する

```lua
pico.destroy(id)
```

破棄はそのフレームの終わりにまとめて行われます。同時に、そのIDに登録した `pico.on()` のコールバックも全部外れます。

- 破棄したウィジェットのIDを `pico.set` / `pico.get` / `pico.on` などに渡すとエラーになります(破棄済みのものを触る間違いに気づけます)。
- `pico.destroy()` だけは、破棄済みのIDを渡しても何もしません(2回呼んでも安全です)。
- アプリを閉じると、作ったウィジェットは全部片付きます。閉じる前に自分で消す必要はありません。

## コンテナへの出し入れ

`LayoutContainer` / `GridContainer` / `ScrollContainer` は子のウィジェットを持てます。

```lua
pico.add_child(container_id, child_id)
pico.remove_child(container_id, child_id)
```

- `add_child` した子は、コンテナが位置を決めます(並べる向き・列数などはコンテナのプロパティ)。コンテナを消すと子も消えます。
- `remove_child` は子を**破棄せずに**取り外します。取り外した子の `x` / `y` はコンテナの中での値のままなので、必要なら置き直してください。
- コンテナ以外を渡したとき、子でないものを `remove_child` したときはエラーになります。

詳しくは [レイアウトコンテナ](../containers/) を見てください。

## リスト・ドロップダウン・タブの項目

`ScrollList` / `DropdownMenu` / `TabBar` は作った時点では空です。項目は専用の関数で足します。

```lua
pico.list_add(list, "りんご")                       -- ScrollList / DropdownMenu
pico.list_add(list, "みかん", { color = pico.COLOR_RED, icon = pico.ICON_Star })  -- 色とアイコン(ScrollListのみ)
pico.list_clear(list)
pico.tab_add(tabs, "設定")                          -- TabBar(8個まで。超えると false)
```

件数は `pico.get(id, "item_count")`(`TabBar` は `"tab_count"`)で読めます。挿入・削除・選択などの関数は [ウィジェット操作API](../../../api/ui/widgets/#リストとドロップダウン) にあります。

## 名前で探す

IDを全部変数で持ち回る代わりに、名前を付けて後から引けます。

```lua
pico.set_name(score_label, "score")
-- 別の場所で
pico.set(pico.find("score"), "text", "100")
```
