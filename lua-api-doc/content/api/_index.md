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
| [ウィジェット操作](widgets/) | `create` `destroy` `set` `get` `on` `off` `add_child` `remove_child` `text_set` `set_dots` `set_name` `find` `parent` `children` `get_rect` `bring_to_front` `send_to_back` `focus` `get_focus` `scroll_to` `show_keyboard` `hide_keyboard` `list_add` `list_insert` `list_remove` `list_get` `list_select` `list_scroll_to` `list_clear` `tab_add` `tab_label` `tab_set_label` `tab_remove` `tab_clear` `tab_link` `tab_unlink` |
| [直接描画](drawing/) | `draw_pixel` `draw_line` `draw_rect` `fill_rect` `draw_circle` `fill_circle` `clear_rect` `draw_text` `draw_text_wrapped` `measure_text` `get_pixel` `set_palette` `get_palette` `reset_palette` `invalidate` `mark_dirty` `set_draw_area` `clear_draw_area` `get_draw_area` |
| [タッチ](touch/) | `get_touch` |
| [コントローラー](pad/) | `pad_connected` `pad_down` `pad_pressed` `pad_released` |
| [画像](images/) | `image_load` `image_create` `image_target` `image_clear` `image_size` `draw_image` `draw_image_part` `draw_image_ex` `image_rotate` `draw_rotated` `image_free` |
| [ゲームエンジン](game/) | `require("pico.game")`(スプライト・タイルマップ・当たり判定・カメラ・入力・状態)、`draw_tilemap` |
| [2.5Dの箱庭](iso/) | `iso.create` `iso.open` `iso.info` `iso.save` `iso.close` `iso.migrate` `iso.get` `iso.set` `iso.first_air` `iso.size` `iso.pump` `iso.pending` `iso.stats` `iso.set_image` `iso.view` `iso.origin` `iso.cursor` `iso.sky` `iso.render` `iso.draw_icon` `iso.block_pos` `iso.pick` `iso.dirty_block` `iso.dirty_edit` `iso.culling` |
| [ラスタキャンバス](canvas/) | `canvas_clear` `canvas_save` `canvas_load` `canvas_undo` `canvas_get_pixel` |
| [SDカード](sdcard/) | `sd_exists` `sd_read` `sd_write` `sd_remove` `sd_mkdir` `sd_list` |
| [シーン制御](scenes/) | `pop` `push_scene` `change_scene` `launch_app` `content_rect` `args` `store_load` `store_save` `on_back` `go_back`(+ `on_suspend` `on_resume` `on_result`) |
| [ダイアログ](dialogs/) | `show_message` `show_input` `show_file_save` `show_file_select` `show_color` `show_choice` `show_date` `show_time` `show_number` `show_progress` |
| [ネットワーク](network/) | `http_request` `http_cancel` |
| [JSON](json/) | `json_decode` `json_encode` `json_null` |
| [タイマー](timers/) | `after` `every` `cancel` |
| [モジュール](modules/) | `require` `pico.require` |
| [同梱モジュール](stdlib/) | `pico.ui` `pico.async` `pico.tween`(`pico.game` は[ゲームエンジン](game/)) |
| [暗号化](crypto/) | `encrypt` `decrypt` `is_encrypted` `hash` `random_bytes`(+ `store_save`/`store_load` の暗号化) |
| [時刻](time/) | `get_time` |
| [通知](notify/) | `notify` `notify_cancel` `notify_list` `launch_reason` |
| [音](sound/) | `sound_play` `sound_stop` `sound_playing` `note_freq` `beep` `sound_available` `music_play` `music_play_text` `music_stop` `music_playing` `wav_play` `wav_stop` `wav_playing` |
| [デバッグ](debug/) | `traceback` `breakpoint` `set_breakpoint` `clear_breakpoint` `debugger_enabled` |
| [その他](misc/) | `log` `show_error` `millis` `battery` `on_key` `app_dir` `path_join` `time` `wifi_status` `url_encode` `url_decode` `base64_encode` `base64_decode` `settings_get` `settings_set` `settings_all` `memory_info` `toast` |
