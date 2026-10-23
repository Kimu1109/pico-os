---
aliases: ["/guide/permissions/"]
title: "権限"
weight: 10
description: "network / sd_outside_app_dir / notify / microphone / background の権限"
---

アプリができることは、`app.cfg` に書く権限で決まります。どれも既定は `false`(許さない)です。

```
permission_network=true
permission_notify=true
```

| 権限(`app.cfg` のキー) | 許すこと | 無いときの動き |
|---|---|---|
| `permission_network` | HTTP通信(`pico.http_request` / `http_cancel`) | 呼んでも `false` が返るだけ |
| `permission_sd_outside_app_dir` | アプリのフォルダの外のSDの読み書き(`pico.sd_*` / `config_*` / `image_load` / `canvas_save`・`canvas_load` / `Image`・`ImageView`・`MarkdownView` の `path` など) | アプリのフォルダの中だけ。外を指すと `false` / `nil` |
| `permission_notify` | 通知(`pico.notify`)。アプリを閉じた後にも画面へ出て音も鳴るため | `nil, 理由` が返るだけ |
| `permission_microphone` | マイクの録音・メーター(`pico.mic_start` など)。周りの音を拾うため | `nil, 理由` が返るだけ。読むだけの `mic_level` などは呼べる(聞いていなければ0) |
| `permission_background` | 閉じている間に動くスクリプト(`background`)。電池とWi-Fiを使い続けるため | `background` を書いても動かない |

- 権限が無いときも、エラーではなく失敗の戻り値になります。戻り値を確かめて、利用者に知らせてください。
- 権限はアプリを開いたときに決まり、スクリプトから変えることはできません。
- `app.cfg` はスクリプトから書き換えられません(自分で権限を足せないように)。

## アプリのフォルダ(app_dir)

`permission_sd_outside_app_dir` が無い間、SDの読み書きは `pico.app_dir()`(`main.lua` のあるフォルダ)の中だけです。

```lua
-- app_dir が "/lua/apps/myapp" のとき
pico.sd_read("/lua/apps/myapp/save.txt")        -- 読める
pico.sd_read("/lua/apps/myapp/sub/data.txt")    -- 読める(下のフォルダも可)
pico.sd_read("/lua/apps/other/save.txt")        -- 読めない(nil。警告がログに出る)
```

## 画面を移ったとき

- `pico.push_scene()` / `pico.change_scene()` で開いた画面も同じアプリなので、権限とアプリのフォルダはそのままです。
- `pico.launch_app()` で別のアプリを開くと、そのアプリ自身の権限で動きます。

## アプリストアで配るとき

`app.cfg` の権限はストアの一覧にも載り、入れる前に利用者へ見せます。一覧に載せた権限より強い権限を `app.cfg` に書いたアプリは、入れるときに断られます([アプリストアで配る](../publishing/))。
