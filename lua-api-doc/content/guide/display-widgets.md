---
title: "表示・日付・時計の部品"
weight: 36
description: "ProgressBar / TextView / ImageView / MarkdownView / AnalogClock / DurationPicker / MonthGrid の使い方"
---

OSの標準アプリが使っている部品を、`pico.create` でそのまま作れます。生成直後の大きさは仮の値なので、`w` / `h` を設定してください。プロパティの一覧は [プロパティ対応表](../../reference/widget-properties/) にあります。

## ProgressBar

進み具合を横棒で見せる表示専用の部品です(タップは下のウィジェットへ素通りします)。

```lua
local bar = pico.create("ProgressBar")
pico.set(bar, "x", 10); pico.set(bar, "y", 40)
pico.set(bar, "w", 200); pico.set(bar, "h", 14)
pico.set(bar, "max_value", 100)   -- 既定は0〜100
pico.set(bar, "color", 10)        -- バーの色(パレット番号)
pico.set(bar, "value", 35)
```

## TextView — 長い文章を軽く出す

見えている行だけを描くので、長い文章でもスクロールが重くなりません。折り返しは幅に合わせて自動、上下のドラッグでスクロールします。`Label` + `ScrollContainer` よりずっと軽いので、ログ表示などに向きます。

```lua
local log = pico.create("TextView")
pico.set(log, "w", 220); pico.set(log, "h", 200)
pico.text_set(log, long_text)               -- 16KiBまで(超えた分は切れて false)
pico.set(log, "scroll_y", pico.get(log, "scroll_y") + 40)
pico.on(log, "text_tap", function(id, byte_offset) ... end)
```

文章を足していくときは、全文を持っておいて毎回 `text_set` します(途中の追記用の関数はありません)。マークアップ(`**` など)は解釈せず、書いてあるとおりに出ます。

## ImageView — スクロールできる画像

`.pimg` を1回だけ読み込んで表示します。表示欄より大きい画像はドラッグで動かせます(大きな画像は表示欄ぶんだけメモリに持ちます)。`pico.image_load` + `Canvas` より手軽で、画像をスロットも消費しません。

```lua
local view = pico.create("ImageView")
pico.set(view, "w", 200); pico.set(view, "h", 150)
pico.set(view, "path", pico.path_join(pico.app_dir(), "map.pimg"))  -- 読めなければエラー
print(pico.get(view, "image_w"), pico.get(view, "image_h"))
```

## MarkdownView

Markdownを整形して表示します。**1つで約40KBのRAMを使う**ので、1つのアプリに1つまでにしてください。

```lua
local md = pico.create("MarkdownView")
pico.set(md, "w", 232); pico.set(md, "h", 260)
pico.text_set(md, "# 見出し\n\n- 項目\n- [リンク](next.md)\n")  -- 8KiBまで
-- またはファイルから: pico.set(md, "path", pico.path_join(pico.app_dir(), "help.md"))
pico.on(md, "link_tap", function(id, path) pico.set(md, "path", path) end)
```

## AnalogClock

文字盤と針です。時刻は自分で流し込みます(自動では進みません)。

```lua
local clock = pico.create("AnalogClock")
pico.set(clock, "w", 90)    -- 直径
pico.every(1000, function()
    local t = pico.get_time()
    pico.set(clock, "hour", t.hour)
    pico.set(clock, "minute", t.min)
    pico.set(clock, "second", t.sec)
end)
```

## DurationPicker

「時:分:秒」を大きく見せ、▲▼で変えられる入力欄です(長押しで連続して変わります)。値はミリ秒の合計(最大 23:59:59)。

```lua
local dp = pico.create("DurationPicker")
pico.set(dp, "total_ms", 25 * 60 * 1000)
pico.on(dp, "duration_changed", function(id, ms) print(ms // 1000, "秒") end)
pico.set(dp, "editable", false)   -- カウントダウン中は▲▼を消す
```

`pico.set` で値を変えても `duration_changed` は呼ばれません(▲▼で変わったときだけ)。

## MonthGrid

1か月ぶんのカレンダーです。日曜が赤・土曜が青で、今日は赤の二重枠、選んだ日は黒塗りになります。予定のある日には `pico.set_dots` で点を付けます。

```lua
local grid = pico.create("MonthGrid")
pico.set(grid, "w", 220); pico.set(grid, "h", 150)
pico.set(grid, "year", 2026); pico.set(grid, "month", 10)
pico.set(grid, "today", 5)
pico.set_dots(grid, { [3] = {12}, [10] = {9, 10} })
pico.on(grid, "day_selected", function(id, day) ... end)
pico.on(grid, "swipe", function(id, dir)         -- 左右のスワイプで月を送る
    ...
end)
```

月を変えると `today` と `selected` と点は残るので、`today` / `selected` / `set_dots` を入れ直してください。

動くサンプルは `pc/sdcard/lua/apps/ウィジェット追加確認/` にあります。
