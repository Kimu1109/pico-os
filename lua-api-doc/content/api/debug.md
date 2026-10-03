---
title: "デバッグ"
weight: 88
description: "traceback / breakpoint / set_breakpoint / clear_breakpoint / debugger_enabled"
---

Luaデバッガ(ブレークポイント・ステップ実行・変数の表示)とスタックトレースの関数です。使い方は [デバッガとサンドボックス](../../guide/debugging/) を参照してください。

デバッガは設定アプリの「その他」→「Luaデバッガ」(`/sys/debug.cfg` の `lua-debugger = true`)を入れたときだけ有効です。**有効にした後に開いたLuaアプリから**効きます。無効の間、`breakpoint` / `set_breakpoint` は何もせず `false` を返します。

## pico.traceback

<div class="sig">pico.traceback([message: string]) <span class="ret">-> string</span></div>

今のスタックトレースを文字列で返します。1行が1段で、`main.lua:12 update` の形(ファイル名:行 関数名)です。`message` を渡すと先頭の行に付けます。標準の `debug.traceback` の代わりです(`debug` ライブラリはサンドボックスで外してあります)。

```lua
local ok, err = pcall(do_something)
if not ok then pico.log(pico.traceback(err)) end
```

## pico.breakpoint

<div class="sig">pico.breakpoint([message: string]) <span class="ret">-> boolean</span></div>

デバッガが有効ならその場で止まり、デバッガの画面を出します(JavaScriptの `debugger;` と同じ)。続行すると `true` を返します。デバッガの画面で「停止」を押すとスクリプトはそこで打ち切られます(`pcall` でも捕まえられません)。無効なら止まらずに `false` を返します。

## pico.set_breakpoint

<div class="sig">pico.set_breakpoint(file: string, line: integer) <span class="ret">-> boolean</span></div>

行のブレークポイントを置きます。`file` はファイル名だけ(`"main.lua"`)でも、アプリのフォルダからのパス(`"テトリス/main.lua"`)でもかまいません。置けたら `true`(デバッガが無効・16個を超えた場合は `false`)。

## pico.clear_breakpoint

<div class="sig">pico.clear_breakpoint([file: string, line: integer]) <span class="ret">-> (なし)</span></div>

引数を渡せばそのブレークポイントを、省略すれば全部を外します。

## pico.debugger_enabled

<div class="sig">pico.debugger_enabled() <span class="ret">-> boolean</span></div>

このアプリでデバッガが有効か。
