# 音: マイク・録音・文字起こし

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `sound.md` から節ごとに分けた(目次は同じフォルダの `README.md`)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

## マイクと録音 (`src/hal/Mic_Functions` / `src/sound/Mic_Dsp.hpp` / `src/sound/Wav_Writer` / `RecorderScene`) (2026-10-18)

SUMMARY.md #11「マイクで録音」。I2Sマイク **SPH0645**(Adafruitのブレイクアウト等)で `/recordings/rec_NNNN.wav` へ録る。方針は利用者と相談して決めた(2026-10-18): 用途は録音 /
出力とBCLK/LRCLKを共有せず**別のI2S**にする(SPH0645はBCLK 2.048〜4.096MHz・32bitスロットが要り、出力の16bitスロット・22050Hzでは範囲外。共有するには出力を44.1kHz・32bitへ固定する必要があった)。

- **配線**: BCLK=GP26、LRCLK=GP27(arduino-picoはBCLK+1固定)、DOUT=GP28(`consts.hpp`の`MIC_I2S_*`)、SEL=GND(左)、3V=3V3、GND。
  GP26〜28は以前「外部コントローラー用に残す」としていたが、コントローラーはI2C(GP0/1)に決まったので使った。
- **I2S**: arduino-picoの`I2S(INPUT)`、32bit、**48000Hz**(BCLK 3.072MHz)、DMAのバッファ8×512ワード(16KB、約42ms)。PIOのステートマシンを出力とは別に1つ使う。
  arduino-picoの入力のPIO(`pio_i2s_in`)は最初に`in`を空打ちして**I2Sの1bitの遅れを合わせてある**(#2037)ので、ワードはそのまま左詰めの18bit。
  SPH0645は「1bit遅い」と言われることがあるが、**補正は入れていない**(実機で録ったWAVは正しい高さ・向きで聞こえた、2026-10-19。声が極端に小さい/割れる・ジーという音なら最初にここを疑う)。
- **2コア目(`MicFunctions::LoopCore1()`、`main.cpp`の`loop1()`で音声の後)**: I2Sの開始/終了(DMA割り込みが2コア目に付くように)、ワードを読んで
  `MicDsp::Pipeline`(左/右を選ぶ → 上位18bit → 直流を落とす一次のハイパス約38Hz → 増幅 → 窓付きsincのFIR(8×間引き+1タップ)で16000/24000Hzへ間引く → 16bitへ頭打ち)、
  録音中は`MicDsp::PcmRing`(16384サンプル=32KB、16kHzで約1秒)へ積む。溢れた分は捨てて数える(`DroppedSamples()`)。メーターの山(`TakePeak()`)と「値が0.5秒変わらない=信号なし」(`HasSignal()`)もここ。
  I2Sが動いている間は`loop1()`の休みを1msにする(`Core1Busy()`)。左右の組は、奇数個のワードが届いても前の余りを持ち越してずらさない。
- **1コア目(`MicFunctions::Update()`)**: 列を256サンプル以上溜まったら最大8192ずつ`WavWriter`(16bitモノラル)へ書く。見出しの大きさの欄は5秒ごとに書き直す
  (電池が切れても途中まで読める。録音アプリの一覧は見出しが0でもファイルの大きさから長さを出す)。止めるときは want_mode を下ろし、
  2コア目が列へ積まなくなった(`rec_active`が偽)のを見てから残りを書いて閉じ、列を返す(`rec_active`は「真を書く→want_modeを読む」の順一貫の読み書きで、入れ違いで消した列へ書かない)。
  SDへ書けない・2GB・I2Sの失敗で止まったら`LastStopReason()`。
- **メモリ**: I2SのDMA 16KBはメーターか録音の間だけ、列32KBは録音の間だけ確保。静的には約1.5KB(読み取りの作業場所)。
- **録音中はバックグラウンドの仕事を止める**(`BackgroundFunctions::suspend_query`、main.cppが差す)。チャットの未読などのTLSのハンドシェイクで1コア目が1〜2秒止まり、列が溢れるため。
  ミュージックの曲の終わりで次へ進むのも止まる。
- **設定**: `/sys/mic.cfg`(無くてよい)の`rate = 16000|24000|48000`(既定16000)、`gain = 0〜36`(dB、既定18。SPH0645は94dB SPLで-26dBFSなので、声には増幅が要る)、
  `channel = left|right`。録音アプリの右上のボタンで周波数・増幅を切り替えると書く。
- **録音アプリ(`RecorderScene`、ランチャの「録音」、アイコンは自作の`custom_icons/microphone.svg`=`IconID::Microphone`)**: 開いている間はメーターのため聞き続ける。
  [録音]/[停止](Space / r / 再生キー)、録った時間、状態(信号なし・取りこぼしは赤)、一覧(新しい順48件、2回タップか[再生]で`FileViewerScene`=WAVを鳴らす、[削除])。
  録音中は`keepForeground`・`KeepAwake()`。画面を離れると止めて保存する。
- **PC/Webビルド**: `pc/compat/I2S.h`の入力が、`begin()`からの経過時間の分だけSPH0645と同じ形のワードを返す。`PICOOS_MIC=tone`(既定。440Hz・-30dBFS+雑音+直流)/
  `silence` / `none`(全部0=「信号なし」)/ `sdl`(PCのマイク。SDLの録音デバイス)。溜まりすぎたら古い分を捨てて`getOverflow()`。
- 検証: `mic_test`(run.sh の media。計算: 振幅・直流・周波数・増幅・頭打ち・12kHzが折り返さない・3kHzは通る・右チャンネル・信号なし、列、
  MicFunctions: 使うまでI2Sを持たない/返す、48000Hz・32bit・GP26/28、奇数個ずつ届いても1kHzのまま、WAVの見出し、番号、取りこぼし・DMAの溢れ、録音中の見出しの書き直し、
  I2Sの失敗で止まる/もう一度試す、SD無し、mic.cfgの読み書き、24000Hz・右)、PCビルドの`--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0): 録音→停止で5.0秒・16000Hz・440Hz・-12dBFS(=-30+18)・直流0のWAV、
  録音中の表示、`PICOOS_MIC=none`で「信号がありません」、一覧。実機ファームのビルドは通る(`pio run`)。**実機・実物のSPH0645で確認済み(2026-10-19)**(メーター・録ったWAVの高さと向き・16kHz/48kHzで取りこぼし無し・増幅+18dBの目安)。
- **Lua(`pico.mic_*`、`src/lua/LuaEngine_Mic.cpp`、2026-10-18)**: `mic_start([path][, {rate=, gain=}])`→パス / `mic_stop` / `mic_recording` / `mic_info` /
  `mic_monitor(on)` / `mic_level()`(0〜1とdBFS。読むと0へ) / `mic_has_signal` / `mic_set_gain`・`mic_gain` / `mic_set_rate`・`mic_rate`。
  **新しい権限`microphone`**(app.cfgの`permission_microphone`、アプリストアの`perms`は`microphone`、確認の表示は「マイク」)。権限が無ければ`nil, 理由`、引数の誤りは先に`luaL_error`。
  録音先は`SdWriteAllowed()`(省略すると`<app_dir>/rec_NNNN.wav`、`.wav`以外はエラー、親フォルダは作る)。
  **増幅・周波数は`persist=false`で変え、`LuaEngine`の破棄(`ReleaseMic()`)で元へ戻す**(mic.cfgへは書かない)。そのアプリが始めた録音・メーターもそこで止める。
  録音の後片付け中は周波数を変えられないので、戻すのは`MicFunctions::RestoreSampleRate()`(終わってから当てる)。録音中は`LuaScene`が`KeepAwake()`(`MicBusy()`)。
  バックグラウンドのスクリプトでは使えない。ドキュメントは`lua-api-doc/content/api/media/mic.md`、動作確認は「動作確認」アプリの「マイク」(`mic.lua`。メニューのボタンは28pxにした)。
  検証: `lua_ext_test`(権限・引数・録音先・同時に1本・mic_level/info・止める・閉じたらメーター/増幅/周波数を戻す・録音したまま閉じても保存・mic.cfgへ書かない)、
  `app_repo_test`(`microphone`の権限)、PCビルドの`--tap`/`--shot`(`PICOOS_MIC=tone`でメーター・録音→停止→保存)。
- 未: 録音中の一時停止、モノラル以外。

## 録音の文字起こし (`src/apps/recorder/Transcribe_Proto` / `Transcribe_Client` / `RecorderScene`) (2026-10-30)

録音アプリで選んだ録音を外の音声認識のAPIへ送り、返ってきた文章を WAV の隣の `.txt` へ置く。**オンラインが前提**
(本体の中では認識しない)。方針は利用者と相談して決めた(2026-10-30): 送り先は**OpenAI互換のAPI**、結果は**WAVの隣の .txt**、
送るのは**ボタンを押したときだけ**(通信量・料金を利用者が決める。自動で送る設定は作っていない)。

- **送り先**: `POST <api>/audio/transcriptions`(multipart/form-data。`model`・`language`・`prompt`・`response_format=text`・`file`)。
  OpenAIの形に揃えたのは、`api` を変えるだけで OpenAI(`https://api.openai.com/v1`。既定)・Groq(`https://api.groq.com/openai/v1`、
  `model = whisper-large-v3-turbo`)・自前の whisper.cpp のサーバ(`--inference-path /v1/audio/transcriptions` で立て、`api = http://<PC>:<port>/v1`)の
  どれにも向けられるため。既定の model は `gpt-4o-mini-transcribe`、language は `ja`(空にすると相手が自動で判定)。
  Google Cloud STT は base64のJSONで1分までの同期認識しかなく、長いものはGCSを経由するので見送った。
