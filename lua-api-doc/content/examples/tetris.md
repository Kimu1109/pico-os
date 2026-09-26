---
title: "テトリス"
weight: 40
description: "pc/sdcard/lua/apps/テトリス/ — 画像のミノ・タッチと外部コントローラーの両対応・変わったマスだけ描き直す盤面"
---

`/lua/apps/テトリス/` に置いたテトリス風ゲームです。ゲームの規則(7種1巡の出現順、SRSの回転と壁蹴り、HOLD、ゴースト、接地してから0.5秒で固定、長押しの連続移動、レベルで速くなる落下)は全てLuaで書いてあります。

| ファイル | 中身 |
|---|---|
| `main.lua` | ゲーム本体・描画・入力 |
| `lib.lua` | ミノの形と壁蹴りの表、操作ボタンの絵(`main.lua` を16KiBに収めるため分けた。`pico.sd_read` + `load()` で読む) |
| `blocks.pimg` | ミノの絵。12x12のタイルを横に8枚(I O T S Z J L ゴースト)。`script/generate_tetris_blocks.py` で作り直せる |
| `bgm.mml` | BGM(コロベイニキ。チャンネル2は効果音用に空けてある) |
| `hiscore.txt` | ハイスコア(ゲームオーバーと「戻る」のときに書く) |

## 画像を1枚にまとめて切り出す

Luaが同時に持てる画像は4枚までなので、ミノの絵は1枚の画像に並べて `pico.draw_image_part()` で切り出します。

```lua
local img = pico.image_load(DIR .. "blocks.pimg")
-- v = 1〜8(タイルの番号)
pico.draw_image_part(img, x, y, (v - 1) * 12, 0, 12, 12)
```

## 変わったマスだけを描き直す

盤面は200マスあるので、毎フレーム全体を描き直すと重くなります。フレームごとに「今の見た目」を配列で作り、前回と違うマスを囲む矩形だけを `pico.mark_dirty()` し、`render` では `pico.get_draw_area()` で描き直す範囲を聞いて、そこにかかるマスだけを描きます。

```lua
pico.on(board, "render", function()
    local x, y, w, h = pico.get_draw_area()
    local c0, c1 = (x - BX) // 12, (x + w - 1 - BX) // 12
    local r0, r1 = (y - BY) // 12, (y + h - 1 - BY) // 12
    -- r0..r1 行、c0..c1 列だけ描く
end)
```

## タッチとコントローラーを同じ形にまとめる

入力は「押しているボタンのビット」1つにまとめます。タッチは操作ボタンの `press_start` / `press_move` / `press_end` で、コントローラーは `pico.pad_down()` で集めてORします。短いタップでもフレームの間に取りこぼさないよう、`press_start` の中では「押された」印(`latch`)も立てておきます。

| 操作 | タッチ | コントローラー | PC/Webのキーボード |
|---|---|---|---|
| 左右に移動(長押しで連続) | ◀ ▶ | 十字キー左右 | ← → |
| ゆっくり落とす | ▼ | 十字キー下 | ↓ |
| すぐ落とす | ▼の下に線 | 十字キー上 | ↑ |
| 右回転 | 右の丸 / 盤面をタップ | A / X | `X` / `S` |
| 左回転 | 左の丸 | B / Y | `Z` / `A` |
| HOLD | HOLD枠をタップ | L / R / ZL / ZR | `Q` `W` `E` `R` |
| 一時停止 / 開始 | 「停止」 / 盤面をタップ | START | `Enter` |
| 戻る | 「戻る」 | HOME | `H` / `Esc` |

コントローラーは、実機ではUSBシリアル(`script/pad_serial.py`)、Webビルドではページのボタンとキーボードから使えます。
