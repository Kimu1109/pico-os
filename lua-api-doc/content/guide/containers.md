---
title: "レイアウトコンテナ"
weight: 30
description: "LayoutContainer / GridContainer / ScrollContainer で子を自動整列する"
---

`pico.add_child()` / `pico.remove_child()` に対応するのは次の3種類だけです。子の**位置だけ**をコンテナが決め、大きさは子ウィジェット自身に委ねます(コンテナ側に `setW`/`setH` に相当する子サイズ制御はありません)。

## LayoutContainer(縦 or 横の1方向integer整列)

```lua
local box = pico.create("LayoutContainer")
pico.set(box, "w", 220)
pico.set(box, "h", 40)
pico.set(box, "direction", 1)   -- 0=縦(VERTICAL) / 1=横(HORIZONTAL)
pico.set(box, "cross_align", 1) -- 0=START / 1=CENTER / 2=END(主軸に垂直な方向の揃え)
pico.set(box, "gap", 8)         -- 子と子の間隔(px)
pico.set(box, "padding", 4)     -- コンテナ内側の余白(px)

local a = pico.create("Label")
pico.set(a, "text", "A")
pico.add_child(box, a)
```

| プロパティ | 型 | 説明 |
|---|---|---|
| `w` / `h` | Int | コンテナ自体の大きさ |
| `direction` | Int | `0`=VERTICAL(縦積み) / `1`=HORIZONTAL(横並び) |
| `cross_align` | Int | `0`=START / `1`=CENTER / `2`=END |
| `gap` | Int | 子同士の間隔(px) |
| `padding` | Int | コンテナ内側の余白(px) |

## GridContainer(列数固定の2次元流し込み)

```lua
local grid = pico.create("GridContainer")
pico.set(grid, "w", 220)
pico.set(grid, "h", 220)
pico.set(grid, "cols", 3)
pico.set(grid, "gap", 6)
pico.set(grid, "h_align", 1) -- セル内、横方向の揃え(0=START/1=CENTER/2=END)
pico.set(grid, "v_align", 1) -- セル内、縦方向の揃え
```

| プロパティ | 型 | 説明 |
|---|---|---|
| `w` / `h` | Int | コンテナ自体の大きさ |
| `cols` | Int | 列数 |
| `gap` | Int | セル間隔(px) |
| `padding` | Int | コンテナ内側の余白(px) |
| `h_align` / `v_align` | Int | セル内での子の横/縦の揃え方(`0`=START/`1`=CENTER/`2`=END) |

## ScrollContainer(スクロール可能な入れ物)

```lua
local scroll = pico.create("ScrollContainer")
pico.set(scroll, "border_color", 8)
```

`ScrollContainer` は `border_color` 以外の専用プロパティを持ちません(大きさは生成時の100×100固定で、`pico.set` からの `w`/`h` 変更には対応していません)。子の配置は自動整列せず、子自身の座標をそのまま使います。

## 子の取り外しと座標

`pico.remove_child(container, child)` で取り外した子は、**コンテナ内での相対座標をそのまま保持**します。取り外し後にコンテナの外の絶対座標へ配置し直したい場合は、`pico.set(child, "x", ...)` / `pico.set(child, "y", ...)` を忘れずに呼んでください。

```lua
pico.remove_child(container, child)
pico.set(child, "x", 100)
pico.set(child, "y", 100)
-- 破棄したい場合はさらに pico.destroy(child)
```
