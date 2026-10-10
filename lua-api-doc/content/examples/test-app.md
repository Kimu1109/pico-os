---
title: "動作確認アプリ"
weight: 10
description: "pc/sdcard/lua/apps/動作確認/ — APIを1画面ずつ試せるページをメニューから開くアプリ"
---

`pc/sdcard/lua/apps/動作確認/` は、Lua APIの動作確認用の画面を1つにまとめたアプリです。`main.lua` はメニューで、各ページ(同じフォルダの `*.lua`)を `pico.push_scene` で開きます。ページの「戻る」(`pico.pop()`)でメニューへ戻ります。

| ページ | ファイル | 確かめるもの |
|---|---|---|
| 基本の部品 | `basics.lua` | `TabBar`(`pico.tab_link`)・`Textbox`(`text_changed`)・`NumberInput`・`Checkbox`(`checked_changed`)・`DropdownMenu`(`list_add` / `dropdown_changed`)・`add_child` / `remove_child`・`pico.beep`・図形ウィジェット(`Rect` / `Ellipse` / `Line` / `Triangle`) |
| 追加の部品 | `extra.lua` | `ProgressBar` / `AnalogClock` / `DurationPicker` / `MonthGrid` / `MarkdownView` / `TextView` / `ImageView`、`pico.ui` / `pico.tween`、ジェスチャー、`pico.encrypt` |
| 直接描画 | `draw.lua` | `Canvas` の `render` で描く図形・太線・多角形・扇形、`draw_image_ex`、`text_width`、`pico.on_key`、`pico.battery` |
| 画面とデータ | `scene.lua` + `picker.lua` + `util.lua` | `require`、タッチ座標の引数、`push_scene(path, args)` → `pico.pop(result)` → `on_result`、`on_suspend` / `on_resume`、JSON、タイマー、`store_save` |
| コントローラー | `pad.lua` | `pico.pad_*`、`pico.on_back` |
| 音 | `sound.lua` + `demo.mml` | `pico.sound_play` の鍵盤、`pico.music_play` |
| 通知 | `notify.lua` | `pico.notify` / `notify_cancel` / `notify_list`(`app.cfg` の `permission_notify=true`) |

## ページの開き方

```lua
local dir = pico.app_dir()
pico.push_scene(pico.path_join(dir, "basics.lua"))
```

開いた先の `app_dir` も同じフォルダなので、`require("util")` や `pico.store_save()` はそのフォルダのファイルを使います。権限(`app.cfg`)も開いたページへ引き継がれます。

## 通知から開かれたとき

`pico.launch_reason()` はアプリの最初の画面で1回だけ読めます。そこでメニューが受け取り、通知のページへ `args` で渡します。

```lua
local tag, data = pico.launch_reason()
if tag then pico.push_scene(pico.path_join(dir, "notify.lua"), { tag = tag, data = data }) end
```
