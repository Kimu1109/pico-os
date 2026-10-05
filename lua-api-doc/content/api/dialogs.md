---
title: "ダイアログ"
weight: 60
description: "show_message / show_input / show_file_save / show_file_select / show_color"
---

いずれも生成した `WidgetId` を返します。閉じたときの通知は `pico.on(id, "closed", function(id, is_ok) ... end)` で受けます([イベント](../../guide/events/) 参照)。

## pico.show_message

<div class="sig">pico.show_message(text: string, cancel_text: string, ok_text: string) <span class="ret">-> id: integer</span></div>

メッセージ+キャンセル/OKボタンのダイアログ。3引数はすべて必須です。

- 本文が収まらないときは、小さい文字(16px)→ダイアログを大きく(最大で画面いっぱい)→枠の中でスクロール(スクロールバーをドラッグ)の順で収める。本文は512バイトまで。
- ボタンの文字を空文字列 `""` にすると、そのボタンは出さずに詰める(例: `pico.show_message("保存しました", "", "OK")` でOKだけ)。両方とも空のときだけ「OK」を出す(閉じられなくならないように)。

## pico.show_input

<div class="sig">pico.show_input(label: string, initial_text?: string, is_single_line?: boolean, submit_text?: string, cancel_text?: string) <span class="ret">-> id: integer</span></div>

- `initial_text` の既定値は空文字列。
- `submit_text` / `cancel_text` はボタンの文字(既定は「決定」「キャンセル」)。空文字列 `""` にするとそのボタンは出さずに詰める(両方とも空のときだけ「決定」を出す)。
- `label` が長いときは `show_message` と同じく小さい文字→ダイアログを大きく→スクロールになる。
- `is_single_line` の既定値は `true`(単一行)。複数行にするには明示的に `false` を渡す。
- 結果の入力文字列は `pico.get(id, "text")` で読む。

## pico.show_file_save

<div class="sig">pico.show_file_save(start_dir?: string, default_name?: string) <span class="ret">-> id: integer</span></div>

- `start_dir` の既定値は `"/"`。
- `default_name` を渡すとファイル名欄の初期値になる(開いているファイルへ上書き保存する流れ等に)。
- 選択された保存先パスは `pico.get(id, "path")` で読む。

## pico.show_file_select

<div class="sig">pico.show_file_select(start_dir?: string) <span class="ret">-> id: integer</span></div>

- `start_dir` の既定値は `"/"`。
- 選択されたパスは `pico.get(id, "path")` で読む(未選択のままOKされた場合は `nil`)。

## pico.show_color

<div class="sig">pico.show_color() <span class="ret">-> id: integer</span></div>

16色パレット(4×4グリッド)から選ばせるダイアログ。選択色は `pico.get(id, "value")` で読む(未選択は `-1`)。

## closed の3番目の引数(結果)

`pico.on(id, "closed", fn)` の `fn(id, is_ok, value)` は、**決定で閉じたとき**に3番目の引数 `value` として結果を受け取れます。閉じた後はダイアログが消えるので、`pico.get` で読む順序を気にしなくて済みます。

| ダイアログ | `value` |
|---|---|
| `show_input` | 入力した文字列 |
| `show_file_save` / `show_file_select` | 選んだパス |
| `show_color` | 選んだ色の番号 |
| `show_choice` | 選んだ項目の番号(0始まり) |
| `show_date` | `"YYYY-MM-DD"` |
| `show_time` | `"HH:MM:SS"` |
| `show_number` | 入力した数字の文字列(`tonumber` で数値に) |

キャンセルで閉じたときは `value` はありません。

## pico.show_choice

<div class="sig">pico.show_choice(title: string, items: table, cancel_text?: string) <span class="ret">-> id: integer</span></div>

選択肢の一覧(アクションシート)です。`items` は文字列の配列(64個まで)。**1回タップで選んで閉じ**、`closed(id, true, 番号)` が呼ばれます。`cancel_text` の既定は「キャンセル」で、`""` にするとボタンを出しません(一覧の外をタップしても閉じないので、出さないときは必ずどれかを選ばせることになります)。

## pico.show_date

<div class="sig">pico.show_date(title: string, year: integer, month: integer, day: integer, ok_text?: string, cancel_text?: string) <span class="ret">-> id: integer</span></div>

日付の選択です。`[<]` `[>]` で月を送り、格子の日付をタップして、決定します。結果は `"2026-03-14"` の形の文字列です(月を変えて日がその月に無ければ、月の最後の日に収まります)。

## pico.show_time

<div class="sig">pico.show_time(title: string, hour: integer, minute: integer, second?: integer, ok_text?: string, cancel_text?: string) <span class="ret">-> id: integer</span></div>

時刻(時:分:秒)の選択です。▲▼で変えます(長押しで連続)。結果は `"07:30:15"` の形です。

## pico.show_number

<div class="sig">pico.show_number(title: string, initial?: number | string, ok_text?: string, cancel_text?: string) <span class="ret">-> id: integer</span></div>

数字専用のキーボードで数を入れさせます。結果は文字列なので、`tonumber(value)` で数値にします(空のままなら `nil`)。

## pico.show_progress

<div class="sig">pico.show_progress(message: string, cancel_text?: string) <span class="ret">-> id: integer</span></div>

進捗バー付きのダイアログです。自動では閉じません。

- 進め方: `pico.set(id, "value", 0〜100)`
- メッセージの差し替え: `pico.set(id, "text", "...")`
- 終わったら `pico.destroy(id)` で閉じます。
- `cancel_text` を渡すとキャンセルボタンが出て、押すと `closed(id, false)` が呼ばれます(処理を止めるのはスクリプトの仕事です)。

```lua
local dlg = pico.show_progress("ダウンロード中...", "中止")
pico.on(dlg, "closed", function() pico.http_cancel() end)
-- 進み具合が分かるたびに
pico.set(dlg, "value", percent)
-- 終わったら
pico.destroy(dlg)
```
