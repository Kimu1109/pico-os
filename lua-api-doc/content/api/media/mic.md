---
aliases: ["/api/mic/"]
title: "マイク"
weight: 20
description: "mic_start / mic_stop / mic_recording / mic_info / mic_monitor / mic_level / mic_has_signal / mic_set_gain / mic_gain / mic_set_rate / mic_rate"
---

本体のマイクで音を聞き、WAV(16bitモノラル)へ録音します。`loop()` が少し重くても録音は途切れません(`loop()` が約1秒より長く止まると、その分を取りこぼして `mic_info().dropped` に数えます)。

- 録音・メーター・増幅/周波数の変更には **権限 `microphone`**(`app.cfg` の `permission_microphone=true`)が要ります。
  無いと `nil, 理由` が返ります([権限モデル](../../../guide/app/permissions/))。
- 引数の誤り(`.wav` でない録音先、使えない周波数、範囲外の増幅)はエラーになります。
- **アプリを閉じると、そのアプリが始めた録音とメーターは止まり、変えた増幅・周波数は元へ戻ります**(他のアプリの設定は変わりません)。録音は止めた時点までが保存されます。
- 録音中はスリープしません。
- 同時に録れるのは1本だけです(録音アプリで録っている間なども含む)。
- バックグラウンドのスクリプト([バックグラウンド](../../system/background/))からは使えません。
- PC版では本物のマイクの代わりに、440Hzの試験音が入ります(環境変数 `PICOOS_MIC=silence|none|sdl` で無音・信号なし・PCのマイクに変えられます)。
- マイクが付いていない本体では、`mic_has_signal()` が `false` のままです。

## pico.mic_start

<div class="sig">pico.mic_start([path] [, opts]) <span class="ret">-> path | nil, 理由</span></div>

録音を始め、録音先のパスを返します。`path` を省くと、アプリのフォルダの `rec_0001.wav` から空いている番号へ録ります。
`path` は `.wav` で終わること。置ける場所は `pico.sd_write` と同じです(アプリのフォルダの外には `sd_outside_app_dir` の権限が要ります)。フォルダが無ければ作ります。

| `opts` のキー | 説明 |
|---|---|
| `rate` | 録音の周波数 `16000`(既定)/ `24000` / `48000` |
| `gain` | 増幅(dB、`0`〜`36`。既定18)。そのままだと声が小さいので、12〜24あたりが目安 |

失敗の理由: 権限が無い / もう録音している / SDカードが使えない / 書けない場所 / メモリが足りない / ファイルを作れない。

```lua
local p, err = pico.mic_start(nil, {rate = 16000, gain = 18})
if not p then pico.toast("録音できません: " .. err) end
```

## pico.mic_stop

<div class="sig">pico.mic_stop() <span class="ret">-> boolean</span></div>

このアプリが始めた録音を止めます。止めたら `true`。残りをSDへ書いてファイルを閉じるまで少しかかり、
その間は `pico.mic_recording()` が `true`(`mic_info().finishing` も `true`)のままです。

## pico.mic_recording

<div class="sig">pico.mic_recording() <span class="ret">-> boolean</span></div>

録音中か(止めた後に書き終えるまでの間も `true`)。

## pico.mic_info

<div class="sig">pico.mic_info() <span class="ret">-> table</span></div>

| キー | 説明 |
|---|---|
| `recording` / `finishing` | 録音中か / 止めて書き終える途中か |
| `path` | 録音中/直前に録ったファイル |
| `ms` | 書き終えた長さ(ミリ秒) |
| `dropped` / `overruns` | 取りこぼしたサンプル数 / 受け取りが溢れた回数(この録音で) |
| `rate` / `gain` | 今の周波数 / 増幅(dB) |
| `input` | マイクの入力の状態 `"off"` `"starting"` `"running"` `"failed"`(始められなかった) |
| `signal` | マイクから値が来ているか(つながっていないと値が変わらないので `false`) |
| `stop_reason` | 直前の録音が途中で止まった理由(SDへ書けない等。普通に止めたなら `"止めました"`)。録音したことが無ければ無い |

## pico.mic_monitor

<div class="sig">pico.mic_monitor(on) <span class="ret">-> true | nil, 理由</span></div>

録音せずにマイクを聞きます(メーター用)。`on` は真偽値。録音中は止めても録音は続きます。

## pico.mic_level

<div class="sig">pico.mic_level() <span class="ret">-> number, number</span></div>

前に呼んでから一番大きかった音の振幅(`0`〜`1`)と、そのdBFS(`0`が最大、無音は`-96`)。読むと0へ戻るので、
`pico.every(50, ...)` などで決まった間隔で呼んでください。聞いていない(メーターも録音もしていない)ときは `0, -96`。

```lua
pico.mic_monitor(true)
pico.every(50, function()
    local v, db = pico.mic_level()
    level = math.max(0, (db + 60) / 60)   -- -60〜0dBFS を 0〜1 に
    pico.invalidate(meter)
end)
```

## pico.mic_has_signal

<div class="sig">pico.mic_has_signal() <span class="ret">-> boolean</span></div>

マイクから値が来ているか。配線が外れている・電源が無いと、値が0.5秒変わらず `false` になります。聞いていなければ `false`。

## pico.mic_set_gain / pico.mic_gain

<div class="sig">pico.mic_set_gain(db) <span class="ret">-> true | nil, 理由</span></div>
<div class="sig">pico.mic_gain() <span class="ret">-> integer</span></div>

増幅(dB)を変える / 読む。`0`〜`36` へ丸めます。録音中でも変えられます。アプリを閉じると元へ戻ります。

## pico.mic_set_rate / pico.mic_rate

<div class="sig">pico.mic_set_rate(rate) <span class="ret">-> true | nil, 理由</span></div>
<div class="sig">pico.mic_rate() <span class="ret">-> integer</span></div>

録音の周波数 `16000` / `24000` / `48000` を変える / 読む。録音中は変えられません(`nil, 理由`)。アプリを閉じると元へ戻ります。

## 例

「動作確認」アプリの「マイク」のページ(`/lua/apps/動作確認/mic.lua`)が、メーター・録音・再生(`pico.wav_play`)・増幅/周波数の切り替えを1画面で試せます。
