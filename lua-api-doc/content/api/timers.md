---
title: "タイマー"
weight: 78
description: "after / every / cancel"
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
