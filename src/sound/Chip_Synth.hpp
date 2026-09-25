#pragma once
#include <cstddef>
#include <cstdint>

// チップチューン音源(SUMMARY.md #11)。4チャンネルを足し合わせてモノラルの16bitを作る。
//
// ゲームボーイのAPUを手本にしているが、チャンネルごとの波形は固定しない
// (どのチャンネルでも矩形波/三角波/のこぎり波/ノイズを選べる)。
// 音量の変化もゲームボーイと同じ「一定間隔で1段ずつ上げ下げする」エンベロープだけを持つ。
//
// - 状態は全てこのオブジェクトの中の固定長の配列で、確保は一切しない
// - スレッド/コアの面倒は見ない。SoundFunctionsが2コア目だけから触る
//   (1コア目からの要求はコマンドの列で渡す)
// - 帯域制限はしない(22050Hzで素朴に矩形を作るので高い音は折り返しで濁るが、チップチューンの味の内)
namespace ChipSynth {

    constexpr int kChannels = 4;

    enum class Wave : uint8_t {
        Pulse12,        // 矩形波 デューティ12.5%
        Pulse25,        // 矩形波 25%
        Pulse50,        // 矩形波 50%(いちばん素直な「ピー」)
        Pulse75,        // 矩形波 75%(25%と同じ音色で位相が逆)
        Triangle,       // 三角波(ベース向き)
        Saw,            // のこぎり波
        Noise,          // ノイズ(15bitのLFSR。打楽器・爆発)
        NoiseShort,     // 周期の短いノイズ(7bitのLFSR。金属的な音)
        kCount,
    };

    // 1音の鳴らし方
    struct Note {
        Wave     wave = Wave::Pulse50;
        // 周波数(Hzの16倍。440Hz = 7040)。ノイズはLFSRを進める速さ
        uint32_t freq_x16 = 440 * 16;
        uint8_t  volume = 15;       // 0〜15(鳴り始めの音量)
        // エンベロープ: 0=一定、-7〜-1=|env|/64秒ごとに1段下げる、1〜7=|env|/64秒ごとに1段上げる。
        // 下げて0になったらその音は終わる
        int8_t   envelope = 0;
        // 長さ(ms)。0なら止めるまで鳴り続ける
        uint32_t length_ms = 0;
    };

    // 周波数の上限(サンプル周波数の半分。これを超えると別の高さに聞こえる)
    constexpr uint32_t kMaxToneFreqX16(uint32_t sample_rate){ return sample_rate / 2 * 16; }

    class Engine {
    public:
        // 1チャンネルの最大振幅(音量15・全体の音量100のとき)。4チャンネル足しても16bitに収まる
        static constexpr int32_t kChannelAmplitude = 7800;

        explicit Engine(uint32_t sample_rate);

        // ch(0〜kChannels-1)で鳴らす。鳴っている音は差し替える。範囲外のchは無視
        void play(uint8_t ch, const Note& note);
        void stop(uint8_t ch);
        void stopAll();

        // 全体の音量(0〜100)
        void setMasterVolume(uint8_t volume);
        uint8_t masterVolume() const { return master_; }

        // n サンプル作る。out が nullptr なら作らずに時間だけ進める(聞こえない間に使う)
        void render(int16_t* out, size_t n);

        // 鳴っているチャンネルのビット(bit0 = ch0)
        uint8_t activeMask() const;

        uint32_t sampleRate() const { return rate_; }

    private:
        struct Channel {
            bool     active = false;
            Wave     wave = Wave::Pulse50;
            uint32_t phase = 0;
            uint32_t inc = 0;           // 1サンプルで進む位相(2^32で1周期)。ノイズは段数(16.16、65536で1段)
            uint8_t  volume = 0;        // 今の音量 0〜15
            int8_t   env_dir = 0;       // -1 / 0 / +1
            uint32_t env_step = 0;      // 何サンプルごとに1段動かすか
            uint32_t env_count = 0;     // 次に動かすまでの残り
            bool     infinite = true;
            uint32_t remaining = 0;     // 残りサンプル数(infiniteでないとき)
            uint16_t lfsr = 0x7FFF;
            int32_t  gain = 0;          // volume と全体の音量から決まる倍率(>>15して使う)
        };

        void updateGain(Channel& c) const;
        int32_t waveValue(Channel& c);  // -32768〜32767
        void stepEnvelope(Channel& c);

        uint32_t rate_;
        uint8_t  master_ = 100;
        int32_t  master_amp_ = kChannelAmplitude;
        Channel  ch_[kChannels];
    };
}
