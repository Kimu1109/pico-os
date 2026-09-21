---
title: "タッチ"
weight: 25
description: "get_touch"
---

## pico.get_touch

<div class="sig">pico.get_touch() <span class="ret">-> x: integer, y: integer, is_touched: boolean</span></div>

現在(直近)のタッチ位置を返します。座標は `pico.draw_*` / `pico.content_rect()` と同じ**絶対スクリーン座標**です。

```lua
pico.on(some_id, "press_start", function(id)
    local x, y = pico.get_touch()
    pico.log("tapped at " .. x .. "," .. y)
end)
```

- `pico.on` のタップ系イベント(`press_start` / `press_end` / `press_move` / `press_out`)はいずれも引数として `id` しか渡しません。タップした**座標**まで必要な場合は、この関数をコールバックの中から呼んで組み合わせます。
- 戻り値はウィジェットの当たり判定(`HitTest`)が実際に使っているのと同じ生のタッチ状態です。そのため `press_start` コールバックの中で呼べば「そのタップが起きた座標」と一致します。
- `press_end`(離した瞬間)のコールバック内で呼んだ場合、`x` / `y` は離す直前の最後の位置を保持したままですが、`is_touched` は `false` になります。
- `press_start` 等のイベントを介さず、`loop(dt)` の中から画面の状態を素朴にポーリングする使い方もできます(`is_touched` で押下中かどうかを見る)。

## 使用例: 1枚のCanvasでマス目を判定する

マス目の数だけ `Canvas` を敷き詰めなくても、1枚の大きな `Canvas` と `pico.get_touch()` の組み合わせで、タップされたマスをスクリプト側で逆算できます。

```lua
local CELL = 24
local canvas_x, canvas_y = 20, 40

local canvas = pico.create("Canvas")
pico.set(canvas, "x", canvas_x)
pico.set(canvas, "y", canvas_y)
pico.set(canvas, "w", CELL * 8)
pico.set(canvas, "h", CELL * 8)

pico.on(canvas, "press_start", function(id)
    local x, y = pico.get_touch()
    local col = math.floor((x - canvas_x) / CELL) + 1
    local row = math.floor((y - canvas_y) / CELL) + 1
    -- (row, col) が実際に押されたマス
end)
```

詳しくは [Canvasと直接描画](../../guide/drawing/) の「タップ位置の取得」も参照してください。
