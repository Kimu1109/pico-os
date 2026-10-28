---
title: "ソケット"
weight: 45
description: "pico.socket: TCP / UDP / WebSocket / MQTT"
---

使い方のまとまった例は [MQTTリモコン](../../../examples/mqtt-remote/)(MQTT)と [HAパネル](../../../examples/ha-panel/)(WebSocket)を見てください。

`pico.http_request` が「1回聞いて1回答えをもらう」ためのものなのに対し、`pico.socket` は**繋いだままにして何度もやり取りする**ためのものです。Home Assistant の WebSocket API・MQTTのブローカ(Mosquitto)・LANの中のIoT機器・リアルタイムのチャットなどに使います。

- `network` 権限が要ります。無いアプリでは作る関数が `nil, "ネットワークの権限がありません"` を返します([権限モデル](../../../guide/app/permissions/) 参照)。
- バックグラウンドのスクリプトでは使えません(`pico.socket` が `nil` です)。
- 同時に開けるのは**4本まで**です。アプリを閉じると全部閉じます(MQTTは `DISCONNECT`、WebSocketは番号1001の close を送ってから)。
- **繋ぐ処理(とTLSのハンドシェイク)は、作った次のフレームで画面を止めて進みます**(HTTPと同じ。最大3秒、TLSは1〜2秒)。
- 届いたものは、作るときに渡した `callback(event, ...)` へ渡ります。最初の引数がイベントの名前です。
- 作るのに成功したら、**最後に必ず1回 `"close"` が来ます**(相手が閉じた・切れた・`pico.socket.close()` を呼んだ、のどれでも)。`"close"` の後、その `id` は使えなくなります。
- 画面が消えている(スリープの)間もWi-Fiは繋がったままなので、接続は続きます(繋ぎかけの間だけスリープしません)。

## pico.socket.tcp

<div class="sig">pico.socket.tcp(
    host: string,
    port: integer,
    callback: function,
    [opts: { tls: boolean }]
) <span class="ret">-> id: integer | nil, err: string</span></div>

| イベント | 引数 | いつ |
|---|---|---|
| `"open"` | なし | 繋がった。ここから `send` できる |
| `"data"` | `bytes: string` | 届いた(区切りはTCPの都合で決まる。1回に最大1472バイト、1フレームに4回まで) |
| `"close"` | `err: string \| nil` | 閉じた。相手が閉じたときと `close()` のときは `nil`、繋がらなかったときは理由 |

`opts.tls = true` でTLS(`https` と同じ証明書の確認)で繋ぎます。

```lua
local buf = ""
local s = pico.socket.tcp("192.168.1.20", 23, function(ev, data)
    if ev == "open" then
        pico.socket.send(s, "status\r\n")
    elseif ev == "data" then
        buf = buf .. data
    elseif ev == "close" then
        pico.log("閉じた: " .. tostring(data))
    end
end)
```

## pico.socket.udp

<div class="sig">pico.socket.udp(
    callback: function,
    [opts: { port: integer }]
) <span class="ret">-> id: integer | nil, err: string</span></div>

| イベント | 引数 |
|---|---|
| `"data"` | `bytes: string, from_host: string, from_port: integer` |
| `"close"` | `nil` |

作ったらすぐ使えます(`"open"` は来ません)。`opts.port` を書くとその番号で待ち受けます(省くと空いている番号。返事を受けるだけならこれで足ります)。送るのは `pico.socket.sendto()` です。1つのデータグラムは1472バイトまで、届いた順に1フレームに16個まで渡します。

```lua
-- WLED のリアルタイム制御(UDPの21324番、DRGB形式: 2, 待ち秒数, R, G, B, ...)で先頭のLEDを赤にする例
local u = pico.socket.udp(function(ev, data, host, port)
    if ev == "data" then pico.log(host .. ":" .. port .. " " .. data) end
end)
pico.socket.sendto(u, "192.168.1.30", 21324, "\2\1\255\0\0")
```

## pico.socket.websocket

<div class="sig">pico.socket.websocket(
    url: string,          -- "ws://..." または "wss://..."
    callback: function,
    [opts: table]
) <span class="ret">-> id: integer | nil, err: string</span></div>

| イベント | 引数 | いつ |
|---|---|---|
| `"open"` | `protocol: string` | 開いた(サーバが選んだサブプロトコル。無ければ `""`) |
| `"message"` | `data: string, is_binary: boolean` | メッセージが届いた(分割されたものは1つにつないで渡す) |
| `"close"` | `err: string \| nil, code: integer, reason: string` | 閉じた。正常に閉じたら `err` は `nil`。`code` は閉じた番号(closeを受けずに切れたら1006) |

| `opts` のキー | 説明 |
|---|---|
| `protocol` | `Sec-WebSocket-Protocol` に入れるサブプロトコル |
| `headers` | 開始の挨拶に足すヘッダ(`{ ["Authorization"] = "Bearer ..." }`。文字列だけ、合計512バイトまで)。`Host` `Upgrade` `Connection` `Sec-WebSocket-*` は指定できません |
| `max_message` | 受けるメッセージの上限(バイト。既定8192、1024〜65536)。超えたら番号1009で閉じます |
| `ping_interval` | 何も届かない時間がこの秒数続いたら ping を送り、次の間隔までに何も届かなければ切ります(既定30、0で送らない) |

- サーバからの ping には自動で答えます。
- テキストのUTF-8が正しいかは確かめません。圧縮(permessage-deflate)は使いません。

