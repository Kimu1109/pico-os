---
title: "画像"
weight: 30
description: "image_load / image_size / draw_image / image_free"
---

## pico.image_load

<div class="sig">pico.image_load(path: string) <span class="ret">-> handle: integer | nil</span></div>

`.pimg` ファイルをデコードし、整数のイメージハンドルを返します。`WidgetId` とは別の体系の整数です。

失敗すると `nil` を返します(エラーにはなりません)。失敗する条件:

- SDカードが使用不可
- `sd_outside_app_dir` 権限が無く、`app_dir` の外を指している
- 同時に保持できる画像数の上限(4枚)に達している
- 画像用メモリ合計の上限(64KiB)を超える
- ファイルが存在しない、または `.pimg` として不正

## pico.image_size

<div class="sig">pico.image_size(handle: integer) <span class="ret">-> width: integer, height: integer</span></div>

無効なハンドル(未発行・解放済み)を渡すとエラーになります。

## pico.draw_image

<div class="sig">pico.draw_image(handle: integer, x: integer, y: integer) <span class="ret">-> (なし)</span></div>

画像を描画します。他の `pico.draw_*` と同様、`Canvas` の `render` コールバックの中で使うこと([直接描画](../drawing/) 参照)。無効なハンドルを渡すとエラーになります。

## pico.image_free

<div class="sig">pico.image_free(handle: integer) <span class="ret">-> (なし)</span></div>

画像を明示的に解放します。無効なハンドル・解放済みハンドルを渡しても**エラーにはならず黙って無視**されます(`pico.destroy()` と同じ二重解放の扱い)。

呼び忘れても、そのLuaアプリの `LuaEngine` が破棄される(画面が閉じる)タイミングで自動的に回収されます。
