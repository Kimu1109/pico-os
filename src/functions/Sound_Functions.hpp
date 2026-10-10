#pragma once
#include <cstdint>
#include "sound/Chip_Synth.hpp"

struct MmlResult;
class GbAudioSink;

// 音声出力(I2S + MAX98357A)とチップチューン音源(SUMMARY.md #11)。
//
// **1コア目と2コア目で役割を分けてある。**
//   1コア目(loop()): アンプの抜き差しの検出・設定・アプリからの要求(Play/Stop/Beep)の受付。
//                    要求はコマンドの列(固定長のリング)へ積むだけで、音源には触らない
//   2コア目(loop1()): コマンドを取り出して音源(ChipSynth::Engine)へ渡し、波形を作ってI2Sへ流す。
//                    I2Sの開始/終了もここでする(I2SのDMA割り込みが2コア目に付くように)
// 1コア目が描画やTLSのハンドシェイクで止まっても、音は途切れない。
// 2つのコアの間は std::atomic だけでやり取りする(ロックは取らない)。
//
// - **アンプの有無はAUDIO_DETECTのピンで見る**(I2Sは一方通行で、信号線からは分からない)。
//   アンプ側でGNDへ落としておき、内部プルアップで読む。LOW=接続。
//   kDetectIntervalMs ごとに読み、kDetectStableCount 回続けて同じ値のときだけ採用する。
// - **つながっていなくても呼び出しは全て受け付ける**。音源は2コア目で時間どおりに進むので、
//   途中で刺せばその時点の続きから鳴る。アプリ側は有無で分岐しなくてよい。
// - **I2Sのバッファ(約2KB)とPIOは、鳴らせる間だけ持つ**。未接続や output=off の間は返し、
//   休止端子(AUDIO_SHUTDOWN)もLOWにする。
// - 設定は /sys/sound.cfg(無くてよい): `output = auto | off`、`volume = 0〜100`。
// - 2コア目はログを出さない(LogFunctionsは1コア目専用)。I2Sの開始/失敗は1コア目が見てログへ出す。
//
// PC/Webビルドでは pc/compat/I2S.h がSDLの音声出力で置き換え、2コア目は
// ネイティブでは別スレッド、Webではフレームごとに loop1() を1回呼ぶ(pc/main_pc.cpp)。
namespace SoundFunctions {

    enum class Output : uint8_t {
        Auto,   // アンプが刺さっていれば鳴らす
        Off,    // 刺さっていても鳴らさない
    };

    enum class State : uint8_t {
        Disconnected,   // アンプが刺さっていない(またはI2Sを開始できなかった)
        Muted,          // 刺さっているが output=off
        Active,         // 鳴らせる(I2Sが動いている)
    };

