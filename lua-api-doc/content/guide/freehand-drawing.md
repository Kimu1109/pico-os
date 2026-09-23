---
title: "手書き入力(CanvasRaster)"
weight: 45
description: "CanvasRasterで自由線を描き、保存・読み込みする"
---

`CanvasRaster`(`pico.create("CanvasRaster")`)は、`Canvas`(`LuaCanvas`)と違って**自分専用のピクセルバッファを持ち続ける**ウィジェットです。タッチのドラッグをそのまま線として焼き込むので、`render` コールバックを自分で書く必要がありません。手書きメモ・お絵描き・署名欄のような自由線描画に向いています。

## 最小構成

```lua
local x, y, w, h = pico.content_rect()

local canvas = pico.create("CanvasRaster")
pico.set(canvas, "x", x)
pico.set(canvas, "y", y)
pico.set(canvas, "w", w)
pico.set(canvas, "h", h)
pico.set(canvas, "canvas_mode", 0) -- Line(フリーハンド)
pico.set(canvas, "color", 0)        -- PICO_BLACK
pico.set(canvas, "brush_radius", 2)
```

これだけで、指(マウス)でドラッグした軌跡がそのまま線として残ります。`Canvas` のように毎フレーム全部を描き直す必要が無く、過去に描いた線をLua側で覚えておく必要もありません。

**`w`/`h` は生成直後のみ変更してください。** `pico.set(id, "w"/"h", ...)` は内部のバッファを作り直すため、変更するとそれまでの描画内容が消えます。

## ペンの色を変える

```lua
pico.set(canvas, "color", 9) -- PICO_BLUE
```

## 消しゴム

専用の「消しゴムモード」はありません。**背景色(`PICO_WHITE` = 15)をペンの色として使う**のが消しゴムです。太めのブラシにしておくと消しやすくなります。

```lua
local ERASER_WHITE = 15
local ERASER_RADIUS = 8

pico.set(canvas, "color", ERASER_WHITE)
pico.set(canvas, "brush_radius", ERASER_RADIUS)
```

## 全消去

```lua
pico.canvas_clear(canvas)
```

## 保存・読み込み

キャンバスの内容は `.pimg`(画像で使うのと同じ4bpp+RLE形式)としてSDへ保存・復元できます。

```lua
local SAVE_PATH = "/lua/apps/myapp/memo.pimg"

pico.canvas_save(canvas, SAVE_PATH) -- 成功ならtrue
pico.canvas_load(canvas, SAVE_PATH) -- 成功ならtrue。キャンバスのw/hも保存時のサイズへ合う
```

厳密なシグネチャ・失敗条件は [APIリファレンス: ラスタキャンバス](../../api/canvas/) を参照してください。

## 描画モード(canvas_mode)

`canvas_mode` を変えると、ドラッグの解釈そのものが変わります。

| 値 | モード | 挙動 |
|---|---|---|
| `0` | Line | ドラッグの軌跡をそのまま線として焼き込む(既定。手書きに使う) |
| `1` | Rect | ドラッグの始点〜終点を対角線とする矩形を1つ描く |
| `2` | Ellipse | ドラッグの始点〜終点を外接矩形とする楕円を1つ描く |
| `3` | Arrow | ドラッグの始点から終点への矢印を1本描く |

Rect/Ellipse/Arrowは「指を離した瞬間」に1つだけ確定します(ドラッグ中はプレビューが表示されます)。手書きメモには `0`(Line)を使ってください。

## 完成例: 2色ペン+消しゴムのスクラッチパッド

黒ペン・青ペン・消しゴム・全消去・保存・読込のボタンを揃えた最小のメモアプリです。完全な実装は `pc/sdcard/lua/apps/スクラッチパッド/main.lua` を参照してください。ツールバーは描画領域を広く取るため、文字ではなくアイコンのボタン(`pico.set(id, "icon_id", ...)`。[アイコンボタン](../../reference/widget-properties/#button) 参照)にしてあります。

```lua
local canvas = pico.create("CanvasRaster")
-- (x/y/w/hの設定は省略)

local btn_black = pico.create("Button")
pico.set(btn_black, "icon_id", 53)   -- Brush(IconIDの一覧は reference/limits.md 参照)
pico.set(btn_black, "background_color", 0) -- 背景色をペンの色そのものにする
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
