---
title: "HAパネル"
weight: 46
description: "/lua/apps/HAパネル/ — pico.socket.websocket で Home Assistant の状態を映して点け消しする"
---

Home Assistant の WebSocket API へ直接繋ぎ、選んだエンティティ(最大6つ)の状態を即座に映して、タップで点け消しするアプリです(`app.cfg` と `icon.pimg` 付き)。[`pico.socket.websocket`](../../api/data/socket/) の使い方のまとまった例になっています。HA側にMQTTなどの準備は要らず、長期アクセストークンだけで使えます。

## 画面

```
[戻る][設定][再接続]
● 接続中: HA 2026.10.0                ← 緑=繋がっている 黄=繋いでいる 赤=切れている
┌──────────┐┌──────────┐
│居間の照明 ││寝室の照明 │            ← 紺=ON/開 灰=OFF(タップで切り替え)
│ ON       ││ OFF      │
└──────────┘└──────────┘
┌──────────┐┌──────────┐
│居間の温度 ││湿度       │            ← 白=表示だけ(センサーは値と単位)
│ 21.5 °C  ││ 48 %     │
└──────────┘└──────────┘
light.bedroom → on                     ← 最近の変化と失敗(3行)
```

## 使い方

1. Home Assistant の「プロフィール → セキュリティ → 長期アクセストークン」でトークンを作る。
2. 「設定」で、サーバのURL(`ws://homeassistant.local:8123/api/websocket`。HTTPSで公開しているなら `wss://…`)とトークンを入れる。
3. 「エンティティ」に、1行に1つ `entity_id`(名前を変えたいときは `名前=entity_id`)を書く。名前を省くと HA の表示名(friendly_name)を使います。

```
light.living_room
扇風機=switch.fan
sensor.living_room_temperature
```

点け消しできるのは `light` `switch` `fan` `input_boolean` `automation` `cover` です。それ以外(`sensor` など)は表示だけです。設定は `store.json` へ**暗号化して**保存します(トークンを含むため)。

## やり取り

```lua
-- 開くと HA が {"type":"auth_required"} を送ってくる
pico.socket.send(ws, pico.json_encode({ type = "auth", access_token = cfg.token }))
-- {"type":"auth_ok"} が来たら、見たいエンティティだけを購読する
send({ type = "subscribe_entities", entity_ids = ids })
-- タップしたら
send({ type = "call_service", domain = "homeassistant", service = "toggle",
       target = { entity_id = "light.living_room" } })
```

- `subscribe_entities` の最初の event の `"a"` に今の状態と属性が、以後は `"c"` の `"+"` に変わった分だけが届きます。全部の状態を返す `get_states` は大きすぎる(数百KB)ので使いません。
- `homeassistant.toggle` を使うと、ドメインごとにサービスを選ばずに済みます。送った後は、状態の変化か失敗の `result` が届くまでタイルに「…」が出ます。
- トークンが違う(`auth_invalid`)ときは繋ぎ直しません(間違ったトークンで繰り返すと、HA が送り元を締め出すため)。それ以外で切れたら、5秒から最大60秒の間隔で繋ぎ直します。
