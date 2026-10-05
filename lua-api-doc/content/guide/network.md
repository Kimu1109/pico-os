---
title: "ネットワーク (HTTP)"
weight: 90
description: "pico.http_requestで平文HTTPのリクエストを送る"
---

## 権限が必要

`pico.http_request` / `pico.http_cancel` は `network` 権限(既定 `false`)が無いと使えません。SDカードで自動登録されたアプリ(`/lua/apps/<名前>/main.lua`)は常に権限なしなので、ネットワークを使うアプリは [権限モデル](../permissions/) に従ってC++側へ手動登録する必要があります。権限が無い状態で呼ぶと `false` が返るだけです(エラーにはなりません)。

## 基本形

```lua
local ok = pico.http_request(method, url, body, content_type, function(ok, status_code, body_or_nil, error_or_nil)
    if ok then
        pico.log("status=" .. status_code)
        pico.log(body_or_nil or "")
    else
        pico.log("失敗: " .. (error_or_nil or "?"))
    end
end)
```

| 引数 | 説明 |
|---|---|
| `method` | `"GET"` / `"POST"` / `"PUT"` / `"PATCH"` / `"DELETE"` のいずれか |
| `url` | `http://` または `https://` のURL |
| `body` | 送信ボディ。無ければ `nil` |
| `content_type` | `Content-Type` ヘッダの値。無ければ `nil` |
| `callback` | 完了時に呼ばれる関数(必須) |
| `opts` | 任意。`headers`(リクエストヘッダ)・`save_to`(保存先) |

呼び出し自体の戻り値(`bool`)は「リクエストを開始できたか」を表します。実際の成否はコールバックの `ok` 引数で判定してください。

## GET

```lua
pico.http_request("GET", "http://example.local/api/status", nil, nil, function(ok, status, body)
    if ok and status == 200 then
        pico.log(body)
    end
end)
```

## POST(JSON送信の例)

```lua
local payload = '{"score": 100}'
pico.http_request("POST", "http://example.local/api/score", payload, "application/json",
    function(ok, status, body, err)
        if ok then
            pico.log("送信完了: " .. status)
        end
    end)
```

## ステータスコードにかかわらず本文が読める

Markdownブラウザ用の内部クライアントと異なり、`pico.http_request` は**2xx以外のステータスコード(404等)でも本文をそのまま渡します**。エラーページの内容を見て挙動を変える、といった使い方ができます。

## 同時に走るのは1本、あとは順番待ち

走っているリクエストがある間に `pico.http_request()` を呼ぶと、**順番待ち**になります(最大4本。呼んだ順に1本ずつ走ります)。これを超えると `false` が返ります。戻り値は成功時にリクエストID(整数)で、`pico.http_cancel(id)` でそのリクエストだけを取り消せます。**コールバックが呼ばれた時点では次のリクエストを送れます**(コールバックの中から連続してリクエストを送る「チェイン」も可能です)。

```lua
local id = pico.http_request("GET", url, nil, nil, on_done)
pico.http_cancel(id)  -- そのリクエストだけ取り消す
pico.http_cancel()    -- 走っているものも順番待ちも全部取り消す
```

## ヘッダ・応答ヘッダ・ファイルへの保存

6番目の引数 `opts` で、認証ヘッダなどの任意のリクエストヘッダと、ファイルへの直接保存を指定できます。コールバックの5番目の引数が応答ヘッダ(`content-type` など)、6番目が本文の大きさと保存先です。詳しくは [ネットワークAPI](../../api/network/) を参照してください。

```lua
pico.http_request("GET", "https://api.example.com/me", nil, nil,
    function(ok, status, body, err, headers)
        local me = pico.json_decode(body)   -- JSONは pico.json_decode で読む
    end,
    { headers = { ["Authorization"] = "Bearer " .. token } })
```

## サイズの上限

送信ボディ・メモリで受ける受信本文ともに**16KiB**までです。超過した場合、送信側は `false` が返って開始できず、受信側は応答全体が失敗として扱われます(黙って切り詰めることはしません)。それより大きな応答は `opts.save_to` でSDへ直接保存してください(最大8MiB)。

## リダイレクト

`GET` のみ自動でリダイレクトを追跡します。`POST`等でリダイレクト(3xx)が返ってきた場合は、追跡せずそのままコールバックへ渡します。

## メインループとの関係

通信の進行は画面のフレームごとに少しずつ進みます(`LuaScene` が毎フレーム内部で処理を進めています)。呼び出し側が明示的にポーリングする必要はありません。
