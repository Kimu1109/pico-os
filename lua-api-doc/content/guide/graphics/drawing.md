---
aliases: ["/guide/drawing/"]
title: "Canvasと直接描画"
weight: 10
description: "円・線・文字・画像を、ウィジェットを使わずに直接描く"
---

## 直接描画は Canvas の render の中で

`pico.draw_*` / `pico.fill_*` / `pico.draw_text` / `pico.draw_image` は画面へ直接描く関数です。ただし **`loop()` やボタンのコールバックから呼んでも、描いた絵はすぐに消えます。** OSは画面の変わった所を背景色で塗り直してからウィジェットを描き直すので、どのウィジェットにも属さない絵は誰も描き直さないからです。

**`Canvas` ウィジェットを置き、その `render` コールバックの中で描きます。**

```lua
local canvas = pico.create("Canvas")
pico.set(canvas, "x", 20)
pico.set(canvas, "y", 40)
pico.set(canvas, "w", 100)
pico.set(canvas, "h", 100)

pico.on(canvas, "render", function(id)
    pico.fill_circle(70, 90, 30, pico.COLOR_BLUE)
    pico.draw_text(30, 50, "canvas")
end)
```

- `render` は描き直しが要るたびに呼ばれます。毎回**全部を描き直す**つもりで書いてください。
- 作った直後に1回呼ばれるので、動かない絵ならこれだけで済みます。
- 絵を変えたいときは `pico.invalidate(canvas)` を呼ぶと、次のフレームで `render` が呼ばれます。

```lua
local angle = 0
function loop(dt)
    angle = angle + dt * 0.002
    pico.invalidate(canvas)   -- 毎フレーム描き直す
end
```

## 座標と色

- 座標は**画面の座標**です(Canvasの左上からではありません)。Canvasの位置は `pico.get_rect(canvas)` で読めます。
- 色は16色のパレットの番号(0〜15)で、`pico.COLOR_RED` などの[定数](../../../reference/constants/)でも書けます。番号と色の対応は [定数](../../../reference/constants/#色パレット) にあります。
- ダークテーマでは番号の色が明暗で入れ替わります([`pico.theme`](../../../api/system/misc/#picotheme))。

## 描く範囲を絞る

Canvasの外にはみ出す絵を切りたいときは、描く範囲(クリップ)を決めます。

```lua
pico.on(canvas, "render", function(id)
    pico.set_draw_area(x, y, w, h)
    pico.fill_circle(cx, cy, 200, pico.COLOR_RED)   -- 範囲の外は描かれない
    pico.clear_draw_area()
end)
```

範囲は画面全体で1つしかありません。`set_draw_area()` を呼んだら、同じコールバックの中で必ず `clear_draw_area()` を呼んでください(呼び忘れても `render` から戻ると元に戻ります)。

## 描画関数の早見表

| 関数 | 説明 |
|---|---|
| `pico.draw_pixel(x, y, color)` | 1画素 |
| `pico.draw_line(x0, y0, x1, y1, color [, width])` | 直線 |
| `pico.draw_rect(x, y, w, h, color)` / `pico.fill_rect(...)` | 四角の枠 / 塗りつぶし |
| `pico.draw_circle(x, y, r, color)` / `pico.fill_circle(...)` | 円の輪郭 / 塗りつぶし |
| `pico.draw_ellipse` / `draw_triangle` / `draw_polygon` / `draw_arc`(と `fill_*`) | 楕円・三角形・多角形・円弧 |
| `pico.clear_rect(x, y, w, h [, color])` | 四角を塗りつぶす(色を省くと背景色) |
| `pico.draw_text(x, y, text [, color, font_size, align])` | 文字 |
| `pico.draw_text_wrapped(x, y, w, text, ...)` | 幅で折り返す文字 |
| `pico.draw_image(handle, x, y)` | 画像([画像](../images/)) |

引数の細かい意味は [直接描画API](../../../api/graphics/drawing/) にあります。

## タップした位置を知る

1枚の `Canvas` で盤面を作るときは、`press_start` の引数のウィジェット内の座標(`lx`, `ly`)からマス目を求めます。

```lua
local CELL = 24
local board = pico.create("Canvas")
pico.set(board, "w", CELL * 8)
pico.set(board, "h", CELL * 8)

pico.on(board, "press_start", function(id, x, y, lx, ly)
    local col = lx // CELL + 1
    local row = ly // CELL + 1
    -- (row, col) が押されたマス
end)
```

## もっと楽に描くには

- 置いたら動かない四角・円・線は、[図形ウィジェット](../../ui/shapes/) にすると描き直しを考えなくて済みます。
- 指でなぞった線を残すなら [手書き入力(CanvasRaster)](../freehand-drawing/)。
- スプライトやタイルマップで動くゲームなら [ゲームエンジン `pico.game`](../../../api/game/engine/) が描き直しまで引き受けます。
