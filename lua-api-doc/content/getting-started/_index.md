---
title: "はじめに"
weight: 10
description: "最初のLuaアプリを書き、SDカードに置いてランチャから起動するまで"
---

## Luaアプリの仕組み

pico-os の Lua アプリは、1つの `.lua` ファイルが1つの画面(`LuaScene`)に対応します。`LuaScene` は内部で `LuaEngine`(1つの `lua_State` を持つ実行エンジン)を1つ生成し、スクリプトを最初から最後まで実行します。

スクリプトのトップレベルコードは、画面が開かれた瞬間(`onEnter()`)に1回だけ実行されます。ウィジェットの生成や `pico.on()` によるイベント登録は、通常このトップレベルで行います。

```lua
-- これはトップレベル。画面が開かれた瞬間に1回だけ実行される
local label = pico.create("Label")
pico.set(label, "text", "Hello, pico-os!")
```

## setup() / loop(dt)

Arduino風に、グローバル関数 `setup()` と `loop(dt)` を定義すると呼び出されます。どちらも定義は任意です。

| 関数 | 呼ばれるタイミング |
|---|---|
| `setup()` | トップレベルの実行が終わった直後に1回 |
| `loop(dt)` | 毎フレーム。`dt` は前回の呼び出しからの経過ミリ秒(整数) |

```lua
function setup()
    pico.log("起動しました")
end

function loop(dt)
    -- 毎フレーム呼ばれる。dtはミリ秒
end
```

`loop()` の実行中にエラーが起きると、以降そのアプリの `loop()` は呼ばれなくなります(毎フレーム同じエラーダイアログが積み上がるのを防ぐための安全弁)。`setup()` は1回きりなのでこの仕組みはありません。

## 画面を閉じる

ランチャへ戻るには `pico.pop()` を呼びます。C++製アプリの「戻る」ボタンと同じ仕組みで、ボタンの `press_start` イベントから呼ぶのが定番です。

```lua
local back = pico.create("Button")
pico.set(back, "text", "戻る")
pico.on(back, "press_start", function()
    pico.pop()
end)
```

`LuaScene` は `Pop()` で戻ってきたときに**スクリプトを最初から実行し直します**。Lua側の変数に持たせた状態(スコア・カウンタなど)は画面を出るたびにリセットされる点に注意してください。実行中の状態を残したい場合は `pico.sd_write()` でSDへ保存し、次回 `pico.sd_read()` で読み戻す設計にします。

## ステータスバーを避けて配置する

`pico.content_rect()` は、画面上部のステータスバーを除いた「アプリが自由に使える領域」を `x, y, w, h` の4値で返します。ウィジェットの初期配置はこれを基準にするのが基本です。

```lua
local x, y, w, h = pico.content_rect()
```

## SDカードへ配置してランチャに登録する

Luaアプリを置く場所は2通りあります。

### 方法A: `/lua/apps/<アプリ名>/main.lua`(推奨)

SDカードの `/lua/apps/` 直下にサブディレクトリを1つ作り、その中に `main.lua` を置くだけで、**起動時の自動走査(`LuaAppScanner`)によってランチャにタイルが追加されます**。ディレクトリ名がそのままアプリ名(タイル名)になります。

```
/lua/apps/マインスイーパー/main.lua
```

- C++側のコードを一切書かずに済みます。
- 自動登録されたアプリの権限は常に既定値(`network=false`, `sd_outside_app_dir=false`)です。ネットワークやアプリ専用ディレクトリの外へのSDアクセスが必要な場合は方法Bを使ってください。
- サブディレクトリ単位で登録されるのは、`main.lua` の親ディレクトリがそのままアプリの「持ち場」(`app_dir`。後述の権限モデル参照)になるためです。他のアプリのファイルを誤って読み書きできないよう、アプリごとにディレクトリを分けます。

### 方法B: C++側に手動登録する(強い権限が要る場合)

ネットワークやSD全域へのアクセスが必要なアプリは、`src/functions/App_List.cpp` に専用の生成関数を書いて登録します。

```cpp
Scene* MakeMyLuaAppScene(const AppEntry& entry) {
    LuaPermissions permissions;
    permissions.network = true;             // pico.http_request を使う場合
    permissions.sd_outside_app_dir = true;  // app_dir外のSDアクセスが要る場合
    return new LuaScene(entry.arg.c_str(), permissions);
}

// App_List.cpp::Setup() 内
Register("My App", IconID::AppBox, &MakeMyLuaAppScene, "/lua/my_app.lua");
```

権限の詳細は [権限モデル](../guide/permissions/) を参照してください。

## 実行時間の上限

`while true do end` のような終わらないループを書いてしまった場合に備え、Luaのバイトコード命令数に上限(暫定 200万命令/1回の呼び出し)を設けています。上限に達すると実行時エラーとして打ち切られ、エラーダイアログが表示されます。詳細は [実行時間の安全網](../guide/execution-limits/) を参照してください。

## 次に読むもの

- [ガイド](../guide/) — ウィジェット・描画・SD・通信・ダイアログの使い方を1つずつ解説
- [APIリファレンス](../api/) — `pico.*` 全関数のシグネチャ
- [サンプル](../examples/) — 実際に動作確認済みのコード一式
