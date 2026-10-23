---
aliases: ["/guide/events/"]
title: "イベント (pico.on)"
weight: 20
description: "タップ・値の変化・ダイアログの結果などをコールバックで受け取る"
---

## 基本形

```lua
pico.on(id, event_name, function(id, ...)
    -- ...
end)
```

- 同じ `id` と `event_name` へもう一度 `pico.on()` すると、前のコールバックと差し替わります。
- `pico.off(id, event_name)` で外せます。ウィジェットを `pico.destroy` すると全部外れます。
- 知らないイベント名や、そのウィジェットが出さないイベント(`Button` に `"checked_changed"` など)はエラーになります。

値が変わるイベントの多くは、引数に `id` しか渡しません。変わった後の値は `pico.get(id, "...")` で読みます。

```lua
local checkbox = pico.create("Checkbox")
pico.on(checkbox, "checked_changed", function(id)
    pico.log(tostring(pico.get(id, "checked")))
end)
```

## タッチ(全ウィジェット)

| イベント名 | 起きるとき |
|---|---|
| `press_start` | 押し始め |
| `press_move` | 押したまま動かした |
| `press_end` | 押した場所の上で離した |
| `press_out` | 押したまま外へ出た |

コールバックには `(id, x, y, lx, ly, dx, dy)` が届きます。

| 引数 | 意味 |
|---|---|
| `x`, `y` | 画面の座標 |
| `lx`, `ly` | そのウィジェットの左上からの座標。盤面のマス目を求めるのに使う |
| `dx`, `dy` | 前のタッチのイベントからの移動量(`press_start` では `0`)。ドラッグに使う |

```lua
local board = pico.create("Canvas")
pico.set(board, "w", 160); pico.set(board, "h", 160)
pico.on(board, "press_start", function(id, x, y, lx, ly)
    local col, row = lx // 20, ly // 20      -- 20pxのマス目
    select_cell(col, row)
end)
pico.on(board, "press_move", function(id, x, y, lx, ly, dx, dy)
    scroll_x = scroll_x - dx                  -- ドラッグでスクロール
end)
```

使わない引数は書かなくてかまいません(`function(id) ... end`)。タッチ以外のイベントの中や `loop()` から今のタッチ位置を知りたいときは [`pico.get_touch()`](../../../api/ui/touch/) を使います。

## ジェスチャー(全ウィジェット)

| イベント名 | 条件 | 引数 |
|---|---|---|
| `long_press` | 動かさずに0.5秒押し続けた(押している間に1回) | `(id, x, y, lx, ly)` |
| `double_tap` | 0.4秒以内の短いタップを、0.4秒以内・24px以内でもう一度 | `(id, x, y, lx, ly)` |
| `swipe` | 24px以上を0.7秒以内に動かして離した | `(id, direction, dx, dy, x, y)` |

`direction` は `"left"` `"right"` `"up"` `"down"` です。

```lua
pico.on(card, "swipe", function(id, dir)
    if dir == "left" then next_page() elseif dir == "right" then prev_page() end
end)
pico.on(card, "long_press", function(id, x, y) show_menu(x, y) end)
```

- `press_*` とは別に判定するので、同じタッチから `press_end` と `swipe` の両方が届くことがあります。
- 届くのは押し始めた位置にあるウィジェットです。無効(`enabled = false`)・非表示のウィジェットには届きません。

## ウィジェットごとのイベント

| イベント名 | ウィジェット | 引数 | 起きるとき・値の読み方 |
|---|---|---|---|
| `checked_changed` | `Checkbox` | `(id)` | `pico.get(id, "checked")` |
| `value_changed` | `NumberSlider` | `(id)` | `pico.get(id, "value")`。ドラッグ中は毎フレーム |
| `tab_changed` | `TabBar` | `(id)` | `pico.get(id, "tab_selected")`。同じタブの押し直しでは起きない |
| `dropdown_changed` | `DropdownMenu` | `(id)` | `pico.get(id, "selected_index")` |
| `text_changed` | `Textbox` | `(id)` | キーボードを閉じて確定したとき。`pico.get(id, "text")` |
| `text_input` | `Textbox` | `(id)` | 1文字ごと。入力中も `pico.get(id, "text")` が最新 |
| `select_item` | `ScrollList` | `(id, already_selected)` | `pico.get(id, "selected_index")`。`already_selected` は同じ項目を続けてタップしたか(2回タップで開く、のようなUIに) |
| `duration_changed` | `DurationPicker` | `(id, total_ms)` | ▲▼で変わったとき(`pico.set` では起きない) |
| `day_selected` | `MonthGrid` | `(id, day)` | 日付をタップした |
| `link_tap` | `MarkdownView` | `(id, path)` | リンクをタップした |
| `text_tap` | `TextView` | `(id, byte_offset)` | 動かさずにタップした |
| `scrolled` | `ScrollContainer` | `(id, scroll_y)` | スクロール位置が変わった |

```lua
local list = pico.create("ScrollList")
pico.on(list, "select_item", function(id, already_selected)
    if already_selected then
        -- 2回目のタップ = 開く
    end
end)
```

## render(Canvas のみ)

`Canvas` の中身を描くコールバックです。描き直しが要るたびに呼ばれ、この中でだけ `pico.draw_*` が正しく描けます。詳しくは [Canvasと直接描画](../../graphics/drawing/) を見てください。

```lua
local canvas = pico.create("Canvas")
pico.on(canvas, "render", function(id)
    pico.fill_rect(10, 10, 50, 50, pico.COLOR_RED)
end)
```

## closed(ダイアログのみ)

`pico.show_*` が返すダイアログが閉じたときに呼ばれます。決定で閉じたときは3番目の引数に結果が入ります。

```lua
pico.on(dialog_id, "closed", function(id, is_ok, value)
    if is_ok then
        -- value: 入力した文字列・選んだ番号など
    end
end)
```

ダイアログは閉じると自動で片付くので、`closed` を登録しなくてもかまいません。詳しくは [ダイアログ](../dialogs/) を見てください。

イベントの一覧表は [イベント対応表](../../../reference/widget-events/) にもあります。
