---
aliases: ["/api/misc/"]
title: "その他"
weight: 70
description: "log / show_error / toast / on_key / app_dir / path_join / settings / clipboard / battery / wifi_status / memory_info / keep_awake / theme / url・base64"
---

## pico.log

<div class="sig">pico.log(message: string) <span class="ret">-> (なし)</span></div>

システムログへメッセージを出力します(アプリログ扱い)。デバッグ用途に使います。標準の `print()` も同じくログへ出ます(複数の引数はタブ区切り)。

## pico.show_error

<div class="sig">pico.show_error(message: string) <span class="ret">-> (なし)</span></div>

エラーをログに出し、メッセージのダイアログを出します。利用者に見せたい失敗を知らせるのに使います。捕まえていないLuaのエラーも同じダイアログで出ます(そのときはスタックトレースの先頭も付き、全部はログと `/crash/lua_NNNN.txt` に残ります)。

## pico.battery

<div class="sig">pico.battery() <span class="ret">-> percent: integer, volts: number, usb: boolean | nil</span></div>

バッテリーの残量(0〜100%)・電圧(V)・USBから給電されているかを返します。まだ読めていなければ `nil`。残量は電圧からの線形の近似(3.0V=0%〜4.2V=100%)で、約1秒ごとに更新されます。`usb` は充電中か充電完了かを区別できません。

## pico.on_key

<div class="sig">pico.on_key(fn: function | nil) <span class="ret">-> (なし)</span></div>

物理キーボード(CardKB、USBシリアル経由のPCのキーボードなど)の打鍵を受け取ります。`nil` で解除。

`fn(key, mods)`:

- `key`: 文字ならその文字(UTF-8。Shiftやキー配列は反映済み)、特殊キーなら `"enter"` `"backspace"` `"tab"` `"escape"` `"delete"` `"left"` `"right"` `"up"` `"down"` `"home"` `"end"` `"pageup"` `"pagedown"`、メディアキーなら `"playpause"` `"stop"` `"nexttrack"` `"prevtrack"` `"volumeup"` `"volumedown"` `"mute"`(メディアキーはオンスクリーンキーボードの入力には使われません)。
- `mods`: `{ctrl = boolean, alt = boolean, shift = boolean}`。
- 戻り値が**真なら「取った」扱い**。偽/`nil` なら、開いているオンスクリーンキーボードの入力へ回ります(`pico.show_input` などで文字入力できる)。
- ダイアログ(`pico.show_msg` `pico.show_input` 等)を開いている間は、メディアキー以外は `fn` へ届かずダイアログが受け取ります(Escでキャンセル・Enterで決定・入力欄へ文字)。

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

## pico.wifi_status

<div class="sig">pico.wifi_status() <span class="ret">-> { connected: boolean, status: string, enabled: boolean, ssid?: string, internet: string, hostname: string }</span></div>

Wi-Fiの状態です。`status` は `"connected"` `"connecting"` `"off"` `"failed"` `"timeout"` `"not_found"` `"setup_ap"`(設定アプリの[スマホ]で本体がアクセスポイントになっている間)のどれか。`ssid` は接続中だけ入ります。

`internet` はインターネットへ出られるかで、`"online"` `"portal"`(ホテル等のログイン/同意のページを通すまで出られない。設定アプリのWi-Fiタブの[ログイン]で通せます) `"offline"`(Wi-Fiには繋がったが外へ出られない) `"checking"` `"unknown"`(未接続・判定前・判定を止めている)のどれか。`connected` が真でも `"portal"` の間は通信が失敗します。`hostname` は本体の名前(同じLANから `<hostname>.local` で見つかる)。

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

アプリ専用の設定です。アプリのフォルダの `settings.cfg`(`key=value` 形式)に読み書きします。パスを意識せずに「音量」「前回の選択」のような小さな値を持てます。値は**文字列**で返ります(`tonumber` や `== "true"` で解釈してください)。キーに `=` や改行は使えません。値に改行を含む場合は `false`。まとまった構造は [`store_save`](../../ui/scenes/) が向いています。

## pico.memory_info

<div class="sig">pico.memory_info() <span class="ret">-> table</span></div>

`{ lua_used, lua_budget, lua_free, heap_free, heap_used, heap_headroom, image_bytes }`(バイト)。`lua_*` はこのアプリのLuaが使っている量と上限(200KB)、`heap_*` は本体全体のメモリ、`image_bytes` は `image_load` / `image_create` で使っている量です。重いアプリの調整に使います。

## pico.toast

<div class="sig">pico.toast(text: string) <span class="ret">-> shown: boolean</span></div>

画面の上に短い通知(トースト)を出します。権限は要りません。通知センターの履歴にも残ります。連続して呼ぶと(300ms以内)断られて `false` を返します。アプリを閉じた後に出したい予約の通知は [`pico.notify`](../notify/) です。

## pico.clipboard_get / pico.clipboard_set

<div class="sig">pico.clipboard_get() <span class="ret">-> string</span></div>
<div class="sig">pico.clipboard_set(text: string) <span class="ret">-> ok: boolean</span></div>

OS全体で1つのクリップボード(テキストだけ)です。テキストエディタのコピー/切り取り/貼り付けや、入力欄での物理キーボードの Ctrl+C / Ctrl+X / Ctrl+V と同じものを読み書きします。権限は要りません。

- `clipboard_get()` は今の中身を返します(空なら `""`)
- `clipboard_set(text)` は中身を差し替えます。`""` で空にします。`\r` とNULは捨てます(改行は `\n` だけになります)
- 中身は**1KiBまで**です。超えた分は文字の途中で切らずに切り捨てて `false` を返します
- 再起動で消えます(SDカードには保存しません)

```lua
pico.clipboard_set("コピーした文字")
pico.log(pico.clipboard_get())
```

## pico.keep_awake

<div class="sig">pico.keep_awake()</div>

このフレームは本体をスリープ(省電力。操作が無いと画面が消える)に入らせません。スリープ中に呼ぶと起きます。ゲームの進行中のように、**操作が無くても画面が動き続ける間だけ毎フレーム**(`loop` の中で)呼んでください。呼ぶのをやめれば、その後は普通にスリープします(呼び忘れても「スリープに入る」だけです)。権限は要りません。

```lua
function loop(dt)
    if playing then pico.keep_awake() end
end
```

## pico.theme

<div class="sig">pico.theme() <span class="ret">-> "light" | "dark"</span></div>

OSの配色を返します。設定アプリの「画面」→「ダークテーマ」を入れると `"dark"` です。
ダークテーマでは**パレットの番号の色が明暗で入れ替わります**(0番=文字の色が明るく、15番=背景の色が暗くなる。
紺・深緑のような濃い色は明るく、薄い灰・水色は暗く)。番号で色を指定している部品・図形・画像は何もしなくても
ダークテーマの色で出ます。絵の色を変えたくないゲームは `pico.set_palette` で自分の色を入れてください
(アプリを閉じるとテーマの色へ戻ります)。