    constexpr uint32_t      kSampleRate        = 44100;   // 2026-10-10に22050から(PCM5102AのDACで鳴らすため)
    constexpr int           kChannels          = ChipSynth::kChannels;
    constexpr unsigned long kDetectIntervalMs  = 100;
    constexpr uint8_t       kDetectStableCount = 3;    // 100ms×3回続けて同じなら採用
    // I2Sのバッファ: 128ワード(1ワード=左右16bitずつの1サンプル)×8本 = 1024サンプル ≒ 23ms / 4KB。
    // 2コア目が専任で流すので短くてよい(短いほど要求から音が出るまでが速い)
    constexpr uint16_t      kBufferWords       = 128;
    constexpr uint8_t       kBufferCount       = 8;
    // 1コア目→2コア目のコマンドの列。溢れた要求は捨てる(DroppedCommands()で数える)
    constexpr uint8_t       kCommandQueueSize  = 32;
    constexpr uint8_t       kDefaultVolume     = 50;
    // 省電力中で何も鳴っていないときの2コア目の休み(ms)。要求から音が出るまでこの分まで遅れうる
    constexpr unsigned long kPowerSaveIdleDelayMs = 20;
    // バッテリー駆動中(VSYSがUSBの5VでなくLiPoセルの電圧、最大4.2V)は、MAX98357Aの
    // 出力ヘッドルームが下がり、通常の音量では実機で音割れ(アナログクリップ)することを
    // 実機で確認した。sound.cfg/SettingsScene上の設定値(master_volume)はそのまま保ち、
    // 2コア目が実際に音源へ渡す値だけをこの上限で頭打ちする(DisplayFunctionsの
    // 自動調光と同じ「保存値」と「実効値」を分ける考え方)。20という値はユーザーの実機での
    // 実測(20%以下では音割れしない)に基づく
    constexpr uint8_t       kBatteryVolumeCapPercent = 20;
    // 演奏データの置き場1つの大きさ。置き場は2つ(鳴らしている曲と、次に読む曲)で、
    // 最初に曲を鳴らすときに読み取り係(約3.5KB)と一緒に確保し、以降は持ち続ける
    constexpr uint16_t      kMusicDataBytes    = 6144;
    // WAV: 1回のUpdate()で読んで積むサンプル数の上限(SDの読み込みで1フレームが長引きすぎないように。
    // 44.1kHzステレオ16bitなら約8KBを読む量)。毎フレーム約735サンプル(60fps)消費するので、
    // 1フレームが約46ms(約21fps)までなら追いつく
    constexpr uint16_t      kWavMaxPerUpdate   = 2048;
    // WAVを鳴らし始める前に先読みしておく数(鳴らし始めの途切れを防ぐ)
    constexpr uint16_t      kWavPrefillSamples = 8192;

    // ===== 1コア目から使う =====

    void Setup();
    void Update();

    State GetState();
    bool IsConnected();     // アンプが刺さっているか(output=offでもtrue)
    bool IsAvailable();     // 実際に音が出るか

    Output GetOutput();
    void SetOutput(Output output);      // 今だけ(sound.cfgへは書かない)

    uint8_t GetVolume();
    void SetVolume(int volume);         // 0〜100。今だけ
    // 今、バッテリー駆動によるkBatteryVolumeCapPercentの頭打ちが掛かっているか
    // (GetVolume()が返す設定値そのものは変わらない。表示上の注記等に使う想定)
    bool IsBatteryVolumeCapActive();

    // chで鳴らす(鳴っている音は差し替え)。列が満杯ならfalse(その要求は捨てる)
    bool Play(uint8_t ch, const ChipSynth::Note& note);
    void Stop(uint8_t ch);
    void StopAll();
    // 動作確認用: ch0で矩形波(50%)を鳴らす。freq_hz/duration_msが0なら止めるだけ
    void Beep(uint16_t freq_hz, uint16_t duration_ms);

    // 何か鳴っている(鳴っていることになっている)か。まだ2コア目が受け取っていない要求も含む
    bool IsPlaying();
    // 鳴っているチャンネルのビット(2コア目が最後に知らせた値)
    ChipSynth::ChannelMask ActiveChannels();
    // 列が満杯で捨てた要求の数
    uint32_t DroppedCommands();

    // ---- 曲(MUSIC_FORMAT.md) ----
    // MMLを読んで鳴らす(鳴っている曲は差し替え)。読めなければfalseで、今の曲はそのまま。
    // result を渡すと、誤りの位置・理由・警告・曲名が入る
    bool MusicPlayFile(const char* path, MmlResult* result = nullptr);
    bool MusicPlayText(const char* text, size_t len, MmlResult* result = nullptr);
    void MusicStop();
    // 曲が鳴っているか(鳴らす/止めるを頼んだ直後から、その結果の扱い)
    bool MusicPlaying();
    // 最後に鳴らした曲の名前(#title。無ければファイル名)
    const char* MusicTitle();
    // 一時停止/再開。曲が鳴っていなければfalse。止めている間も MusicPlaying() は true のまま。
    // 再開は今の音符の途中ではなく次の音符から鳴る。新しく鳴らす/止めると解除される
    bool MusicPause(bool pause);
    bool MusicPaused();
    // 鳴らし始めてからの時間(ms。一時停止中は進まない。鳴らす要求を2コア目が受け取るまでは0)
    uint32_t MusicElapsedMs();
    // 曲の長さ(ms)。L(ループ位置)がある曲は終わりが無く、*loops が true で、最初に戻るまでの長さを返す
    uint32_t MusicTotalMs(bool* loops = nullptr);

