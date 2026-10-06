---
title: "同梱モジュール"
weight: 80
description: "require(\"pico.ui\") / require(\"pico.async\") / require(\"pico.tween\")"
---

OSに入っているLuaモジュールです(2Dゲーム向けの `pico.game` は[ゲームエンジン](../game/)のページ)。`require("pico.ui")` のように読みます(アプリのフォルダにあるファイルより優先され、名前は `pico.` で始まります)。スクリプトの先頭で `require("pico.ui")` と**文字列のまま**書いてください(実行中のコンパイルを避けるため、`require` の引数に組み立てた名前は使えません)。

## pico.ui — 宣言的なUIの組み立て

<div class="sig">local ui = require("pico.ui")</div>
<div class="sig">ui(spec: table) <span class="ret">-> id: integer, named: table</span></div>
<div class="sig">ui.種類名(spec: table) <span class="ret">-> id: integer, named: table</span></div>

`pico.create` → `pico.set` × N → `pico.on` → `pico.add_child` の定型を、1つの表で書けます。

- `spec[1]`(または `spec.type`): ウィジェットの種別名(`"Button"` など)
- `on_イベント名 = fn`: `pico.on` を呼びます(`on_press_end` → `"press_end"`)
- `name = "..."`: `pico.set_name` を呼び、戻り値の `named` にも入ります
- `items = {...}`: リストの項目(文字列、または `{"文字", color = 12}`)
- `tabs = {...}`: タブのラベル
- `children = {...}`: 子の仕様。作って `add_child` します
- それ以外のキー: `pico.set(id, キー, 値)`(`min_value` `max_value` を先に、`value` `selected_index` `tab_selected` `checked` を最後に設定します)

```lua
local ui = require("pico.ui")
local root, w = ui{ "LayoutContainer", x = 10, y = 30, w = 220, h = 200, children = {
    { "Label", text = "名前", name = "title" },
    { "Button", text = "保存", name = "save",
      on_press_end = function() pico.toast("保存") end },
    { "NumberSlider", min_value = 0, max_value = 10, value = 4, name = "level" },
    { "ScrollList", items = {"りんご", "みかん"}, selected_index = 0, w = 200, h = 80 },
} }
pico.set(w.title, "text", "こんにちは")
```

## pico.async — 待つ処理を直列に書く

<div class="sig">local async = require("pico.async")</div>

コルーチンで、ダイアログ・通信・時間待ちを上から順に書けます。**`async.run` の中でだけ**使えます(メインの流れで呼ぶとエラー)。

| 関数 | 戻り値 |
|---|---|
| `async.run(fn, ...)` | コルーチン。`fn` の中のエラーはダイアログに出ます |
| `async.sleep(ms)` | (なし)`ms` ミリ秒待つ |
| `async.await(starter)` | `starter(resume)` を呼び、`resume(...)` に渡された値を返す。独自の「待つ」を作るための基本部品 |
| `async.http(method, url, body, content_type, opts)` | `ok, status, body, err, headers, info`(`pico.http_request` のコールバックの引数と同じ) |
| `async.message(text, cancel_text, ok_text)` | `true`(決定側)/ `false` |
| `async.input(label, initial, single_line)` | 入力した文字列 / `nil`(キャンセル) |
| `async.choice(title, items, cancel_text)` | 選んだ番号(0始まり)/ `nil` |
| `async.date(title, y, m, d)` | `"YYYY-MM-DD"` / `nil` |
| `async.time(title, h, m, s)` | `"HH:MM:SS"` / `nil` |
| `async.number(title, initial)` | 数値 / `nil` |
| `async.file_select(dir)` / `async.file_save(dir, name)` | パス / `nil` |
| `async.color()` | 色の番号 / `nil` |

```lua
local async = require("pico.async")
async.run(function()
    if not async.message("取得しますか?", "いいえ", "はい") then return end
    local ok, status, body = async.http("GET", "https://example.com/data.json")
    if not ok or status ~= 200 then pico.toast("失敗") return end
    local data = pico.json_decode(body)
    local day = async.date("いつ?", 2026, 10, 5)
    if day then pico.toast(day .. " に保存") end
end)
```

別の画面へ `push_scene` するとスクリプトごと作り直されるので、待っている処理は消えます。

## pico.tween — 値を時間かけて変える

<div class="sig">local tween = require("pico.tween")</div>
<div class="sig">tween.start(opts) <span class="ret">-> handle</span></div>
<div class="sig">tween.cancel(handle)</div>

`opts`: `duration`(ms、既定300)、`from`(既定0)、`to`(既定1)、`ease`(`"linear"` `"ease_in"` `"ease_out"` `"ease_in_out"` か関数。既定 `"linear"`)、`interval`(更新の間隔ms、既定40・最小10)、`on_update(value)`、`on_done()`。開始の瞬間に `on_update(from)` が、最後に `on_update(to)` と `on_done()` が呼ばれます。内部は `pico.every` なので、タイマーの枠(16個)を1つ使います。

```lua
tween.start{ from = 0, to = 100, duration = 600, ease = "ease_out",
             on_update = function(v) pico.set(bar, "value", v) end }
```
