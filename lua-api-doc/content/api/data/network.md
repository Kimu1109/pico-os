---
aliases: ["/api/network/"]
title: "ネットワーク"
weight: 40
description: "http_request / http_cancel / http_close / form_encode"
---

`network` 権限が無いアプリは、これらを呼んでも常に `false` が返るだけです([権限モデル](../../../guide/app/permissions/) 参照)。

## pico.http_request

<div class="sig">pico.http_request(
    method: string,       -- "GET" / "POST" / "PUT" / "PATCH" / "DELETE"
    url: string,          -- "http://..." または "https://..."
    body: string | nil,
    content_type: string | nil,
    callback: function,
    [opts: table]
) <span class="ret">-> id: integer | false</span></div>

`callback` は完了時に1回呼ばれます。

<div class="sig">callback(
    ok: boolean,
    status_code: integer,
    body: string | nil,
    error: string | nil,
    headers: table,
    info: table
)</div>

- 戻り値の `id` はリクエストID(1以上の整数)で、`pico.http_cancel(id)` で取り消せます。`false` になる条件: 権限なし / 不正なURL / 送信ボディが16KiB超 / 待たせられる数(後述)を超えた / `save_to` が書き込めない場所。未知のメソッドや、`opts` の誤り(使えないヘッダ名・値など)はエラーになります。
- `ok` が `false` の場合、`body` は `nil`、`error` に失敗理由の文字列が入ります。
- `ok` が `true` の場合、`status_code` はサーバの応答コード(2xx/4xx/5xxを問わず本文が渡ります)。応答本文が空なら `body` は `nil`。
- 受信本文をメモリで受けるときの上限は16KiBです(超過は失敗扱い)。それより大きなものは `save_to` を使ってください。
- `body` が無い(`nil` か空文字列の)`POST` `PUT` `PATCH` には `Content-Length: 0` が付きます(長さが無いと 411 Length Required を返すサーバがあるため)。`GET` `DELETE` には付きません。
- **同時に走るのは1本だけ**です。走っている間に呼ぶと**順番待ち**になり(最大4本)、前のものが終わり次第、呼んだ順に始まります。待たせられる数を超えると `false` です。

### opts

| キー | 説明 |
|---|---|
| `headers` | リクエストヘッダのテーブル。`{ ["Authorization"] = "Bearer xxx", ["X-Count"] = 3 }`。値は文字列か数値。合計1000バイトまで、1行(`名前: 値`)は512バイト未満(Bearerトークン数百文字が入ります)。 |
| `save_to` | 本文をメモリに溜めず、SDのこのパスへ直接保存します。16KiBを超えるファイル(最大8MiB)も受けられます。 |
| `keep_alive` | `true` で接続を使い回します(下記)。既定は `false`(毎回 `Connection: close`)。 |

**`headers` に指定できないもの**: `Host` `Content-Length` `Content-Type`(`content_type` 引数で指定します) `Connection` `Transfer-Encoding` `Upgrade` `TE` `Trailer` `Keep-Alive` `Proxy-*`。名前に使えるのは英数字と `-` `_` `.` だけで、値に改行などの制御文字は使えません(別のヘッダや別のリクエストを差し込まれないため)。どれもエラーになります。

**`save_to`**: 保存先は `sd_write` と同じ確認(アプリのフォルダの外・`app.cfg` は不可)を通ります。まず `<パス>.part` に書き、**最後まで受け取れたときだけ**本来の名前に差し替えるので、途中で切れた半端なファイルは残りません(失敗・取り消しのときは `.part` も消えます)。保存先がすでにあれば置き換えます。ステータスが404などでも、受け取った本文は保存されます(`status_code` で判断してください)。保存中は、10秒の全体タイムアウトではなく「10秒間なにも届かない」ときに失敗とみなします。

**`keep_alive`**: 応答の後も接続を閉じずに持っておき、次の `keep_alive = true` の要求が**同じ相手(ホスト・ポート・http/https)**なら、接続とTLSのハンドシェイクを飛ばしてすぐ送ります。httpsはハンドシェイクの間(1〜2秒)画面が止まるので、数秒おきに同じAPIを呼ぶアプリでは付けてください。

