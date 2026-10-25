---
aliases: ["/api/constants/"]
title: "定数"
weight: 40
description: "COLOR_* / FONT_* / ICON_* / TEXT_ALIGN_* / DIRECTION_* / ALIGN_* / CANVAS_* / WDAY_* / SCREEN_* / iso.BLOCK_* / iso.KIND_*"
---

数値を直接書く代わりに、名前で書けます。名前で書けば、OSの版が上がって番号が変わっても追従します。

```lua
pico.fill_rect(10, 40, 100, 50, pico.COLOR_RED)
pico.set(label, "text_align", pico.TEXT_ALIGN_CENTER)
pico.set(icon, "icon_id", pico.ICON_Home)
```

- 定数は読むだけにしてください(書き換えてはいけません)。
- 無い名前は `nil` です。書き間違いに気づきたいときは `assert(pico.COLOR_RED)` のように書けます。

## 一覧

| 名前 | 値 | 使う場所 |
|---|---|---|
| `COLOR_BLACK` … `COLOR_WHITE` | 0〜15 | 色(下の「色パレット」) |
| `COLOR_BACKGROUND` / `COLOR_FORECOLOR` | 15 / 0 | 既定の背景色(白)/ 文字の色(黒) |
| `FONT_SMALL` `FONT_NORMAL` `FONT_BIG` `FONT_BIGGER` | 0〜3 | `font_size` |
| `ICON_SIZE_16` `ICON_SIZE_24` `ICON_SIZE_32` `ICON_SIZE_48` `ICON_SIZE_64` | 0〜4 | `icon_size` |
| `ICON_<名前>` | アイコンの番号 | `icon_id`(下の「アイコン」) |
| `TEXT_ALIGN_LEFT` `TEXT_ALIGN_CENTER` `TEXT_ALIGN_RIGHT` | 0〜2 | `text_align` |
| `DIRECTION_VERTICAL` `DIRECTION_HORIZONTAL` | 0 / 1 | `LayoutContainer` の `direction` |
| `ALIGN_START` `ALIGN_CENTER` `ALIGN_END` | 0〜2 | `cross_align` / `h_align` / `v_align` |
| `CANVAS_LINE` `CANVAS_RECT` `CANVAS_ELLIPSE` `CANVAS_ARROW` `CANVAS_STRAIGHT` `CANVAS_FILL` | 0〜5 | `CanvasRaster` の `canvas_mode` |
| `QR_ECC_LOW` `QR_ECC_MEDIUM` `QR_ECC_QUARTILE` `QR_ECC_HIGH` | 0〜3 | `QRCode` の `ecc`(誤り訂正の強さ) |
| `WDAY_SUNDAY` … `WDAY_SATURDAY` | 0〜6 | `pico.get_time().wday` |
| `SCREEN_WIDTH` / `SCREEN_HEIGHT` / `STATUSBAR_HEIGHT` | 240 / 320 / 20 | 画面の大きさ |
| `iso.KIND_NATURAL` `KIND_FLAT` `KIND_DEMO` `KIND_EMPTY` `KIND_ARENA` | 0〜4 | `pico.iso.create` の `kind` |
| `iso.BLOCK_AIR` … `iso.BLOCK_TORCH` | 0〜25 | 箱庭のブロック(下の「ブロック」) |

## 色パレット

| 値 | 定数 | 色 | 値 | 定数 | 色 |
|---|---|---|---|---|---|
| 0 | `COLOR_BLACK` | 黒(既定の文字の色) | 8 | `COLOR_DARKGREY` | ダークグレー |
| 1 | `COLOR_NAVY` | ネイビー | 9 | `COLOR_BLUE` | 青 |
| 2 | `COLOR_DARKGREEN` | ダークグリーン | 10 | `COLOR_GREEN` | 緑 |
| 3 | `COLOR_DARKCYAN` | ダークシアン | 11 | `COLOR_CYAN` | シアン |
| 4 | `COLOR_MAROON` | マルーン | 12 | `COLOR_RED` | 赤 |
| 5 | `COLOR_PURPLE` | パープル | 13 | `COLOR_MAGENTA` | マゼンタ |
| 6 | `COLOR_OLIVE` | オリーブ | 14 | `COLOR_YELLOW` | 黄 |
| 7 | `COLOR_LIGHTGREY` | ライトグレー | 15 | `COLOR_WHITE` | 白(既定の背景色) |

