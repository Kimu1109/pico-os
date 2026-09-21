---
title: "ダイアログ"
weight: 80
description: "メッセージ・入力・ファイル選択・色選択のモーダルダイアログ"
---

`WidgetFactory` 経由ではなく、専用の `pico.show_xxx()` で生成します(コンストラクタが必須引数を取るため)。いずれも生成した `WidgetId` を返し、閉じたときの結果は共通の `closed` イベント、または `pico.get()` で読みます。

## 一覧

| 関数 | ダイアログ | 結果の読み方 |
|---|---|---|
| `pico.show_message(text, cancel_text, ok_text)` | メッセージ+OK/キャンセル | `is_ok`(`closed`イベントの第2引数) |
| `pico.show_input(label, initial_text, is_single_line)` | ラベル+テキスト入力 | `pico.get(id, "text")` |
| `pico.show_file_save(start_dir)` | ファイル保存 | `pico.get(id, "path")` |
| `pico.show_file_select(start_dir)` | ファイル選択 | `pico.get(id, "path")`(未選択は`nil`) |
| `pico.show_color()` | 16色パレット選択 | `pico.get(id, "value")`(未選択は`-1`) |

## 基本パターン

```lua
local id = pico.show_message("保存しますか?", "キャンセル", "OK")
pico.on(id, "closed", function(dialog_id, is_ok)
    if is_ok then
        pico.log("OKが押された")
    end
end)
```

```lua
local id = pico.show_input("名前を入力", "デフォルト名", true)
pico.on(id, "closed", function(dialog_id, is_ok)
    if is_ok then
        local text = pico.get(dialog_id, "text")
        pico.log("入力: " .. text)
    end
end)
```

```lua
local id = pico.show_color()
pico.on(id, "closed", function(dialog_id, is_ok)
    local color = pico.get(dialog_id, "value") -- 未選択のままOKされた場合は -1
    if is_ok and color >= 0 then
        pico.set(some_widget, "background_color", color)
    end
end)
```

```lua
local id = pico.show_file_select("/lua/apps/myapp")
pico.on(id, "closed", function(dialog_id, is_ok)
    local path = pico.get(dialog_id, "path") -- 未選択なら nil
    if is_ok and path then
        local content = pico.sd_read(path)
    end
end)
```

## `pico.on()` を呼ばなくても勝手に片付く

ダイアログはモーダルなので、閉じたときに `WidgetFunctions::DestroyLater()` される保証は**ダイアログを生成した時点で既に配線済み**です。`pico.on(id, "closed", fn)` を呼ばなくても画面に残り続けることはありません。`pico.on()` はあくまで「閉じたことをLuaへ通知してほしい場合の上乗せ」の位置づけです。

## `show_input` の引数の既定値

```lua
pico.show_input(label, initial_text, is_single_line)
```

- `initial_text` を省略すると空文字列になります。
- `is_single_line` を省略すると**単一行**(`true`)になります。複数行にしたい場合は明示的に `false` を渡してください。

## `show_file_save` / `show_file_select` の開始ディレクトリ

`start_dir` を省略すると `"/"` から始まります。開いた後に選んだディレクトリを覚えておく機能はダイアログ側には無いので、必要ならアプリ側で最後に使ったパスをSDへ保存してください。
