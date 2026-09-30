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
pico.config_read(path)                  -- { key = "value", ... } or nil
pico.config_get(path, key)              -- "value" or nil
pico.config_write(path, key, value)     -- true/false
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

## 設定ファイル(key=value)

設定は `pico.config_*` で読み書きするのが手軽です。書式はOSの `/sys/*.cfg` や `app.cfg` と同じ `key=value`(1行1項目、`#` で始まる行はコメント、同じキーは後勝ち)です。

```lua
local CFG = "/lua/apps/myapp/settings.cfg"

local volume = tonumber(pico.config_get(CFG, "volume") or "") or 50
local all = pico.config_read(CFG) or {}   -- { volume = "50", ... }(値は常に文字列)
local muted = all.muted == "true"

pico.config_write(CFG, "volume", 70)      -- そのキーの行だけ差し替え(無ければ追記)
pico.config_write(CFG, "muted", true)
```

`config_write` はコメントや他の行をそのまま残し、一時ファイルへ書いてから差し替えるので、途中で電源が落ちても設定が壊れません。

## app.cfgは書き換えられない

アプリ自身の `app.cfg`(`/lua/apps/<名前>/app.cfg`)は権限(`permission_network` 等)を持つため、**読めますが書き換えられません**。`sd_write` / `sd_remove` / `sd_mkdir` / `canvas_save` / `config_write` のどれでも `false` が返ります(大文字小文字や `..` の違いも同じファイルとして扱います)。`app.cfg` を含むディレクトリ(自分のアプリのディレクトリや `/lua/apps` 自体)を `sd_remove` で消すこともできません。自分の設定は `settings.cfg` のような別の名前のファイルへ書いてください。

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
