---
title: "ラスタキャンバス"
weight: 32
description: "canvas_clear / canvas_save / canvas_load(CanvasRaster専用)"
---

`CanvasRaster`(`pico.create("CanvasRaster")`)専用の3関数です。他のウィジェットや `Canvas`(`LuaCanvas`)には使えません(渡すとエラーになります)。

`CanvasRaster` は自分専用のピクセルバッファを持ち続けるウィジェットで、`press_move` のドラッグをそのまま線として焼き込みます(手書きメモのような自由線描画に向きます)。詳しい使い方は [手書き入力(CanvasRaster)](../../guide/freehand-drawing/) を参照してください。

## pico.canvas_clear

<div class="sig">pico.canvas_clear(id: WidgetId) <span class="ret">-> (なし)</span></div>

キャンバスを白紙(`PICO_WHITE`)に戻します。

## pico.canvas_save

<div class="sig">pico.canvas_save(id: WidgetId, path: string) <span class="ret">-> ok: boolean</span></div>

キャンバスの内容を `.pimg`(4bpp+RLE。[画像](../images/) が読み込むのと同じ形式)としてSDへ書き出します。

失敗すると `false` を返します(エラーにはなりません)。失敗する条件:

- SDカードが使用不可
- `sd_outside_app_dir` 権限が無く、`app_dir` の外を指している
- ファイルを開けない(SD容量不足等)

## pico.canvas_load

<div class="sig">pico.canvas_load(id: WidgetId, path: string) <span class="ret">-> ok: boolean</span></div>

`.pimg` ファイルを読み込み、キャンバスへ反映します。

**読み込んだ画像のサイズに合わせて、キャンバス自身の `w`/`h` も変わります**(`pico.canvas_save()` した時と違うサイズの `CanvasRaster` へ読み込んでも構いません)。リサイズが起きるため、読み込み前の内容は失われます。

失敗すると `false` を返し、キャンバスの内容・サイズは変更されません。失敗する条件:

- SDカードが使用不可
- `sd_outside_app_dir` 権限が無く、`app_dir` の外を指している
- ファイルが存在しない、または `.pimg` として不正
- 画像の width/height が画面サイズ(`SCREEN_WIDTH`×`SCREEN_HEIGHT`)を超えている(壊れた/不正なファイルによる巨大確保を防ぐための安全策)

## サイズを変える

`CanvasRaster` の `w`/`h` は生成直後の固定値(100×100)から `pico.set()` で変更できます。

```lua
local x, y, w, h = pico.content_rect()
local canvas = pico.create("CanvasRaster")
pico.set(canvas, "x", x)
pico.set(canvas, "y", y)
pico.set(canvas, "w", w)
pico.set(canvas, "h", h)
```

**`w`/`h` を変更すると内部のバッファを作り直すため、それまでの描画内容は消えます。** 生成直後、まだ何も描いていない段階で一度だけ呼ぶ使い方を想定しています。
