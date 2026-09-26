---
title: "画像 (.pimg)"
weight: 50
description: ".pimgファイルをデコードしてCanvasへ描く"
---

pico-os が扱える唯一の画像形式は独自形式の `.pimg`(4bpp + RLE圧縮)です。PNG/BMPなどは対応していません。`.pimg` の生成には `script/generate_pimg.py` を使います。

## 読み込み・描画・解放

```lua
local handle = pico.image_load("/lua/apps/myapp/icon.pimg")
if handle then
    local w, h = pico.image_size(handle)

    local canvas = pico.create("Canvas")
    pico.set(canvas, "w", w)
    pico.set(canvas, "h", h)
    pico.on(canvas, "render", function(id)
        pico.draw_image(handle, 0, 0)
    end)
end

-- 使い終わったら明示的に解放できる(必須ではない)
pico.image_free(handle)
```

| 関数 | 説明 |
|---|---|
| `pico.image_load(path)` | `.pimg`をデコードし整数ハンドルを返す。失敗(SD無し/権限外/壊れたファイル/上限超過)は `nil` |
| `pico.image_size(handle)` | `width, height` を返す |
| `pico.draw_image(handle, x, y)` | 描画(他の `pico.draw_*` と同じく `Canvas` の `render` の中で使うこと) |
| `pico.draw_image_part(handle, x, y, sx, sy, w, h)` | 画像の一部だけを描く(スプライトシートからの切り出し) |
| `pico.image_free(handle)` | 明示的に解放する |

## 画像はウィジェットではない

`pico.image_load()` が返すハンドルは `WidgetId` ではありません。`pico.destroy()` の対象にはならず、専用の `pico.image_free()` で解放します。**解放済みハンドルを2回解放しても無害**ですが、解放済みハンドルを `draw_image` / `image_size` に渡すとエラーになります(ハンドルの取り違えを検出するため)。

解放を呼び忘れても、そのLuaアプリが終了すれば(`pico.pop()` で画面を抜ければ)自動的に回収されます。`pico.image_free()` は「使い終わったタイミングが分かっているので、終了を待たず今すぐ解放したい」場合のための明示APIです。

## 上限

- 同時に保持できる画像は**最大4枚**(1つの `LuaEngine` インスタンス=1アプリあたり)。
- 画像データの合計サイズは**64KiB**まで(4bppなので `幅×高さ/2` バイトで概算)。

上限に達した状態で `pico.image_load()` を呼ぶと `nil` が返ります(エラーにはなりません)。

## 権限

`sd_outside_app_dir` 権限が `false`(既定)の間、`pico.image_load()` はそのアプリの `app_dir`(通常は自分の `main.lua` があるディレクトリ)配下しか読めません。詳細は [権限モデル](../permissions/) を参照してください。
