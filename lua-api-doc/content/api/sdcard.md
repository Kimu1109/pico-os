---
title: "SDカード"
weight: 40
description: "sd_exists / sd_read / sd_write / sd_remove / sd_mkdir / sd_list"
---

> SDカードが使用不可の間、および `sd_outside_app_dir` 権限が無いのに `app_dir` の外を指した場合、これらはすべて**エラーにならず失敗値(`false`/`nil`)を返すだけ**です。権限の詳細は [権限モデル](../../guide/permissions/) を参照してください。

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

`path` がディレクトリの場合、`{ {name = string, is_dir = boolean}, ... }` の配列(1始まりのテーブル)を返します。`path` が存在しない・ディレクトリでない場合は `nil`。

```lua
local entries = pico.sd_list("/lua/apps/myapp")
if entries then
    for i, e in ipairs(entries) do
        print(i, e.name, e.is_dir)
    end
end
```
