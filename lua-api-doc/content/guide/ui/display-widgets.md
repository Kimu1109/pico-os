---
aliases: ["/guide/display-widgets/"]
title: "表示・日付・時計の部品"
weight: 50
description: "ProgressBar / QRCode / TextView / ImageView / MarkdownView / AnalogClock / DurationPicker / MonthGrid の使い方"
---

OSの標準アプリと同じ部品を、`pico.create` で作れます。生成直後の大きさは仮の値なので、`w` / `h` を設定してください。プロパティの一覧は [プロパティ対応表](../../../reference/widget-properties/) にあります。

## ProgressBar

進み具合を横棒で見せる表示専用の部品です(タップは下のウィジェットへ素通りします)。

```lua
local bar = pico.create("ProgressBar")
pico.set(bar, "x", 10); pico.set(bar, "y", 40)
pico.set(bar, "w", 200); pico.set(bar, "h", 14)
pico.set(bar, "max_value", 100)   -- 既定は0〜100
pico.set(bar, "color", pico.COLOR_GREEN)  -- バーの色
pico.set(bar, "value", 35)
```

## QRCode — QRコードを出す

文字(URLなど)をQRコードにして表示します。スマホのカメラで読み取れます。表示専用で、タップは下のウィジェットへ素通りします。

```lua
local qr = pico.create("QRCode")
pico.set(qr, "x", 40); pico.set(qr, "y", 60)
pico.set(qr, "w", 160); pico.set(qr, "h", 160)
pico.set(qr, "ecc", pico.QR_ECC_MEDIUM)            -- 誤り訂正の強さ(既定はMEDIUM)
if not pico.text_set(qr, "https://example.com/") then
    print("長すぎてQRに入りません")                    -- 255バイトを超えると空になって false
end
print(pico.get(qr, "qr_size"))                      -- 1辺の升目の数(21〜85。空なら0)
```

- 升目は `w` × `h` の中央に正方形で描きます。升1つの大きさは入る範囲で一番大きい整数px で、周りの余白(地の色)は入る範囲で規格の4升まで取ります。升が2px以上になる大きさにすると読み取りやすくなります(升目の数が29なら`w`/`h`を62以上)。
- 255バイトまでなら、誤り訂正の強さに関係なく必ず入ります。数字だけ・英大文字と一部の記号だけの文字は詰めた形になり、日本語はUTF-8のまま入ります。
- 誤り訂正は `QR_ECC_LOW`(約7%の欠けまで)・`MEDIUM`(15%)・`QUARTILE`(25%)・`HIGH`(30%)。強いほど升目が増えます。升目の数が変わらない範囲では自動で強めます。
- 色は `color`(升目)と `background_color`(地、既定は白)。**明るい方を地にして描く**ので、ダークテーマでも反転しません。
- Wi-Fiへ繋ぐQRは `WIFI:T:WPA;S:<名前>;P:<パスワード>;;`(名前とパスワードの中の `\` `;` `,` `:` `"` は前に `\` を付ける)。

- 連絡先のQRは vCard(`BEGIN:VCARD` `VERSION:3.0` `FN:名前` `TEL:…` `EMAIL:…` `END:VCARD` を改行でつなぐ。値の中の `\` `,` `;` は前に `\`)。スマホのカメラが連絡先として読みます。

動くサンプルは「QRコード」アプリ(`/lua/apps/QRコード/main.lua`。URL・連絡先・Wi-Fi・テキストを保存して一覧から出す)にあります。

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

`.pimg`・`.bmp`・`.jpg`・`.png` を読み込んで表示します(形式はファイルの中身で見分けます)。表示欄より大きい画像はドラッグで動かせます(大きな画像でも表示欄ぶんだけしかメモリを使いません)。JPEG・PNG は表示欄に収まる一番大きい段(1/1・1/2・1/4・1/8)へ縮めて開きます。`pico.image_load` + `Canvas` より手軽で、`pico.image_load` の枚数の枠も使いません。

**写真はフルカラーで見えます。** JPEG・PNG・フルカラーのBMPは、16色へ寄せた絵を描いた上に、SDから解きながら液晶へそのままの色で直接描きます(絵をメモリに持ちません)。ドラッグ中と、上にダイアログ・トースト・ほかの部品が重なっている間は16色の絵になります(直描きは上に載ったものを塗りつぶしてしまうため。どけば元に戻ります)。描き直すたびにSDから解き直すので、写真の上で頻繁に描き直す部品は重ねないでください。16色の絵だけでよければ `full_color` を `false` にします。

```lua
local view = pico.create("ImageView")
pico.set(view, "w", 200); pico.set(view, "h", 150)
pico.set(view, "path", pico.path_join(pico.app_dir(), "map.pimg"))  -- 読めなければエラー
print(pico.get(view, "image_w"), pico.get(view, "image_h"))  -- 縮めた後の大きさ
```

## MarkdownView

Markdownを整形して表示します。**1つで約40KBのRAMを使う**ので、1つのアプリに1つまでにしてください。

```lua
local md = pico.create("MarkdownView")
pico.set(md, "w", 232); pico.set(md, "h", 260)
pico.text_set(md, "# 見出し\n\n- 項目\n- [リンク](../../display-widgets/next.md/)\n")  -- 8KiBまで
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

動くサンプルは「動作確認」アプリの「追加の部品」(`/lua/apps/動作確認/extra.lua`)にあります。
