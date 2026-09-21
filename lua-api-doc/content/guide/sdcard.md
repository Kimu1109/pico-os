---
title: "SDカードアクセス"
weight: 60
description: "設定やセーブデータをSDへ読み書きする"
---

## 一覧

```lua
pico.sd_exists(path)                    -- true/false
pico.sd_read(path)                      -- 文字列 or nil
pico.sd_write(path, content [, append]) -- true/false
pico.sd_remove(path)                    -- true/false(ディレクトリなら再帰削除)
pico.sd_mkdir(path)                     -- true/false
pico.sd_list(path)                      -- { {name=..., is_dir=...}, ... } or nil
```

パスは `FileExplorer` などと同じ**SD絶対パス**(`/`始まり)です。

## SD無しは例外ではなく失敗値

SDカードが挿さっていない・読めない状態は「プログラマの書き間違い」ではなく「実行時の状態」として扱われます。`OSData::SD_usable == false` の間、これらの関数はエラーにならず `false` / `nil` を返すだけです。呼び出し側は戻り値を必ずチェックしてください。

```lua
local ok = pico.sd_write("/lua/apps/myapp/save.txt", "score=100")
if not ok then
    pico.show_error("セーブに失敗しました")
end
```

## セーブデータの典型パターン

```lua
local SAVE_PATH = "/lua/apps/myapp/save.txt"

local function save(score)
    pico.sd_write(SAVE_PATH, "score=" .. score)
end

local function load()
    local content = pico.sd_read(SAVE_PATH)
    if not content then return 0 end
    return tonumber(content:match("score=(%d+)")) or 0
end
```

## 読み込みサイズの上限

`pico.sd_read()` には**16KiBの上限**があります。上限を超えるファイルは、スクリプト読み込み(超過分を切り詰めて使う)とは違い、**切り詰めずに `nil` を返します**。壊れたデータを気付かず使ってしまうのを避けるためです。大きなデータを扱う場合はファイルを分割してください。

## ディレクトリの列挙

```lua
local entries = pico.sd_list("/lua/apps/myapp")
if entries then
    for _, e in ipairs(entries) do
        pico.log(e.name .. (e.is_dir and "/" or ""))
    end
end
```

## 権限とアプリの持ち場(app_dir)

`sd_outside_app_dir` 権限が `false`(既定、かつ SD走査で自動登録されたアプリは常にこれ)の間、SD操作はすべて**そのアプリの `app_dir`(通常は自分の `main.lua` があるディレクトリ)の配下だけ**に制限されます。範囲外のパスを渡すと、警告ログを出したうえで `false` / `nil` を返します(エラーにはしません)。

```lua
-- app_dir が "/lua/apps/myapp" の場合
pico.sd_read("/lua/apps/myapp/data/save.txt")  -- OK(配下)
pico.sd_read("/lua/apps/other_app/save.txt")   -- 拒否(nilが返る)
```

詳細は [権限モデル](../permissions/) を参照してください。
