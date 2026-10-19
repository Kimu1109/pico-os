#include "apps/video/Pvid_Player.hpp"

#include "OS_Data.hpp"

#include <cstdlib>
#include <cstring>

namespace {
    constexpr int kHeaderSize = 64;
    constexpr uint8_t kVersion = 1;
    constexpr uint8_t kFlagKey = 0x01;
    constexpr uint8_t kModeRaw = 0, kModeSolid = 1, kModeTwo = 2;
    constexpr int kTileBytes = PvidPlayer::kTile * PvidPlayer::kTile / 2;

    uint16_t U16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
    uint32_t U32(const uint8_t* p){
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}

const char* PvidPlayer::ErrorToStr(Error e){
    switch(e){
        case Error::None:        return "問題なし";
        case Error::NoSd:        return "SDカードがありません";
        case Error::Open:        return "ファイルを開けません";
        case Error::BadHeader:   return ".pvidの動画ではありません";
        case Error::Unsupported: return "対応していない版・大きさの動画です";
        case Error::BadIndex:    return "動画の目録が壊れています";
        case Error::OutOfMemory: return "メモリが足りません";
        case Error::BadFrame:    return "動画のデータが壊れています";
        case Error::Ended:       return "最後まで再生しました";
    }
    return "不明な誤り";
}

void PvidPlayer::close(){
    if(this->file_) this->file_.close();
    free(this->screen_);
    this->screen_ = nullptr;
    free(this->index_);
    this->index_ = nullptr;
    this->index_count_ = 0;
    this->w_ = this->h_ = 0;
    this->tile_rows_ = this->tile_cols_ = 0;
    this->frame_count_ = 0;
    this->current_ = -1;
    this->error_ = Error::None;
    this->buf_len_ = this->buf_at_ = 0;
}

PvidPlayer::Error PvidPlayer::open(const char* path){
    this->close();
    if(!OSData::SD_usable) return this->fail(Error::NoSd);
    if(!path) return this->fail(Error::Open);
    this->file_ = OSData::SD.open(path, O_RDONLY);
    if(!this->file_) return this->fail(Error::Open);
    this->file_size_ = (uint32_t)this->file_.fileSize();

    uint8_t h[kHeaderSize];
    if(!this->readAt(0, h, sizeof(h)) || memcmp(h, "PVID", 4) != 0){
        this->close();
        return this->fail(Error::BadHeader);
    }
    const int w = U16(h + 8), hh = U16(h + 10);
    this->fps_num_ = U16(h + 12);
    this->fps_den_ = U16(h + 14);
    this->frame_count_ = U32(h + 16);
    this->index_offset_ = U32(h + 20);
    const uint32_t index_count = U32(h + 24);
    if(h[4] != kVersion || h[5] != kTile || U16(h + 6) != kHeaderSize ||
       w <= 0 || hh <= 0 || w > kMaxW || hh > kMaxH || (w % kTile) || (hh % kTile) ||
       this->fps_num_ == 0 || this->fps_den_ == 0 || this->fps_num_ / this->fps_den_ > 60){
        this->close();
        return this->fail(Error::Unsupported);
    }
    if(this->frame_count_ == 0 || index_count == 0 || index_count > kMaxIndex ||
       this->index_offset_ < (uint32_t)kHeaderSize || this->index_offset_ > this->file_size_ ||
       this->file_size_ - this->index_offset_ < index_count * 8){
        this->close();
        return this->fail(Error::BadIndex);
    }
    for(int i = 0; i < 16; i++) this->palette_[i] = U16(h + 32 + i * 2);

    this->w_ = w;
    this->h_ = hh;
    this->tile_cols_ = w / kTile;
    this->tile_rows_ = hh / kTile;
    this->bitmap_bytes_ = (this->tile_cols_ * this->tile_rows_ + 7) / 8;

    // 目録: フレーム番号が増える順・位置は見出しより後ろで目録より前・最初は0フレーム目
    this->index_ = static_cast<uint32_t*>(malloc((size_t)index_count * 8));
    this->screen_ = static_cast<uint8_t*>(malloc((size_t)this->stride() * this->h_));
    if(!this->index_ || !this->screen_){
        this->close();
        return this->fail(Error::OutOfMemory);
    }
    this->index_count_ = index_count;
    this->seekRead(this->index_offset_);
    for(uint32_t i = 0; i < index_count; i++){
        uint8_t e[8];
        if(!this->readBytes(e, 8)){ this->close(); return this->fail(Error::BadIndex); }
        const uint32_t fr = U32(e), off = U32(e + 4);
        const bool ok = fr < this->frame_count_ && off >= (uint32_t)kHeaderSize && off < this->index_offset_ &&
                        (i == 0 ? fr == 0 : fr > this->index_[(i - 1) * 2]);
        if(!ok){ this->close(); return this->fail(Error::BadIndex); }
        this->index_[i * 2] = fr;
        this->index_[i * 2 + 1] = off;
    }

    memset(this->screen_, 0, (size_t)this->stride() * this->h_);
    this->current_ = -1;
    this->next_offset_ = this->index_[1];
    this->error_ = Error::None;
    this->markAllDirty();
    return Error::None;
}

uint32_t PvidPlayer::frameToMs(uint32_t frame) const {
    if(this->fps_num_ == 0) return 0;
    return (uint32_t)((uint64_t)frame * 1000u * this->fps_den_ / this->fps_num_);
}

uint32_t PvidPlayer::msToFrame(uint32_t ms) const {
    if(this->fps_den_ == 0) return 0;
    return (uint32_t)((uint64_t)ms * this->fps_num_ / (1000u * (uint64_t)this->fps_den_));
}

void PvidPlayer::markAllDirty(){
    for(int r = 0; r < this->tile_rows_; r++){
        this->dirty_lo_[r] = 0;
        this->dirty_hi_[r] = (uint8_t)(this->tile_cols_ - 1);
    }
}

// ---------------------------------------------------------------- 読み込み

void PvidPlayer::seekRead(uint32_t offset){
    if(offset >= this->buf_pos_ && offset < this->buf_pos_ + this->buf_len_){
        this->buf_at_ = (uint16_t)(offset - this->buf_pos_);
        return;
    }
    this->buf_pos_ = offset;
    this->buf_len_ = 0;
    this->buf_at_ = 0;
}

bool PvidPlayer::readBytes(void* dst, size_t n){
    uint8_t* out = static_cast<uint8_t*>(dst);
    while(n > 0){
        if(this->buf_at_ >= this->buf_len_){
            const uint32_t pos = this->buf_pos_ + this->buf_len_;
            if(pos >= this->file_size_) return false;
            if(!this->file_.seekSet(pos)) return false;
            const int got = this->file_.read(this->buf_, sizeof(this->buf_));
            if(got <= 0) return false;
            this->buf_pos_ = pos;
            this->buf_len_ = (uint16_t)got;
            this->buf_at_ = 0;
        }
        size_t take = this->buf_len_ - this->buf_at_;
        if(take > n) take = n;
        memcpy(out, this->buf_ + this->buf_at_, take);
        this->buf_at_ += (uint16_t)take;
        out += take;
        n -= take;
    }
    return true;
}

bool PvidPlayer::readAt(uint32_t offset, void* dst, size_t n){
    this->seekRead(offset);
    return this->readBytes(dst, n);
}

// ---------------------------------------------------------------- 解く

void PvidPlayer::putTile(int tile, const uint8_t* rows4bpp){
    const int tx = tile % this->tile_cols_, ty = tile / this->tile_cols_;
    const int st = this->stride();
    uint8_t* dst = this->screen_ + (size_t)ty * kTile * st + tx * (kTile / 2);
    for(int r = 0; r < kTile; r++) memcpy(dst + r * st, rows4bpp + r * (kTile / 2), kTile / 2);
    if(tx < this->dirty_lo_[ty]) this->dirty_lo_[ty] = (uint8_t)tx;
    if(tx > this->dirty_hi_[ty]) this->dirty_hi_[ty] = (uint8_t)tx;
}

PvidPlayer::Error PvidPlayer::decodeAt(uint32_t offset, bool must_be_key){
    uint8_t head[5];
    if(!this->readAt(offset, head, 5)) return this->fail(Error::BadFrame);
    const uint32_t length = U32(head);
    const uint32_t end = offset + 4 + length;
    if(length < (uint32_t)(1 + this->bitmap_bytes_) || end < offset || end > this->index_offset_){
        return this->fail(Error::BadFrame);
    }
    if(must_be_key && !(head[4] & kFlagKey)) return this->fail(Error::BadFrame);

    uint8_t bitmap[(kMaxW / kTile) * (kMaxH / kTile) / 8 + 1];
    if(!this->readBytes(bitmap, (size_t)this->bitmap_bytes_)) return this->fail(Error::BadFrame);

    const int n_tiles = this->tile_cols_ * this->tile_rows_;
    uint8_t tile[kTileBytes];
    for(int i = 0; i < n_tiles; i++){
        if(!(bitmap[i >> 3] & (1u << (i & 7)))) continue;
        uint8_t th;
        if(this->readPos() >= end || !this->readBytes(&th, 1)) return this->fail(Error::BadFrame);
        const uint8_t mode = th >> 4, c0 = th & 0x0F;
        if(mode == kModeSolid){
            memset(tile, (uint8_t)((c0 << 4) | c0), sizeof(tile));
        }else if(mode == kModeTwo){
            uint8_t extra[1 + kTile];
            if(!this->readBytes(extra, sizeof(extra))) return this->fail(Error::BadFrame);
            const uint8_t c1 = extra[0] & 0x0F;
            for(int r = 0; r < kTile; r++){
                const uint8_t m = extra[1 + r];
                for(int b = 0; b < kTile / 2; b++){
                    const uint8_t hi = (m & (0x80 >> (b * 2))) ? c1 : c0;
                    const uint8_t lo = (m & (0x40 >> (b * 2))) ? c1 : c0;
                    tile[r * (kTile / 2) + b] = (uint8_t)((hi << 4) | lo);
                }
            }
        }else if(mode == kModeRaw){
            if(!this->readBytes(tile, sizeof(tile))) return this->fail(Error::BadFrame);
        }else{
            return this->fail(Error::BadFrame);
        }
        if(this->readPos() > end) return this->fail(Error::BadFrame);
        this->putTile(i, tile);
    }
    if(this->readPos() != end) return this->fail(Error::BadFrame);
    this->next_offset_ = end;
    return Error::None;
}

PvidPlayer::Error PvidPlayer::decodeNext(){
    if(!this->screen_) return this->fail(Error::Open);
    if(this->error_ != Error::None) return this->error_;
    if(this->current_ + 1 >= (int32_t)this->frame_count_) return Error::Ended;
    // 最初のフレームはキーフレームでなければならない(差分を当てる絵が無いため)
    const Error e = this->decodeAt(this->next_offset_, this->current_ < 0);
    if(e != Error::None) return e;
    this->current_++;
    return Error::None;
}

PvidPlayer::Error PvidPlayer::seekTo(uint32_t frame){
    if(!this->screen_) return this->fail(Error::Open);
    if(this->error_ != Error::None) return this->error_;
    if(frame >= this->frame_count_) frame = this->frame_count_ - 1;
    if((int32_t)frame == this->current_) return Error::None;

    // frame 以前で一番近いキーフレーム
    uint32_t k = 0;
    for(uint32_t i = 0; i < this->index_count_ && this->index_[i * 2] <= frame; i++) k = i;
    const uint32_t key_frame = this->index_[k * 2];

    // 今より先で、キーフレームより今のほうが近ければそのまま進める
    if(!((int32_t)frame > this->current_ && this->current_ >= (int32_t)key_frame)){
        this->current_ = (int32_t)key_frame - 1;
        this->next_offset_ = this->index_[k * 2 + 1];
        const Error e = this->decodeAt(this->next_offset_, true);
        if(e != Error::None) return e;
        this->current_ = (int32_t)key_frame;
    }
    while(this->current_ < (int32_t)frame){
        const Error e = this->decodeNext();
        if(e != Error::None) return e;
    }
    return Error::None;
}
