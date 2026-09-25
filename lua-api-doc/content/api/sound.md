---
title: "音"
weight: 85
description: "sound_available / beep"
---

音はI2Sのアンプ(MAX98357A)から出ます。**アンプがつながっていない本体でも、以下の関数はエラーにならず普通に呼べます**(音が出ないだけです)。アンプの有無でスクリプトを書き分ける必要はありません。

チップチューンの音源(音色・曲の再生)はまだありません。今あるのは動作確認用の `beep` だけです。

## pico.sound_available

<div class="sig">pico.sound_available() <span class="ret">-> boolean</span></div>

実際に音が出る状態なら `true` を返します。次のどちらかなら `false` です。

- アンプがつながっていない
- `/sys/sound.cfg` で `output = off`(消音)にされている

音だけで知らせると、音が出ない本体では何も起きないように見えます。そういう場面では、この値を見て画面でも知らせてください。

```lua
if not pico.sound_available() then
    pico.set(label, "text", "時間です!")   -- 音の代わりに画面で知らせる
end
pico.beep(880, 500)
```

アンプは実行中に抜き差しできるので、値も途中で変わります。起動時に1回だけ読むのではなく、必要になった時点で読んでください。

## pico.beep

<div class="sig">pico.beep(freq, ms) <span class="ret">-> (なし)</span></div>

矩形波を鳴らします。

| 引数 | 説明 |
|---|---|
| `freq` | 周波数(Hz)。`0` なら鳴っている音を止めるだけ。上限は11025Hz |
| `ms` | 長さ(ミリ秒)。`0` なら止めるだけ。10000(10秒)で頭打ち |

- 前の音が鳴っている間に呼ぶと、前の音を止めて差し替えます(重ねて鳴らすことはできません)。
- 音量は `/sys/sound.cfg` の `volume`(0〜100)に従います。
- アプリを閉じても、鳴らし始めた音は `ms` の長さまで鳴り続けます。
