---
aliases: ["/guide/sdcard/"]
title: "保存とSDカード"
weight: 10
description: "セーブデータ・設定・ファイルをSDカードへ読み書きする"
---

## どれを使うか

| 残したいもの | 使うもの |
|---|---|
| 最高得点・途中経過など、まとまった値 | [`pico.store_save()` / `pico.store_load()`](../../../api/ui/scenes/#picostore_load--picostore_save)(テーブルをそのまま保存) |
| 音量・前回の選択など、小さな設定 | [`pico.settings_set()` / `pico.settings_get()`](../../../api/system/misc/#picosettings_get--settings_set--settings_all) |
| 自分で形式を決めるファイル | `pico.sd_read()` / `pico.sd_write()` など(このページ) |

```lua
-- いちばん簡単なセーブ
local save = pico.store_load() or { best = 0 }
if score > save.best then
    save.best = score
    pico.store_save(save)
end
```

`store_save` と `settings_set` はアプリのフォルダに書き、書いている途中で電源が切れても前の内容が壊れないようにしてあります。

## ファイルの関数

```lua
pico.sd_exists(path)                    -- true/false
pico.sd_read(path)                      -- 文字列 or nil(16KiBまで)
pico.sd_write(path, content [, append]) -- true/false
pico.sd_remove(path)                    -- true/false(フォルダなら中身ごと)
pico.sd_mkdir(path)                     -- true/false
pico.sd_list(path)                      -- { {name=, is_dir=, size=}, ... } or nil
pico.config_read(path)                  -- { key = "value", ... } or nil
pico.config_get(path, key)              -- "value" or nil
pico.config_write(path, key, value)     -- true/false
```

パスは `/` で始まるSDの絶対パスです。アプリのフォルダの中なら `pico.path_join(pico.app_dir(), "save.txt")` と書きます。

## 失敗はエラーではなく戻り値

SDカードが挿さっていない・読めない、権限の外を指した、といった失敗はエラーにならず、`false` / `nil` が返るだけです。戻り値を必ず確かめてください。

```lua
local ok = pico.sd_write(pico.path_join(pico.app_dir(), "save.txt"), "score=100")
if not ok then
    pico.show_error("セーブに失敗しました")
end
```

## 設定ファイル(key=value)

`pico.config_*` は、`app.cfg` と同じ `key=value` の形式(1行1項目、`#` で始まる行はコメント)のファイルを読み書きします。

```lua
local CFG = pico.path_join(pico.app_dir(), "options.cfg")

local volume = tonumber(pico.config_get(CFG, "volume") or "") or 50
local all = pico.config_read(CFG) or {}   -- 値はいつも文字列
local muted = all.muted == "true"

pico.config_write(CFG, "volume", 70)      -- そのキーの行だけ書き換える(無ければ足す)
pico.config_write(CFG, "muted", true)
```

`config_write` はコメントや他の行をそのまま残し、途中で電源が切れても壊れないように書きます。

## 大きさの上限

`pico.sd_read()` は **16KiB** までです。それより大きいファイルは、切り詰めずに `nil` を返します(壊れたデータに気づかず使わないように)。大きなファイルは `pico.sd_read_part()` で少しずつ読むか、分けて保存してください。

## フォルダの中を列挙する

```lua
local entries = pico.sd_list(pico.app_dir())
if entries then
    for _, e in ipairs(entries) do
        pico.log(e.name .. (e.is_dir and "/" or ""))
    end
end
```

## 読み書きできる場所

- `sd_outside_app_dir` の権限が無いアプリ(既定)は、**自分のアプリのフォルダの中だけ**を読み書きできます。外を指すと、警告をログに出して `false` / `nil` を返します。
- `app.cfg` は読めますが、書き換え・削除はできません(権限に関わらず)。`app.cfg` を含むフォルダを `sd_remove` で消すこともできません。

詳しくは [権限](../../app/permissions/) を見てください。関数ごとの細かい動きは [SDカードAPI](../../../api/data/sdcard/) にあります。
