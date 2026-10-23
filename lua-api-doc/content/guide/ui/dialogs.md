---
aliases: ["/guide/dialogs/"]
title: "ダイアログ"
weight: 60
description: "メッセージ・入力・選択肢・日付・ファイル選択などのダイアログ"
---

ダイアログは `pico.create` ではなく、専用の `pico.show_*()` で開きます。どれも開いたダイアログのIDを返し、閉じたときに `closed` イベントが届きます。

## 一覧

| 関数 | ダイアログ | 決定で閉じたときの結果(`closed` の3番目の引数) |
|---|---|---|
| `pico.show_message(text, cancel_text, ok_text)` | メッセージとボタン | (なし。`is_ok` で判断) |
| `pico.show_input(label [, initial, single_line])` | 文字の入力 | 入力した文字列 |
| `pico.show_number(title [, initial])` | 数字の入力 | 入力した数字の文字列 |
| `pico.show_choice(title, items [, cancel_text])` | 選択肢の一覧 | 選んだ番号(0始まり) |
| `pico.show_date(title, y, m, d)` | 日付 | `"YYYY-MM-DD"` |
| `pico.show_time(title, h, m [, s])` | 時刻 | `"HH:MM:SS"` |
| `pico.show_color()` | 16色から選ぶ | 色の番号 |
| `pico.show_file_select([start_dir])` | ファイルを選ぶ | パス |
| `pico.show_file_save([start_dir, default_name])` | 保存先を選ぶ | パス |
| `pico.show_progress(message [, cancel_text])` | 進み具合 | (自分で閉じる) |

引数の細かい意味は [ダイアログAPI](../../../api/ui/dialogs/) にあります。

## 基本の書き方

```lua
local id = pico.show_message("保存しますか?", "キャンセル", "OK")
pico.on(id, "closed", function(id, is_ok)
    if is_ok then save() end
end)
```

```lua
local id = pico.show_input("名前を入力", "ななし")
pico.on(id, "closed", function(id, is_ok, text)
    if is_ok then pico.log("入力: " .. text) end
end)
```

```lua
local id = pico.show_file_select(pico.app_dir())
pico.on(id, "closed", function(id, is_ok, path)
    if is_ok and path then
        local content = pico.sd_read(path)
    end
end)
```

- ダイアログは閉じると自動で片付きます。結果が要らなければ `closed` を登録しなくてもかまいません。
- `show_input` の3番目の引数を省くと1行の入力になります。複数行にするなら `false` を渡します。
- `show_file_select` / `show_file_save` の `start_dir` を省くと `/` から始まります。前に開いた場所は覚えないので、必要ならアプリで覚えておいてください。

## 上から順に書く(pico.async)

ダイアログを続けて出す処理は、コールバックが入れ子になりがちです。同梱の [`pico.async`](../../../api/system/stdlib/#picoasync--待つ処理を直列に書く) を使うと、結果を待ちながら上から順に書けます。

```lua
local async = require("pico.async")
async.run(function()
    local name = async.input("名前は?")
    if not name then return end
    if async.message(name .. " で保存しますか?", "いいえ", "はい") then save(name) end
end)
```

## ダイアログを続けて開くとき

`closed` の中ですぐ次のダイアログを開くと、閉じたほうの跡が画面に残ることがあります。続けて開くときは、`pico.after(50, ...)` などで少し待ってから開いてください(`pico.async` なら間に `async.sleep(50)` を入れます)。