    // ---- ゲームボーイの音(GBエミュ) ----
    // GbEmu::setAudioSink() へ渡す窓口。音源チップ(GbApu)は2コア目にあり、レジスタへの書き込みは
    // 時刻付きの列(GbAudioLink、約4KB)で渡す。列は最初にROMを起動したときに確保し、以降は持ち続ける。
    // 曲・効果音と同時に鳴らせる(足し合わせる)
    GbAudioSink* GbAudio();
    // 列が満杯で捨てた書き込みの数
    uint32_t GbDroppedWrites();

    // ---- WAV(SDの .wav をそのまま鳴らす) ----
    // 1コア目がSDから少しずつ読んでモノラル44100Hzへ直し、列(WavStream、約32KB)で2コア目へ渡す。
    // 曲・効果音・GBの音と足し合わせる。同時に鳴らせるWAVは1本(鳴らすと前のWAVは止まる)。
    // 列と読み取り係(合わせて約17KB)は最初に鳴らすときに確保し、以降は持ち続ける。
    // 読み込みが WavStream::kRingSamples(約370ms)より長く止まると途切れる(WavUnderruns())
    struct WavInfo {
        uint16_t channels = 0;
        uint16_t bits = 0;
        uint32_t sample_rate = 0;
        uint32_t duration_ms = 0;
    };
    // 鳴らせなければfalseで、error に理由(読み取り係は1つなので、今のWAVは止まる)。volumeは0〜100(全体の音量と両方掛かる)
    bool WavPlay(const char* path, bool loop = false, uint8_t volume = 100,
                 const char** error = nullptr, WavInfo* info = nullptr);
    void WavStop();
    // 読んでいる途中か、まだ鳴らしていないものが残っているか
    bool WavPlaying();
    // 最後に鳴らしたWAVのファイル名
    const char* WavTitle();
    // 一時停止/再開。WAVが鳴っていなければfalse。止めている間も WavPlaying() は true のまま。
    // 新しく鳴らす/止めると解除される。止めている間はスリープに入ってよい(PowerFunctions)
    bool WavPause(bool pause);
    bool WavPaused();
    // 再生位置と全体の長さ(ms)。ループ再生中の位置は 0〜長さ の中を巡る。
    // 位置は2コア目が受け取った分を引いた値(I2Sのバッファ約23msぶん先を指す)
    uint32_t WavPositionMs();
    uint32_t WavDurationMs();
    // 位置へ飛ぶ(読み込み済みの先読みは捨てて、その位置から読み直す)。
    // 読み終えて止まった直後でも開き直して飛べる。止めた(WavStop)後や未再生ならfalse
    bool WavSeekMs(uint32_t ms);
    // 読み込みが間に合わず途切れた回数
    uint32_t WavUnderruns();

    // ---- 省電力(スリープ中) ----
    // 何も鳴っていない間だけ、I2Sとアンプ(休止端子)を止めて2コア目をゆっくり回す。
    // 鳴らす要求が来れば(効果音・曲・GBの音)自動で動き直す。呼び出しはPowerFunctionsだけ
    void SetPowerSave(bool enable);
    bool IsPowerSave();

    // ===== 2コア目から使う =====

    void SetupCore1();
    // 1回ぶんの仕事をする。何もすることが無かったらfalse(呼び出し側が少し休んでよい)
    bool LoopCore1();
    // LoopCore1()がfalseのとき休む長さ(ms)。省電力中で何も鳴っていなければ長く休む
    unsigned long IdleDelayMs();

    // ===== テスト用(時刻を外から与える。ホストテストのmillis()は常に0のため) =====
    void SetupAt(unsigned long now_ms);
    void UpdateAt(unsigned long now_ms);
    bool Core1StepAt(unsigned long now_ms);
}
