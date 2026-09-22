---
title: "図形ウィジェット"
weight: 35
description: "Rect / Ellipse / Line / Triangle で矩形・楕円・線分・三角形を描く"
---

## 他のウィジェットとの違い

`Rect` / `Ellipse` / `Line` / `Triangle` は、`Button` や `Icon` と同じ**普通のウィジェット**です。`pico.create()` で生成し、`pico.set()` で位置や色を決め、他のウィジェットと同じように再描画・当たり判定の対象になります。

[Canvasと直接描画](../drawing/) の `pico.draw_rect()` 等との違いは、**自分で再描画を管理しなくてよい**ことです。`Canvas` の `pico.draw_*` は `render` コールバックの中で毎フレーム描き直す前提でしたが、図形ウィジェットは一度置けば、他のウィジェットと同じく値が変わったときだけ自動的に再描画されます。逆に、`Canvas` のように1枚のキャンバスへ自由な絵を重ねて描く用途には向きません(1個のウィジェット=1つの図形)。

## Rect / Ellipse: 箱の形で置く

`Rect` と `Ellipse` は `x` / `y` / `w` / `h` の箱で位置と大きさを決めます。`Ellipse` は箱に内接する楕円(`w == h` なら円)です。

```lua
local rect = pico.create("Rect")
pico.set(rect, "x", 10)
pico.set(rect, "y", 10)
pico.set(rect, "w", 60)
pico.set(rect, "h", 40)
pico.set(rect, "color", 9)      -- PICO_BLUE
pico.set(rect, "filled", false) -- 枠線だけにする
pico.set(rect, "thickness", 2)  -- 枠線の太さ(filled=falseのときのみ意味を持つ)

local circle = pico.create("Ellipse")
pico.set(circle, "x", 100)
pico.set(circle, "y", 10)
pico.set(circle, "w", 40)
pico.set(circle, "h", 40) -- w==hなので正円になる
pico.set(circle, "color", 12) -- PICO_RED
```

`filled` は両方とも既定 `true`(塗りつぶし)です。`thickness` は `filled = false` のときだけ効きます。

## Line / Triangle: 点で形を決める

`Line` と `Triangle` は箱ではなく、**点の座標**で形を決めます。共通プロパティの `x` / `y` / `w` / `h` は読み取り専用の外接矩形(点から自動計算される)で、`w` / `h` は直接 `pico.set()` できません。

```lua
local line = pico.create("Line")
pico.set(line, "x1", 0)
pico.set(line, "y1", 0)
pico.set(line, "x2", 60)
pico.set(line, "y2", 40)
pico.set(line, "color", 0)
pico.set(line, "thickness", 3)

local triangle = pico.create("Triangle")
pico.set(triangle, "x1", 0)
pico.set(triangle, "y1", 20)
pico.set(triangle, "x2", 20)
pico.set(triangle, "y2", 0)
pico.set(triangle, "x3", 40)
pico.set(triangle, "y3", 20)
pico.set(triangle, "color", 10) -- PICO_GREEN
pico.set(triangle, "filled", true)
```

座標は `Line`/`Triangle` が置かれている親から見た**ローカル座標**です(他のウィジェットの `x`/`y` と同じ座標系)。`LayoutContainer`/`GridContainer` の子にした場合は、コンテナの内側を基準にした座標になります。

### x / y は形全体の平行移動

`Line`/`Triangle` に共通プロパティの `x` を `pico.set()` すると、**点をまとめて平行移動**します(外接矩形の左上を動かすだけで、形は変わりません)。これは `LayoutContainer`/`GridContainer` が子を並べるとき `x`/`y` だけを書き換える前提に合わせた挙動です。形そのもの(向きや長さ)を変えたいときは `x1`/`y1`/`x2`/`y2`(`Triangle` はさらに `x3`/`y3`)を個別に設定してください。

```lua
-- コンテナへ入れても、コンテナのレイアウトが動かすのはx/y(=平行移動)だけなので、
-- 線の向き・長さは崩れない
pico.add_child(container, line)
```

## 太さの実装について

このOSのフレームバッファは4bitパレット(16色固定)のため、`LovyanGFX` のアンチエイリアス付き太線描画(`drawWideLine`)は使えません(中間色を混ぜようとして4bitパレットに無い色を要求してしまいます)。そのため `thickness > 1` の線・輪郭は、進行方向に垂直な単位ベクトルへオフセットした2枚の三角形の塗りつぶしで表現しています。見た目はアンチエイリアス無しの単色ですが、パレットが崩れる心配はありません。

## プロパティ一覧

完全な対応表は [プロパティ対応表](../../reference/widget-properties/) の `Rect` / `Ellipse` / `Line` / `Triangle` の項を参照してください。
