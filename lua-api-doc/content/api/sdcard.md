---
title: "SDカード"
weight: 40
description: "sd_exists / sd_read / sd_write / sd_remove / sd_mkdir / sd_list / config_read / config_get / config_write"
---

> SDカードが使用不可の間、および `sd_outside_app_dir` 権限が無いのに `app_dir` の外を指した場合、これらはすべて**エラーにならず失敗値(`false`/`nil`)を返すだけ**です。権限の詳細は [権限モデル](../../guide/permissions/) を参照してください。

> **アプリの `app.cfg` は書き換えられません。** 書き込み系(`sd_write` / `sd_remove` / `sd_mkdir` / `canvas_save` / `config_write`)は、自分のアプリディレクトリ直下の `app.cfg` と `/lua/apps/<名前>/app.cfg` を指すと `false` を返します(`sd_outside_app_dir` 権限があっても同じ)。`sd_remove` は、それらを含むディレクトリ(自分のアプリディレクトリとその親、`/lua/apps` とその下の各アプリのディレクトリ)も消せません。読むのは自由です。

## pico.sd_exists

<div class="sig">pico.sd_exists(path: string) <span class="ret">-> exists: boolean</span></div>

## pico.sd_read

<div class="sig">pico.sd_read(path: string) <span class="ret">-> content: string | nil</span></div>

ファイル全体を文字列で読み込みます。上限は**16KiB**で、超過するファイルは切り詰めずに `nil` を返します(壊れたデータを気付かず使わせないため)。読み取り自体が失敗した場合も `nil`。

## pico.sd_write

<div class="sig">pico.sd_write(path: string, content: string, append?: boolean) <span class="ret">-> ok: boolean</span></div>

`append` の既定値は `false`(新規作成+上書き)。`true` を渡すとファイル末尾へ追記します。

## pico.sd_remove

<div class="sig">pico.sd_remove(path: string) <span class="ret">-> ok: boolean</span></div>

ファイルなら削除、ディレクトリなら**再帰的に**削除します(`FileExplorer` の削除と同じ挙動)。

## pico.sd_mkdir

<div class="sig">pico.sd_mkdir(path: string) <span class="ret">-> ok: boolean</span></div>

## pico.sd_list

<div class="sig">pico.sd_list(path: string) <span class="ret">-> entries: table | nil</span></div>

`path` がディレクトリの場合、`{ {name = string, is_dir = boolean, size = integer}, ... }` の配列(`size` はバイト数、ディレクトリは `0`)(1始まりのテーブル)を返します。`path` が存在しない・ディレクトリでない場合は `nil`。

```lua
local entries = pico.sd_list("/lua/apps/myapp")
if entries then
    for i, e in ipairs(entries) do
        print(i, e.name, e.is_dir)
    end
end
```

## pico.sd_stat

<div class="sig">pico.sd_stat(path: string) <span class="ret">-> info: table | nil</span></div>

ファイル/ディレクトリの情報 `{size = integer, is_dir = boolean}` を返します。存在しない・SDが使えない・権限外のときは `nil`。更新日時は持っていません。

## pico.sd_read_part

<div class="sig">pico.sd_read_part(path: string, offset: integer, length: integer) <span class="ret">-> data: string | nil</span></div>

`offset` バイト目から最大 `length` バイトを読みます。ファイルの終わりを越える分は短く(完全に越えていれば空文字列)返ります。`length` の上限は `sd_read` と同じ16KiB。`offset` / `length` が負だとエラー、存在しない・ディレクトリは `nil`。`sd_read` では読めない大きなファイルを少しずつ読むためのものです。

## pico.config_read

<div class="sig">pico.config_read(path: string) <span class="ret">-> entries: table | nil</span></div>

`key=value` 形式の設定ファイルを読み、`{ key = value, ... }` のテーブルで返します。値は**常に文字列**です(前後の空白は除かれます)。`#` で始まる行はコメント、同じキーが複数あれば後の行が勝ちます。ファイルが無い・ディレクトリ・16KiBを超える場合は `nil`。

## pico.config_get

<div class="sig">pico.config_get(path: string, key: string) <span class="ret">-> value: string | nil</span></div>

1つのキーの値だけを返します。ファイルかキーが無ければ `nil`。

## pico.config_write

<div class="sig">pico.config_write(path: string, key: string, value: string | number | boolean) <span class="ret">-> ok: boolean</span></div>

`key` の行だけを書き換えます(無ければ末尾へ追記、ファイルが無ければ作成)。コメントや他の行はそのまま残ります。一時ファイル(`path .. ".tmp"`)へ書いてから差し替えます。

- 数値は `55` / `0.25` のように、真偽値は `true` / `false` として書きます(読むときは `tonumber(v)` や `v == "true"`)。
- キーは64バイト未満で、`=`・改行・前後の空白を含まず `#` で始まらないこと(違反はエラー)。値の上限は160バイト未満。
- 改行を含む値は書けず `false` を返します。
