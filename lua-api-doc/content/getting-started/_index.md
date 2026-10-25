---
title: "はじめに"
weight: 10
description: "最初のLuaアプリを書き、SDカードに置いてランチャから起動するまで"
---

## アプリを置く場所

SDカードの `/lua/apps/` の下にフォルダを1つ作り、その中に `main.lua` を置きます。次に起動したとき、ランチャにアプリのタイルが並びます(アプリストアから入れたアプリは再起動しなくても並びます)。

```
/lua/apps/はじめてのアプリ/main.lua
```

- フォルダ名がアプリの名前になります(`app.cfg` の `name` で変えられます)。
- このフォルダがアプリの「持ち場」です。アプリは既定でこのフォルダの中だけを読み書きできます([権限](../guide/app/permissions/))。
- アイコンや権限などの設定は、同じフォルダの `app.cfg` に書きます([アプリのフォルダと app.cfg](app-folder/))。

## 最初のスクリプト

ボタンを押すとカウンタが増えるだけのアプリです。

```lua
local x, y, w, h = pico.content_rect()   -- ステータスバーを除いた、アプリが使える範囲

local count = 0
local label = pico.create("Label")
pico.set(label, "x", x + 10)
pico.set(label, "y", y + 10)
pico.set(label, "text", "count: 0")

local button = pico.create("Button")
pico.set(button, "x", x + 10)
pico.set(button, "y", y + 50)
pico.set(button, "w", 80)
pico.set(button, "h", 30)
pico.set(button, "text", "+1")
pico.on(button, "press_start", function()
    count = count + 1
    pico.set(label, "text", "count: " .. count)
end)
```

スクリプトは**アプリを開いた瞬間に上から1回だけ**実行されます。ウィジェットを作る(`pico.create`)・イベントを登録する(`pico.on`)のは、ふつうこのトップレベルで行います。ウィジェットの位置は `pico.content_rect()` を基準にします。

## setup() と loop(dt)

グローバル関数 `setup()` と `loop(dt)` を定義すると、OSが呼びます。どちらも書かなくてかまいません。

| 関数 | 呼ばれるとき |
|---|---|
| `setup()` | トップレベルの実行が終わった直後に1回 |
| `loop(dt)` | 毎フレーム。`dt` は前の呼び出しからの経過ミリ秒(整数) |

```lua
function setup()
    pico.log("起動しました")
end

function loop(dt)
    -- アニメーションや時間で進む処理を書く
end
```

`loop()` の中でエラーが起きると、それ以降そのアプリの `loop()` は呼ばれなくなります(同じエラーのダイアログが毎フレーム出続けないように)。一定時間ごとの処理は [タイマー](../api/system/timers/)(`pico.after` / `pico.every`)でも書けます。

## 画面を閉じる

ランチャへ戻るには `pico.pop()` を呼びます。「戻る」ボタンから呼ぶのが定番です。

```lua
local back = pico.create("Button")
pico.set(back, "text", "戻る")
pico.on(back, "press_start", function()
    pico.pop()
end)
```

Escキーやコントローラーの HOME でも戻れます(横取りしたいときは [`pico.on_back`](../api/ui/scenes/#picoon_back--picogo_back))。

**アプリを閉じると、Luaの変数は全部消えます。** 次に開いたときはスクリプトを最初から実行し直します。最高得点や設定のように残したい値は [`pico.store_save()`](../api/ui/scenes/#picostore_load--picostore_save) で保存してください。

## エラーとログ

- 捕まえていないエラーは、ファイル名・行番号とスタックトレースつきのダイアログで出ます。
- `print()` と `pico.log()` はシステムログへ出ます。
- 詳しい調べ方は [デバッガとサンドボックス](../guide/app/debugging/) にあります。

## 実行時間とメモリの上限

終わらないループ(`while true do end`)を書いてしまってもOSが固まらないよう、1回の呼び出しで実行できる命令数に上限があります。超えるとエラーとして打ち切られます。Luaが使えるメモリも1アプリ200KBまでです。詳しくは [実行時間とメモリの上限](../guide/app/execution-limits/) を見てください。

## PCで試す

リポジトリのPC版(`pc/`)でもアプリを動かせます。リポジトリの `pc/sdcard/` がSDカードの代わりなので、`pc/sdcard/lua/apps/<アプリ名>/main.lua` に置いてPC版を起動します(ビルドの仕方はリポジトリの `pc/README.md`)。ブラウザで動く [Playground](../../playground/) では、OSに入っているサンプルアプリを試せます。保存するたびに開き直すホットリロードと自動テストは [PCでの開発](../guide/app/testing/) にあります。

## 次に読むもの

- [アプリのフォルダと app.cfg](app-folder/) — アイコン・権限・ファイルの分け方
- [ガイド](../guide/) — ウィジェット・描画・保存・通信の使い方
- [APIリファレンス](../api/) — `pico.*` 全関数
- [サンプル](../examples/) — そのまま動くアプリ
