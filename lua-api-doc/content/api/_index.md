---
title: "APIリファレンス"
weight: 30
description: "pico.* 全関数の引数・戻り値・失敗したときの動き。使い方の解説はガイドを参照してください。"
---

`pico` テーブルの関数を、機能ごとに分けて載せています。使い方の流れは [ガイド](../guide/) を、ウィジェットのプロパティやイベントの一覧は [リファレンス](../reference/) を見てください。

## 表記

- `pico.関数名(引数, ...) -> 戻り値` の形で示します。`[引数]` と `引数?` は省略できる引数です。
- 戻り値の無い関数は `-> (なし)` と書きます。
- WidgetId・画像のハンドル・タイマーのハンドルは、どれもLuaでは整数です。
- 「エラーになる」は Lua のエラー(`pcall` で捕まえられる)を、「失敗値を返す」は `false` / `nil` などを返すことを指します。引数の書き間違いはエラー、SDが無い・権限が無いといった実行時の状態は失敗値、が基本です。

## 全関数一覧

| ページ | 関数 |
|---|---|
| [ウィジェット操作](ui/widgets/) | `create` `destroy` `set` `get` `on` `off` `add_child` `remove_child` `text_set` `set_dots` `set_name` `find` `parent` `children` `get_rect` `bring_to_front` `send_to_back` `focus` `get_focus` `scroll_to` `show_keyboard` `hide_keyboard` `list_add` `list_insert` `list_remove` `list_get` `list_select` `list_scroll_to` `list_clear` `tab_add` `tab_label` `tab_set_label` `tab_remove` `tab_clear` `tab_link` `tab_unlink` |
| [ダイアログ](ui/dialogs/) | `show_message` `show_input` `show_number` `show_choice` `show_date` `show_time` `show_color` `show_file_select` `show_file_save` `show_progress` |
| [画面遷移と保存](ui/scenes/) | `pop` `push_scene` `change_scene` `launch_app` `content_rect` `args` `store_load` `store_save` `on_back` `go_back`(+ `on_suspend` `on_resume` `on_result`) |
| [タッチ](ui/touch/) | `get_touch` |
| [コントローラー](ui/pad/) | `pad_connected` `pad_down` `pad_pressed` `pad_released` |
| [直接描画](graphics/drawing/) | `draw_pixel` `draw_line` `draw_rect` `fill_rect` `draw_circle` `fill_circle` `draw_ellipse` `fill_ellipse` `draw_triangle` `fill_triangle` `draw_polygon` `fill_polygon` `draw_arc` `fill_arc` `clear_rect` `draw_text` `draw_text_wrapped` `measure_text` `text_width` `get_pixel` `set_palette` `get_palette` `reset_palette` `invalidate` `mark_dirty` `set_draw_area` `clear_draw_area` `get_draw_area` |
| [画像](graphics/images/) | `image_load` `image_create` `image_target` `image_clear` `image_size` `draw_image` `draw_image_part` `draw_image_ex` `image_rotate` `draw_rotated` `image_free` |
| [ラスタキャンバス](graphics/canvas/) | `canvas_clear` `canvas_save` `canvas_load` `canvas_undo` `canvas_get_pixel` |
| [ゲームエンジン](game/engine/) | `require("pico.game")`(スプライト・タイルマップ・当たり判定・物理・カメラ・入力・状態)、`draw_tilemap` |
| [2.5Dの箱庭](game/iso/) | `pico.iso.*`(ワールド・表示・人や物・経路探索・タワーディフェンス向けの道具) |
| [SDカード](data/sdcard/) | `sd_exists` `sd_read` `sd_read_part` `sd_write` `sd_remove` `sd_mkdir` `sd_list` `sd_stat` `config_read` `config_get` `config_write` |
| [JSON](data/json/) | `json_decode` `json_encode` `json_null` `json_pick` `sd_json_pick` |
| [XML](data/xml/) | `xml_pick` `sd_xml_pick` |
| [ネットワーク](data/network/) | `http_request` `http_cancel` `http_close` `form_encode` |
| [暗号化](data/crypto/) | `encrypt` `decrypt` `is_encrypted` `hash` `random_bytes`(+ `store_save` / `store_load` の暗号化) |
| [音](media/sound/) | `sound_play` `sound_stop` `sound_playing` `note_freq` `beep` `sound_available` `sound_rate` `sound_set_rate` `music_play` `music_play_text` `music_stop` `music_playing` `wav_play` `wav_stop` `wav_playing` `wav_pause` `wav_paused` `wav_position` `wav_duration` `wav_seek` |
| [マイク](media/mic/) | `mic_start` `mic_stop` `mic_recording` `mic_info` `mic_monitor` `mic_level` `mic_has_signal` `mic_set_gain` `mic_gain` `mic_set_rate` `mic_rate` |
| [時間とタイマー](system/timers/) | `after` `every` `cancel` `get_time` `time` `millis` `micros` |
| [通知](system/notify/) | `notify` `notify_cancel` `notify_list` `launch_reason` |
| [バックグラウンド](system/background/) | `is_background` `finish`(+ `app.cfg` の `background`) |
| [モジュール](system/modules/) | `require` `pico.require` |
| [同梱モジュール](system/stdlib/) | `pico.ui` `pico.async` `pico.tween` |
| [デバッグ](system/debug/) | `traceback` `breakpoint` `set_breakpoint` `clear_breakpoint` `debugger_enabled` |
| [その他](system/misc/) | `log` `show_error` `toast` `on_key` `app_dir` `path_join` `settings_get` `settings_set` `settings_all` `clipboard_get` `clipboard_set` `battery` `wifi_status` `memory_info` `keep_awake` `theme` `url_encode` `url_decode` `base64_encode` `base64_decode` |
| [定数](../reference/constants/) | `COLOR_*` `FONT_*` `ICON_*` `TEXT_ALIGN_*` `CANVAS_*` `SCREEN_*` など |
