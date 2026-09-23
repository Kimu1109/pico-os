---
title: "ネットワーク"
weight: 70
description: "http_request / http_cancel"
---

`network` 権限が無いアプリは、これらを呼んでも常に `false` が返るだけです([権限モデル](../../guide/permissions/) 参照)。

## pico.http_request

<div class="sig">pico.http_request(
    method: string,       -- "GET" / "POST" / "PUT" / "PATCH" / "DELETE"
    url: string,          -- "http://..." または "https://..."
    body: string | nil,
    content_type: string | nil,
    callback: function
) <span class="ret">-> accepted: boolean</span></div>

`callback` は完了時に1回呼ばれます。

<div class="sig">callback(
    ok: boolean,
    status_code: integer,
    body: string | nil,
    error: string | nil
)</div>

- `accepted`(呼び出し自体の戻り値)が `false` になる条件: 権限なし / 未知のメソッド(エラーではなく `false`) / 同時実行数の上限(進行中のリクエストがある) / 不正なURL / 送信ボディが16KiB超。
- `ok` が `false` の場合、`body` は `nil`、`error` に失敗理由の文字列が入ります。
- `ok` が `true` の場合、`status_code` はサーバの応答コード(2xx/4xx/5xxを問わず本文が渡ります)。応答本文が空なら `body` は `nil`。
- 受信本文の上限は16KiBです(超過分はコネクションごと失敗扱い)。

## pico.http_cancel

<div class="sig">pico.http_cancel() <span class="ret">-> (なし)</span></div>

進行中のリクエストを取り消します。進行中のリクエストが無い場合は何もしません。取り消した場合、そのリクエストの `callback` は呼ばれません。
