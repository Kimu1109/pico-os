---
title: "その他"
weight: 90
description: "log / show_error / millis / battery / on_key"
---

## pico.log

<div class="sig">pico.log(message: string) <span class="ret">-> (なし)</span></div>

システムログへメッセージを出力します(アプリログ扱い)。デバッグ用途に使います。標準の `print()` も同じくログへ出ます(複数の引数はタブ区切り)。

## pico.show_error

<div class="sig">pico.show_error(message: string) <span class="ret">-> (なし)</span></div>

エラーログを出したうえで、メッセージダイアログをその場で表示します。「ユーザーへ見せるべき失敗」を通知する共通の窓口です。Luaスクリプト自体の構文エラー・実行時エラー(捕捉されなかったもの)も、内部的にはこれと同じ経路で表示されます。そのときはメッセージの下にスタックトレースの先頭(数段)も出ます。全部はログと `/crash/lua_NNNN.txt` に残ります。

## pico.millis

<div class="sig">pico.millis() <span class="ret">-> ms: integer</span></div>

起動からのミリ秒。単調に増え、NTPの同期で飛びません(`get_time` は壁時計で飛びます)。経過時間の計測に使います。

## pico.battery

<div class="sig">pico.battery() <span class="ret">-> percent: integer, volts: number, usb: boolean | nil</span></div>

バッテリーの残量(0〜100%)・電圧(V)・USBから給電されているかを返します。まだ読めていなければ `nil`。残量は電圧からの線形の近似(3.0V=0%〜4.2V=100%)で、約60秒ごとに更新されます。`usb` は充電中か充電完了かを区別できません。

## pico.on_key

<div class="sig">pico.on_key(fn: function | nil) <span class="ret">-> (なし)</span></div>

物理キーボード(PCのキーボードをUSBシリアル経由で使う入力など)の打鍵を受け取ります。`nil` で解除。

`fn(key, mods)`:

- `key`: 文字ならその文字(UTF-8。Shiftやキー配列は反映済み)、特殊キーなら `"enter"` `"backspace"` `"tab"` `"escape"` `"delete"` `"left"` `"right"` `"up"` `"down"` `"home"` `"end"` `"pageup"` `"pagedown"`。
- `mods`: `{ctrl = boolean, alt = boolean, shift = boolean}`。
- 戻り値が**真なら「取った」扱い**。偽/`nil` なら、開いているオンスクリーンキーボードの入力へ回ります(`pico.show_input` などで文字入力できる)。

```lua
pico.on_key(function(key, mods)
    if key == "left" then x = x - 1; return true end
    if key == "s" and mods.ctrl then save(); return true end
    return false
end)
```

`fn` がエラーになるとダイアログを1回出し、以降は呼ばれません。打鍵はタッチのイベントと違いコールバックで届くので、`loop(dt)` の中でポーリングする必要はありません。