- 持っている間は**TLSの道具一式(約40KB)も持ち続けます**。しばらく使わないときは `pico.http_close()` で閉じてください。アプリを閉じれば必ず閉じます。
- `keep_alive` を付けない要求を始めると、持っていた接続はその時点で閉じます。
- 相手が `Connection: close` を返した・長さの分からない応答だった・失敗した場合は使い回しません(次は新しく繋ぎます)。
- 相手が黙って接続を閉じていた(アイドルのタイムアウト等)場合は、1回だけ繋ぎ直して送り直します。**POST等も送り直す**ので、相手が処理した直後に閉じたごくまれな場合は二重に届くことがあります。
- 使い回して送れたかは `info.reused` で分かります。

### headers と info

コールバックの `headers` は応答ヘッダのテーブルで、**キーはすべて小文字**です。読めるのは次の5つだけです(無いものは入りません)。

| キー | 内容 |
|---|---|
| `content-type` | 例: `"application/json; charset=utf-8"` |
| `content-length` | 整数 |
| `etag` | 引用符を除いた値 |
| `last-modified` | `etag` が無いときだけ |
| `location` | リダイレクト先(POST等で3xxが返ったとき) |

`info` は `{ size = 受け取った本文のバイト数, saved = 保存先のパス, reused = 接続を使い回したか }` です(`saved` は `save_to` で保存できたときだけ)。`save_to` を使ったときの `body` は `nil` です。

```lua
pico.http_request("GET", "https://api.example.com/items", nil, nil,
    function(ok, status, body, err, headers, info)
        if ok and status == 200 then
            local items = pico.json_decode(body)
            ...
        end
    end,
    { headers = { ["Authorization"] = "Bearer " .. token, ["Accept"] = "application/json" } })

-- 大きなファイルをSDへ
pico.http_request("GET", "https://example.com/big.bin", nil, nil,
    function(ok, status, _, err, _, info)
        if ok then pico.log("保存した: " .. info.saved .. " (" .. info.size .. "バイト)") end
    end,
    { save_to = "/lua/apps/demo/big.bin" })
```

## pico.http_cancel

<div class="sig">pico.http_cancel([id: integer]) <span class="ret">-> cancelled: boolean</span></div>

リクエストを取り消します。`id` を渡すと、そのリクエスト(走っているもの、または順番待ちのもの)だけを取り消します。**`id` を省略すると、走っているものも順番待ちのものも全部**取り消します。取り消したものの `callback` は呼ばれません。取り消せたら `true`、該当するものが無ければ `false`。

## pico.http_close

<div class="sig">pico.http_close() <span class="ret">-> closed: boolean</span></div>

`keep_alive` で持っている接続を閉じます(TLSの約40KBを返します)。閉じた接続があれば `true`、何も持っていなければ `false`。走っている要求があれば、その応答を受け取った後で閉じます(取り消しはしません)。

```lua
-- 2秒おきに状態を取る(同じ接続を使い回す)
local timer = pico.every(2000, function()
    pico.http_request("GET", "https://api.example.com/state", nil, nil, on_state,
        { keep_alive = true, headers = { ["Authorization"] = "Bearer " .. token } })
end)

pico.on_back(function()
    pico.cancel(timer)
    pico.http_close()
    pico.pop()
end)
```

## pico.form_encode

<div class="sig">pico.form_encode(fields: table) <span class="ret">-> text: string</span></div>

表を `application/x-www-form-urlencoded` の本文(`a=1&b=x+y`)にします。OAuthのトークンの取り直しのように、フォームで送るAPIに使います。

- キーは名前順に並べます(結果がいつも同じ形になる)。空白は `+`、英数字と `- _ . *` 以外は `%XX`(UTF-8のバイトごと)。
- 値は文字列・数値・真偽値(`true`/`false`)。配列を渡すと同じキーを繰り返します(`{scope = {"a", "b"}}` → `scope=a&scope=b`)。
- キーが文字列でない・値が表や関数(配列の中の表も)ならエラーです。

```lua
local body = pico.form_encode({
    grant_type = "refresh_token", refresh_token = token.refresh, client_id = CLIENT_ID,
})
pico.http_request("POST", "https://accounts.spotify.com/api/token", body,
    "application/x-www-form-urlencoded", on_token, { keep_alive = true })
```
