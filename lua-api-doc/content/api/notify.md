---
title: "通知"
weight: 85
description: "notify / notify_cancel / notify_list / launch_reason"
---

アプリを閉じた後でも、時間が経った・決まった時刻になった・電池が減った、といった条件で通知を出せます。
条件を見張るのはOSです。Luaアプリは閉じるとスクリプトごと消えるので、アプリ側は**予約するだけ**です
(Luaのコードで判定する条件は使えません)。

通知は画面上部のトースト(約5秒)と通知音で知らせ、通知センター(ステータスバーをタップ、またはランチャの「通知」)に残ります。
タップすると送ったアプリが開き、`pico.launch_reason()` で「どの通知から開かれたか」が分かります。

通知を使うには `app.cfg` に **`permission_notify=true`** が要ります([権限モデル](../../guide/permissions/))。

## pico.notify

<div class="sig">pico.notify(t: table) <span class="ret">-> id: integer | nil, err: string</span></div>

```lua
pico.notify{ title = "できました", body = "変換が終わりました" }            -- すぐ出す(0が返る)
local id = pico.notify{ title = "休憩", delay_ms = 25 * 60 * 1000, tag = "pomodoro" }
pico.notify{ title = "おはよう", daily = "07:30" }
pico.notify{ title = "締め切り", at = { year = 2026, month = 10, day = 3, hour = 9, min = 0 } }
pico.notify{ title = "水を飲む", every_ms = 60 * 60 * 1000, sound = false }
pico.notify{ title = "電池が少ない", when = "battery_low", below = 15 }
```

| フィールド | 説明 |
|---|---|
| `title` | **必須**。タイトル(48バイト=日本語16文字まで。超えた分は切れる) |
| `body` | 本文(96バイトまで) |
| `tag` | 同じアプリ・同じtagの予約/通知は**置き換え**になる(二重登録を防ぐ)。`notify_cancel(tag)` でも使う(24バイトまで) |
| `data` | タップで開かれたときに `launch_reason()` で受け取る値(24バイトまで) |
| `sound` | `false` で通知音を鳴らさない(既定 `true`) |

**いつ出すか**は次のどれか1つ(無ければすぐ出す)。2つ以上指定するとエラーです。

| フィールド | 説明 | 予約は |
|---|---|---|
| `delay_ms` | 今からN ms後 | 1回出したら消える。**再起動で消える** |
| `at` | 日時。エポック秒か `{year=, month=, day=, hour=, min=, sec=}`(現地時刻、hour以降は省略時0) | 1回出したら消える。時計が合う(NTP同期)まで待ち、過ぎていれば同期した時点で出す |
| `daily` | 毎日 `"HH:MM"` | 残る |
| `every_ms` | N msごと(**10000以上**) | 残る |
| `when` | `"battery_low"`(電池が `below`%未満になったとき。既定15。5%以上戻るまで次は出さない) / `"wifi_connected"` / `"wifi_disconnected"`(つながった/切れた瞬間) | 残る |

`delay_ms` 以外の予約はSD(`/sys/notify_rules.tsv`)に保存され、再起動しても残ります。

戻り値は予約のid(すぐ出した場合は `0`)。次の場合は `nil` と理由が返ります(エラーにはなりません):

- 権限(`permission_notify`)が無い
- このアプリの予約が上限(**4件**)。OS全体の予約の表(16件)が満杯

引数の型・書式の誤り(titleが無い、`daily`が`"HH:MM"`でない、知らない`when`等)はエラーになります。

## pico.notify_cancel

<div class="sig">pico.notify_cancel([id_or_tag]) <span class="ret">-> count: integer</span></div>

予約を取り消し、取り消した件数を返します。引数無しで自分の予約を全部、整数ならそのid、文字列ならそのtagの予約を取り消します。
他のアプリの予約は取り消せません。

## pico.notify_list

<div class="sig">pico.notify_list() <span class="ret">-> list: table</span></div>

自分の予約を `{ {id=, tag=, title=, kind=}, ... }` で返します。`kind` は `"delay"` `"at"` `"daily"` `"every"` `"battery_low"` `"wifi_connected"` `"wifi_disconnected"`。

## pico.launch_reason

<div class="sig">pico.launch_reason() <span class="ret">-> tag: string, data: string | nil</span></div>

通知をタップして開かれたとき、その通知の `tag` と `data` を返します。それ以外(ランチャから開いた、`pop()` で戻ってきた)は `nil`。

```lua
local tag, data = pico.launch_reason()
if tag == "pomodoro" then
    -- タイマーの画面から始める
end
```

## 見せ方

通知センターの下のボタンで、利用者が **通常 / 控えめ**(トーストも音も出さず、ステータスバーの印と通知センターだけ)と
**音あり / 音なし** を切り替えられます(`/sys/notify.cfg`)。ゲームボーイの画面の間は、トーストと音を自動で控えます。
