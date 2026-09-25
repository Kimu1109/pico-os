#pragma once
#include <cstdint>

// 演奏データ(MUSIC_FORMAT.md「本体の中での扱い」)。MMLの読み取り(Mml_Compiler)が書き、
// 2コア目のシーケンサー(Music_Player)が読む、小さなバイト列の取り決め。
// ファイル形式ではない(SDへは書かない)ので、版を上げずに変えてよい。
//
// [ヘッダ 16バイト]
//   0: 'P' 'M'   1: 版(kVersion)   3: 予備
//   4: 最初のテンポ(u16)   6: 予備(u16)
//   8: チャンネル0〜3の命令列の開始位置(u16×4、バッファの先頭から。0なら空のチャンネル)
// [命令列] チャンネルごとに END で終わる。数値はすべてリトルエンディアン
namespace MusicData {

    constexpr uint8_t  kMagic0 = 'P';
    constexpr uint8_t  kMagic1 = 'M';
    constexpr uint8_t  kVersion = 1;
    constexpr uint16_t kHeaderBytes = 16;
    constexpr int      kChannels = 4;
    constexpr uint16_t kTicksPerQuarter = 48;
    constexpr uint16_t kTicksPerWhole = kTicksPerQuarter * 4;
    constexpr int      kMaxLoopDepth = 4;

    enum Op : uint8_t {
        End        = 0x00,  // チャンネルの終わり(SEGNOがあればそこへ戻る)
        Note       = 0x01,  // midi(u8) len(u16)
        Rest       = 0x02,  // len(u16)
        Wave       = 0x03,  // ChipSynth::Wave(u8)
        Volume     = 0x04,  // 0〜15(u8)
        Envelope   = 0x05,  // -7〜7(i8)
        Gate       = 0x06,  // q 1〜8(u8)
        Tempo      = 0x07,  // bpm(u16)。全チャンネル共通
        LoopBegin  = 0x08,  // 回数(u8)
        LoopBreak  = 0x09,  // 抜けた先(u16、LoopEndの次)。最後の回ならそこへ飛ぶ
        LoopEnd    = 0x0A,  // 回数が残っていればLoopBeginの次へ戻る
        Segno      = 0x0B,  // ループ位置(L)
        SaveState  = 0x0C,  // 波形/音量/減衰/qを退避(マクロの入口)
        RestoreState = 0x0D,// 退避したものへ戻す(マクロの出口)
    };

    inline uint16_t ReadU16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
}
