---
title: "Canvasと直接描画"
weight: 40
description: "円・線・画像などをウィジェットを介さず直接描く"
---

## 最重要ルール: 直接描画は render コールバックの中で使う

`pico.draw_*` / `pico.fill_*` / `pico.clear_rect` / `pico.draw_text` / `pico.draw_image` は、ウィジェットを介さず画面の共有フレームへ直接描きます。しかし **これらを `loop()` やボタンのコールバックから単発で呼んでも、描いた内容はすぐに消えます。**

理由: 画面の再描画は「dirty(変化があった)矩形ごとに、背景色で塗りつぶしてから、その上に重なるウィジェットだけを描き直す」という仕組みで動いています。ウィジェットに属さない場所へ直接描いても、次にその領域が dirty になった瞬間(シーン遷移時の全画面更新を含め、ほぼ確実に起こります)に誰も描き直さないまま消えてしまいます。

**正しい使い方は `Canvas` ウィジェットに乗せることです。**

```lua
local canvas = pico.create("Canvas")
pico.set(canvas, "x", 20)
pico.set(canvas, "y", 20)
pico.set(canvas, "w", 100)
pico.set(canvas, "h", 100)

pico.on(canvas, "render", function(id)
    -- ここで呼んだ pico.draw_* だけが、再描画のたびに正しく反映される
    pico.fill_circle(70, 70, 30, 9)   -- PICO_BLUE
    pico.draw_text(30, 30, "canvas")
end)
```

`render` コールバックは `FlushDirty()` の合成サイクルの中で毎回呼ばれるため、そのたびに**内容を全部描き直す**前提で書きます。差分描画は考えなくて構いません。

生成直後のウィジェットは初期状態で dirty なので、静的な内容なら追加の操作なしで一度 `render` が呼ばれます。アニメーションなど再描画したいときだけ `loop()` から `pico.invalidate()` を呼んでください。

## 再描画のリクエスト

```lua
pico.invalidate(id)             -- そのウィジェットの矩形をdirty化(次のFlushDirty()でrenderが呼ばれる)
pico.mark_dirty(x, y, w, h)     -- 任意の矩形を直接dirty化する低レベルAPI
```

`Canvas` の内容を毎フレーム変えたい場合は `loop(dt)` の中で `pico.invalidate(canvas)` を呼びます。

```lua
local angle = 0
function loop(dt)
    angle = angle + dt * 0.002
    pico.invalidate(canvas)
end
```

## 座標系と色

- 座標は `pico.content_rect()` と同じ**絶対スクリーン座標**です(Canvas自身の左上を基準にした相対座標ではありません)。
- 色はPICO-8風16色パレットの番号(0〜15)です。範囲外の値は素通りするので注意してください。

| 番号 | 色 | 番号 | 色 |
|---|---|---|---|
| 0 | 黒(既定の前景色) | 8 | ダークグレー |
| 1 | ネイビー | 9 | 青 |
| 2 | ダークグリーン | 10 | 緑 |
| 3 | ダークシアン | 11 | シアン |
| 4 | マルーン | 12 | 赤 |
| 5 | パープル | 13 | マゼンタ |
| 6 | オリーブ | 14 | 黄 |
| 7 | ライトグレー | 15 | 白(既定の背景色) |

## 直接描画エリア(クリップ)

`Canvas` の矩形からはみ出す描画を防ぎたい場合に使います。

```lua
pico.on(canvas, "render", function(id)
    pico.set_draw_area(x, y, w, h)
    -- この範囲の外は描かれない
    pico.fill_circle(cx, cy, huge_radius, 12)
    pico.clear_draw_area()
end)
```

> 共有フレームはクリップ矩形を1個しか持てません。`set_draw_area()` を呼んだまま `render` コールバックを抜けると、以降**他のウィジェットの描画まで**同じ矩形に切り詰められてしまいます。この事故を防ぐため、`Canvas` は `render` コールバックから戻った直後に自動で `clear_draw_area()` 相当の処理を行いますが、**呼び出したら同じコールバック内で必ず `pico.clear_draw_area()` を呼ぶ**のが安全な書き方です。

## 図形描画API早見表

| 関数 | 説明 |
|---|---|
| `pico.draw_pixel(x, y, color)` | 1ピクセル |
| `pico.draw_line(x0, y0, x1, y1, color)` | 直線 |
| `pico.draw_rect(x, y, w, h, color)` | 矩形の枠線 |
| `pico.fill_rect(x, y, w, h, color)` | 矩形の塗りつぶし |
| `pico.draw_circle(x, y, r, color)` | 円の輪郭 |
| `pico.fill_circle(x, y, r, color)` | 円の塗りつぶし |
| `pico.clear_rect(x, y, w, h [, color])` | 矩形を塗りつぶす(色省略時は背景色) |
| `pico.draw_text(x, y, text [, color [, font_size]])` | テキスト描画。右端は画面幅に自動で収まる |

厳密なシグネチャは [APIリファレンス: 描画](../../api/drawing/) を参照してください。

## 画像を描く

`.pimg` 形式の画像は `pico.image_load()` でデコードしてから `pico.draw_image()` で描きます。詳細は [画像](../images/) を参照してください。

## タップ位置の取得

`Canvas` の `press_start` 等のコールバックは、他のウィジェットと同じく `id` しか受け取りません。「盤面全体を1枚の `Canvas` にして、押された座標からマス目を逆算する」といった使い方には [`pico.get_touch()`](../../api/touch/) を組み合わせます。

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

`pico.get_touch()` が返す座標は `pico.draw_*` と同じ絶対スクリーン座標なので、`Canvas` 自身の位置(`canvas_x`/`canvas_y`)を引いてから逆算します。`pico.get_touch()` が無かった頃は、マス目の数だけ小さな `Canvas` を敷き詰めてそれぞれの `press_start` で判定するしかありませんでした(この方法自体は今でも有効で、マスごとに全く違う描画をしたい場合はこちらのほうが素直なこともあります)。
