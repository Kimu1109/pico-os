---
title: "音"
weight: 85
description: "sound_play / sound_stop / sound_playing / note_freq / beep / sound_available / music_play / music_play_text / music_stop / music_playing"
---

音はI2Sのアンプ(MAX98357A)から出ます。**アンプがつながっていない本体でも、以下の関数はエラーにならず普通に呼べます**(音が出ないだけで、音の長さや「鳴っているか」は時間どおりに進みます)。アンプの有無でスクリプトを書き分ける必要はありません。

音源はゲームボーイ風のチップチューンで、**チャンネルが4つ**あります(`1`〜`4`)。各チャンネルは同時に1音だけ鳴らせ、どのチャンネルでもどの波形でも使えます。音は2コア目で作られるので、`loop()`が重い処理をしていても途切れません。

アプリを閉じると、そのアプリが鳴らしていた音は全部止まります。

## pico.sound_play

<div class="sig">pico.sound_play(ch, freq, ms [, opts]) <span class="ret">-> boolean</span></div>

チャンネル `ch` で音を鳴らします。そのチャンネルで鳴っていた音は止めて差し替えます。

| 引数 | 説明 |
|---|---|
| `ch` | チャンネル `1`〜`4`。範囲外はエラー |
| `freq` | 周波数(Hz、小数可)。`0` なら止めるだけ(休符)。上限は11025Hz。ノイズでは「ザー」の粗さ(大きいほど細かい。〜22050) |
| `ms` | 長さ(ミリ秒)。`0` なら `pico.sound_stop()` するまで鳴り続ける。60000で頭打ち |
| `opts` | 省略可。下の表 |

| `opts` のキー | 既定 | 説明 |
|---|---|---|
| `wave` | `"pulse50"` | 波形。`"pulse12"` `"pulse25"` `"pulse50"` `"pulse75"`(矩形波。数字はデューティ%)、`"triangle"`(三角波)、`"saw"`(のこぎり波)、`"noise"`(ノイズ)、`"noise_short"`(周期の短い金属的なノイズ)。不明な名前はエラー |
| `volume` | `15` | 鳴り始めの音量 `0`〜`15` |
| `envelope` | `0` | 音量の変化。`0`=一定、`-1`〜`-7`=`|envelope|`/64秒ごとに1段下げる(0になったら音が終わる)、`1`〜`7`=1段ずつ上げる |

戻り値は、要求を受け付けたら `true`。1フレームに大量に呼んで要求の列(32件)があふれると `false` です(その要求は捨てられます)。

```lua
-- ラ(440Hz)を0.5秒
pico.sound_play(1, 440, 500)

-- 音名から周波数を引き、三角波のベースを止めるまで鳴らす
pico.sound_play(3, pico.note_freq("C2"), 0, { wave = "triangle" })

-- ドラム(ノイズを素早く減衰させる)
pico.sound_play(4, 600, 120, { wave = "noise", volume = 13, envelope = -1 })
```

全体の音量(`/sys/sound.cfg` の `volume`)が最後に掛かります。4チャンネルを最大の音量で同時に鳴らしても音割れしないようにしてあります。

## pico.sound_stop

<div class="sig">pico.sound_stop([ch]) <span class="ret">-> (なし)</span></div>

チャンネル `ch` の音を止めます。`ch` を省略すると全チャンネルを止めます。

曲(`pico.music_play`)が鳴っている間は、**効果音だけ**を止めます(曲の音は止めません)。曲を止めるのは `pico.music_stop()` です。

## pico.sound_playing

<div class="sig">pico.sound_playing([ch]) <span class="ret">-> boolean</span></div>

チャンネル `ch` が鳴っていれば `true`。`ch` を省略すると、どれか1つでも鳴っていれば `true` です(曲の音も含みます)。長さを指定した音や、減衰して消えた音は、終わった時点で `false` になります。

`ch` を指定した場合は音源が最後に知らせた状態なので、`pico.sound_play()` の直後だけは数ミリ秒遅れて `true` になることがあります(`ch` を省略した場合は直後から `true`)。

## pico.note_freq

<div class="sig">pico.note_freq(note) <span class="ret">-> number | nil</span></div>

音名またはMIDIノート番号から周波数(Hz)を返します(平均律、A4 = 440Hz)。読めなければ `nil`。

| 書き方 | 例 |
|---|---|
| 音名 | `"C4"`(真ん中のド)、`"A#3"`、`"Eb5"`。大文字小文字は問わない。オクターブは `-1`〜`9` |
| ノート番号 | `60`(= C4)、`69`(= A4)。`0`〜`127` |