- **`response_format=text`**: 成功の返事が本文そのものになるので、長い文章でもJSONの文字列をRAMへ載せずにそのままSDへ流せる。
  200の本文は `<名前>.txt.part` へ書き、終わったら `.txt` へ入れ替える(途中で失敗しても前の `.txt` は壊れない)。
  200以外は先頭512Bだけ取り、`{"error":{"message"}}`(OpenAI/Groq)・`{"error":"…"}`(whisper.cpp)・`detail`・JSONでなければ1行目から理由を拾う。
- **WAVはRAMへ載せない**: multipartの前後(1KiB未満)だけを作り、間はSDから少しずつ流す。そのために `HttpRequest` に
  **`beginStream()` と `IHttpBodySource`** を足した(1回の`update()`で8KBまで送るのでフレームは止まらない。送り終える前に
  相手が応答してきたら(401・413等)送るのをやめてその応答を読む。`setTimeoutMs()` で待ち時間を延ばせる)。
  送り終えてから返事が来るまでは長い録音だと数十秒かかるので、何も届かない時間の上限を180秒にしている。
  `HttpRequest` の `Content-Type` の置き場は multipart の区切りが入るよう48Bから96Bへ広げた。
- **上限**: OpenAIの25MB(16kHzのモノラルで約13分、48kHzで約4分)。超えるものは送る前に断る。間引いて送る(48kHz→16kHz)のは未。
- **設定**: `/sys/transcribe.cfg`(`api` / `model` / `language` / `prompt`)。**APIキーは `/sys/transcribe_key.cfg`**
  (`crypto/Secret_File`。OpenAIのキーは約164文字あり、設定ファイルの1つの値(平文で約77文字まで)には入らないため、
  暗号化した行を255Bずつ並べる。メールのMicrosoftのサインインの更新用トークンと同じ形で、その処理をここへ切り出して共用にした)。
  **母艦やWi-Fi転送で平文の1行を置いてもよく、読んだときに暗号化して書き直す**(長いキーを画面のキーボードで打たなくて済むように)。
  画面の[設定]からも入れられる(192文字未満)。キーの無い送り先(手元のサーバ)なら無くてよい。
