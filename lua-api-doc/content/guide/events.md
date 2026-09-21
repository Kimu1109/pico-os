---
title: "イベント (pico.on)"
weight: 20
description: "タップ・値変更・ダイアログの結果などをコールバックで受け取る"
---

## 基本形

```lua
pico.on(id, event_name, function(...)
    -- ...
end)
```

同じ `id` + `event_name` の組み合わせへ `pico.on()` を再度呼ぶと、古いコールバックは破棄されて新しい関数に差し替わります(リークしません)。対応していないイベント名や、そのウィジェット種別に対応しないイベントを指定するとエラーになります。

コールバックの引数は「変わった後の値そのもの」ではなく、多くの場合 **WidgetId のみ**です。値は `pico.get(id, "...")` でそのつど読みます(値の型・個数をイベントごとに変える複雑さを避けるための設計です)。例外は `select_item` と `closed` で、2つ目の追加の引数を渡します(下表参照)。

タップ座標だけは例外的に、イベント引数ではなく [`pico.get_touch()`](../../api/touch/) という別の関数で問い合わせます。コールバックの中から呼べば「今起きたタップの座標」を絶対スクリーン座標で取得できます。

```lua
pico.on(some_id, "press_start", function(id)
    local x, y = pico.get_touch()
    pico.log("tapped at " .. x .. "," .. y)
end)
```

## 共通4イベント(全ウィジェット対応)

タップ操作に関する4つのイベントは、生成できる全ウィジェットで使えます。

| イベント名 | 発生タイミング | コールバック引数 |
|---|---|---|
| `press_start` | 押し始め | `(id)` |
| `press_end` | 離した瞬間(押した場所の上で) | `(id)` |
| `press_move` | 押しながら移動 | `(id)` |
| `press_out` | 押したまま当たり判定の外へ出た | `(id)` |

```lua
local button = pico.create("Button")
pico.on(button, "press_start", function(id)
    pico.log("押された: " .. id)
end)
```

## ウィジェット固有イベント

| イベント名 | 対象ウィジェット | コールバック引数 | 値の読み方 |
|---|---|---|---|
| `checked_changed` | `Checkbox` | `(id)` | `pico.get(id, "checked")` |
| `value_changed` | `NumberSlider` | `(id)` | `pico.get(id, "value")`(ドラッグ中は毎フレーム発火) |
| `tab_changed` | `TabBar` | `(id)` | `pico.get(id, "tab_selected")`(同じタブの押し直しでは発火しない) |
| `dropdown_changed` | `DropdownMenu` | `(id)` | `pico.get(id, "selected_index")` |
| `text_changed` | `Textbox` | `(id)` | `pico.get(id, "text")`(オンスクリーンキーボードを閉じて確定した時のみ。1文字ごとには発火しない) |
| `select_item` | `ScrollList` | `(id, already_selected)` | `pico.get(id, "selected_index")`。`already_selected` は「同じ項目を2回連続でタップしたか」(2回タップで開く、のようなUIに使う) |

```lua
local checkbox = pico.create("Checkbox")
pico.on(checkbox, "checked_changed", function(id)
    local checked = pico.get(id, "checked")
    pico.log(tostring(checked))
end)

local list = pico.create("ScrollList")
pico.on(list, "select_item", function(id, already_selected)
    if already_selected then
        -- 2回目のタップ = 確定操作として扱う
    end
end)
```

対応するウィジェット種別以外へ登録しようとするとエラーになります(例: `Button` へ `"checked_changed"` を登録するとエラー)。

## `render`(Canvas限定)

`Canvas`(`pico.create("Canvas")`)にのみ登録できます。`FlushDirty()` の合成サイクルの中で呼ばれ、この中でだけ `pico.draw_*` 系が正しく機能します。詳細は [Canvasと直接描画](../drawing/) を参照してください。

```lua
local canvas = pico.create("Canvas")
pico.on(canvas, "render", function(id)
    pico.fill_rect(10, 10, 50, 50, 12)
end)
```

## `closed`(ダイアログ限定)

`pico.show_message` / `show_input` / `show_file_save` / `show_file_select` / `show_color` が返すIDにのみ登録できます。

```lua
pico.on(dialog_id, "closed", function(id, is_ok)
    if is_ok then
        -- InputDialogなら pico.get(id, "text") で入力文字列が読める
    end
end)
```

`closed` の実際のダイアログ破棄(`pico.destroy` 相当)は、`pico.on()` の登録有無に関わらず**ダイアログの生成時点で保証済み**です。`pico.on()` を呼ばなくても画面に居座り続けることはありません。詳細は [ダイアログ](../dialogs/) を参照してください。
