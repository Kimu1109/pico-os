---
title: "ダイアログ"
weight: 60
description: "show_message / show_input / show_file_save / show_file_select / show_color"
---

いずれも生成した `WidgetId` を返します。閉じたときの通知は `pico.on(id, "closed", function(id, is_ok) ... end)` で受けます([イベント](../../guide/events/) 参照)。

## pico.show_message

<div class="sig">pico.show_message(text: string, cancel_text: string, ok_text: string) <span class="ret">-> id: integer</span></div>

メッセージ+キャンセル/OKボタンのダイアログ。3引数はすべて必須です。

## pico.show_input

<div class="sig">pico.show_input(label: string, initial_text?: string, is_single_line?: boolean) <span class="ret">-> id: integer</span></div>

- `initial_text` の既定値は空文字列。
- `is_single_line` の既定値は `true`(単一行)。複数行にするには明示的に `false` を渡す。
- 結果の入力文字列は `pico.get(id, "text")` で読む。

## pico.show_file_save

<div class="sig">pico.show_file_save(start_dir?: string) <span class="ret">-> id: integer</span></div>

- `start_dir` の既定値は `"/"`。
- 選択された保存先パスは `pico.get(id, "path")` で読む。

## pico.show_file_select

<div class="sig">pico.show_file_select(start_dir?: string) <span class="ret">-> id: integer</span></div>

- `start_dir` の既定値は `"/"`。
- 選択されたパスは `pico.get(id, "path")` で読む(未選択のままOKされた場合は `nil`)。

## pico.show_color

<div class="sig">pico.show_color() <span class="ret">-> id: integer</span></div>

16色パレット(4×4グリッド)から選ばせるダイアログ。選択色は `pico.get(id, "value")` で読む(未選択は `-1`)。
