#pragma once
#include <cstddef>
#include <cstdint>

// ゲームボーイ(DMG)の音源チップ(APU)の再現。GBエミュの音(SUMMARY.md #11「GB対応」)に使う。
//
// レジスタ(0xFF10〜0xFF3F)へ書かれた値だけを見て、4つのチャンネルを22050Hzのモノラルへ合成する:
//   ch1 矩形波 + 周波数スイープ / ch2 矩形波 / ch3 波形メモリ(32段×4bit) / ch4 ノイズ(LFSR)
// 長さ・エンベロープ・スイープは512Hzのフレームシーケンサーで進める(実機と同じ刻み)。
//
// - **2コア目だけから触る**(ChipSynth::Engineと同じ)。レジスタの書き込みはGBエミュ(1コア目)が
//   時刻付きで積み、Gb_Audio_Linkが2コア目で時刻どおりにwrite()する
// - 読み出し(ゲームがNR52等を読む)はここでは扱わない。エミュ側が書いた値の控えから答える(Gb_Emu.cpp)
// - 実機のクロック単位の細かい癖(長さカウンタの余分な1回、波形メモリへの書き込みの化け等)は再現しない。
//   周波数はクロックではなく位相の積み上げで作るので、1サンプル(約45µs)より細かい変化は出ない
// - 状態は固定長で確保はしない(約100バイト)
class GbApu {
public:
    static constexpr uint32_t kCpuHz = 4194304;
    static constexpr int kChannels = 4;

    explicit GbApu(uint32_t sample_rate);

    // 電源を入れ直した状態(全レジスタ0、全チャンネル停止)へ
    void reset();

    // reg は 0xFF10 からの位置(0x00〜0x2F)
    void write(uint8_t reg, uint8_t val);

    // n サンプル作って out へ**足す**(16bitで頭打ち)。out が nullptr なら時間だけ進める
    void renderAdd(int16_t* out, size_t n);

    // 全体の音量(0〜100。sound.cfgのvolume)
    void setMasterVolume(uint8_t volume);

    // 鳴っているチャンネルのビット(NR52の下位4bitと同じ並び)
    uint8_t activeMask() const;
    bool powered() const { return power_; }

    // 1チャンネルが出す最大の振幅(音量15・NR50=7・左右とも・全体の音量100)。
    // 鳴り始めの一瞬は直流を落とす前の段差がそのまま出て2倍になるので、4チャンネルが同時に
    // 鳴り始めても16bitに収まるようにしてある
    static constexpr int32_t kChannelAmplitude = 4000;

private:
    struct Envelope {
        uint8_t initial = 0;    // NRx2の上位4bit
        bool    up = false;
        uint8_t period = 0;
        uint8_t volume = 0;
        uint8_t timer = 0;
        void load(uint8_t nrx2){ initial = nrx2 >> 4; up = (nrx2 & 0x08) != 0; period = nrx2 & 0x07; }
        void trigger(){ volume = initial; timer = period ? period : 8; }
        void clock();
    };
    struct Channel {
        bool     enabled = false;   // 鳴っている(NR52のビット)
        bool     dac = false;       // DACが入っている(切れていると何も出さず、トリガーしても鳴らない)
        bool     length_enable = false;
        uint16_t length = 0;        // 残り(0になったら止まる)
        uint16_t freq = 0;          // 11bitの周波数の値
        uint32_t phase = 0;         // 2^32で波形1周期
        uint32_t inc = 0;           // 1サンプルで進む位相
        Envelope env;
        uint8_t  duty = 0;
    };

    void updateInc(int ch);
    void trigger(int ch);
    void clockLength();
    void clockSweep();
    void clockEnvelope();
    uint16_t sweepCalc();
    void powerOff();
    void updateNoiseRate();

    uint32_t rate_;
    uint8_t  regs_[0x30] = {};  // 書かれた値の控え(波形メモリも)
    bool     power_ = false;
    Channel  ch_[kChannels];

    // ch1のスイープ
    bool     sweep_enabled_ = false;
    uint16_t sweep_shadow_ = 0;
    uint8_t  sweep_timer_ = 0;

    // ch3
    uint8_t  wave_shift_ = 4;   // 出力を右へずらす量(4 = 無音)

    // ch4
    uint16_t lfsr_ = 0x7FFF;
    bool     lfsr_short_ = false;
    uint32_t noise_frac_ = 0;   // 16.16。1サンプルで進むLFSRの段数ぶんを積む
    uint32_t noise_inc_ = 0;

    // フレームシーケンサー(512Hz)
    uint32_t seq_acc_ = 0;
    uint8_t  seq_step_ = 0;

    // 直流を落とす(実機の出力のコンデンサ)。電圧の高い/低いが続いても0へ戻る
    float    hp_cap_ = 0.0f;
    int32_t  gain_ = 0;
};