ダークテーマでは、同じ番号の色が明暗で入れ替わって表示されます([`pico.theme`](../../api/system/misc/#picotheme))。1〜14の色は [`pico.set_palette`](../../api/graphics/drawing/#picoset_palette) で変えられます。

## 文字の大きさ(font_size)

| 値 | 定数 | 大きさ |
|---|---|---|
| `0` | `FONT_SMALL` | 16px |
| `1` | `FONT_NORMAL` | 24px(既定) |
| `2` | `FONT_BIG` | 32px |
| `3` | `FONT_BIGGER` | 48px |

## アイコンの大きさ(icon_size)

| 値 | 定数 | 大きさ |
|---|---|---|
| `0` | `ICON_SIZE_16` | 16px(既定) |
| `1` | `ICON_SIZE_24` | 24px |
| `2` | `ICON_SIZE_32` | 32px |
| `3` | `ICON_SIZE_48` | 48px |
| `4` | `ICON_SIZE_64` | 64px |

## アイコン

`Icon` と、アイコンボタンにした `Button` の `icon_id`、`ScrollList` の項目の `icon` に渡します。番号はOSの版で変わることがあるので、**名前(`pico.ICON_Home` など)で書いてください**。

| 名前 | 名前 | 名前 | 名前 |
|---|---|---|---|
| `ICON_WifiSignal1` | `ICON_WifiSignal2` | `ICON_WifiSignal3` | `ICON_WifiSignal4` |
| `ICON_Battery0` | `ICON_Battery1` | `ICON_Battery2` | `ICON_Battery3` |
| `ICON_Battery4` | `ICON_BatteryCharging` | `ICON_SdCard` | `ICON_VolumeHigh` |
| `ICON_VolumeLow` | `ICON_VolumeOff` | `ICON_Sun0` | `ICON_Sun1` |
| `ICON_Sun2` | `ICON_Sun3` | `ICON_Bell` | `ICON_BellOff` |
| `ICON_BellRinging` | `ICON_Lock` | `ICON_LockOff` | `ICON_Eye` |
| `ICON_EyeOff` | `ICON_Home` | `ICON_Search` | `ICON_Settings` |
| `ICON_Trash` | `ICON_ArrowUp` | `ICON_ArrowLeft` | `ICON_ArrowDown` |
| `ICON_ArrowRight` | `ICON_X` | `ICON_CheckboxOn` | `ICON_CheckboxOff` |
| `ICON_CircleDashedPlus` | `ICON_Refresh` | `ICON_Power` | `ICON_Menu` |
| `ICON_DotsVertical` | `ICON_ChevronDown` | `ICON_ChevronUp` | `ICON_Star` |
| `ICON_Share` | `ICON_User` | `ICON_Send` | `ICON_Keyboard` |
| `ICON_Language` | `ICON_Link` | `ICON_Save` | `ICON_StackPop` |
| `ICON_StackPush` | `ICON_Brush` | `ICON_Eraser` | `ICON_AlertTriangle` |
| `ICON_InfoCircle` | `ICON_Help` | `ICON_AppBox` | `ICON_AppStore` |
| `ICON_Calendar` | `ICON_Game` | `ICON_Clock` | `ICON_Calculator` |
| `ICON_LanguageHiragana` | `ICON_Browser` | `ICON_Edit` | `ICON_File` |
| `ICON_Folder` | `ICON_Copy` | `ICON_Pencil` | `ICON_Line` |
| `ICON_Circle` | `ICON_SquareFilled` | `ICON_CircleFilled` | `ICON_Bucket` |
| `ICON_Palette` | `ICON_Undo` | `ICON_FolderOpen` | `ICON_FilePlus` |
| `ICON_Messages` | `ICON_Music` | `ICON_Terminal` | `ICON_Camera` |
| `ICON_Video` | `ICON_Microphone` | `ICON_FileImage` | `ICON_FileVideo` |
| `ICON_FileAudio` | `ICON_FileText` | `ICON_FileCode` | `ICON_FileArchive` |
| `ICON_Mail` | `ICON_MailOpened` | `ICON_Book` | |

`ICON_CheckboxOff` は白抜きの四角なので、四角形の道具のアイコンにも使えます。

## ブロック

`pico.iso` の箱庭のブロックの番号です。

| 値 | 名前 | 値 | 名前 |
|---|---|---|---|
| 0 | `iso.BLOCK_AIR` | 13 | `iso.BLOCK_TNT` |
| 1 | `iso.BLOCK_WATER` | 14 | `iso.BLOCK_CRAFTING` |
| 2 | `iso.BLOCK_STONE` | 15 | `iso.BLOCK_FURNACE` |
| 3 | `iso.BLOCK_GRASS` | 16 | `iso.BLOCK_JUKEBOX` |
| 4 | `iso.BLOCK_DIRT` | 17 | `iso.BLOCK_SPONGE` |
| 5 | `iso.BLOCK_COBBLE` | 18 | `iso.BLOCK_GRAVEL` |
| 6 | `iso.BLOCK_PLANKS` | 19 | `iso.BLOCK_MOSS` |
| 7 | `iso.BLOCK_BRICKS` | 20 | `iso.BLOCK_COAL_ORE` |
| 8 | `iso.BLOCK_SLABS` | 21 | `iso.BLOCK_IRON_ORE` |
| 9 | `iso.BLOCK_WOOD` | 22 | `iso.BLOCK_BEDROCK` |
| 10 | `iso.BLOCK_LEAVES` | 23 | `iso.BLOCK_IRON` |
| 11 | `iso.BLOCK_SAND` | 24 | `iso.BLOCK_GOLD` |
| 12 | `iso.BLOCK_BOOKS` | 25 | `iso.BLOCK_TORCH` |
