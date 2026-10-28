---
title: "MQTTリモコン"
weight: 45
description: "/lua/apps/MQTTリモコン/ — pico.socket.mqtt で照明やコンセントを点け消しする"
---

MQTTのブローカ(Home Assistant の Mosquitto アドオン等)へ繋ぎ、照明・コンセントなどを点け消しするアプリです(`app.cfg` と `icon.pimg` 付き)。[`pico.socket.mqtt`](../../api/data/socket/) の使い方のまとまった例になっています。

## 画面

```
[戻る][設定][再接続]
● 接続中: mqtt://homeassistant.local       ← 緑=繋がっている 黄=繋いでいる 赤=切れている
┌──────────┐┌──────────┐
│居間の照明 ││寝室の照明 │                 ← タップで点け消し(緑=ON、灰=OFF/不明)
│ ON       ││ OFF      │
└──────────┘└──────────┘
  …(最大6台)
home/fan/state = OFF                        ← 最近届いたもの(3行)
```

## 機器との約束

機器ごとに話題(例 `home/living/light`)を1つ決め、次の形でやり取りします。

| 話題 | 向き | 中身 |
|---|---|---|
| `<話題>/state` | 機器 → アプリ | 今の状態。`ON` / `OFF`(`1`/`0`、`true`/`false`、Zigbee2MQTTの `{"state":"ON"}` も読む)。**retain を付けて流す**と、アプリを開いた瞬間に今の状態が分かります |
| `<話題>/set` | アプリ → 機器 | タップで今の逆の `ON` / `OFF` を QoS 1 で送る |
| `pico/remote/<クライアントID>/status` | アプリ → 皆 | 繋いだら `online`、切れたら遺言で `offline`(retain) |

送った後は、機器から状態が返ってくるまでタイルに「…」が出ます(画面には機器の本当の状態だけを出す)。

Home Assistant なら、MQTT のスイッチを `command_topic: home/living/light/set` / `state_topic: home/living/light/state` で作り、オートメーションで本物の照明とつなぐ形になります。

## 設定

「設定」から、ブローカのURL(`mqtt://ホスト[:ポート]` / `mqtts://…`)・ユーザー名・パスワード・機器の一覧を変えられます。機器の一覧は1行に1台、`名前=話題` の形で書きます。

```
居間の照明=home/living/light
扇風機=home/fan
```

設定はアプリのフォルダの `store.json` へ**暗号化して**保存します(パスワードを含むため)。

## 作りのポイント

```lua
sock = pico.socket.mqtt(cfg.url, on_event, {
    client_id = cfg.client_id,
    username = cfg.user ~= "" and cfg.user or nil,
    password = cfg.pass ~= "" and cfg.pass or nil,
    keepalive = 60,
    will = { topic = status_topic(), payload = "offline", retain = true },
})
```

- `"connect"` で機器ごとに `<話題>/state` を購読し、`online` を置きます。
- `"message"` で状態を覚えて `pico.invalidate()` で描き直すだけです。
- `"close"` を受けたら `pico.after()` で繋ぎ直します(5秒から、続けて失敗すると倍にして最大60秒)。
- 戻るときは `pico.socket.close()` の前に `offline` を置きます(自分から閉じると遺言は流れないため)。
