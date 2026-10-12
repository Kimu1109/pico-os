#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

#include <SdFat.h>
#include <cstdint>
#include <cstddef>

// .pvid(VIDEO_FORMAT.md)を読んで、今のフレームの絵(4bpp)を持つ係。描画にもLovyanGFXにも依存しない
// (ホストテストでは本物をそのまま動かす)。
//
// ---- 持つもの ----
//   - 画面1枚ぶんの4bppの絵(幅/2 × 高さ バイト。240x176で約21KB)… open()で確保、close()で返す
//   - キーフレームの目録(1件8バイト)… 同上
//   - SDからの読み込みの溜め(512バイト、メンバ)
// フレームの本体は512バイトずつ読みながら解くので、1フレームぶんのバッファは要らない。
//
// ---- 壊れたファイル ----
// 見出しの値・目録・タイルの形・フレームの長さを全部確かめ、合わなければ止まって Error を返す
// (絵のバッファの外へは書かない)。止まったら close() するまで次へ進まない。
class PvidPlayer {
    public:
        constexpr static int kTile = 8;
        constexpr static int kMaxW = 240;
        constexpr static int kMaxH = 320;
        constexpr static int kMaxTileRows = kMaxH / kTile;
        // 目録の上限(2秒ごとのキーフレームで約4.5時間)。超えるファイルは開かない
        constexpr static uint32_t kMaxIndex = 8192;

        enum class Error : uint8_t {
            None, NoSd, Open, BadHeader, Unsupported, BadIndex, OutOfMemory, BadFrame, Ended
        };
        static const char* ErrorToStr(Error e);

        PvidPlayer() = default;
        ~PvidPlayer(){ this->close(); }
        PvidPlayer(const PvidPlayer&) = delete;
        PvidPlayer& operator=(const PvidPlayer&) = delete;

        Error open(const char* path);
        void close();
        bool isOpen() const { return this->screen_ != nullptr; }

        int width() const { return this->w_; }
        int height() const { return this->h_; }
        uint32_t frameCount() const { return this->frame_count_; }
        uint16_t fpsNum() const { return this->fps_num_; }
        uint16_t fpsDen() const { return this->fps_den_; }
        // パレット(RGB565。PICO_GFX::COLORSと同じ形)
        const uint16_t* palette() const { return this->palette_; }
        // 長さ(ミリ秒)と、ある時刻に出すべきフレーム
        uint32_t durationMs() const { return this->frameToMs(this->frame_count_); }
        uint32_t frameToMs(uint32_t frame) const;
        uint32_t msToFrame(uint32_t ms) const;

        // 今出している絵。1行は width()/2 バイト、左の画素が上位4bit
        const uint8_t* screen() const { return this->screen_; }
        int stride() const { return this->w_ / 2; }

        // 次のフレームを1枚解いて絵へ当てる。最後まで来たら Error::Ended
        Error decodeNext();
        // 今の絵が何フレーム目か(まだ1枚も解いていなければ -1)
        int32_t current() const { return this->current_; }
        // frame 目の絵にする(その前の一番近いキーフレームから解き直す。進む方向で近ければ目録は使わない)
        Error seekTo(uint32_t frame);
        Error lastError() const { return this->error_; }

        // 前に takeDirty() してから変わったタイルを、タイルの行ごとの横の範囲で返す。
        // fn(tile_row, col_first, col_last) を変わった行の数だけ呼ぶ(座標はタイル単位)
        template<class F> void takeDirty(F&& fn){
            for(int r = 0; r < this->tile_rows_; r++){
                if(this->dirty_lo_[r] <= this->dirty_hi_[r]) fn(r, this->dirty_lo_[r], this->dirty_hi_[r]);
                this->dirty_lo_[r] = 0xFF;
                this->dirty_hi_[r] = 0;
            }
        }
        void markAllDirty();

    private:
        FsFile file_;
        uint8_t* screen_ = nullptr;
        uint32_t* index_ = nullptr;      // [frame, offset] の組 × index_count_
        uint32_t index_count_ = 0;

        int w_ = 0, h_ = 0;
        int tile_cols_ = 0, tile_rows_ = 0;
        int bitmap_bytes_ = 0;
        uint16_t fps_num_ = 0, fps_den_ = 1;
        uint32_t frame_count_ = 0;
        uint32_t index_offset_ = 0;
        uint32_t file_size_ = 0;
        uint16_t palette_[16] = {};

        int32_t current_ = -1;
        uint32_t next_offset_ = 0;       // 次のフレームの位置
        Error error_ = Error::None;

        uint8_t dirty_lo_[kMaxTileRows];
        uint8_t dirty_hi_[kMaxTileRows];

        // 512バイトずつの読み込み
        uint8_t buf_[512];
        uint32_t buf_pos_ = 0;           // buf_[0] のファイル上の位置
        uint16_t buf_len_ = 0;
        uint16_t buf_at_ = 0;
        bool readAt(uint32_t offset, void* dst, size_t n);
        void seekRead(uint32_t offset);
        bool readBytes(void* dst, size_t n);
        uint32_t readPos() const { return this->buf_pos_ + this->buf_at_; }

        Error fail(Error e){ this->error_ = e; return e; }
        Error decodeAt(uint32_t offset, bool key_only_check);
        void putTile(int tile, const uint8_t* rows4bpp);
};
