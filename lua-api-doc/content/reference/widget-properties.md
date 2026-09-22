---
title: "プロパティ対応表"
weight: 20
description: "pico.set / pico.get で読み書きできるプロパティの、ウィジェットごとの完全な一覧"
---

`pico.set(id, name, value)` / `pico.get(id, name)` の `name` に渡せる文字列と、対応する型・対応ウィジェットの一覧です。表に無い名前を渡すとエラーになります(綴りミスとして検出されます)。表にあっても、そのウィジェット種別が対応していない場合は `pico.set` はエラー、`pico.get` は `nil` を返します。

## 全ウィジェット共通

`pico.create()` で作れる全種別、および `pico.show_*()` が返すダイアログすべてに共通です。

| name | 型 | get | set |
|---|---|---|---|
| `x` | Int | ✓ | ✓ |
| `y` | Int | ✓ | ✓ |
| `w` | Int | ✓ | ウィジェットによる(下表) |
| `h` | Int | ✓ | ウィジェットによる(下表) |
| `visible` | Bool | ✓ | ✓ |
| `background_color` | Int | ✓ | ✓ |

`w` / `h` の**取得**は常にできますが(現在の実寸が返ります)、**設定**できるかはウィジェットごとに異なります。下表で `w`/`h` の行がある種別のみ `pico.set` に対応しています。

## Button

| name | 型 | get | set |
|---|---|---|---|
| `text` | Str | ✓ | ✓ |
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `font_size` | Int | ✓ | ✓ |
| `text_color` | Int | ✓ | ✓ |
| `border_color` | Int | ✓ | ✓ |
| `icon_id` | Int | ✓ | ✗(**読み取り専用**) |

## Label

| name | 型 | get | set |
|---|---|---|---|
| `text` | Str | ✓ | ✓ |
| `placeholder` | Str | ✓ | ✓ |
| `font_size` | Int | ✓ | ✓ |
| `text_color` | Int | ✓ | ✓ |
| `border_color` | Int | ✓ | ✓ |
| `max_width` | Int | ✓ | ✓ |
| `max_height` | Int | ✓ | ✓ |
| `text_align` | Int(`0`=Left/`1`=Center/`2`=Right) | ✓ | ✓ |

`w` / `h` は直接設定できません。`max_width` / `max_height` とテキスト内容から自動的に決まります。

## Textbox

| name | 型 | get | set |
|---|---|---|---|
| `text` | Str | ✓ | ✓ |
| `placeholder` | Str | ✓ | ✓ |
| `font_size` | Int | ✓ | ✓ |
| `text_color` | Int | ✓ | ✓ |
| `border_color` | Int | ✓ | ✓ |
| `max_width` | Int | ✓ | ✓ |
| `max_height` | Int | ✓ | ✓ |
| `is_single_line` | Bool | ✓ | ✓ |

`Label` と同じく `w` / `h` は直接設定できません。

## NumberInput

| name | 型 | get | set |
|---|---|---|---|
| `text` | Str(入力された数字文字列。数値化は呼び出し側の責任) | ✓ | ✓ |
| `font_size` | Int | ✓ | ✓ |
| `text_color` | Int | ✓ | ✓ |
| `border_color` | Int | ✓ | ✓ |

## Checkbox

| name | 型 | get | set |
|---|---|---|---|
| `text` | Str | ✓ | ✓ |
| `checked` | Bool | ✓ | ✓ |
| `font_size` | Int | ✓ | ✓ |
| `text_color` | Int | ✓ | ✓ |

## Icon

| name | 型 | get | set |
|---|---|---|---|
| `icon_id` | Int(`IconID`) | ✓ | ✓ |
| `icon_size` | Int(`0`=16px/`1`=24px/`2`=32px/`3`=48px/`4`=64px) | ✓ | ✓ |
| `color` | Int | ✓ | ✓ |
| `icon_opaque` | Bool | ✓ | ✓ |

## Image

| name | 型 | get | set |
|---|---|---|---|
| `path` | Str | ✓ | ✓ |

## NumberSlider

| name | 型 | get | set |
|---|---|---|---|
| `value` | Float | ✓ | ✓(Int/Floatどちらでも可) |
| `min_value` | Float | ✓ | ✓ |
| `max_value` | Float | ✓ | ✓ |
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `color` | Int | ✓ | ✓ |
| `visible_num` | Bool | ✓ | ✓ |
| `decimal_places` | Int | ✓ | ✓ |

## ScrollContainer

| name | 型 | get | set |
|---|---|---|---|
| `border_color` | Int | ✓ | ✓ |

`w` / `h` の設定には対応していません(生成時の100×100固定)。

## ScrollList

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `font_size` | Int | ✓ | ✓ |
| `text_color` | Int | ✓ | ✓ |
| `border_color` | Int | ✓ | ✓ |
| `selected_index` | Int | ✓ | ✓ |
| `enable_icon` | Bool | ✓ | ✓ |
| `item_count` | Int | ✓ | ✗(**読み取り専用**。`pico.list_add`/`pico.list_clear`で増減) |

## CanvasRaster

