---
aliases: ["/guide/containers/"]
title: "レイアウトコンテナ"
weight: 30
description: "LayoutContainer / GridContainer / ScrollContainer で子を並べる"
---

`pico.add_child()` で子を入れられるのは次の3種類です。`LayoutContainer` と `GridContainer` は子の**位置**だけを決め、大きさは子のままです。

## LayoutContainer(縦か横に1列)

```lua
local box = pico.create("LayoutContainer")
pico.set(box, "w", 220)
pico.set(box, "h", 40)
pico.set(box, "direction", pico.DIRECTION_HORIZONTAL)  -- 横に並べる
pico.set(box, "cross_align", pico.ALIGN_CENTER)        -- 並べる向きと直角の方向の揃え
pico.set(box, "gap", 8)                                -- 子と子の間(px)
pico.set(box, "padding", 4)                            -- 内側の余白(px)

local a = pico.create("Label")
pico.set(a, "text", "A")
pico.add_child(box, a)
```

| プロパティ | 説明 |
|---|---|
| `w` / `h` | コンテナの大きさ |
| `direction` | `0`(`DIRECTION_VERTICAL`)=縦に積む / `1`(`DIRECTION_HORIZONTAL`)=横に並べる |
| `cross_align` | `0`(`ALIGN_START`) / `1`(`ALIGN_CENTER`) / `2`(`ALIGN_END`) |
| `gap` | 子と子の間(px) |
| `padding` | 内側の余白(px) |

## GridContainer(列数を決めて流し込む)

```lua
local grid = pico.create("GridContainer")
pico.set(grid, "w", 220)
pico.set(grid, "h", 220)
pico.set(grid, "cols", 3)
pico.set(grid, "gap", 6)
pico.set(grid, "h_align", pico.ALIGN_CENTER)  -- マスの中の横の揃え
pico.set(grid, "v_align", pico.ALIGN_CENTER)  -- マスの中の縦の揃え
```

| プロパティ | 説明 |
|---|---|
| `w` / `h` | コンテナの大きさ |
| `cols` | 列の数 |
| `gap` | マスとマスの間(px) |
| `padding` | 内側の余白(px) |
| `h_align` / `v_align` | マスの中での子の横/縦の揃え(`ALIGN_START` / `ALIGN_CENTER` / `ALIGN_END`) |

## ScrollContainer(スクロールできる入れ物)

```lua
local scroll = pico.create("ScrollContainer")
pico.set(scroll, "w", 220)
pico.set(scroll, "h", 200)
pico.set(scroll, "border_color", pico.COLOR_DARKGREY)
for i = 1, 20 do
    local b = pico.create("Button")
    pico.set(b, "y", (i - 1) * 32)
    pico.set(b, "text", "項目 " .. i)
    pico.add_child(scroll, b)
end
```

- 子は並べ替えず、子の `x` / `y`(コンテナの中の座標)をそのまま使います。
- 上下のドラッグでスクロールします。位置は `scroll_y`(読み書き)、動かせる最大は `max_scroll_y`(`add_child` の後に読む)、変化は `scrolled` イベントで分かります。
- 子が見える位置まで動かすには `pico.scroll_to(scroll, child)` を使います。

## 子の取り外し

`pico.remove_child(container, child)` で取り外した子は、**コンテナの中での座標のまま**です。外に置き直すなら `x` / `y` を設定し直してください。

```lua
pico.remove_child(container, child)
pico.set(child, "x", 100)
pico.set(child, "y", 100)
-- 要らなければ pico.destroy(child)
```
