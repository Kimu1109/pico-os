---
aliases: ["/guide/freehand-drawing/"]
title: "手書き入力(CanvasRaster)"
weight: 30
description: "CanvasRasterで自由線を描き、保存・読み込みする"
---

`CanvasRaster`(`pico.create("CanvasRaster")`)は、`Canvas` と違って**描いた絵を自分で覚えている**ウィジェットです。指でなぞった跡をそのまま線として残すので、`render` コールバックを書く必要がありません。手書きメモ・お絵描き・署名欄のような自由線描画に向いています。

## 最小構成

```lua
local x, y, w, h = pico.content_rect()

local canvas = pico.create("CanvasRaster")
pico.set(canvas, "x", x)
pico.set(canvas, "y", y)
pico.set(canvas, "w", w)
pico.set(canvas, "h", h)
pico.set(canvas, "canvas_mode", pico.CANVAS_LINE) -- フリーハンド
pico.set(canvas, "color", pico.COLOR_BLACK)
pico.set(canvas, "brush_radius", 2)
```

これだけで、指(マウス)でドラッグした軌跡がそのまま線として残ります。`Canvas` のように毎フレーム全部を描き直す必要が無く、過去に描いた線をLua側で覚えておく必要もありません。

**`w`/`h` は作った直後に1回だけ設定してください。** 大きさを変えると、それまでに描いた絵は消えます。

## ペンの色を変える

```lua
pico.set(canvas, "color", pico.COLOR_BLUE)
```

## 消しゴム

専用の「消しゴムモード」はありません。**白(`pico.COLOR_WHITE` = 15)をペンの色にする**のが消しゴムです。太めのブラシにしておくと消しやすくなります。

```lua
pico.set(canvas, "color", pico.COLOR_WHITE)
pico.set(canvas, "brush_radius", 8)
```

## 全消去

```lua
pico.canvas_clear(canvas)
```

## 保存・読み込み

キャンバスの絵は `.pimg`(画像と同じ形式)としてSDへ保存し、読み戻せます。

```lua
local SAVE_PATH = pico.path_join(pico.app_dir(), "memo.pimg")

pico.canvas_save(canvas, SAVE_PATH) -- 成功ならtrue
pico.canvas_load(canvas, SAVE_PATH) -- 成功ならtrue。キャンバスの大きさも保存したときの大きさになる
```

引数・失敗する条件・元に戻す(`pico.canvas_undo`)は [APIリファレンス: ラスタキャンバス](../../../api/graphics/canvas/) を参照してください。

## 描画モード(canvas_mode)

`canvas_mode` を変えると、なぞったときに描くものが変わります。

| 値 | 定数 | 描くもの |
|---|---|---|
| `0` | `CANVAS_LINE` | なぞった跡をそのまま線にする(既定。手書きに使う) |
| `1` | `CANVAS_RECT` | なぞり始めと終わりを対角にした四角 |
| `2` | `CANVAS_ELLIPSE` | なぞった範囲に内接する楕円 |
| `3` | `CANVAS_ARROW` | なぞり始めから終わりへの矢印 |
| `4` | `CANVAS_STRAIGHT` | なぞり始めから終わりへの直線 |
| `5` | `CANVAS_FILL` | 触れた所とつながった同じ色の範囲を塗りつぶす |

四角・楕円・矢印・直線は、なぞっている間は下書きだけを見せ、指を離したときに描きます。四角・楕円は `filled = true` で塗りつぶしになります。

## 完成例: 2色ペン+消しゴムのスクラッチパッド

黒ペン・青ペン・消しゴム・全消去・保存・読込のボタンを揃えた最小のメモアプリです。全体は「スクラッチパッド」アプリ(`/lua/apps/スクラッチパッド/main.lua`)にあります。描く場所を広く取るため、ツールバーは文字ではなくアイコンのボタン(`pico.set(id, "icon_id", ...)`。[Button](../../../reference/widget-properties/#button))にしてあります。

```lua
local canvas = pico.create("CanvasRaster")
-- (x/y/w/hの設定は省略)

local btn_black = pico.create("Button")
pico.set(btn_black, "icon_id", pico.ICON_Brush)
pico.set(btn_black, "background_color", pico.COLOR_BLACK) -- 背景をペンの色にする
-- (他のボタンも同様。x/y/w/hの設定は省略)

local function selectPen(color, radius)
    pico.set(canvas, "color", color)
    pico.set(canvas, "brush_radius", radius)
end

pico.on(btn_black, "press_start", function() selectPen(0, 2) end)   -- 黒
pico.on(btn_blue, "press_start", function() selectPen(9, 2) end)    -- 青
pico.on(btn_eraser, "press_start", function() selectPen(15, 8) end) -- 消しゴム

pico.on(btn_clear, "press_start", function() pico.canvas_clear(canvas) end)
pico.on(btn_save, "press_start", function() pico.canvas_save(canvas, SAVE_PATH) end)
pico.on(btn_load, "press_start", function() pico.canvas_load(canvas, SAVE_PATH) end)
```