| name | 型 | get | set |
|---|---|---|---|
| `color` | Int(ブラシ色) | ✓ | ✓ |
| `brush_radius` | Float | ✓ | ✓(Int/Floatどちらでも可) |
| `canvas_mode` | Int(`0`=Line/`1`=Rect/`2`=Ellipse/`3`=Arrow) | ✓ | ✓ |

`w` / `h` の設定には対応していません(生成時の100×100固定)。

## LayoutContainer

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `direction` | Int(`0`=VERTICAL/`1`=HORIZONTAL) | ✓ | ✓ |
| `cross_align` | Int(`0`=START/`1`=CENTER/`2`=END) | ✓ | ✓ |
| `gap` | Int | ✓ | ✓ |
| `padding` | Int | ✓ | ✓ |

## GridContainer

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `cols` | Int | ✓ | ✓ |
| `gap` | Int | ✓ | ✓ |
| `padding` | Int | ✓ | ✓ |
| `h_align` | Int(`0`=START/`1`=CENTER/`2`=END) | ✓ | ✓ |
| `v_align` | Int(同上) | ✓ | ✓ |

## TabBar

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `tab_selected` | Int | ✓ | ✓(setするとタップ経由と同じ扱いで`tab_changed`が発火する) |
| `tab_count` | Int | ✓ | ✗(**読み取り専用**。`pico.tab_add`で増やす) |
| `font_size` | Int | ✓ | ✓ |
| `border_color` | Int | ✓ | ✓ |

## DropdownMenu

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `selected_index` | Int | ✓ | ✓ |
| `item_count` | Int | ✓ | ✗(**読み取り専用**。`pico.list_add`/`pico.list_clear`で増減) |

`h` の設定には対応していません(内容に応じた固定の高さ)。

## Canvas(`LuaCanvas`)

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |

これ以外の固有プロパティはありません。内容は `pico.on(id, "render", fn)` で描きます([Canvasと直接描画](../../guide/drawing/) 参照)。

## Rect(`RectShape`)

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `color` | Int | ✓ | ✓ |
| `filled` | Bool(既定`true`) | ✓ | ✓ |
| `thickness` | Int(既定`1`。`filled=false`のときの枠線の太さ) | ✓ | ✓ |

## Ellipse(`EllipseShape`)

| name | 型 | get | set |
|---|---|---|---|
| `w` | Int | ✓ | ✓ |
| `h` | Int | ✓ | ✓ |
| `color` | Int | ✓ | ✓ |
| `filled` | Bool(既定`true`) | ✓ | ✓ |
| `thickness` | Int(既定`1`。`filled=false`のときの輪郭の太さ) | ✓ | ✓ |

`w`/`h`に内接する楕円(`w==h`なら円)を描きます。

## Line(`LineShape`)

| name | 型 | get | set |
|---|---|---|---|
| `color` | Int | ✓ | ✓ |
| `thickness` | Int(既定`1`) | ✓ | ✓ |
| `x1` | Int(始点) | ✓ | ✓ |
| `y1` | Int(始点) | ✓ | ✓ |
| `x2` | Int(終点) | ✓ | ✓ |
| `y2` | Int(終点) | ✓ | ✓ |

`w`/`h`の**設定**には対応していません(`x1`/`y1`/`x2`/`y2`と`thickness`から外接矩形として自動計算されます)。共通プロパティの`x`/`y`は外接矩形の左上を指し、**setすると2点をまとめて平行移動**します(線の向き・長さは変わりません)。線の形そのものを変えたい場合は`x1`/`y1`/`x2`/`y2`を個別に設定してください。

## Triangle(`TriangleShape`)

| name | 型 | get | set |
|---|---|---|---|
| `color` | Int | ✓ | ✓ |
| `filled` | Bool(既定`true`) | ✓ | ✓ |
| `thickness` | Int(既定`1`。`filled=false`のときの輪郭の太さ) | ✓ | ✓ |
| `x1` | Int(頂点1) | ✓ | ✓ |
| `y1` | Int(頂点1) | ✓ | ✓ |
| `x2` | Int(頂点2) | ✓ | ✓ |
| `y2` | Int(頂点2) | ✓ | ✓ |
| `x3` | Int(頂点3) | ✓ | ✓ |
| `y3` | Int(頂点3) | ✓ | ✓ |

`Line`と同じく`w`/`h`の設定には対応していません(3頂点+`thickness`から自動計算)。共通プロパティの`x`/`y`は外接矩形の左上を指し、**setすると3頂点をまとめて平行移動**します。

## ダイアログ

`pico.show_xxx()` が返すIDに対しては、共通プロパティに加えて次だけ使えます。

| ダイアログ | name | 型 | get | set |
|---|---|---|---|---|
| `InputDialog` | `text` | Str | ✓ | ✓ |
| `InputDialog` | `is_single_line` | Bool | ✗ | ✓ |
| `FileSaveDialog` | `path` | Str | ✓ | ✗ |
| `FileSelectDialog` | `path` | Str | ✓(未選択は`nil`) | ✗ |
| `ColorDialog` | `value` | Int | ✓(未選択は`-1`) | ✗ |
| `MsgDialog` | (専用プロパティなし。結果は`closed`イベントの`is_ok`のみ) | — | — | — |

`start_dir`(FileSaveDialog/FileSelectDialog)や選択色の初期値など、生成時のみ決まる値は後から差し替えられません。
