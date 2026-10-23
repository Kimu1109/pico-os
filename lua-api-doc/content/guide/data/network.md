---
aliases: ["/guide/network/"]
title: "HTTP通信"
weight: 20
description: "pico.http_request でWeb APIを呼ぶ"
---

## 権限が要る

HTTP通信には `app.cfg` の `permission_network=true` が要ります([権限](../../app/permissions/))。権限が無いと、`pico.http_request()` は何もせず `false` を返します。

## 基本形

```lua
pico.http_request("GET", "https://example.com/api/status", nil, nil,
    function(ok, status, body, err)
        if ok and status == 200 then
            pico.log(body)
        elseif not ok then
            pico.log("失敗: " .. err)
        end
    end)
```

| 引数 | 説明 |
|---|---|
| `method` | `"GET"` / `"POST"` / `"PUT"` / `"PATCH"` / `"DELETE"` |
| `url` | `http://` か `https://` のURL |
| `body` | 送る本文。無ければ `nil` |
| `content_type` | `Content-Type` の値。無ければ `nil` |
| `callback` | 終わったときに1回呼ばれる関数 |
| `opts` | 任意。`headers`(送るヘッダ)・`save_to`(SDへ保存)・`keep_alive` |

- `pico.http_request()` の戻り値は「始められたか」です(始められたらリクエストのID、だめなら `false`)。うまくいったかはコールバックの `ok` で判断します。
- 通信は裏で進みます。終わるまで待つ必要はなく、その間も画面は動きます(`https://` の接続を始めるときだけ、1〜2秒止まることがあります)。
- `ok` が `true` なら、404などのエラーのステータスでも本文が届きます。`status` を見て判断してください。

## JSONを送る・受け取る

```lua
local body = pico.json_encode({ score = 100 })
pico.http_request("POST", "https://example.com/api/score", body, "application/json",
    function(ok, status, res)
        if ok and status == 200 then
            local data = pico.json_decode(res)
        end
    end)
```

大きな応答から数個の値だけ欲しいときは、`pico.json_pick()` のほうがメモリを使いません([JSON](../../../api/data/json/))。

## ヘッダ・大きなファイル

6番目の引数 `opts` で、認証のヘッダを付けたり、本文をSDへ直接保存したりできます。

```lua
pico.http_request("GET", "https://api.example.com/me", nil, nil, on_done,
    { headers = { ["Authorization"] = "Bearer " .. token } })

pico.http_request("GET", "https://example.com/big.bin", nil, nil, on_done,
    { save_to = pico.path_join(pico.app_dir(), "big.bin") })
```

## 同時に走るのは1本

走っている間に次を呼ぶと順番待ちになります(4本まで。超えると `false`)。コールバックの中から次のリクエストを始めてもかまいません。

```lua
local id = pico.http_request("GET", url, nil, nil, on_done)
pico.http_cancel(id)  -- そのリクエストだけ取り消す
pico.http_cancel()    -- 全部取り消す
```

## 上限など

- 送る本文・メモリで受け取る本文は **16KiB** まで。超えると送る側は `false`、受け取る側は失敗になります(黙って切り詰めません)。大きな応答は `save_to` でSDへ保存してください(8MiBまで)。
- リダイレクトは `GET` のときだけ自動でたどります。
- `https://` は、本体の時計が合う(Wi-Fiにつながって時刻を合わせる)まで繋がりません。

引数・コールバックの細かい意味は [ネットワークAPI](../../../api/data/network/) にあります。
