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
| `url` | `http://` のURL(**HTTPSは未対応**。渡すと `false` が返る) |
| `body` | 送信ボディ。無ければ `nil` |
| `content_type` | `Content-Type` ヘッダの値。無ければ `nil` |
| `callback` | 完了時に呼ばれる関数(必須) |

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

## 同時に1本まで

進行中のリクエストがある間に `pico.http_request()` を呼ぶと `false` が返り、新しいリクエストは開始されません。**コールバックが呼ばれた時点では次のリクエストを送れます**(コールバックの中から連続してリクエストを送る「チェイン」も可能です)。

```lua
pico.http_cancel() -- 進行中のリクエストを取り消す
```

## サイズの上限

送信ボディ・受信本文ともに**16KiB**までです。超過した場合、送信側は `false` が返って開始できず、受信側は応答全体が失敗として扱われます(黙って切り詰めることはしません)。

## リダイレクト

`GET` のみ自動でリダイレクトを追跡します。`POST`等でリダイレクト(3xx)が返ってきた場合は、追跡せずそのままコールバックへ渡します。

## メインループとの関係

通信の進行は画面のフレームごとに少しずつ進みます(`LuaScene` が毎フレーム内部で処理を進めています)。呼び出し側が明示的にポーリングする必要はありません。