```lua
-- Home Assistant の WebSocket API
local token = "長期アクセストークン"
local ws
ws = pico.socket.websocket("ws://homeassistant.local:8123/api/websocket", function(ev, data)
    if ev ~= "message" then return end
    local m = pico.json_decode(data)
    if m.type == "auth_required" then
        pico.socket.send(ws, pico.json_encode({type = "auth", access_token = token}))
    elseif m.type == "auth_ok" then
        pico.socket.send(ws, pico.json_encode({id = 1, type = "subscribe_events", event_type = "state_changed"}))
    elseif m.type == "event" then
        local e = m.event.data
        pico.log(e.entity_id .. " = " .. e.new_state.state)
    end
end, {max_message = 16384})
```

## pico.socket.mqtt

<div class="sig">pico.socket.mqtt(
    url: string,          -- "mqtt://host[:1883]" または "mqtts://host[:8883]"
    callback: function,
    [opts: table]
) <span class="ret">-> id: integer | nil, err: string</span></div>

MQTT 3.1.1 です。

| イベント | 引数 | いつ |
|---|---|---|
| `"connect"` | `session_present: boolean` | ブローカに受け入れられた。ここから購読・送信できる |
| `"message"` | `topic: string, payload: string, info: { qos, retain }` | 購読した話題に届いた |
| `"subscribed"` | `packet_id: integer, granted_qos: integer` | 購読が受け付けられた(`granted_qos` が128なら断られた) |
| `"unsubscribed"` | `packet_id: integer` | 購読をやめられた |
| `"published"` | `packet_id: integer` | 送った QoS 1 がブローカに届いた |
| `"close"` | `err: string \| nil` | 閉じた。`close()` で閉じたら `nil`。断られたとき(パスワード違い等)・切れたときは理由 |

| `opts` のキー | 説明 |
|---|---|
| `client_id` | クライアントID(47バイトまで)。省くとブローカが付けます(`clean = false` には必須) |
| `username` / `password` | 認証(95 / 255バイトまで) |
| `keepalive` | 秒(既定60、0で使わない)。この間なにも送っていなければ PINGREQ を送り、1.5倍の間なにも届かなければ切ります |
| `clean` | クリーンセッション(既定 `true`) |
| `will` | 遺言 `{ topic = "...", payload = "...", qos = 0, retain = false }`(切れたときにブローカが代わりに流す) |
| `max_packet` | 受けるパケットの上限(バイト。既定4096、1024〜65536)。超えたものは読み捨てます(QoS 1/2 には受け取ったと答えます) |

- 送れるのは QoS 0 と 1 です(受けるのは 0/1/2)。QoS 1 の送り直しはしません(切れたら届いたかは分かりません)。
- 切れても自動では繋ぎ直しません。`"close"` を見て `pico.after()` などで作り直してください。

```lua
local m
m = pico.socket.mqtt("mqtt://192.168.1.10", function(ev, a, b)
    if ev == "connect" then
        pico.socket.subscribe(m, "home/+/state", 1)
        pico.socket.publish(m, "home/pico/status", "online", {retain = true})
    elseif ev == "message" then
        pico.log(a .. " = " .. b)
    elseif ev == "close" then
        pico.log("切れた: " .. tostring(a))
    end
end, {client_id = "pico-remote", username = "mqtt", password = "secret",
      will = {topic = "home/pico/status", payload = "offline", retain = true}})
```

## pico.socket.send

<div class="sig">pico.socket.send(id: integer, data: string, [opts: { binary: boolean }]) <span class="ret">-> boolean</span></div>

TCP と WebSocket で送ります。WebSocket は既定でテキスト、`{binary = true}` でバイナリ。1回16KiBまで。繋がる前・閉じかけ・知らない `id` は `false`。UDPには `sendto`、MQTTには `publish` を使います(種類違いはエラー)。

## pico.socket.sendto

<div class="sig">pico.socket.sendto(id: integer, host: string, port: integer, data: string) <span class="ret">-> boolean</span></div>

UDPで1つのデータグラムを送ります(1472バイトまで)。名前で書くと名前解決の間だけ画面が止まります。

## pico.socket.publish / subscribe / unsubscribe

<div class="sig">pico.socket.publish(id, topic: string, payload: string, [opts: { qos: 0|1, retain: boolean }]) <span class="ret">-> packet_id: integer | false</span></div>
<div class="sig">pico.socket.subscribe(id, topic: string, [qos: 0|1|2]) <span class="ret">-> packet_id: integer | false</span></div>
<div class="sig">pico.socket.unsubscribe(id, topic: string) <span class="ret">-> packet_id: integer | false</span></div>

- `publish` の戻りは、QoS 1 なら識別子(`"published"` と対応)、QoS 0 なら `0`。繋がっていなければ `false`。
- `subscribe` / `unsubscribe` の識別子は `"subscribed"` / `"unsubscribed"` と対応します。
- 話題は256バイトまで。`publish` に `+` `#` は使えません。`subscribe` のワイルドカードは段まるごと(`a/+/b`、`#` は最後だけ)。形が違うとエラーです。

## pico.socket.close

<div class="sig">pico.socket.close(id: integer) <span class="ret">-> boolean</span></div>

閉じ始めます(閉じ始めたら `true`、閉じかけ・知らない `id` は `false`)。この後に `"close"` が1回来ます。WebSocketは close を送って相手の返事(最大2秒)を待ち、MQTTは `DISCONNECT` を送ります。

## pico.socket.state

<div class="sig">pico.socket.state(id: integer) <span class="ret">-> "connecting" | "open" | "closing" | "closed" | nil</span></div>

`"close"` を渡し終えた後と、知らない `id` は `nil` です。
