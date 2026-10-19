#pragma once
#include <cstddef>
#include <cstdint>

// I2Sマイク(SPH0645)の入力と、WAVへの録音。
//
// **2つのコアで役割を分ける(音声出力のSoundFunctionsと同じ考え方)。**
//   2コア目(loop1() → LoopCore1()): I2Sの開始/終了、DMAからワードを取り出して MicDsp::Pipeline で
//       16bitモノラルへ直し、録音中なら列(MicDsp::PcmRing)へ積む。音量の山(メーター)も数える
//   1コア目(loop() → Update()): 列を取り出してSDのWAV(WavWriter)へ書く。開始/停止の受付とログ
// 1コア目が描画やSDの書き込みで止まっても、2コア目がI2Sを取りこぼさない(列は16kHzで約1秒ぶん)。
// 2つのコアの間は std::atomic だけでやり取りする。**2コア目はログもSDも触らない。**
//
// - 配線は consts.hpp の MIC_I2S_*(GP26=BCLK、GP27=LRCLK、GP28=DOUT。SELはGNDで左チャンネル)。
//   I2Sは出力とは別(別のPIOのステートマシン)。48000Hz・32bitスロットで受け、録音する周波数へ間引く
// - **I2Sは使う間だけ持つ**(DMAのバッファ 8×512ワード=16KB)。メーター(SetMonitor(true))か録音の間だけ
// - **録音の列(32KB)は録音の間だけ確保する**
// - マイクの有無はI2Sからは分からないので、「値が0.5秒変わらない」を「信号なし」として HasSignal() で見せる
// - 設定は /sys/mic.cfg(無くてよい): `rate = 16000|24000|48000`(既定16000)、`gain = 0〜36`(dB、既定18)、
//   `channel = left|right`(既定left。マイクのSELをGNDにしたらleft)
//
// PC/Webビルドでは pc/compat/I2S.h が入力を真似る(既定は440Hzの試験音。PICOOS_MIC=sdl でPCのマイク)。
namespace MicFunctions {

    constexpr uint32_t kDefaultRate     = 16000;
    constexpr int      kDefaultGainDb   = 18;
    // DMAのバッファ: 48kHz×左右で1ミリ秒に96ワード。8×512ワードで約42ms(2コア目の休みより十分長い)
    constexpr uint16_t kBufferWords     = 512;
    constexpr uint8_t  kBufferCount     = 8;
    // 2コア目→1コア目の列(サンプル数、2のべき乗)。16kHzで約1秒、48kHzで約0.34秒
    constexpr uint32_t kRingSamples     = 16384;
    // 1回のUpdate()でSDへ書く上限(16KB。SDの書き込みで1フレームが長引きすぎないように)
    constexpr uint32_t kMaxDrainSamples = 8192;
    // 列にこれだけ溜まるまでは書かない(SDは512バイト単位でまとめて書くほうが速い)
    constexpr uint32_t kMinDrainSamples = 256;
    // 録音中、見出しの大きさの欄をこの間隔で書き直す(電池が切れても途中までは読めるように)
    constexpr uint32_t kHeaderUpdateMs  = 5000;

    enum class Input : uint8_t {
        Off,        // 使っていない
        Starting,   // 頼んだが2コア目がまだ開始していない
        Running,    // I2Sが動いている
        Failed,     // I2Sを開始できなかった(PIO/DMAが足りない等)
    };

    enum class RecError : uint8_t {
        None,
        NoSd,           // SDが使えない
        Busy,           // もう録音している
        NoMemory,       // 列を確保できない
        OpenFailed,     // ファイルを作れない
        NoName,         // 空いている番号が無い
    };

    // 録音が終わった理由(LastStopReason())
    enum class StopReason : uint8_t {
        None,
        User,           // StopRecording()
        WriteFailed,    // SDへ書けなかった(満杯・抜けた)
        InputFailed,    // I2Sを開始できなかった
        TooLong,        // WAVの上限(2GB)
    };

    // ===== 1コア目から使う =====

    void Setup();       // mic.cfg を読む
    void Update();      // 列をSDへ書く・停止の後片付け・ログ

    // メーター(録音せずに聞く)。録音中は止めても録音は続く
    void SetMonitor(bool on);
    Input InputState();
    // 前に読んでからの一番大きい振幅(0〜32768)。読むと0へ戻る
    int32_t TakePeak();
    // マイクから値が来ているか(I2Sが動いていなければfalse)
    bool HasSignal();

    // path が nullptr なら /recordings/rec_NNNN.wav の空いている番号へ
    RecError StartRecording(const char* path = nullptr);
    // 止める(後片付けは次からのUpdate()。IsRecording()が偽になったら終わり)
    void StopRecording();
    bool IsRecording();         // 録音中か、止めて後片付け中
    bool IsFinishing();
    const char* RecordingPath(); // 録音中/直前に録音したファイル
    uint32_t RecordedMs();       // 書き終えた長さ
    uint32_t DroppedSamples();   // 列が溢れて捨てたサンプル数(この録音で)
    uint32_t Overruns();         // DMAのバッファが溢れた回数(この録音で)
    StopReason LastStopReason();

    // 空いている名前(/recordings/rec_NNNN.wav)を作る
    bool NextRecordingPath(char* out, size_t cap);

    // 設定。周波数は録音中は変えられない(false)。persist=true なら mic.cfg へ書く
    uint32_t SampleRate();
    bool SetSampleRate(uint32_t rate, bool persist = true);
    // 周波数を戻す(mic.cfgへは書かない)。録音中/後片付け中なら終わってから(Luaのアプリを閉じたとき)
    void RestoreSampleRate(uint32_t rate);
    int GainDb();
    void SetGainDb(int db, bool persist = true);
    bool RightChannel();

    const char* RecErrorToStr(RecError e);
    const char* StopReasonToStr(StopReason r);

    // ===== 2コア目から使う =====

    // 仕事をしたらtrue(I2Sから何か読めた)
    bool LoopCore1();
    // I2Sが動いている間は休みを短くする(main.cppのloop1())
    bool Core1Busy();
}
