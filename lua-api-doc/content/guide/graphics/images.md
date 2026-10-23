---
aliases: ["/guide/images/"]
title: "画像"
weight: 20
description: ".pimg / BMP / JPEG を読み込んでCanvasへ描く"
---

## 読める形式

| 形式 | 説明 |
|---|---|
| `.pimg` | OS独自の16色・RLE圧縮の形式。一番軽く読める。PNGなどからはリポジトリの `script/generate_pimg.py` で作る |
| `.bmp` | 無圧縮のBMP(1〜32bpp)。16色へ寄せて読む |
| `.jpg` | ベースラインのJPEG(プログレッシブは不可)。16色へ寄せ、大きい写真は縮めて読める |

PNGはそのままでは読めません。

## 読み込み・描画・解放

```lua
local img = pico.image_load(pico.path_join(pico.app_dir(), "icon.pimg"))
if img then
    local w, h = pico.image_size(img)

    local canvas = pico.create("Canvas")
    pico.set(canvas, "w", w)
    pico.set(canvas, "h", h)
    pico.on(canvas, "render", function(id)
        local x, y = pico.get_rect(id)
        pico.draw_image(img, x, y)
    end)
end
```

| 関数 | 説明 |
|---|---|
| `pico.image_load(path [, opts])` | 画像を読み込み、整数のハンドルを返す。読めなければ `nil` |
| `pico.image_size(handle)` | 幅と高さ |
| `pico.draw_image(handle, x, y)` | 描く(Canvasの `render` の中で) |
| `pico.draw_image_part(handle, x, y, sx, sy, w, h)` | 画像の一部だけ描く(スプライトシートから1コマ切り出す) |
| `pico.draw_image_ex(handle, x, y, r, sx, sy, ox, oy)` | 回転・拡大縮小・反転して描く |
| `pico.image_free(handle)` | 解放する |

ほかに、画像へ描いておいて後で貼る(オフスクリーン)、回転した絵を先に作っておく、といった関数があります([画像API](../../../api/graphics/images/))。

## ハンドルと解放

- `pico.image_load()` が返すのはウィジェットのIDではなく画像のハンドルです。`pico.destroy()` ではなく `pico.image_free()` で解放します。
- 解放したハンドルを `draw_image` などに渡すとエラーになります(`image_free` を2回呼ぶのは安全です)。
- アプリを閉じれば全部解放されます。`image_free` は、使い終わった画像の枠をすぐ空けたいときに呼びます。

## 上限

- 同時に持てる画像は **8枚** まで、画像データの合計は **96KiB** まで(16色なので `幅×高さ÷2` バイトが目安)。
- 上限を超えると `pico.image_load()` は `nil` を返します(エラーにはなりません)。
- 小さい絵がたくさんあるときは1枚の画像に並べ、`draw_image_part` で切り出すと枠を節約できます。

## スクロールして見せるだけなら

画像を表示するだけなら `ImageView` ウィジェットが手軽です(大きい画像はドラッグで動かせ、上の枚数の枠も使いません)。[表示・日付・時計の部品](../../ui/display-widgets/#imageview--スクロールできる画像) を見てください。

## 権限

`sd_outside_app_dir` の権限が無いアプリは、アプリのフォルダの中の画像だけを読めます([権限](../../app/permissions/))。
