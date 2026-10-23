---
aliases: ["/api/touch/"]
title: "タッチ"
weight: 40
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

- タッチのイベント(`press_start` など)は座標を引数でも受け取れます([イベント](../../../guide/ui/events/#タッチ全ウィジェット))。この関数は、それ以外の場所で今のタッチを知りたいときに使います。
- `press_start` のコールバックの中で呼べば、そのタップの座標と一致します。
- `press_end`(離した瞬間)のコールバック内で呼んだ場合、`x` / `y` は離す直前の最後の位置を保持したままですが、`is_touched` は `false` になります。
- `press_start` 等のイベントを介さず、`loop(dt)` の中から画面の状態を素朴にポーリングする使い方もできます(`is_touched` で押下中かどうかを見る)。

## 使用例: 1枚のCanvasでマス目を判定する

1枚の大きな `Canvas` で、タップされたマスを求めます(イベントの引数 `lx`, `ly` を使っても同じことができます)。

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

詳しくは [Canvasと直接描画](../../../guide/graphics/drawing/) の「タップ位置の取得」も参照してください。
