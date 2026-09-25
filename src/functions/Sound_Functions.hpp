#pragma once
#include <cstdint>

// 音声出力(I2S + MAX98357A)。SUMMARY.md #11「Chiptune音声再生」の土台。
//
// 役割は「アンプが刺さっているかを見張り、刺さっている間だけI2Sを動かして音を流す」ことだけ。
// 音色やシーケンサー(チップチューンの合成)はこの上に載せる別の段の仕事で、
// 今は動作確認用の矩形波(Beep)だけを持つ。
//
// - **アンプの有無はAUDIO_DETECTのピンで見る**(I2Sは一方通行で、信号線からは分からない)。
//   アンプ側でGNDへ落としておき、内部プルアップで読む。LOW=接続。
//   抜き差しは kDetectIntervalMs ごとに読み、kDetectStableCount 回続けて同じ値のときだけ採用する。
// - **つながっていなくても呼び出しは全て受け付ける**。Beep()等は黙って「鳴ったこと」になり、
//   鳴っている長さの分だけ時間(millis())で進む。アプリ側は有無で分岐しなくてよい。
//   途中で刺せば、その時点の続きから鳴る。
// - **I2Sのバッファ(約8KB)とPIOは、鳴らせる間だけ持つ**。未接続や output=off の間は
//   end()で返し、休止端子(AUDIO_SHUTDOWN)もLOWにする。
// - 設定は /sys/sound.cfg(無くてよい): `output = auto | off`、`volume = 0〜100`。
//   off は刺さっていても鳴らさない(消音)。
// - 流すのは loop() から(Update())。バッファは約90ms分なので、それより長く loop() が止まる
//   (TLSのハンドシェイク等)と途切れる。合成を2コア目へ移すのは次の段。
//
// PC/Webビルドでは pc/compat/I2S.h がSDLの音声出力で置き換える(src/は同じ)。
// 音声デバイスが開けなければ「未接続」になる。
namespace SoundFunctions {

    // 出力の設定(sound.cfg の output)
    enum class Output : uint8_t {
        Auto,   // アンプが刺さっていれば鳴らす
        Off,    // 刺さっていても鳴らさない
    };

    // 今の状態(ステータスバーの出し分けに使う)
    enum class State : uint8_t {
        Disconnected,   // アンプが刺さっていない
        Muted,          // 刺さっているが output=off
        Active,         // 鳴らせる(I2Sが動いている)
    };

    constexpr uint32_t      kSampleRate        = 22050;
    constexpr unsigned long kDetectIntervalMs  = 100;
    constexpr uint8_t       kDetectStableCount = 3;    // 100ms×3回続けて同じなら採用
    // I2Sのバッファ: 256ワード(1ワード=左右16bitずつの1サンプル)×8本 = 2048サンプル ≒ 93ms / 8KB
    constexpr uint16_t      kBufferWords       = 256;
    constexpr uint8_t       kBufferCount       = 8;
    // 音量100のときの振幅(16bitの最大は32767)。チャンネルを重ねる余地を残して抑えてある
    constexpr int16_t       kMaxAmplitude      = 12000;
    constexpr uint8_t       kDefaultVolume     = 50;

    void Setup();
    void Update();

    // テスト用: 時刻を外から与える版(ホストテストのmillis()は常に0のため)
    void SetupAt(unsigned long now_ms);
    void UpdateAt(unsigned long now_ms);

    State GetState();
    // アンプが刺さっているか(output=offでもtrue)
    bool IsConnected();
    // 実際に音が出るか(刺さっていて、かつoutput=autoのとき)
    bool IsAvailable();

    Output GetOutput();
    // 今だけ切り替える(sound.cfgへは書かない)
    void SetOutput(Output output);

    uint8_t GetVolume();
    // 0〜100。範囲外は丸める(sound.cfgへは書かない)
    void SetVolume(int volume);

    // 矩形波を鳴らす(動作確認用)。鳴っている音は止めて差し替える。
    // freq_hzが0、またはduration_msが0なら止めるだけ
    void Beep(uint16_t freq_hz, uint16_t duration_ms);
    void StopAll();
    // 何か鳴っている(鳴っていることになっている)か
    bool IsPlaying();

    // 以下はテストと出力の下請け用

    // 次の1サンプル(モノラル)を作る
    int16_t NextSample();
    // サンプルを作らずに時間だけ進める(未接続の間に使う)
    void Advance(uint32_t samples);
}
