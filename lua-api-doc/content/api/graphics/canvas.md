---
aliases: ["/api/canvas/"]
title: "ラスタキャンバス"
weight: 30
description: "canvas_clear / canvas_save / canvas_load / canvas_undo(CanvasRaster専用)"
---

`CanvasRaster`(`pico.create("CanvasRaster")`)専用の関数です。他のウィジェットや `Canvas` に渡すとエラーになります。

`CanvasRaster` は描いた絵を自分で覚えているウィジェットで、指でなぞった跡をそのまま線として残します(手書きメモ・お絵描き向き)。詳しい使い方は [手書き入力(CanvasRaster)](../../../guide/graphics/freehand-drawing/) を参照してください。

## pico.canvas_clear

<div class="sig">pico.canvas_clear(id: WidgetId) <span class="ret">-> (なし)</span></div>

キャンバスを白紙(白)に戻します。

## pico.canvas_save

<div class="sig">pico.canvas_save(id: WidgetId, path: string) <span class="ret">-> ok: boolean</span></div>

キャンバスの絵を `.pimg`([画像](../images/) で読めるのと同じ形式)としてSDへ書き出します。

失敗すると `false` を返します(エラーにはなりません)。失敗する条件:

- SDカードが使用不可
- `sd_outside_app_dir` 権限が無く、`app_dir` の外を指している
- ファイルを開けない(SD容量不足等)

## pico.canvas_load

<div class="sig">pico.canvas_load(id: WidgetId, path: string, keep_size?: boolean) <span class="ret">-> ok: boolean</span></div>

`.pimg` ファイルを読み込み、キャンバスへ反映します。

- `keep_size` を省略/`false`: **読み込んだ画像のサイズに合わせて、キャンバス自身の `w`/`h` も変わります**(`pico.canvas_save()` した時と違うサイズの `CanvasRaster` へ読み込んでも構いません)。リサイズが起きるため、読み込み前の内容は失われます(元に戻すこともできません)。
- `keep_size=true`: キャンバスの大きさは変えず、**白紙にしてから左上に合わせて**読み込みます。画像のほうが大きければ右/下が切れ、小さければ残りは白のままです。画面の配置が決まっているアプリ(ペイント等)向けで、`undo_enabled` なら読み込みも `pico.canvas_undo()` で戻せます。

失敗すると `false` を返し、キャンバスの内容・サイズは変更されません。失敗する条件:

- SDカードが使用不可
- `sd_outside_app_dir` 権限が無く、`app_dir` の外を指している
- ファイルが存在しない、または `.pimg` として不正
- 画像が画面(`SCREEN_WIDTH`×`SCREEN_HEIGHT`。既定は240×320)より大きい

## pico.canvas_undo

<div class="sig">pico.canvas_undo(id: WidgetId) <span class="ret">-> ok: boolean</span></div>

直前の描き込み(ペンの1ストローク・図形1つ・塗りつぶし1回・`canvas_clear`・`keep_size`での読み込み)を取り消します。**もう一度呼ぶとやり直し**になります(1段だけの控えと中身を入れ替えるため)。

あらかじめ `pico.set(id, "undo_enabled", true)` で有効にしておきます。有効な間は、キャンバスと同じ大きさの控え(`w*h/2` バイト。240×200で約23KB)のメモリを使います。戻せるものが無い・無効なら `false` を返します。

```lua
pico.set(canvas, "undo_enabled", true)
if not pico.get(canvas, "undo_enabled") then
    -- メモリ不足で控えを確保できなかった
end
pico.on(undo_button, "press_start", function() pico.canvas_undo(canvas) end)
```

`w`/`h` を変えたり、`keep_size` 無しで `canvas_load` してサイズが変わると、控えは捨てられます。

## 描き方(canvas_mode)

| 値 | 定数 | 動き |
|---|---|---|
| `0` | `CANVAS_LINE` | フリーハンド。ドラッグした軌跡をそのまま焼き込む |
| `1` | `CANVAS_RECT` | 四角形。ドラッグ中はプレビューだけ出し、指を離したときに焼き込む |
| `2` | `CANVAS_ELLIPSE` | 楕円(ドラッグした範囲に内接)。同上 |
| `3` | `CANVAS_ARROW` | 矢印。同上 |
| `4` | `CANVAS_STRAIGHT` | 直線。同上 |
| `5` | `CANVAS_FILL` | 塗りつぶし(バケツ)。触れた点と同じ色で上下左右に繋がった領域を `color` で塗る |

線の太さは `brush_radius`(`0` で1px、`r` で太さ `2r+1`px)。四角形/楕円は `filled=true` で塗りつぶしになり、`false`(既定)なら `brush_radius` の太さの輪郭になります。消しゴムは「`color` を白(`15`)にした Line」です。

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

**`w`/`h` を変えると、それまでに描いた絵は消えます。** 作った直後、まだ何も描いていないうちに1回だけ設定してください。

## pico.canvas_get_pixel

<div class="sig">pico.canvas_get_pixel(id: integer, x: integer, y: integer) <span class="ret">-> color: integer | nil</span></div>

`CanvasRaster` の1画素のパレット番号(0〜15)を読みます。範囲外は `nil`。塗りつぶしの判定など、描いた結果を使う処理に使えます(白紙は `15`)。
