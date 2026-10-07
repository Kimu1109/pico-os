---
title: "直接描画"
weight: 20
description: "draw_* / fill_* / clear_rect / draw_text / text_width / invalidate / mark_dirty / set_draw_area / get_draw_area"
---

> これらの関数は `Canvas`(`pico.create("Canvas")`)の `render` コールバックの中で使うことを前提としています。詳細と理由は [Canvasと直接描画](../../guide/drawing/) を参照してください。座標は絶対スクリーン座標、色は0〜15のPICO-8風パレット番号です。

## pico.draw_pixel

<div class="sig">pico.draw_pixel(x: integer, y: integer, color: integer) <span class="ret">-> (なし)</span></div>

## pico.draw_line

<div class="sig">pico.draw_line(x0: integer, y0: integer, x1: integer, y1: integer, color: integer, width?: integer) <span class="ret">-> (なし)</span></div>

`width`(px、既定1、最大64)を指定すると太い線を引きます。両端は丸くなります。

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

## pico.draw_ellipse / pico.fill_ellipse

<div class="sig">pico.draw_ellipse(x: integer, y: integer, rx: integer, ry: integer, color: integer) <span class="ret">-> (なし)</span></div>
<div class="sig">pico.fill_ellipse(x: integer, y: integer, rx: integer, ry: integer, color: integer) <span class="ret">-> (なし)</span></div>

中心 `(x, y)`、横半径 `rx`・縦半径 `ry` の楕円の輪郭/塗りつぶし。半径が負だとエラー。

## pico.draw_triangle / pico.fill_triangle

<div class="sig">pico.draw_triangle(x0, y0, x1, y1, x2, y2, color: integer, width?: integer) <span class="ret">-> (なし)</span></div>
<div class="sig">pico.fill_triangle(x0, y0, x1, y1, x2, y2, color: integer) <span class="ret">-> (なし)</span></div>

三角形の輪郭(`width` で太さ)/塗りつぶし。

## pico.draw_polygon / pico.fill_polygon

<div class="sig">pico.draw_polygon(points: table, color: integer, width?: integer) <span class="ret">-> (なし)</span></div>
<div class="sig">pico.fill_polygon(points: table, color: integer) <span class="ret">-> (なし)</span></div>

`points` は `{x1, y1, x2, y2, ...}` の平らな配列で、**3点以上・32点まで**。点列は最後から最初へ自動で閉じます。`fill_polygon` は偶奇規則で塗るので、凹んだ形や自己交差する形も塗れます。点が足りない・奇数個・数値以外・33点以上はエラー。

```lua
pico.fill_polygon({100,100, 130,100, 130,110, 110,110, 110,130, 100,130}, 9) -- L字
```

## pico.draw_arc / pico.fill_arc

<div class="sig">pico.draw_arc(x: integer, y: integer, r: integer, a0: number, a1: number, color: integer, width?: integer) <span class="ret">-> (なし)</span></div>
<div class="sig">pico.fill_arc(x: integer, y: integer, r: integer, a0: number, a1: number, color: integer) <span class="ret">-> (なし)</span></div>

中心 `(x, y)`・半径 `r` の円弧(`draw_arc`)と扇形(`fill_arc`、中心から円弧までを塗る)。角度は**ラジアン**で、`0` が右、増えると**時計回り**(y が下向きのため)。`a1 < a0` なら逆回り。`fill_arc` は円弧の分割が粗くなる(最大約30分割)ので、大きな円では角が見えます。

```lua
pico.fill_arc(150, 150, 20, 0, math.pi / 2, 6) -- 右下の1/4
```

## pico.text_width

<div class="sig">pico.text_width(text: string, font_size?: integer) <span class="ret">-> width: integer</span></div>

`draw_text` と同じフォントで1行に描いたときの幅(px)。`font_size` の既定は `1`(`Normal`)。右寄せ・中央寄せの位置決めに使います。

```lua
local w = pico.text_width("Score", 1)
pico.draw_text(240 - w - 4, 4, "Score", 0, 1)
```

## pico.clear_rect

<div class="sig">pico.clear_rect(x: integer, y: integer, w: integer, h: integer, color?: integer) <span class="ret">-> (なし)</span></div>

矩形を塗りつぶします(`fill_rect` と同じ実装)。`color` を省略すると背景色(`PICO_BACKGROUND` = 15)になります。

## pico.draw_text

<div class="sig">pico.draw_text(x: integer, y: integer, text: string, color?: integer, font_size?: integer) <span class="ret">-> (なし)</span></div>

