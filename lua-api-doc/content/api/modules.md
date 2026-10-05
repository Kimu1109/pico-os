---
title: "モジュール (require)"
weight: 79
description: "require / pico.require"
---

スクリプト本体(`main.lua`)は16KiBまでです。大きなアプリは機能ごとにファイルを分けて `require` で読み込めます。標準Luaの `require`/`package` とは別の、アプリのフォルダだけを見る仕組みです。

## require

<div class="sig">require(name: string) <span class="ret">-> value</span></div>

アプリのフォルダ(`main.lua` のあるフォルダ)から次の順で探して読み込みます。`pico.require` も同じ関数です。

1. `<名前>.lua`
2. `<名前>/init.lua`

名前の `.` はフォルダの区切りです(`require("ui.dialogs")` → `ui/dialogs.lua`)。

```lua
-- main.lua
local util = require("util")      -- util.lua
local menu = require("ui.menu")   -- ui/menu.lua
print(util.double(21))

-- util.lua
local M = {}
function M.double(x) return x * 2 end
return M
```

- 1つのモジュールは**一度だけ実行**され、結果(`return` した値。無ければ `true`)が覚えられます。2回目以降の `require` は同じ値を返します。
- モジュールの中からさらに `require` できます。**循環**(AがBを、BがAを読む)はエラーです。
- 1つのファイルは**32KiBまで**です(`main.lua` は16KiB)。
- 名前に使えるのは英数字・`_`・`-` と区切りの `.` だけです。`..` や `/` は使えません。アプリのフォルダの外(`sd_outside_app_dir` 権限があっても)は探しません。
- 構文エラーや実行時エラーは、モジュールのファイル名と行つきでそのまま `require` の呼び出し元へ伝わります(`pcall(require, ...)` で受けられます)。

## 先読みについて(重要)

端末のスタックの都合で、Luaを実行している最中にファイルをコンパイルするのは避けたい処理です。そこで、**スクリプトを実行する前に**、ソースの中の `require("名前")`(`require "名前"` / `require('名前')` を含む)を文字列として拾って読み込んでおきます。拾ったモジュールの中の `require` も同様に辿ります(最大16モジュール)。実行時の `require` はその読み込み済みのものを呼ぶだけです。

つまり**名前は文字列リテラルで書くのが基本**です。

```lua
local util = require("util")        -- OK: 先読みされる
local name = "util"
local util2 = require(name)         -- 先読みされない
```

組み立てた名前(変数)も、スクリプトの**トップレベル近く**(関数の呼び出しが深くない所)で呼ぶ分には、その場で読み込めます。関数の奥深くから、先読みされていない名前を読もうとするとエラーになります。モジュールは使う前にスクリプトの先頭で読み込んでおいてください。

## 従来の lib.lua との関係

同じフォルダに `lib.lua` があると本体より先に実行される従来の仕組みは、そのまま使えます。新しく作るアプリには `require` をおすすめします。
