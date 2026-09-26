---
title: "APIリファレンス"
weight: 30
description: "pico.* 全関数のシグネチャ・引数・戻り値。使い方の解説はガイドを参照してください。"
---

`pico` テーブルに生えている全関数を、機能カテゴリごとに一覧しています。各関数の役割や典型的な使用例が知りたい場合は [ガイド](../guide/) を参照してください。

## 表記ルール

- `pico.関数名(引数, ...) -> 戻り値` の形式でシグネチャを示します。
- `[引数]` は省略可能な引数です。省略時の既定値を併記します。
- 戻り値が無い関数は `-> (なし)` と表記します。
- WidgetId・イメージハンドルはいずれも Lua 側では通常の整数(number)です。

## 全関数一覧

| カテゴリ | 関数 |
|---|---|
| [ウィジェット操作](widgets/) | `create` `destroy` `set` `get` `on` `add_child` `remove_child` `list_add` `list_clear` `tab_add` |
| [直接描画](drawing/) | `draw_pixel` `draw_line` `draw_rect` `fill_rect` `draw_circle` `fill_circle` `clear_rect` `draw_text` `invalidate` `mark_dirty` `set_draw_area` `clear_draw_area` `get_draw_area` |
| [タッチ](touch/) | `get_touch` |
| [コントローラー](pad/) | `pad_connected` `pad_down` `pad_pressed` `pad_released` |
| [画像](images/) | `image_load` `image_size` `draw_image` `draw_image_part` `image_free` |
| [ラスタキャンバス](canvas/) | `canvas_clear` `canvas_save` `canvas_load` `canvas_undo` |
| [SDカード](sdcard/) | `sd_exists` `sd_read` `sd_write` `sd_remove` `sd_mkdir` `sd_list` |
| [シーン制御](scenes/) | `pop` `push_scene` `change_scene` `launch_app` `content_rect` |
| [ダイアログ](dialogs/) | `show_message` `show_input` `show_file_save` `show_file_select` `show_color` |
| [ネットワーク](network/) | `http_request` `http_cancel` |
| [時刻](time/) | `get_time` |
| [音](sound/) | `sound_play` `sound_stop` `sound_playing` `note_freq` `beep` `sound_available` `music_play` `music_play_text` `music_stop` `music_playing` |
| [その他](misc/) | `log` `show_error` |