- **送り先(ホスト)も画面から**(2026-10-31): [設定]を押すと、まず送り先(`api`)、続けてAPIキーを聞く(どちらも変えないなら[決定]/空欄)。
  入れ方は `192.168.1.10:8080`(scheme無し=`https://`、パス無し=`/v1`を足す)・`http://pc.local:8080`(平文で良ければ自分で `http://`)・
  完全なURL。空欄で[決定]すると既定(OpenAI)へ戻る。`TranscribeClient::NormalizeApi()` が整え(空白・制御文字・`"`・長すぎるものは断る)、
  `SaveApi()` が `/sys/transcribe.cfg` の `api` だけを書き換える(他のキーとコメントは残る)。検証: `transcribe_test`。実機では未確認(Todoist参照)。
- **画面**: 下の段に `[削除][再生][文字起こし][設定]`(240x320に収まるよう小さい字)。文字起こし済みの録音は一覧のアイコンが文書になり、
  ボタンが[文章](`.txt` をファイルビューワーで開く)に変わる。送っている間は[やめる]、状態の行に「送っています NN%」→「返事を待っています…」、
  失敗は赤で理由(例:「APIキーが違います(HTTP 401: …)」)。終わると文章を開く。キーが無いときに[文字起こし]を押すとキーの欄を開く。
  キーボードの `t` でも同じ。もう一度送りたいときは `.txt` を消す。録音を削除すると `.txt` も消す。
- **録音と同時には使わない**: 送っている間は録音できず、メーターも止める(I2SのDMAの16KBを返し、TLSの約40KBの場所を空ける。
  TLSの処理で1コア目が止まると録音の列が溢れるため)。画面を離れたら送るのをやめる。送っている間は `KeepAwake()`。
- **TLS**: api.openai.com・api.groq.com は Cloudflare の下で、証明書は Google Trust Services(GTS Root R4 等)の見込み(焼き込みのルートにある)。
  このリモート環境からは出口のゲートウェイが証明書を差し替えるので確かめられていない。**実機で本物のAPIへ送れるかは未確認**(Todoist参照)。
- 検証: `transcribe_test`(run.sh の apps。multipartの形・空の値は送らない・改行や " を断る・失敗の理由の拾い方(1バイトずつも)・説明・
  .txtのパス・設定(既定はキーが要る/手元ならキー無し/使えない文字)・設定ファイルと平文のキーの暗号化し直し(2行に分かれる)・
  送る本文を半端な大きさで読む/読み直す・始められない場合とやめたとき)、**`transcribe_net_test`(run_net.sh。
  `script/host_test/transcribe_fake_server.py`=OpenAI互換の偽物を平文/HTTPS/上限を小さくしたもので立てる)**: 400KBを流して送り、
  相手が受け取ったWAVの長さとSHA-256が手元と同じ・割合が進む・.partを残さない・401/500の理由・前の.txtを壊さない・
  送り終える前の413・途中でやめる・設定ファイルから。PCビルドの`--tap`/`--shot`(偽物へ向けて): 録音→選ぶ→[文字起こし]→文章が開く→
  戻ると一覧のアイコンが文書。**実機ファームのビルドはこの環境ではできない**(ボードの定義が無い。CIの`firmware-build.yml`で通す)。
