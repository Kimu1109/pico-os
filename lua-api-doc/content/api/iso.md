---
title: "2.5Dの箱庭"
weight: 82
description: "pico.iso: 斜め上から見たボクセルの箱庭(ワールドの読み込み・生成・保存、影つきの描画、タップ位置の引き当て)"
---

`pico.iso` は、斜め上から見た(アイソメトリックの)マインクラフト風の箱庭を動かす C++ のエンジンです。Luaアプリ「ブロック」(`/lua/apps/ブロック/`。TheScienceElf/Blocks-TI-84 の移植)のために、Luaで書いていた描画とワールドの処理を C++ へ移したものです。アプリは画面の流れと操作だけを書きます。

- **ワールド**: 1辺 1024 マス(8x8 の柱 = 1チャンクが 128x128 個)× 高さ 16。見えている所のまわりのチャンク(最大 56 個、約61KB)だけを読み込み、離れたものは手放す。地形は種と位置だけで決まるので、SDへ書くのは書き換えたチャンクだけ
- **描画**: 影(太陽は左奥の上。面を三角形2つに分けて半分だけの影も)、半透明の水、水面は2px低く、手前のブロックに完全に隠れるブロックは描かない
- **引き当て**: 画面のタップ位置に見えているブロックとその面

ブロックの番号は 0=空気、1=水、2〜24 が石・草・土…(元の Blocks-TI-84 と同じ並び)。

## はじめの一歩

```lua
local iso = pico.iso
local img = pico.image_load(pico.path_join(pico.app_dir(), "faces.pimg"))
iso.set_image(img)                 -- 面の絵(208x575、1種類につき高さ23pxの1段)
iso.sky(7)                         -- 空の色(パレット番号)
iso.view(0, 20, 240, 204)          -- 表示範囲(Canvas と同じ矩形)

local dir = pico.path_join(pico.app_dir(), "worlds/A")
pico.sd_mkdir(dir)
local x, y, z = iso.create(dir, 0, 12345)  -- 自然のワールド(種 12345)。始めのカーソルの位置
iso.cursor(x, y, z, true)
-- (x, y, z) を表示の真ん中へ
iso.origin(120 - 16 - 16 * (x - z), 122 - 16 + 8 * (x + z) + 16 * y)

local view = pico.create("Canvas")
pico.set(view, "x", 0); pico.set(view, "y", 20); pico.set(view, "w", 240); pico.set(view, "h", 204)
pico.on(view, "render", function() iso.render() end)

function loop(dt)
    -- 見えている所のチャンクを少しずつ読み込む(視点が変わっていれば範囲を決め直す)
    if iso.pump(8, 10) > 0 then pico.invalidate(view) end
end
```

## ワールド

| 関数 | 説明 |
|---|---|
| `iso.create(dir, kind, seed[, k])` → `x, y, z` \| `nil, 理由` | 新しいワールド。`kind`: 0=自然 1=平ら 2=デモ 3=空。`k` は1辺のチャンク数(既定128、1〜128)。始めのカーソルの位置を返す |
| `iso.open(dir)` → `x, y, z, ブロック` \| `nil, 理由` | `dir/world.dat` を開く。保存したカーソルの位置と選んでいたブロック |
| `iso.info(path)` → `種類, 1辺のマス数` \| `nil` | `world.dat` の見出しだけ読む(ワールドを選ぶ画面に) |
| `iso.save(x, y, z, ブロック)` → `bool` | 書き換えたチャンクと見出しを書き出す |
| `iso.close()` | 閉じる(書き出さない。置き場のメモリも返す) |
| `iso.migrate(古いファイル, dir)` → `true` \| `nil, 理由` | 前の版の「ブロック」の保存(48x16x48 を1ファイル)を移し、古いファイルを消す |
| `iso.get(x, y, z)` / `iso.set(x, y, z, ブロック)` | ブロックを読む/書く。世界の外と読み込んでいないチャンクは空気。`set` は読み込んでいなければその場で読み込む |
| `iso.first_air(x, z)` | 下から見て最初の空気の高さ |
| `iso.size()` → `1辺, 16, 種類` \| `nil` | 開いていなければ nil |
| `iso.pump(max[, ms])` → 読み込んだ数 | 予定のチャンクを最大 `max` 個読み込む(`ms` を過ぎたら途中でやめる)。視点が変わっていれば先に範囲を決め直す |
| `iso.pending()` | 読み込む予定の残り |
| `iso.stats()` | `{ chunks, bytes, faces, pending }`(`faces` は直前の `render` で描いた面の数) |

`dir` は `pico.sd_write` と同じ権限の確認を通ります(`permission_sd_outside_app_dir` が無ければアプリのフォルダの中だけ)。ファイルは `dir/world.dat`(見出し17バイト + 書き出したチャンクの印)と `dir/c_<cx>_<cz>.dat`(チャンク)。

## 表示

| 関数 | 説明 |
|---|---|
| `iso.set_image(handle)` | 面の絵(`pico.image_load` したもの)。透けないブロック(手前に置くと後ろを完全に隠す)を絵から調べておく |
| `iso.view(x, y, w, h)` | 表示範囲。読み込む範囲と、描き直す範囲の切り取りに使う |
| `iso.origin([ox, oy])` → `ox, oy` | ブロック(0,0,0)の絵の左上。ブロック(x,y,z)の絵(32x31)の左上は `(ox + 16*(x-z), oy - 8*(x+z) - 16*y)` |
| `iso.cursor(x, y, z[, show])` | カーソルの位置と表示 |
| `iso.sky(色)` | `render` が塗る空の色 |
| `iso.render([x, y, w, h])` | 空で塗ってワールドとカーソルを描く。**Canvas の `render` の中で呼ぶ**(省略すると今の描き直す範囲) |
| `iso.draw_icon(ブロック, x, y)` | 1個のブロックをまるごと描く(ボタンや選ぶ画面に) |
| `iso.block_pos(x, y, z)` → `px, py` | ブロックの絵の左上 |
| `iso.pick(px, py)` → `x, y, z, 面` \| `nil` | 画面の点に見えている一番手前のブロックと面(`"top"` `"left"` `"right"`) |
| `iso.dirty_block(x, y, z)` | そのブロックの絵の所を描き直す(カーソルを動かしたとき) |
| `iso.dirty_edit(x, y, z)` | 置いた/壊したとき。そのブロックと、影が変わりうる面だけを描き直す |
| `iso.culling(bool)` | 隠れたブロックを描かない(既定 true。比べるとき用) |

## 速さ

描画は C++ で、面の絵は 4bpp のバッファどうしで写します。透けないブロックの面は「上面のひし形・左右の平行四辺形」の形どおりなので、1行を1区間として透過の判定なしに `memcpy` します(葉・水・カーソルだけ1画素ずつ)。手前の透けないブロック `(x-k, y+k, z-k)` は画面のちょうど同じ六角形に重なるので、その後ろのブロックは描きません(省いても画素が1つも変わらないことをテストで確かめています)。PCでの計測では、Lua版の描画の処理(絵を写す前まで)の約55倍の速さで、表示全体で描く面は約3割減りました。
