#pragma once
#include <cstdint>
#include "sound/Chip_Synth.hpp"

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

    constexpr uint32_t      kSampleRate        = 22050;
    constexpr int           kChannels          = ChipSynth::kChannels;
    constexpr unsigned long kDetectIntervalMs  = 100;
    constexpr uint8_t       kDetectStableCount = 3;    // 100ms×3回続けて同じなら採用
    // I2Sのバッファ: 64ワード(1ワード=左右16bitずつの1サンプル)×8本 = 512サンプル ≒ 23ms / 2KB。
    // 2コア目が専任で流すので短くてよい(短いほど要求から音が出るまでが速い)
    constexpr uint16_t      kBufferWords       = 64;
    constexpr uint8_t       kBufferCount       = 8;
    // 1コア目→2コア目のコマンドの列。溢れた要求は捨てる(DroppedCommands()で数える)
    constexpr uint8_t       kCommandQueueSize  = 32;
    constexpr uint8_t       kDefaultVolume     = 50;

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

    // chで鳴らす(鳴っている音は差し替え)。列が満杯ならfalse(その要求は捨てる)
    bool Play(uint8_t ch, const ChipSynth::Note& note);
    void Stop(uint8_t ch);
    void StopAll();
    // 動作確認用: ch0で矩形波(50%)を鳴らす。freq_hz/duration_msが0なら止めるだけ
    void Beep(uint16_t freq_hz, uint16_t duration_ms);

    // 何か鳴っている(鳴っていることになっている)か。まだ2コア目が受け取っていない要求も含む
    bool IsPlaying();
    // 鳴っているチャンネルのビット(2コア目が最後に知らせた値)
    uint8_t ActiveChannels();
    // 列が満杯で捨てた要求の数
    uint32_t DroppedCommands();

    // ===== 2コア目から使う =====

    void SetupCore1();
    // 1回ぶんの仕事をする。何もすることが無かったらfalse(呼び出し側が少し休んでよい)
    bool LoopCore1();

    // ===== テスト用(時刻を外から与える。ホストテストのmillis()は常に0のため) =====
    void SetupAt(unsigned long now_ms);
    void UpdateAt(unsigned long now_ms);
    bool Core1StepAt(unsigned long now_ms);
}
