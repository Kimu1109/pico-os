---
title: "ウィジェット種別一覧"
weight: 10
description: "pico.createで生成できる種別と、生成直後の初期値"
---

## pico.create() で生成できる20種別

生成直後の位置・大きさは仮の値です。実際に配置する前に `pico.set()` で `x`/`y`/`w`/`h` 等を設定してください。

| `type_name` | 生成直後の状態 | 対応プロパティ(抜粋) |
|---|---|---|
| `"Button"` | x=0, y=0, text="" | text, w, h, font_size, text_color, border_color, icon_id, icon_size(アイコンボタン化) |
| `"Label"` | x=0, y=0, text="" | text, placeholder, font_size, text_color, border_color, max_width, max_height, text_align |
| `"Textbox"` | x=0, y=0, w=100, h=24, 単一行 | text, placeholder, font_size, text_color, border_color, max_width, max_height, is_single_line |
| `"NumberInput"` | x=0, y=0, w=60 | text(入力された数字文字列), font_size, text_color, border_color |
| `"Checkbox"` | x=0, y=0, text="" | text, checked, font_size, text_color |
| `"Icon"` | x=0, y=0, IconID::AppBox, 16px | icon_id, icon_size, color, icon_opaque |
| `"Image"` | x=0, y=0, path="" (未設定は何も描かない) | path |
| `"NumberSlider"` | x=0, y=0, w=100 | value, min_value, max_value, w, h, color, visible_num, decimal_places |
| `"ScrollContainer"` | x=0, y=0, 100×100 | border_color(のみ。w/hは変更不可) |
| `"ScrollList"` | x=0, y=0, 100×100 | w, h, font_size, text_color, border_color, selected_index, enable_icon, item_count(読み取り専用) |
| `"CanvasRaster"` | x=0, y=0, 100×100 | w, h, color, brush_radius, canvas_mode |
| `"LayoutContainer"` | x=0, y=0, 100×100 | w, h, direction, cross_align, gap, padding |
| `"GridContainer"` | x=0, y=0, 100×100, 2列 | w, h, cols, gap, padding, h_align, v_align |
| `"TabBar"` | x=0, y=0, w=100, h=24, タブ0個 | w, h, tab_selected, tab_count(読み取り専用), font_size, border_color |
| `"DropdownMenu"` | x=0, y=0, w=100, 項目0個 | w, selected_index, item_count(読み取り専用) |
| `"Canvas"` | x=0, y=0, 50×50(内部クラス名は`LuaCanvas`) | w, h(描画は`render`イベントで行う) |
| `"Rect"` | x=0, y=0, 40×24(内部クラス名は`RectShape`) | w, h, color, filled, thickness |
| `"Ellipse"` | x=0, y=0, 40×24(内部クラス名は`EllipseShape`) | w, h, color, filled, thickness |
| `"Line"` | (0,0)-(40,24)(内部クラス名は`LineShape`) | color, thickness, x1, y1, x2, y2 |
| `"Triangle"` | (0,20)/(20,0)/(40,20)(内部クラス名は`TriangleShape`) | color, filled, thickness, x1, y1, x2, y2, x3, y3 |

図形ウィジェット4種(`Rect`/`Ellipse`/`Line`/`Triangle`)の詳しい使い方は [図形ウィジェット](../../guide/shapes/) を参照してください。

全プロパティの詳細は [プロパティ対応表](../widget-properties/) を、共通/固有イベントは [イベント対応表](../widget-events/) を参照してください。

## pico.show_xxx() でのみ生成できる5種別

`pico.create()` の対象外です(コンストラクタが必須引数を取るため)。[ダイアログ](../../guide/dialogs/) を参照してください。

| ダイアログ | 生成関数 |
|---|---|
| `MsgDialog` | `pico.show_message()` |
| `InputDialog` | `pico.show_input()` |
| `FileSaveDialog` | `pico.show_file_save()` |
| `FileSelectDialog` | `pico.show_file_select()` |
| `ColorDialog` | `pico.show_color()` |

## Luaから生成できないウィジェット

OS内部専用(SD走査・シーン固有の状態への依存が強い)で、Luaからは生成できません。

`AnalogClock` / `AppGrid` / `CalculatorKeypad` / `DurationPicker` / `FileExplorer` / `Keyboard` / `KeyboardEng` / `KeyboardNum` / `MarkdownView` / `SearchDialog` / `Statusbar`

これらのウィジェットを使うC++製アプリ(時計・電卓・ファイルエクスプローラー等)へ遷移したい場合は `pico.launch_app(name)` を使ってください。
