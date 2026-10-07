---
title: "テトリス"
weight: 40
description: "pc/sdcard/lua/apps/テトリス/ — pico.game のタイルマップ(盤面)とスプライト(落ちるミノ)・画面ボタンで作る"
---

`/lua/apps/テトリス/` に置いたテトリス風ゲームです。[ゲームエンジン `pico.game`](../../api/game/) の上に作ってあり、ゲームの規則(7種1巡の出現順、SRSの回転と壁蹴り、HOLD、ゴースト、接地してから0.5秒で固定、長押しの連続移動、レベルで速くなる落下)だけを書いています。

| ファイル | 中身 |
|---|---|
| `main.lua` | ゲーム本体(規則・状態・見た目への写し方) |
| `lib.lua` | ミノの形と壁蹴りの表、操作ボタンの絵、HOLD/NEXTのミノの絵(LuaScene が本体より先に実行し、グローバル変数 `LIB` で渡る) |
| `blocks.pimg` | ミノの絵。12x12のタイルを横に10枚(I O T S Z J L ゴースト 消えるラインの白 空きマスの黒)。`script/generate_tetris_blocks.py` で作り直せる |
| `bgm.mml` | BGM(コロベイニキ。チャンネル2は効果音用に空けてある) |
| `hiscore.txt` | ハイスコア(ゲームオーバーと「戻る」のときに書く) |

## エンジンの部品との対応

| 見えるもの | エンジンの部品 | 書いていること |
|---|---|---|
| 盤面(10x20) | タイルマップ | 固まったミノ・消えるラインの白・空きマスの黒はすべて「タイルの値」。変わったマスを `map:set(x, y, 値)` するだけで、エンジンがそのマスだけ描き直す |
| 落ちているミノ・ゴースト | スプライト4枚ずつ | 毎フレーム `x` `y` `frame` `visible` を書き換える(見えない上の2段にいるマスは隠す) |
| HOLD・NEXT・点数 | `g:on_draw` | 中身が変わったときだけ `g:dirty()` で知らせる。描き直す範囲に重ならなければ描かない(`pico.get_draw_area()`) |
| 下の6つの操作ボタン | `g:button`(`draw` で自前の絵) | 名前をコントローラーのボタン名(`left` `right` `down` `up` `a` `b`)と同じにしたので、画面のボタンも十字キー・A/Bも同じ `g:down` / `g:pressed` で読める |
| タイトル・一時停止・ゲームオーバー・ライン消し | `g:state`(title / play / pause / clear / over) | 各状態の `update` と、盤面の真ん中へ出す枠(`draw`) |

```lua
local game = require("pico.game")
local g = game.new{ bg = 15 }
local img = g:image(pico.path_join(pico.app_dir(), "blocks.pimg"))
local map = g:tilemap{ image = img, tile = 12, x = 5, y = 3, cols = 10,
                       data = string.rep(string.char(10), 10 * 20) }   -- 10 = 空きマス

map:set(3, 19, 1)                                  -- (3, 19) にIのミノを置く(変わったマスだけ描き直される)
local s = g:sprite{ image = img, w = 12, h = 12, frame = 2, layer = 2 }   -- Tのミノ1マス
s.x, s.y = 5 + 4 * 12, 3 + 5 * 12                  -- 動かすだけ(動いた前後の矩形だけ描き直される)
```

**スプライトの `draw` や画像は `w` x `h` の外へはみ出させない**でください。はみ出した画素は消去の範囲に入らず、残ってしまいます。

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