- `color` の既定値は前景色(`PICO_FORECOLOR` = 0)。
- `font_size` の既定値は `1`(`Normal`、24px)。値は [定数・上限一覧](../../reference/limits/) を参照。
- 描画幅は自動で残りスクリーン幅(`SCREEN_WIDTH - x`)に収まるよう切り詰められます(折り返しはしません)。

`align` で揃えを選べます(6番目の引数、既定 `"left"`)。`"left"` は `x` が左端、`"center"` は `x` が中心、`"right"` は `x` が右端になります。

<div class="sig">pico.draw_text(x, y, text, color?, font_size?, align?) <span class="ret">-> (なし)</span></div>

## pico.draw_text_wrapped

<div class="sig">pico.draw_text_wrapped(x: integer, y: integer, w: integer, text: string, color?: integer, font_size?: integer, align?: string, line_gap?: integer) <span class="ret">-> lines: integer, height: integer</span></div>

幅 `w` に収まるように折り返して描きます。`\n` で改行し、英語は空白で、日本語は文字の途中で折れます。戻り値は行数と全体の高さ(`line_gap` は行と行の間の追加の余白)。`align` は `"left"` / `"center"` / `"right"`(幅 `w` の中での揃え)。2048バイト・64行までです。

## pico.measure_text

<div class="sig">pico.measure_text(text: string, w: integer, font_size?: integer, line_gap?: integer) <span class="ret">-> lines: integer, height: integer</span></div>

描かずに、`draw_text_wrapped` と同じ折り返しの行数と高さだけを返します。枠の大きさを先に決めるときに使います。

## pico.get_pixel

<div class="sig">pico.get_pixel(x: integer, y: integer) <span class="ret">-> color: integer | nil</span></div>

画面(今描いている合成先)の1画素のパレット番号(0〜15)を読みます。範囲外は `nil`。`Canvas` の `render` の中では、その時点までに合成された絵が読めます。`CanvasRaster` の中身は [`pico.canvas_get_pixel`](../canvas/) で読みます。


## pico.invalidate

<div class="sig">pico.invalidate(id: integer) <span class="ret">-> (なし)</span></div>

指定したウィジェットの画面矩形を dirty 化します。次の `FlushDirty()` でそのウィジェットの `render`(`Canvas` の場合は登録したコールバック)が呼ばれます。`Canvas` に限らず任意のウィジェットに使える汎用APIです。

## pico.mark_dirty

<div class="sig">pico.mark_dirty(x: integer, y: integer, w: integer, h: integer) <span class="ret">-> (なし)</span></div>

任意の矩形を直接dirty化する低レベルAPIです。

## pico.set_draw_area

<div class="sig">pico.set_draw_area(x: integer, y: integer, w: integer, h: integer) <span class="ret">-> (なし)</span></div>

以降の `pico.draw_*` をこの矩形の内側だけに制限します(クリップ矩形)。`render` コールバックの中では、今回描き直す範囲(Canvasとdirty矩形の重なり)との**重なり**に制限します(その外へは広げられません。描いても液晶へ送られないため)。

## pico.clear_draw_area

<div class="sig">pico.clear_draw_area() <span class="ret">-> (なし)</span></div>

`set_draw_area` で設定したクリップを解除します(`render` コールバックの中では、今回描き直す範囲へ戻します)。**`set_draw_area` を呼んだら、同じ `render` コールバック内で必ず対にして呼んでください**(クリップ矩形は画面全体で1個しか無い共有状態です)。

## pico.get_draw_area

<div class="sig">pico.get_draw_area() <span class="ret">-> x: integer, y: integer, w: integer, h: integer</span></div>

今のクリップ矩形を返します。`render` コールバックの中では「そのCanvasのうち、今回描き直す部分(dirty矩形との重なり)」になるので、**部品の多い絵で、描き直しが要る部分だけを描く**のに使えます(`pico.mark_dirty` で小さな矩形だけをdirtyにした場合、`render` はその矩形ぶんだけ呼ばれます)。クリップが無いときは `w` と `h` が0です。

```lua
pico.on(board, "render", function()
    local x, y, w, h = pico.get_draw_area()
    -- (x, y, w, h) にかかるマスだけを描く
end)
```

`set_draw_area` を呼ぶとこの値も(元の範囲との重なりへ)狭まる点に注意してください。実例は [ゲームエンジン](../game/) の描画(`pico.game` は描き直す範囲をこれで絞っている)と、「テトリス」のHOLD/NEXT欄。
