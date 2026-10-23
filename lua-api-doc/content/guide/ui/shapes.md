---
aliases: ["/guide/shapes/"]
title: "図形ウィジェット"
weight: 40
description: "Rect / Ellipse / Line / Triangle で四角・楕円・線・三角形を置く"
---

## Canvas との違い

`Rect` / `Ellipse` / `Line` / `Triangle` は、`Button` などと同じ普通のウィジェットです。`pico.create()` で作って `pico.set()` で位置や色を決めれば、あとはOSが描き直してくれます。

[Canvasと直接描画](../../graphics/drawing/) の `pico.draw_rect()` などは、`render` コールバックの中で毎回描き直す前提でした。図形ウィジェットは一度置けば、値を変えたときだけ自動で描き直されます。そのかわり1つのウィジェットが1つの図形なので、たくさんの図形で絵を描くなら `Canvas` が向いています。

## Rect / Ellipse: 箱で置く

`Rect` と `Ellipse` は `x` / `y` / `w` / `h` の箱で位置と大きさを決めます。`Ellipse` は箱に内接する楕円です(`w == h` なら円)。

```lua
local rect = pico.create("Rect")
pico.set(rect, "x", 10)
pico.set(rect, "y", 10)
pico.set(rect, "w", 60)
pico.set(rect, "h", 40)
pico.set(rect, "color", pico.COLOR_BLUE)
pico.set(rect, "filled", false) -- 枠線だけにする
pico.set(rect, "thickness", 2)  -- 枠線の太さ

local circle = pico.create("Ellipse")
pico.set(circle, "x", 100)
pico.set(circle, "y", 10)
pico.set(circle, "w", 40)
pico.set(circle, "h", 40)       -- w == h なので円
pico.set(circle, "color", pico.COLOR_RED)
```

`filled` は既定で `true`(塗りつぶし)です。`thickness` は `filled = false` のときだけ効きます。

## Line / Triangle: 点で形を決める

`Line` と `Triangle` は箱ではなく**点の座標**で形を決めます。`w` / `h` は点から自動で決まるので、設定できません。

```lua
local line = pico.create("Line")
pico.set(line, "x1", 0);  pico.set(line, "y1", 0)
pico.set(line, "x2", 60); pico.set(line, "y2", 40)
pico.set(line, "color", pico.COLOR_BLACK)
pico.set(line, "thickness", 3)

local triangle = pico.create("Triangle")
pico.set(triangle, "x1", 0);  pico.set(triangle, "y1", 20)
pico.set(triangle, "x2", 20); pico.set(triangle, "y2", 0)
pico.set(triangle, "x3", 40); pico.set(triangle, "y3", 20)
pico.set(triangle, "color", pico.COLOR_GREEN)
```

点の座標は、他のウィジェットの `x` / `y` と同じ座標系です(コンテナに入れたらコンテナの内側が基準)。

`x` / `y` を `pico.set()` すると、**形を保ったまま全体を平行移動**します。コンテナに入れても線の向きや長さは崩れません。形そのものを変えるときは `x1` / `y1` などを設定します。

## 見た目

太い線もアンチエイリアス(色のにじみ)の無い単色で描きます(画面が16色のため)。

プロパティの全部は [プロパティ対応表](../../../reference/widget-properties/) の `Rect` / `Ellipse` / `Line` / `Triangle` にあります。
