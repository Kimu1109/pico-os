---
title: "その他"
weight: 90
description: "log / show_error / millis / micros / battery / on_key / app_dir / path_join / time / wifi_status / url・base64 / settings / memory_info / toast"
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

## pico.micros

<div class="sig">pico.micros() <span class="ret">-> us: integer</span></div>

起動からのマイクロ秒(32bitの値で、約71分ごとに0へ戻ります)。1フレームの中の処理の時間のような、ミリ秒では粗すぎる計測に使います。差は `(b - a) % 4294967296` で取ると、0へ戻った後でも正しく出ます。

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

## pico.app_dir

<div class="sig">pico.app_dir() <span class="ret">-> path: string</span></div>

このアプリのフォルダ(`main.lua` のあるフォルダ。末尾に `/` は付きません)。画像やデータのパスをハードコードせずに済みます。

## pico.path_join

<div class="sig">pico.path_join(a: string, b: string, ...) <span class="ret">-> path: string</span></div>

パスをつなぎます。`/` で始まる要素が来たらそこからやり直し、`.` と `..` は畳みます(SDのルートより上へは出ません)。

```lua
local img = pico.path_join(pico.app_dir(), "img", "title.pimg")
```

## pico.time

<div class="sig">pico.time() <span class="ret">-> epoch_seconds: integer | nil</span></div>

現在時刻のUNIX時間(秒)。NTPで時計が合っていなければ `nil`。年月日が欲しいときは `pico.get_time()`。

## pico.wifi_status

<div class="sig">pico.wifi_status() <span class="ret">-> { connected: boolean, status: string, enabled: boolean, ssid?: string }</span></div>

Wi-Fiの状態です。`status` は `"connected"` `"connecting"` `"off"` `"failed"` `"timeout"` `"not_found"` のどれか。`ssid` は接続中だけ入ります。

## pico.url_encode / pico.url_decode

<div class="sig">pico.url_encode(s: string) <span class="ret">-> string</span></div>
<div class="sig">pico.url_decode(s: string) <span class="ret">-> string</span></div>

パーセントエンコード(英数字と `-` `_` `.` `~` 以外を `%XX` に)。`url_decode` は `+` も空白に戻します。壊れた `%` はそのまま残します。

## pico.base64_encode / pico.base64_decode

<div class="sig">pico.base64_encode(data: string, url_safe?: boolean) <span class="ret">-> string</span></div>
<div class="sig">pico.base64_decode(text: string) <span class="ret">-> data: string | nil</span></div>

base64です(`data` は12KiB、`text` は16KiBまで)。`url_safe = true` なら `-` `_` を使いパディング(`=`)を付けません。`base64_decode` は標準とURL安全の両方を受け付け、改行や空白は飛ばし、不正なら `nil`。バイナリも往復できます。

## pico.settings_get / settings_set / settings_all

<div class="sig">pico.settings_get(key: string, default?: any) <span class="ret">-> value: string | default</span></div>
<div class="sig">pico.settings_set(key: string, value: string | number | boolean) <span class="ret">-> ok: boolean</span></div>
<div class="sig">pico.settings_all() <span class="ret">-> table</span></div>

アプリ専用の設定です。アプリのフォルダの `settings.cfg`(`key=value` 形式)に読み書きします。パスを意識せずに「音量」「前回の選択」のような小さな値を持てます。値は**文字列**で返ります(`tonumber` や `== "true"` で解釈してください)。キーに `=` や改行は使えません。値に改行を含む場合は `false`。まとまった構造は [`store_save`](../scenes/) が向いています。

## pico.memory_info

<div class="sig">pico.memory_info() <span class="ret">-> table</span></div>

`{ lua_used, lua_budget, lua_free, heap_free, heap_used, heap_headroom, image_bytes }`(バイト)。`lua_*` はこのアプリのLuaが使っている量と予算(既定200KB)、`heap_*` は本体のヒープ(`heap_free` は確保済みのヒープの中の空きの合計、`heap_headroom` はヒープの末尾とスタックの間のまだ使っていない広さ。PCでは0)、`image_bytes` は `image_load`/`image_create` で使っている量です。重いアプリの調整に使います。

## pico.toast

<div class="sig">pico.toast(text: string) <span class="ret">-> shown: boolean</span></div>

画面の上に短い通知(トースト)を出します。権限は要りません。通知センターの履歴にも残ります。連続して呼ぶと(300ms以内)断られて `false` を返します。アプリを閉じた後に出したい予約の通知は [`pico.notify`](../notify/) です。
