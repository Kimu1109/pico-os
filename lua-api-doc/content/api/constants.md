---
title: "定数"
weight: 95
description: "COLOR_* / FONT_* / ICON_* / ICON_SIZE_* / TEXT_ALIGN_* / DIRECTION_* / ALIGN_* / CANVAS_* / WDAY_* / SCREEN_* / iso.BLOCK_* / iso.KIND_*"
---

数値を直接書く代わりに、名前で指定できます(`pico.set(id, "color", pico.COLOR_RED)`)。値はC++側の定義をそのまま引くので、`consts.hpp` の `PICO_RED` などと常に一致します。定数は読み取り専用の約束です(書き換えても他のアプリには影響しませんが、してはいけません)。

```lua
pico.fill_rect(10, 40, 100, 50, pico.COLOR_RED)
pico.set(label, "text_align", pico.TEXT_ALIGN_CENTER)
pico.set(icon, "icon_id", pico.ICON_Home)
```

| 名前 | 値 | 使う場所 |
|---|---|---|
| `COLOR_BLACK` `COLOR_NAVY` `COLOR_DARKGREEN` `COLOR_DARKCYAN` `COLOR_MAROON` `COLOR_PURPLE` `COLOR_OLIVE` `COLOR_LIGHTGREY` `COLOR_DARKGREY` `COLOR_BLUE` `COLOR_GREEN` `COLOR_CYAN` `COLOR_RED` `COLOR_MAGENTA` `COLOR_YELLOW` `COLOR_WHITE` | 0〜15 | 色(パレット番号) |
| `COLOR_BACKGROUND` / `COLOR_FORECOLOR` | 15 / 0 | 既定の背景色(白)/ 前景色(黒) |
| `FONT_SMALL` `FONT_NORMAL` `FONT_BIG` `FONT_BIGGER` | 0〜3 | `font_size` |
| `ICON_SIZE_16` `ICON_SIZE_24` `ICON_SIZE_32` `ICON_SIZE_48` `ICON_SIZE_64` | 0〜4 | `icon_size` |
| `ICON_<名前>` | アイコンの番号 | `icon_id`。名前は [定数・上限一覧](../../reference/limits/) の `icon_id` の表と同じ(`ICON_Home` `ICON_Settings` `ICON_FileArchive` …)。並び順が変わっても名前なら追従します |
| `TEXT_ALIGN_LEFT` `TEXT_ALIGN_CENTER` `TEXT_ALIGN_RIGHT` | 0〜2 | `text_align` |
| `DIRECTION_VERTICAL` `DIRECTION_HORIZONTAL` | 0 / 1 | `direction` |
| `ALIGN_START` `ALIGN_CENTER` `ALIGN_END` | 0〜2 | `cross_align` / `h_align` / `v_align` |
| `CANVAS_LINE` `CANVAS_RECT` `CANVAS_ELLIPSE` `CANVAS_ARROW` `CANVAS_STRAIGHT` `CANVAS_FILL` | 0〜5 | `canvas_mode` |
| `WDAY_SUNDAY` … `WDAY_SATURDAY` | 0〜6 | `pico.get_time().wday` |
| `SCREEN_WIDTH` / `SCREEN_HEIGHT` / `STATUSBAR_HEIGHT` | 240 / 320 / 20 | 画面の大きさ |
| `iso.KIND_NATURAL` `KIND_FLAT` `KIND_DEMO` `KIND_EMPTY` `KIND_ARENA` | 0〜4 | `pico.iso.create` の `kind` |
| `iso.BLOCK_AIR` `BLOCK_WATER` `BLOCK_STONE` `BLOCK_GRASS` … `BLOCK_TORCH` | 0〜25 | ブロックの番号([ブロック](../iso/) の一覧と同じ並び) |

存在しない名前は `nil` です(書き間違いは `nil` になって気づけるよう、使う前に `assert(pico.COLOR_RED)` と書いてもかまいません)。
