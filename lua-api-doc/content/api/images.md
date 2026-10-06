---
title: "画像"
weight: 30
description: "image_load / image_create / image_target / image_size / draw_image / draw_image_part / image_free"
---

## pico.image_load

<div class="sig">pico.image_load(path: string) <span class="ret">-> handle: integer | nil</span></div>

`.pimg` ファイルをデコードし、整数のイメージハンドルを返します。`WidgetId` とは別の体系の整数です。

失敗すると `nil` を返します(エラーにはなりません)。失敗する条件:

- SDカードが使用不可
- `sd_outside_app_dir` 権限が無く、`app_dir` の外を指している
- 同時に保持できる画像数の上限(8枚)に達している
- 画像用メモリ合計の上限(96KiB)を超える
- ファイルが存在しない、または `.pimg` として不正

## pico.image_size

<div class="sig">pico.image_size(handle: integer) <span class="ret">-> width: integer, height: integer</span></div>

無効なハンドル(未発行・解放済み)を渡すとエラーになります。

## pico.draw_image

<div class="sig">pico.draw_image(handle: integer, x: integer, y: integer) <span class="ret">-> (なし)</span></div>

画像を描画します。他の `pico.draw_*` と同様、`Canvas` の `render` コールバックの中で使うこと([直接描画](../drawing/) 参照)。無効なハンドルを渡すとエラーになります。

## pico.draw_image_part

<div class="sig">pico.draw_image_part(handle: integer, x: integer, y: integer, sx: integer, sy: integer, w: integer, h: integer, flip_x?: boolean, flip_y?: boolean) <span class="ret">-> (なし)</span></div>

画像のうち `(sx, sy)` から幅 `w`・高さ `h` の部分だけを `(x, y)` へ描きます。**同じ大きさの絵を1枚に並べた画像(スプライトシート)から1つずつ切り出す**ためのもので、画像は8枚までしか持てないので、部品の多い絵はまとめて1枚にしてこれで描き分けます。画像の外にはみ出す分は描きません。`draw_image` と同じく `Canvas` の `render` コールバックの中で使い、無効なハンドルはエラーです。

```lua
-- 12x12のタイルを横に並べた画像から、n番目(1始まり)を描く
pico.draw_image_part(img, x, y, (n - 1) * 12, 0, 12, 12)
```

`flip_x` / `flip_y` が真なら左右/上下を反転して描きます(キャラクターの向きを変えるのに。反転は1画素ずつ描くので、しないときより遅い)。

実例は「テトリス」(`/lua/apps/テトリス/`。ミノの絵を1枚の `blocks.pimg` から切り出す)。タイルマップをまとめて描く `pico.draw_tilemap` は [ゲームエンジン](../game/#picodraw_tilemap) にあります。

## pico.draw_image_ex

<div class="sig">pico.draw_image_ex(handle: integer, x: number, y: number, r?: number, sx?: number, sy?: number, ox?: number, oy?: number) <span class="ret">-> (なし)</span></div>

Love2D の `love.graphics.draw(image, x, y, r, sx, sy, ox, oy)` と同じ並びで、画像を拡大縮小・回転・反転して描きます。

- 画像の `(ox, oy)`(元画像のピクセル座標、既定 `0, 0`)を画面の `(x, y)` に置く。
- `r`: 回転(ラジアン、時計回り、既定0)。回転の中心も `(ox, oy)`。
- `sx`, `sy`: 倍率(既定1)。`sy` を省くと `sx` と同じ。**負にすると反転**(`sx = -1` で左右反転)。倍率は±64まで、0は何もしない。
- 最近傍で、補間はしません。透過指定の画像は index 0 を描きません。
- 描くのは今のクリップ(`set_draw_area` / `Canvas` の `render` 中なら dirty 矩形)の内側だけです。

```lua
-- 画像の中心を軸に、2倍で45度回して描く
local w, h = pico.image_size(img)
pico.draw_image_ex(img, 120, 160, math.pi / 4, 2, 2, w / 2, h / 2)
```

## pico.image_free

<div class="sig">pico.image_free(handle: integer) <span class="ret">-> (なし)</span></div>

画像を明示的に解放します。無効なハンドル・解放済みハンドルを渡しても**エラーにはならず黙って無視**されます(`pico.destroy()` と同じ二重解放の扱い)。

呼び忘れても、そのLuaアプリの `LuaEngine` が破棄される(画面が閉じる)タイミングで自動的に回収されます。

## オフスクリーン画像

画面ではなく**画像に描いて**おき、後でまとめて画面へ貼れます。盤面のように重い絵を一度だけ描いて使い回したいときに使います。画像は `image_load` したものと同じスロット(8枚・合計96KiB)を使い、`draw_image` / `draw_image_part` / `draw_image_ex` で画面へ描けます。

### pico.image_create

<div class="sig">pico.image_create(w: integer, h: integer, transparent?: boolean) <span class="ret">-> handle: integer | nil</span></div>

`w`×`h`(最大480×640)の空の画像を作ります。中身は白(`transparent = true` なら0番色=透過)。枚数や容量の上限を超えると `nil` です。大きさが不正ならエラー。

### pico.image_target

<div class="sig">pico.image_target(handle: integer | nil) <span class="ret">-> (なし)</span></div>

以降の `pico.draw_*`(`fill_rect` `draw_text` `draw_image` など)の**描き先をその画像に切り替え**ます。`nil` で画面へ戻します。座標は画像の左上が(0,0)です。**コールバックを抜けると自動で画面へ戻ります**(戻し忘れても他の描画が画像に吸い込まれません)。画像へ描いている間は画面のdirtyは積まれません。

### pico.image_clear

<div class="sig">pico.image_clear(handle: integer, color?: integer) <span class="ret">-> (なし)</span></div>

画像を1色で塗りつぶします(既定は白、透過画像は0番色)。

```lua
local cache = pico.image_create(120, 120)
pico.image_target(cache)
pico.fill_rect(0, 0, 120, 120, 7)
pico.draw_text(8, 8, "キャッシュ", 0)
pico.image_target(nil)
-- Canvasのrenderの中で
pico.draw_image(cache, 10, 40)
```
