# 音: 音声出力・ステレオ・出力の周波数・イヤホン

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `sound.md` から節ごとに分けた(目次は同じフォルダの `README.md`)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

## 音声出力 (`src/hal/Sound_Functions` / `src/sound/`) (2026-09-25)

SUMMARY.md #11。**出力の土台 + チップチューン音源(既定8チャンネル。2026-09-28に4→任意数対応)+ 2コア目での合成**。曲(MML)は下の「曲データ」、GBエミュの音は「ゲームボーイの音」。

- **配線はI2SのD級アンプ MAX98357A**(ピンは上の「ハードウェア構成」)。PWM+RCフィルタも検討したが、
  3.3Vの電源ノイズ(Wi-FiとSPIの液晶が同じ基板で動いている)がそのまま音に乗るため見送った。
  アンプの電源は**3V3ではなく5V**(スピーカーの電流の揺れを液晶/Wi-Fiの電源へ乗せないため)。
  - arduino-picoの`I2S`はPIOで動き、**LRCLKはBCLK+1に固定**。GP14/15はGP15がSDのCSなので使えず、
    ADCの使えるGP26〜28は外部コントローラー(#10)用に残して GP2/3/4 にした。
- **I2Sは一方通行なので、アンプの有無は信号線からは分からない**。検出用に1本(GP5)足し、アンプ側でGNDへ落とす。
  内部プルアップで読んで LOW=接続。MAX98357AのSD端子の電圧で見分ける案は、チップ内部の100kΩと基板の1MΩの
  分圧で刺さっていても外れていてもLOW付近になるため使えない。
  - 100msごとに読み、**3回続けて同じ値のときだけ採用**(抜き差しの瞬間のばたつき)。起動時だけは待たずに採用する。

**2つのコアの分担**(`Sound_Functions.cpp`の中で「共有」「1コア目だけ」「2コア目だけ」の3区画に分けてある):

| | 1コア目(`loop()` → `Update()`) | 2コア目(`loop1()` → `LoopCore1()`) |
|---|---|---|
| 持つもの | 検出の状態・`output`・ログ | `I2S`・`ChipSynth::Engine`・書き込み前の一時置き場(64サンプル) |
| 仕事 | ピンを読んで`want_run`を出す、`Play()/Stop()`をコマンドの列へ積む、2コア目の状態をログへ出す | コマンドを取り出して音源へ渡す、I2Sの開始/終了、波形を作ってI2Sへ書く、未接続の間は時間で進める |

- やり取りは**`std::atomic`だけ**(ロック無し)。1コア目→2コア目: `want_run`/`retry_epoch`/`master_volume`/コマンドの列(32件の固定長リング、
  1対1なので`head`/`tail`の2つのatomicで足りる。溢れたら捨てて`DroppedCommands()`で数え、1コア目がWARNを出す)。
  2コア目→1コア目: `core1_running`/`core1_failed`/`core1_active`(鳴っているチャンネル)/`core1_processed`(音源へ渡し終えた数)。
- **I2Sの`begin()`/`end()`は2コア目で呼ぶ**(DMA割り込みが呼んだコアに付くため)。休止端子(GP6)も2コア目が上げ下げする。
- **2コア目はログを出さない**(`LogFunctions`はロックを持たない)。I2Sの開始/停止/失敗は`core1_*`を1コア目が見てログへ出す。
- `IsPlaying()`は「積んだ数 != 渡し終えた数 なら鳴っている扱い、追いついていれば`core1_active`」。2コア目は
  `core1_active`→`core1_processed`の順に書くので、追いついた後に読むチャンネルは必ずその後の状態。
  `Play()`した直後から`true`になる(チャンネル単位の`ActiveChannels()`は2コア目が受け取るまで数ms遅れる)。
- 1コア目の`Setup()`が済むまで(`ready`)2コア目は何もしない。arduino-picoは`setup()`と`setup1()`を同時に走らせるため。
- `loop1()`は仕事が無ければ(I2Sのバッファが満杯、または未接続で何も鳴っていない)`delay(1)`で休む。
- **I2Sのバッファは64ワード×8本 = 512サンプル ≒ 23ms / 2KB**(22050Hz。44100Hzでは128ワード×8本=4KB)(2コア目が専任で流すので短くした。要求から音が出るまでの遅れもこの程度)。
  鳴らせる間だけ持ち、未接続や`output = off`の間は`end()`で返す。`begin()`に失敗したら刺し直すまで試し直さない。
- **つながっていないときは「鳴らさないだけ」**: 2コア目が`millis()`の差の分だけ音源を空回し(`render(nullptr, n)`)して進めるので、
  長さ/エンベロープは時間どおりに進み、**途中で刺すとその時点の続きから鳴る**。鳴らせる間は**I2Sが引き取ったサンプル数**が時計。
  I2Sを開始する回も、開始する時刻までの分を先に進めてから開始する(逆にすると最後の1回分が抜ける。テストで踏んだ)。

**音源(`src/sound/Chip_Synth`)**:
- **チャンネル数は`ChipSynth::kChannels`(既定8。2026-09-28に4→8。「任意チャンネル数対応」参照)**、22050Hz(既定)か44100Hz(下の「出力の周波数を選ぶ」)、
  モノラル(I2Sへは左右同値で送る)。ゲームボーイのAPUが手本だが**チャンネルごとの波形は固定しない**:
  矩形(デューティ12.5/25/50/75%)・三角・のこぎり・ノイズ(15bitのLFSR)・短いノイズ(7bit、127段で一巡)をどのチャンネルでも選べる。
- 1音 = `ChipSynth::Note{wave, freq_x16(Hzの16倍), volume 0〜15, envelope -7〜7, length_ms(0=止めるまで)}`。
  エンベロープはゲームボーイと同じ「|env|/64秒ごとに1段上げ下げ」だけで、下げて0になったら音が終わる。
- 位相は32bit(2^32で1周期)。ノイズだけは「1サンプルで何段進めるか」を16.16で持つ(1サンプルに1段が上限)。
  32bitの位相の増分を`0xFFFFFFFF`で頭打ちにする書き方だと最初の1段がずれて周期が崩れた(テストで踏んだ)。
- 振幅: 1チャンネルの最大は`kChannelAmplitude = 7800`(音量15・全体100)。**`kHeadroomChannels`(=4)個までなら
  最大で鳴らしても16bitに収まる**。それを超えて同時に鳴らすと、超えた比率ぶん全体を下げて歪みを防ぐ(2026-09-28。
  詳細は「任意チャンネル数対応」参照)。全体の音量(`sound.cfg`の`volume`、既定50)が最後に掛かる。
- 状態は全て固定長の配列で、確保は一切しない。帯域制限はしない(高い音は折り返しで濁るが、チップチューンの味の内)。
- `sound/Note_Name.hpp`: 音名("C4" "A#3" "Eb5")/MIDIノート番号 → 周波数(平均律、A4=440Hz)。

**外から使う口**:
- C++: `SoundFunctions::Play(ch, Note)`/`Stop(ch)`/`StopAll()`/`Beep(freq, ms)`(ch0の矩形波)/`IsPlaying()`/`ActiveChannels()`。
  Luaの`pico.beep`も同じ(「動作確認」アプリの「基本の部品」の「テスト音」)。
- Lua: `pico.sound_play(ch 1〜kChannels, freq, ms, {wave=, volume=, envelope=})`/`sound_stop([ch])`/`sound_playing([ch])`/`note_freq(音名|番号)`/
  `beep(freq, ms)`/`sound_available()`。**チャンネルはLuaでは1始まり**。音を使った`LuaEngine`は壊れるとき(=アプリを閉じるとき)に`StopAll()`する
  (長さ0の音が鳴り止まなくなるため)。
- 動作確認は「動作確認」アプリの「音」(`pico-os-apps/apps/api-test/app/sound.lua`): 1オクターブの鍵盤(Canvas 1枚 + `pico.get_touch()`)、
  波形/減衰の切り替え、デモ曲(同じフォルダの`demo.mml`を`pico.music_play`で鳴らす。鍵盤の音は効果音として曲のチャンネル1を借りる)。
- 曲(MML)は下の「曲データ」。

**設定・表示**:
- `/sys/sound.cfg`(無くてよい): `output = auto | off`、`volume = 0〜100`(既定50)。`SetOutput()`/`SetVolume()`は今だけ切り替える。
  音量は設定アプリ(`SettingsScene`)の`NumberSlider`でも変えられる(ドラッグ中は`SetVolume()`だけ、指を離したら`sound.cfg`へ書いて確認音)。
- ステータスバー: 鳴らせる=スピーカー、`off`=消音のスピーカー、未接続=スピーカー+赤のX(SD/Wi-Fiと同じ組み立て方)。
  `Active`になるのは**2コア目がI2Sを開始した後**(1コア目が接続を採用しただけではまだ`Disconnected`)。

**PC/Webビルド**:
- `pc/compat/I2S.h`がSDLの音声出力で置き換える(`src/`は同じ)。アンプの検出ピンは`pc/compat/Arduino.h`の
  `PicoPcGpio::read_hook`で代わりに答える(音声デバイスを開けたら「刺さっている」)。`/sys/sound.cfg`の
  `pc-sound-state = auto | connected | disconnected`か環境変数`PICOOS_SOUND_STATE`(Webは`?sound=`)で固定できる。
- **2コア目の代わり**: ネイティブは`pc/main_pc.cpp`が別スレッドで`setup1()`→`loop1()`を回し続ける(終了時は止めて`join`)。
  Webはスレッドが無いので、フレームごとに`loop()`の後で`loop1()`を1回呼ぶ(1回で最大512サンプル書けるので60fpsで足りる。
  フレームのぶれで途切れないよう、Webだけリングを2048サンプルにしてある)。
- ヘッドレスなら`SDL_AUDIODRIVER=disk`で`SDL_DISKAUDIOFILE`へ生の音(出力の周波数/16bit/ステレオ)を書き出せるので、波形を数値で確かめられる。
  Webはブラウザが利用者の操作までAudioContextを止めるので、`shell.html`が最初の操作で`resume()`する。

**検証**: `sound_test`(run.sh。前半は音源: デューティ比・三角/のこぎりの形・ノイズの周期(127/32767)・長さ・エンベロープ・4ch足し合わせ・音名。
後半はSoundFunctionsで、1コア目と2コア目を1本のスレッドで交互に呼ぶ: 検出のばたつき、I2Sの開始は2コア目、コマンドの列の溢れ、
抜き差しで続きから鳴る、sound.cfg、begin()失敗)、`lua_engine_test`(Lua API)。PCビルドで`SDL_AUDIODRIVER=disk`の出力を確認
(テスト音: 300ms・約880Hz、デモ曲: 4ch足して最大約12000で音割れなし)。
**実機(arduino-picoのI2S・2コア目・MAX98357A)で確認済み(2026-10-22)**(音が出る・アンプの抜き差しを検出する)。**Webビルドも未確認**(emsdkが無い)。

## ステレオ出力とパン (`stereo` 引数 / `ChipSynth::Note::pan` / MMLの`p`) (2026-10-14)

2コア目の出力段は左右交互(L,R,L,R…)のバッファで、I2Sへは左を上位16bit・右を下位16bitに詰めて書く(以前は同じ値を両方へ)。
- 音を作る関数(`Engine::render` / `MusicPlayer::render` / `GbAudioLink::render` / `GbApu::renderAdd` / `WavStream::renderAdd`)は
  `bool stereo = false`を取る。省略はモノラル(今までどおり)で、`stereo=true`なら`out`は左右交互で`n*2`個。
- `GbApu`はステレオのとき、NR51の振り分け・NR50の左右の音量をそのまま左右に出す(片側だけでもモノラルの和と同じ大きさになるよう2倍。最大振幅は変わらない)。
- チップチューン: `Note::pan`(-8左〜0中央〜8右、`kPanMax`)。中央は左右とも全開で反対側を線形に絞る。MMLは`p n`(`Op::Pan`)、Luaは`pico.sound_play`の`pan`。
  モノラルの出力では無視。
- **WAVのステレオ**: `WavDecoder::setStereoOutput(true)`(`SoundFunctions::WavPlay()`が常に立てる)で、2ch以上のファイルは偶数番目のチャンネルの平均=左・
  奇数番目の平均=右の左右交互で出す(2chはそのまま。1chはモノラルのまま。立てなければ従来の全チャンネル平均のモノラル)。周波数の変換とフィルタは左右それぞれに掛ける。
  `WavStream`の列は24KB→32KB(`kRingSamples`=16384サンプル。head/tailはint16の数で数え、ステレオは左右の組)。先読みはモノラル44100Hzで約370ms・ステレオ約190ms。
  形は`setStereo()`で決め(flushを兼ねる)、2コア目は次のflushで取り込む。位置・長さ・`kWavMaxPerUpdate`/`kWavPrefillSamples`はフレームで数える。
  モノラルのWAVはステレオ出力の左右へ同じ値を足す。
- 検証: `gb_apu_test` `sound_test` `music_test`(`TestPan`) `lua_engine_test`。実機のPCM5102Aで鳴ること(2026-10-19)・2コア目の負荷で途切れないこと(2026-10-22)は確認済み。ステレオの定位の聞こえ方は記録が無い。

## 出力の周波数を選ぶ(22050 / 44100Hz、`SoundFunctions::SetSampleRate()` / PCM5102A) (2026-10-10)

(2026-11-06に48000Hzを足した。下の「48kHzの出力と、ミュージックが曲に合わせて周波数を選ぶ」)

外付けのDACにライン出力のPCM5102Aを使えるよう、出力の周波数を**22050Hz(既定)と44100Hzから実行中に選べる**ようにした。
44100Hzなら44.1kHzのWAVは変換せずそのまま鳴る(22050Hzでは間引くので11kHzより上は出ない)。

- **選び方**: `/sys/sound.cfg`の`sample-rate = 22050|44100`(無ければ・読めない値なら22050)、設定アプリの「音」→「音質」(22kHz/44.1kHz。
  変えるとすぐ反映し、`sound.cfg`へ書いて確認音を鳴らす)。C++は`SoundFunctions::SampleRate()`/`SetSampleRate(rate)`(それ以外の値はfalse)。
  既定を22050Hzにしたのは、2コア目の計算が約半分で、今のMAX98357A+スピーカーでは違いが分からないため。
- **切り替えの流れ**: 1コア目の`SetSampleRate()`がWAV・曲・効果音を止めてから、コマンドの列へ`SetRate`を積む(止める要求との順番が列で保たれる)。
  2コア目は受け取ったら`ApplyRate()`で音源(`ChipSynth::Engine`/`GbApu`/`MusicPlayer`)を新しい周波数で作り直し、GBの列(`GbAudioLink::setRate()`。
  フレームの中の位置を換算、音源の中身は消えるのでゲームが次に書くまで鳴らない)を合わせ、動いているI2Sを止める→すぐ新しい周波数・バッファで開き直す。
  1コア目の周波数(`out_rate`)はWAVの読み取り(`WavDecoder::open()`の出力の周波数)・WAVの位置/シークの換算・GBの列を作るときに使う。
- **画面ごとの要求(2026-10-10)**: `Scene::preferredSampleRate()`(既定0=設定のまま)を`main.cpp`の`loop()`が`SceneFunctions::Update()`の直後に毎フレーム
  `SoundFunctions::RequestSampleRate()`へ渡す。要求(`requested_rate`)は設定(`configured_rate`)より優先し、0で取り下げると設定へ戻る(設定は書き換えない)。
  変わったときだけ`SetSampleRate()`と同じ切り替えをする(同じ値なら毎フレーム呼んでも何もしない)。**ミュージックアプリは44100Hz**を返し、
  `onEnter()`の頭でも`RequestSampleRate()`する(その画面の最初のフレームで鳴らす曲が切り替えで止まらないように)。
  Luaは`pico.sound_rate()`/`pico.sound_set_rate(44100|22050|nil)`(`LuaEngine::requested_rate_`。`LuaScene::preferredSampleRate()`がそれを返し、
  `push_scene`/`change_scene`で開いた画面へ引き継ぐ=`LuaScene::setInheritedSampleRate()`)。アプリを閉じると次の画面の要求(ランチャは0)で設定へ戻る。
  確認: `sound_test`(要求の優先・取り下げ・同じ要求で開き直さない)、`lua_engine_test`、PCビルドでミュージックアプリを開くと44100Hz・「戻る」で22050Hzのログ。
- **周波数に比例させたもの**: I2Sのバッファ(1本64/128ワード×8本=どちらも約23ms、2KB/4KB。`kBufferWordsLow/High`)、
  GBの直流を落とす係数(コンストラクタで`0.999958^(4194304/周波数)`。22050Hzで約0.992、44100Hzで約0.996)。
- **WAVの列は24KB(12288サンプル)**: 44100Hzで約280ms、22050Hzで約560ms先読みできる。`kWavMaxPerUpdate`=2048、`kWavPrefillSamples`=8192。
  2のべき乗でないので、head/tail は 0〜`kWrap`-1(`kRingSamples`の倍数で2^31に近いもの)を巡る数で持つ(`Dist`/`Add`/`Ahead`)。
  捨てる位置(`flush_to_`)は古いまま残ると、tail が巡る範囲の半分を進んだときに「先にある」と取り違えるので、`effectiveTail()`が
  2コア目に追い越されていたら tail へ寄せ直す。
- PC/Web(`pc/compat/I2S.h`): `begin(rate)`のたびにその周波数で音声デバイスを開く。Webのリングの下限は4096サンプル(44100Hzで約93ms)。
- **2コア目の計算は44100Hzでほぼ倍**(音源8チャンネル+GBのAPU+WAVの足し合わせ)。実機では22050Hz/44100Hzとも途切れない(2026-10-22。負荷の数値は未計測)。
  MMLの曲の長さを数える`MusicPlayer::MeasureMs()`は22050Hzで数えたまま(ミリ秒で返すので周波数に依らない)。
- **PCM5102Aの配線はMAX98357Aと同じ端子**(BCK=GP2・LRCK=GP3・DIN=GP4)。基板の SCK を GND(内部のPLLでBCKから作る)、FMT=GND(I2S)、
  XSMT=3.3V(または GP6。休止端子と同じ向きで HIGH=鳴らす。MAX98357Aと並べてイヤホン用にするなら GP14。下の「イヤホンとスピーカーの切り替え」)、FLT/DEMP=GND。**アンプの検出(GP5をGNDへ落とす)は基板側に無いので、GP5をGNDへ配線すること**
  (しないと「アンプ未接続」としてI2Sを動かさない)。
- **`sound.cfg`の`battery-cap = false`**(設定アプリの「電池で音量を絞る」): 電池駆動中に音量を20%で頭打ちする対策(下の「バッテリー駆動中の音割れ対策」)は
  MAX98357A(VINがVSYS)のためのもので、ライン出力のPCM5102Aには要らない。既定は`true`のまま。`SoundFunctions::GetBatteryCap()/SetBatteryCap()`。
- 検証: ホストテスト全部。`sound_test`(切り替えでI2Sを開き直す・バッファの大きさ・鳴っていた音が止まる・同じ周波数なら何もしない・両方の周波数で441Hzの高さ・
  44100Hzで長さを数える・`sample-rate`の読み込みと読めない値・`battery-cap`)、`wav_test`(SoundFunctions経由のテストを22050Hzと44100Hzの両方で。
  列が何周も巡る・2コア目が止まったまま積み直す)、`gb_apu_test`(44100Hzの高さと直流の戻る速さ。係数を定数のままにすると失敗することを確かめた)。
  PCビルド: 設定アプリで「音質」を44.1kHzへ→`sound.cfg`に書かれ、再起動しても44100Hzで始まる。44.1kHz・15kHzのWAVをミュージックアプリから鳴らし
  `SDL_AUDIODRIVER=disk`の出力が15kHzのまま。実機・PCM5102Aの44.1kHzで鳴ることを確認済み(2026-10-19)。

## 48kHzの出力と、ミュージックが曲に合わせて周波数を選ぶ (`SoundFunctions::kSampleRate48k` / `NativeOutputRate()` / `MusicService::PreferredSampleRate()`) (2026-11-06)

上の「出力の周波数を選ぶ」に3つ目の**48000Hz**を足した。48kHz系の曲(48kHz・96kHz。動画から取り出した音やハイレゾに多い)を、44.1kHzへ間引かず
整数比のまま鳴らすため(96kHzは半分に下げるだけ)。`sound/playback.md`「88.2/96kHzの曲」と一緒に入れた。

- **選べる値**: `SoundFunctions::IsValidSampleRate()` = 22050 / 44100 / 48000(`kSampleRateLow` / `kSampleRateHigh` / `kSampleRate48k`)。`sound.cfg` の
  `sample-rate`、設定アプリの「音質」(22kHz / 44.1kHz / 48kHz)、`RequestSampleRate()`、Luaの `pico.sound_set_rate(48000)` が受け付ける(空の実装 `Sound_Functions_Off.cpp` も同じ)。
  I2Sのバッファは44100Hzと同じ128ワード×8本(48000Hzで約21ms)。MAX98357A(8〜96kHz)・PCM5102A(8〜384kHz)とも48kHzに対応する。
  設定アプリの「音質」の表は `SettingsScene.cpp` にだけ置く(`SettingsScene_Other/Time/Wifi.cpp` にあった使われていない写しは消した)。
- **曲に合う周波数**: `SoundFunctions::NativeOutputRate(元の周波数)` が、8000で割り切れて11025で割り切れないもの(8k/16k/24k/32k/48k/96k/192k)は48000、
  ほか(44.1k系・不明)は44100を返す。
- **ミュージックは曲ごとに選ぶ**: `MusicService` が望む周波数(`want_rate`、最初は44100)を持ち、`MusicScene::preferredSampleRate()` と
  バックグラウンドの仕事の `preferredSampleRate()` がそれを返す(前は44100Hz固定)。WAV/MP3/FLAC/ALACを鳴らすとき(`TryPlay()`)、
  開いて分かった元の周波数(`WavInfo::sample_rate`)の系統が今の出力と違えば、`want_rate` を変えて `RequestSampleRate()` で切り替え、**もう一度開いて鳴らす**
  (切り替えは鳴らし始めた音を止めるため。曲の周波数は開くまで分からないので、系統が変わるときだけ2回開く)。
  変えてよいのは、今の要求が0か `want_rate`(=ミュージックの画面かバックグラウンドの要求)のときだけ。他の画面が周波数を決めていれば(Luaの
  `pico.sound_set_rate(22050)` 等)それに従い、曲を変換して鳴らす。ただし他の画面がたまたま44100/48000Hzを望んでいると見分けられず、ミュージックが
  切り替えた次のフレームでその画面の要求に戻され、曲が止まった扱いになりうる(バックグラウンドの曲とLuaアプリの要求が重なったときだけ)。
- **曲と曲の間を空けない(`sound/playback.md`「曲と曲の間を空けない」)との関係**: 次の曲を続けて積む(`ChainNext()`)とき、**今の曲を変換せずに鳴らしていて
  (出力 = 今の曲の `NativeOutputRate()`)、次の曲の系統が違えば積まない**(ログ「次の曲は周波数の系統が違う…ので、間を空けて鳴らします」)。
  そのまま積むと次の曲は変換を通ってしまうため。積まなければ、ミュージックが曲の終わりを見て次の曲を鳴らし、そのとき周波数を切り替える
  (44.1kHz系と48kHz系をまたぐ所だけ、切り替えの分の短い間が空く)。同じ系統同士、または出力がどちらの系統でもない(22050Hz)ときは今までどおり続けて積む。
  読み取り係が今読んでいる曲の元の周波数は `decoder_src_rate`(`WavPlay`・続けて積んだとき・シークで開き直したときに覚える)。
- **2コア目の計算**: 48000Hzは44100Hzの約1.09倍。44100Hzで実機は途切れない(上の節)ので余裕の範囲の見込み。**実機では未確認(48kHzで鳴るか)(Todoist参照)**。
- 検証: ホストテスト。`sound_test`(48000Hzへ切り替えて大きいバッファで開き直す・480Hzの高さ・`sample-rate = 48000` を読む・32000/96000は断る・
  `NativeOutputRate()` の値は static_assert)、`lua_engine_test`(`pico.sound_set_rate(48000)`・32000はエラー)、
  `flac_test`(44100Hzで44.1kHzの曲の次に96kHzを予約すると続けず終わる・48000Hzで48kHzのWAV→96kHzのFLACは続けて移り1曲目は変換されない・
  22050Hzなら系統をまたいでも続けて移る・ミュージックで44.1kHz→96kHz→44.1kHz(MP3)の並びを鳴らすと44100→48000→44100Hzと切り替わり止まった扱いにならない・
  他の画面が22050Hzを望んでいれば切り替えない)。

## 24bitの出力 (`PICO_HW_SOUND_24BIT` / `SoundFunctions::SetBitDepth()` / `PcmSample`) (2026-11-07)

利用者の要望: ハイレゾの見積もり(`sound/playback.md`「88.2/96kHzの曲」の前置き)の段階3(出力を88.2/96kHzにする)は飛ばし、段階4(24bit)を先に。
**24bitは切り替えられるように、通常のビルドでは16bit固定**に。

- **ビルド時の旗 `PICO_HW_SOUND_24BIT`**(`config/Hardware_Config.hpp`、既定0。部品は `buildcfg/options.ini` の `opt_sound_24bit`)。
  **音楽再生に特化した版(`env:rpipico2w_music`)だけ1**。0の版は今までと同じ16bitの経路だけが入る(標本の型は `int16_t` のまま。
  `SetBitDepth(24)` は false、設定アプリに行は出ない、`sound.cfg` の `bit-depth = 24` はログを出して16bitで鳴らす)。
- **実行時の切り替え**: `sound.cfg` の `bit-depth = 16|24`(既定16)、設定アプリの音タブ「ビット深度」(16bit / 24bit。出力先の下。
  1の版だけ)。C++は `SoundFunctions::BitDepth()` / `SetBitDepth(bits)` / `IsBitDepthSupported(bits)`。既定を16bitにしたのは、実機で確かめていないため。
  切り替えは周波数の切り替えと同じ流れ: 1コア目がWAV・曲・効果音を止め(列の中の値の桁が変わるので先読みは捨てる)、`playback_serial` を進め
  (ミュージックは止まった扱い)、コマンドの列へ `SetBits` を積む。2コア目は `ApplyBits()` でI2Sを止めて新しいスロットの幅で開き直す。
- **経路**(1の版):
  - 標本の型 `PcmSample`(`sound/Pcm_Sample.hpp`)が `int32_t` になる。読み取り係(`PcmFileDecoder::setWideOutput(true)`)が**24bitの桁(±8388607)**で出す:
    FLAC/ALAC は24bitまでの下位ビットを残し(`PcmWide::FromBits()`。16bitの桁へは今までどおり四捨五入)、WAVは24/32bitの上位24bitと浮動小数点を24bitで、
    16bit以下とMP3(minimp3は16bitで出す)は8bit上へずらす。16bitで鳴らすときも32bitの入れ物に16bitの桁で入れる(同じ列・同じ係で両方を扱う)。
  - 周波数の変換(`PcmResampler`)は24bitの桁×Q15が32bitを超えるので、1の版だけ64bitで足す(フィルタと補間)。
  - 先読みの列(`WavStream`)は1サンプル4バイトで **12288サンプル=48KB**(0の版は16384サンプル=32KB)。ステレオ44.1kHzで約139ms
    (0の版は約190ms)。**1コア目がこれより長く止まると途切れる**ので、24bitで鳴らすかに関わらず1の版は少し途切れやすい。
    その代わり大きさは16KB増に抑えた(16384サンプルのままだと64KBで、FLAC等の係の約46KBと合わせてヒープの空きを超えかねない)。
    `renderAddWide()` が24bitの桁のまま音量(64bitで掛ける)を掛けて足す。
  - 2コア目(`PumpWide()`): 効果音・曲(MML)・GBの音は今までどおり16bitで作り、8bit上へずらして `chunk_wide` へ写し、WAVを24bitの桁で足して
    ±8388607で頭打ちする。I2Sは `setBitsPerSample(32)` の32bitスロットで、**値は上位24bit**(`v << 8`。arduino-pico の `write24()` と同じ並び)。
    1ワードが片方のチャンネルなので、左右の2ワードが空いているとき(`availableForWrite() >= 8`)だけ書く(片方だけ書けて左右がずれないように)。
    バッファは同じ長さ(約23ms)にするためワード数を倍(44.1/48kHzで256ワード×8本=8KB)。MAX98357A・PCM5102Aとも32bitスロットを受け付ける
    (BCLKは48kHzで3.072MHz)。
  - 1コア目のWAVの読み込みの一時置き場は、スタック(4KB)を増やさないよう512バイトのまま(24bitでは128サンプル)。
- **効くところ**: PCM5102A(ライン出力)で、24bitのFLAC/ALAC/WAVの下位8bitが出力まで残る。また音量は数値で掛けているので、16bitの曲でも
  音量を下げたときの細かさが残る(16bitの経路は音量25%で約2bit落ちる)。MAX98357A(スピーカー)では聞いて分かる差は無い見込み。
- **PC/Web**(`pc/compat/I2S.h`): `setBitsPerSample(32)` なら `AUDIO_S32SYS` で開き、1ワードを片方のチャンネルとして流す。`availableForWrite()` を足した。
  ホストテストの代替(`stubs/I2S.h`)にも `availableForWrite()`。
- 大きさ(実機ファーム、2026-11-07。前 → 後):

  | 版 | フラッシュ | 静的RAM | 実行中のヒープ |
  |---|---|---|---|
  | `rpipico2w`(0の版) | 2,170,300 → 2,171,212B(+912B) | 155,460 → 155,468B(+8B) | 変わらない |
  | `rpipico2w_music`(1の版) | 1,807,376 → 1,809,304B(+1,928B) | 149,616 → 150,128B(+512B。`chunk_wide`) | 先読みの列 +16KB、24bitで鳴らす間はI2Sのバッファ +4KB |

  0の版の +912B は、どの版にも入る切り替えの口(`SetBitDepth()`・`bit-depth` の読み込み・設定の `updateBitDepth()`・コマンド)。
  経路(32bitの標本・64bitの変換・`PumpWide()`)は入らない。
- 検証: ホストテスト。`sound24_test`(1の版の定義で音の経路のファイルを自分に入れる): 24bitのFLAC/ALAC(48kHz・96kHz)を24bitの桁で読むと
  ffmpegの24bitの展開結果とビット単位で一致(`gen_*_test_data.py` が `〜Hash24` を書く)・16bitの桁なら今までの16bitの結果と一致・
  16bitのFLACとMP3は8bit上へずらしただけ・24bitのWAVの下位8bit・96kHz→48kHzで大きさが合い下位ビットが残る、
  SoundFunctions: 既定は16bit・16/24以外は断る・切り替えで鳴っていたWAVが止まる・32bitスロット/倍のワード数で開き直す・
  24bitのWAVの値(下位8bitを含む)が音量を掛けてそのまま出る・効果音も鳴る・`bit-depth` の読み込み・16bitへ戻す。
  `flac24_test`(`flac_test.cpp` を1の版で作ったもの。16bitで鳴らすときは今までどおり)。既定の版のホストテストは全部そのまま通る。
  PCビルド(`-DPICOOS_VARIANT=rpipico2w_music`): 設定の音タブに「ビット深度」が出て、24bitを選ぶと「出力を24bitにします」。
  **実機では未確認(24bitで鳴るか・途切れないか)(Todoist参照)**。

## イヤホンとスピーカーの切り替え(`SoundFunctions::Route` / PCM5102A + 差し込み検出) (2026-10-24)

SUMMARY.md #11。**スピーカー(MAX98357A)とイヤホン(PCM5102A)を両方つなぎ、イヤホンを差すと自動でイヤホンへ移る**。

- **MAX98357Aにはイヤホンを直接つながない**。BTL出力(両方の端子が信号でGNDが無い)なので、3.5mmジャックのスリーブ(GND)で片側がショートする。
  イヤホンには上の「出力の周波数を選ぶ」で使えるようにしたライン出力のPCM5102Aを使う。
- **配線**: 2台を同じI2Sの3本(GP2/3/4)へ並べる(I2Sは一方通行で、BCLKは22050Hzで約0.7MHzなので分岐してよい)。
  **どちらが鳴るかは休止端子だけで決める**: MAX98357AのSD=GP6(`AUDIO_SHUTDOWN`。今までどおり)、PCM5102AのXSMT=GP14(`AUDIO_HP_MUTE`、HIGH=鳴らす)。
  イヤホンの差し込みはジャックの検出接点をGP8(`AUDIO_HP_DETECT`)へ、差すとGNDへ落ちる向きでつなぐ(内部プルアップ、LOW=差してある)。
  空いていたGPIOはGP8とGP14の2本だけで、ちょうど使い切った。
  - XSMTを3.3Vへ固定したままだと、スピーカーへ移ってもPCM5102Aは鳴り続ける(イヤホンを抜いていれば聞こえないので実害は無い)。
    GP6を配線していないMAX98357Aは、イヤホンへ移ってもスピーカーが黙らない(SD端子が基板の既定で鳴る)。
- **鳴らす先の決め方**(`ResolveRoute()`、1コア目): `route = auto` なら差し込み検出で決める。アンプ(GP5)と差し込み(GP8)は
  `DebouncedPin` で同じばたつき除け(100ms×3回)を通す。「つながっている」(`IsConnected()`)は**今の鳴らす先**について見る:
  スピーカーなら GP5、イヤホンなら GP8(`route = headphone` の決め打ちのときは検出を信じず、つながっている扱い)。
- **片方しかつないでいなくても動く**:
  - MAX98357Aだけ: GP8 は浮いて HIGH = 差していない → 常にスピーカー。GP14 を上げ下げしても何もつながっていないだけ。今までと同じ。
  - PCM5102Aだけ: GP5 は浮いて HIGH。イヤホンを差せばイヤホンで鳴り、抜けば(スピーカーを選んで)「未接続」でI2Sを止める。
    鳴らせる方があっても勝手には移らない(移る先が無いので)。ジャックに検出接点が無ければ `route = headphone` にする。
  - 今までのPCM5102Aだけの配線(GP5をGNDへ、XSMTを3.3VかGP6へ)もそのまま動く: スピーカーとして鳴らすので、GP6 が上がる。
- **2コア目**: `want_headphone`(atomic)を見て `ApplyAmpPins()` が鳴らす方の休止端子だけを上げる。鳴らしている途中に鳴らす先が変わったら、
  **I2Sは開き直さず休止端子だけを切り替える**(音は途切れない)。止めるときは両方とも下げる。
- **音量は鳴らす先ごと**(`volume` / `hp-volume`、既定50 / 25)。ライン出力を直接イヤホンへつなぐと同じ値でもずっと大きく聞こえるため。
  `GetVolume()/SetVolume()` は今の鳴らす先の値で、`master_volume`(2コア目が読む)は鳴らす先が変わるたびに出し直す。
  **電池駆動中の20%の頭打ち(battery-cap)はスピーカーにだけ掛ける**(MAX98357Aの音割れ対策なので)。
- 設定アプリ「音」タブに「出力先」(自動/スピーカー/イヤホン、`route`)と「今は〜から鳴ります」。音量のスライダーは鳴らす先が変わると出し直し、
  離したときに今の鳴らす先のキーへ書く。ステータスバーは鳴らす先がイヤホンならスピーカーの代わりにイヤホンの絵(`custom_icons/headphones.svg`)。
- PC: `PICOOS_HEADPHONE=inserted`(Webは `?headphone=inserted`)で差してある扱い(起動時に決まる)。PCではどちらへ向けても同じ音声デバイスから鳴る。
- 検証: `sound_test` の `TestRoute`(差す/抜くで休止端子が入れ替わりI2Sは開き直さない・ばたつき除け・イヤホンの音量の振幅・頭打ちはスピーカーだけ・
  イヤホンだけ/スピーカーだけ・決め打ち・`route`/`hp-volume` の読み込み)。PCビルドで設定アプリの表示とステータスバーの絵。
  片方だけつないだ場合(MAX98357Aだけ・PCM5102Aだけ・検出接点が無く「出力先」で決め打ち)は**実機で確認済み(2026-10-25)**。
  **両方を並べた自動切り替え(配線・聞こえ方・切り替えのプチ音)は実機では未確認(Todoist参照)**。