## pico.beep

<div class="sig">pico.beep(freq, ms) <span class="ret">-> (なし)</span></div>

**チャンネル1**で矩形波(`pulse50`、音量15)を鳴らす簡易版です。`pico.sound_play(1, freq, ms)` とほぼ同じで、長さは10000(10秒)で頭打ちです。`freq` か `ms` が `0` ならチャンネル1を止めます。

## pico.sound_available

<div class="sig">pico.sound_available() <span class="ret">-> boolean</span></div>

実際に音が出る状態なら `true` を返します。次のどちらかなら `false` です。

- アンプがつながっていない
- `/sys/sound.cfg` で `output = off`(消音)にされている

音だけで知らせると、音が出ない本体では何も起きないように見えます。そういう場面では、この値を見て画面でも知らせてください。アンプは実行中に抜き差しできるので、値も途中で変わります。

```lua
if not pico.sound_available() then
    pico.set(label, "text", "時間です!")   -- 音の代わりに画面で知らせる
end
pico.beep(880, 500)
```

## 曲を鳴らす(pico-os MML)

曲は **MML(テキスト)** で書き、`pico.music_play()` で鳴らします。書き方は [`MUSIC_FORMAT.md`](https://github.com/Kimu1109/pico-os/blob/main/MUSIC_FORMAT.md) を見てください。MIDIファイルは、PCで `python3 script/midi2mml.py song.mid -o song.mml` を実行するとMMLの下書きに変換できます。曲は2コア目で鳴るので、`loop()` が重くてもテンポは揺れません。

```
; demo.mml
#title デモ
#tempo 150
A @pulse25 v12 E-4 o5 l8  L c e g e  c e g4
C @triangle        o3 l4  L c   c    g   g
```

### pico.music_play

<div class="sig">pico.music_play(path) <span class="ret">-> true | nil, err</span></div>

SDの `.mml` を読んで鳴らします。鳴っている曲は差し替えます。読めなければ `nil` と理由(`"3行12列: v の後ろは0〜15です"` のような、行・列つきの文字列)を返し、鳴っている曲はそのまま鳴り続けます。

SDの権限に従います。`sd_outside_app_dir` の無いアプリは、自分のフォルダの中の曲だけを鳴らせます。

```lua
local ok, err = pico.music_play("/lua/apps/わたしのゲーム/bgm.mml")
if not ok then pico.show_error("BGMを読めません\n" .. err) end
```

### pico.music_play_text

<div class="sig">pico.music_play_text(mml) <span class="ret">-> true | nil, err</span></div>

文字列に書いたMMLをそのまま鳴らします(アプリに短い曲を埋め込む用)。戻り値は `pico.music_play` と同じです。

```lua
pico.music_play_text([[
A @pulse50 v12 E-3 o5 l16 c e g > c
]])
```

### pico.music_stop

<div class="sig">pico.music_stop() <span class="ret">-> (なし)</span></div>

曲を止めます。効果音は止めません。

### pico.music_playing

<div class="sig">pico.music_playing() <span class="ret">-> boolean</span></div>

曲が鳴っていれば `true`。`pico.music_play()` / `pico.music_stop()` の直後から、その結果の値を返します。`L`(ループ位置)の無い曲は、最後まで行くと `false` になります。

### 効果音との同居

曲を鳴らしている間に `pico.sound_play()` や `pico.beep()` を呼ぶと、**効果音がそのチャンネルを借ります**。借りている間、曲のそのチャンネルは黙りますが進行は止まらず、効果音が終わったら次の音符から曲へ戻ります。BGMと効果音を同時に使うゲームでは、効果音を曲があまり使わないチャンネル(例えば4)で鳴らすと、曲が途切れにくくなります。

アプリを閉じると、曲も効果音も止まります。

## 例: 1拍ずつ鳴らす

曲の形式を使わずに、`loop(dt)` で時間を数えて1拍ずつ `pico.sound_play()` することもできます(自動で作る音や、画面の動きに合わせる音向け)。こちらは1コア目で数えるので、`loop()` が重いとテンポが揺れます。

```lua
local STEP_MS = 150
local melody = { "C5", "E5", "G5", "E5" }
local step, elapsed = 0, 0

function loop(dt)
    elapsed = elapsed + dt
    while elapsed >= STEP_MS do
        elapsed = elapsed - STEP_MS
        step = step % #melody + 1
        pico.sound_play(1, pico.note_freq(melody[step]), STEP_MS - 20,
                        { wave = "pulse25", envelope = -4 })
    end
end
```
