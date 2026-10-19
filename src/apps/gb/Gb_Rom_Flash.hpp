#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

class FlashArea;

// ゲームボーイのROMを内蔵フラッシュ(ファームの更新のLittleFSの領域を生で間借り。storage/Flash_Area)へ
// 書き、エミュがそこを直接(XIP)読めるようにする係。RAMには丸ごと載らない大きなROM(512KB・1MB)のため。
//
// ---- 置き方 ----
//   0〜4KB   見出し(Header)。最後に書くので、書いている途中で止まっても「置いていない」になる
//   4KB〜    ROM(連続。エミュは data() + kDataOffset を指すだけで読める)
// 領域は1728KBなので、置けるのは 1724KB まで(ROMの大きさは2のべき乗なので実質1MBまで)。
//
// ---- 同じROMなら書き直さない ----
// 見出しに「ROMの大きさ・ROMのヘッダ(0x100〜0x14F。題名・全体のチェックサムを含む)・
// 頭と末尾16KBのハッシュ(sample_hash)・全体のハッシュ(full_hash)」を持つ。
// begin()はSDから頭と末尾だけ読んで比べ、合えば置いてある全体のハッシュも確かめて(フラッシュを読むだけ)
// 書かずに Done になる。SDを全部読み直さないので、ROMの途中だけを書き換えた別物を同じと見なすことはありうる
// (全体のチェックサムもヘッダにあるので、普通の改造ROMなら違いが出る)。
//
// ---- 書き方 ----
// step()を毎フレーム呼ぶ。SDから4KBずつ読み、消した所へ書いてすぐ読み戻して比べる。
// 消去は64KBに揃っていれば64KBずつ(1回で百数十ms)、端は4KBずつ。1回のstep()は budget_ms を過ぎたら戻る
// (最低1回は進める)。消す/書く間は2コア目が止まるので、呼ぶ側は先に音を止める。
// 1MBで数秒〜十秒ほどの見込み(実機では未計測)。
//
// RAM: このオブジェクト(約4.4KB。4KBの作業場所を含む)は書く間だけ new する。置いた後のROMはRAMを使わない。
class GbRomFlash {
    public:
        constexpr static uint32_t kDataOffset = 4096;
        constexpr static uint32_t kChunkBytes = 4096;
        constexpr static uint32_t kSampleBytes = 16384;
        constexpr static uint32_t kRomHeaderStart = 0x100;
        constexpr static uint32_t kRomHeaderEnd = 0x150;
        constexpr static uint32_t kVersion = 1;

        enum class State : uint8_t { Idle, Writing, Done, Failed };

        explicit GbRomFlash(FlashArea* area) : area_(area) {}
        ~GbRomFlash(){ this->cancel(); }
        GbRomFlash(const GbRomFlash&) = delete;
        GbRomFlash& operator=(const GbRomFlash&) = delete;

        // ROMに使える大きさ(領域が無ければ0)
        static uint32_t Capacity(const FlashArea* area);
        // 置いてあるROMを無効にする(見出しを消す)
        static void Invalidate(FlashArea* area);

        // SDのROMを開いて始める。同じROMが置いてあれば Done(書かない。reused()が真)、
        // 違えば見出しを消して Writing、だめなら Failed(message())
        State begin(const char* rom_path);
        // 書き進める。Writing のときだけ働く
        void step(uint32_t budget_ms);
        // やめる(見出しは消したままなので、置いていない状態になる)
        void cancel();

        State state() const { return state_; }
        const char* message() const { return message_.c_str(); }
        bool reused() const { return reused_; }
        uint32_t written() const { return pos_; }
        uint32_t total() const { return size_; }
        // Done のとき: ROMの先頭(フラッシュをそのまま指す)と大きさ
        const uint8_t* image() const;
        uint32_t imageSize() const { return state_ == State::Done ? size_ : 0; }

    private:
        struct Header {
            char magic[4];        // FlashArea::kRawMagic
            char kind[4];         // "GBRM"
            uint32_t version;
            uint32_t data_offset;
            uint32_t rom_size;
            uint32_t sample_hash;
            uint32_t full_hash;
            uint8_t rom_header[kRomHeaderEnd - kRomHeaderStart];
            uint32_t header_hash; // ここより前のFNV-1a
        };

        FlashArea* area_;
        State state_ = State::Idle;
        FixedString<PICO_STR_LL> message_;
        bool reused_ = false;

        FsFile file_;
        uint32_t size_ = 0;
        uint32_t pos_ = 0;          // 書き終えたROMのバイト数
        uint32_t erased_to_ = 0;    // 消し終えたROMの範囲(kDataOffsetからの相対)
        uint32_t full_hash_ = 0;
        uint32_t sample_hash_ = 0;
        uint8_t rom_header_[kRomHeaderEnd - kRomHeaderStart] = {};
        uint8_t buf_[kChunkBytes];

        void fail(const char* msg);
        bool readAt(uint32_t off, uint8_t* p, uint32_t n);
        bool computeSample();
        bool readHeader(Header& h) const;
        bool eraseNext();
        bool writeChunk();
        bool writeHeader();
        static uint32_t Fnv(uint32_t h, const uint8_t* p, size_t n);
        static uint32_t HeaderHash(const Header& h);
};
