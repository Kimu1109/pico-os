---
aliases: ["/api/timers/", "/api/time/"]
title: "時間とタイマー"
weight: 10
description: "after / every / cancel / get_time / time / millis / micros"
---

`loop(dt)` の中で経過時間を自分で数えなくても、一定時間後・一定間隔でコールバックを呼べます。時間は画面のフレームごとに進むので、精度はフレーム間隔(おおよそ数ms〜数十ms)です。

## pico.after

<div class="sig">pico.after(ms: integer, fn: function) <span class="ret">-> handle: integer | nil</span></div>

`ms` ミリ秒後に `fn(handle)` を1回呼びます。戻り値のハンドルは `pico.cancel` に渡せます。同時に使えるタイマーは16個までで、超えると `nil` を返します(ログに警告が出ます)。`ms` は 0〜86400000 です。

```lua
pico.after(1500, function()
    pico.set(label, "text", "もう一度どうぞ")
end)
```

## pico.every

<div class="sig">pico.every(ms: integer, fn: function) <span class="ret">-> handle: integer | nil</span></div>

`ms` ミリ秒ごとに `fn(handle)` を呼び続けます(`ms` は1以上)。処理が遅れても**溜めて何回も呼ぶことはしません**(遅れたら1回呼び、次は「そこからms後」になります)。

```lua
local sec = 0
pico.every(1000, function(h)
    sec = sec + 1
    pico.set(timer_label, "text", tostring(sec))
    if sec >= 60 then pico.cancel(h) end   -- コールバックの中から自分を止められる
end)
```

## pico.cancel

<div class="sig">pico.cancel(handle: integer) <span class="ret">-> cancelled: boolean</span></div>

タイマーを止めます。止められたら `true`、すでに鳴り終わった・止めたタイマーのハンドルは `false`(二重に呼んでも安全です)。解放したスロットが別のタイマーに再利用されても、古いハンドルでそのタイマーを止めてしまうことはありません。

## 注意

- コールバックがエラーになると、ダイアログを出して**そのタイマーは止まります**(繰り返しの場合は同じエラーが毎回出ないようにするため)。
- 別の画面へ `push_scene` している間、この画面のタイマーは動きません(画面を離れるとLuaの状態ごと作り直されるため。戻ったときに必要なら `on_suspend` / `on_resume` で状態を持ち越し、`setup()` でタイマーを作り直してください)。
- 画面が非アクティブな間(スリープ中を含む)に経過した時間は、フレームが進んだ分だけ加算されます。長いスリープをまたぐ正確な時刻には `pico.get_time()` を使ってください。

## pico.get_time

<div class="sig">pico.get_time() <span class="ret">-> t: table</span></div>

今の日時(現地時刻)を1つのテーブルで返します。

```lua
local t = pico.get_time()
print(string.format("%04d-%02d-%02d %02d:%02d:%02d",
    t.year, t.month, t.day, t.hour, t.min, t.sec))
```

| フィールド | 説明 |
|---|---|
| `year` | 西暦(例: `2026`) |
| `month` | 月(`1`〜`12`) |
| `day` | 日 |
| `hour` | 時(`0`〜`23`) |
| `min` | 分 |
| `sec` | 秒 |
| `wday` | 曜日。`0`=日曜 〜 `6`=土曜(`pico.WDAY_SUNDAY` など) |

起動してすぐ、Wi-Fiで時刻を合わせる前は正しい日時とは限りません(合っているかは `pico.time()` が `nil` でないかで分かります)。

## pico.time

<div class="sig">pico.time() <span class="ret">-> epoch_seconds: integer | nil</span></div>

今の時刻のUNIX時間(秒)。時計が合っていなければ `nil`。年月日が欲しいときは `pico.get_time()`。

## pico.millis

<div class="sig">pico.millis() <span class="ret">-> ms: integer</span></div>

起動してからのミリ秒。時計を合わせても飛ばないので、経過時間を測るのに使います(`get_time` は時計合わせで飛びます)。Luaの整数は32bitなので約24.8日で負へ回りますが、差 `b - a` は回った後も正しく出ます。

## pico.micros

<div class="sig">pico.micros() <span class="ret">-> us: integer</span></div>

起動してからのマイクロ秒(約36分で負へ回ります)。1フレームの中の処理時間のように、ミリ秒では粗すぎる計測に使います。差 `b - a` は約36分より短い間隔なら正しく出ます。
