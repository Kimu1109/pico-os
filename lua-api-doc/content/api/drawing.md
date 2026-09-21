---
title: "直接描画"
weight: 20
description: "draw_* / fill_* / clear_rect / draw_text / invalidate / mark_dirty / set_draw_area"
---

> これらの関数は `Canvas`(`pico.create("Canvas")`)の `render` コールバックの中で使うことを前提としています。詳細と理由は [Canvasと直接描画](../../guide/drawing/) を参照してください。座標は絶対スクリーン座標、色は0〜15のPICO-8風パレット番号です。

## pico.draw_pixel

<div class="sig">pico.draw_pixel(x: integer, y: integer, color: integer) <span class="ret">-> (なし)</span></div>

## pico.draw_line

<div class="sig">pico.draw_line(x0: integer, y0: integer, x1: integer, y1: integer, color: integer) <span class="ret">-> (なし)</span></div>

## pico.draw_rect

<div class="sig">pico.draw_rect(x: integer, y: integer, w: integer, h: integer, color: integer) <span class="ret">-> (なし)</span></div>

矩形の**枠線**のみ描画します。

## pico.fill_rect

<div class="sig">pico.fill_rect(x: integer, y: integer, w: integer, h: integer, color: integer) <span class="ret">-> (なし)</span></div>

矩形を塗りつぶします。

## pico.draw_circle

<div class="sig">pico.draw_circle(x: integer, y: integer, r: integer, color: integer) <span class="ret">-> (なし)</span></div>

円の輪郭のみ描画します。

## pico.fill_circle

<div class="sig">pico.fill_circle(x: integer, y: integer, r: integer, color: integer) <span class="ret">-> (なし)</span></div>

円を塗りつぶします。

## pico.clear_rect

<div class="sig">pico.clear_rect(x: integer, y: integer, w: integer, h: integer, color?: integer) <span class="ret">-> (なし)</span></div>

矩形を塗りつぶします(`fill_rect` と同じ実装)。`color` を省略すると背景色(`PICO_BACKGROUND` = 15)になります。

## pico.draw_text

<div class="sig">pico.draw_text(x: integer, y: integer, text: string, color?: integer, font_size?: integer) <span class="ret">-> (なし)</span></div>

- `color` の既定値は前景色(`PICO_FORECOLOR` = 0)。
- `font_size` の既定値は `1`(`Normal`、24px)。値は [定数・上限一覧](../../reference/limits/) を参照。
- 描画幅は自動で残りスクリーン幅(`SCREEN_WIDTH - x`)に収まるよう切り詰められます(折り返しはしません)。

## pico.invalidate

<div class="sig">pico.invalidate(id: integer) <span class="ret">-> (なし)</span></div>

指定したウィジェットの画面矩形を dirty 化します。次の `FlushDirty()` でそのウィジェットの `render`(`Canvas` の場合は登録したコールバック)が呼ばれます。`Canvas` に限らず任意のウィジェットに使える汎用APIです。

## pico.mark_dirty

<div class="sig">pico.mark_dirty(x: integer, y: integer, w: integer, h: integer) <span class="ret">-> (なし)</span></div>

任意の矩形を直接dirty化する低レベルAPIです。

## pico.set_draw_area

<div class="sig">pico.set_draw_area(x: integer, y: integer, w: integer, h: integer) <span class="ret">-> (なし)</span></div>

以降の `pico.draw_*` をこの矩形の内側だけに制限します(クリップ矩形)。

## pico.clear_draw_area

<div class="sig">pico.clear_draw_area() <span class="ret">-> (なし)</span></div>

`set_draw_area` で設定したクリップを解除します。**`set_draw_area` を呼んだら、同じ `render` コールバック内で必ず対にして呼んでください**(クリップ矩形は画面全体で1個しか無い共有状態です)。
