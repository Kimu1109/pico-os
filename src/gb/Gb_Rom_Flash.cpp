#include "gb/Gb_Rom_Flash.hpp"

#include "storage/Flash_Area.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

static constexpr char kKind[4] = { 'G', 'B', 'R', 'M' };
static constexpr uint32_t kFnvBasis = 2166136261u;

uint32_t GbRomFlash::Fnv(uint32_t h, const uint8_t* p, size_t n){
    for(size_t i = 0; i < n; i++){
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t GbRomFlash::HeaderHash(const Header& h){
    return Fnv(kFnvBasis, reinterpret_cast<const uint8_t*>(&h), offsetof(Header, header_hash));
}

uint32_t GbRomFlash::Capacity(const FlashArea* area){
    if(!area || area->size() <= kDataOffset) return 0;
    return area->size() - kDataOffset;
}

void GbRomFlash::Invalidate(FlashArea* area){
    if(area && area->holdsRawData()) area->erase(0, FlashArea::kSectorBytes);
}

const uint8_t* GbRomFlash::image() const {
    if(this->state_ != State::Done || !this->area_) return nullptr;
    return this->area_->data() + kDataOffset;
}

void GbRomFlash::fail(const char* msg){
    if(this->file_) this->file_.close();
    this->message_.assign(msg);
    this->state_ = State::Failed;
    LOG_APP_WARN("GbRomFlash: %s", msg);
}

void GbRomFlash::cancel(){
    if(this->file_) this->file_.close();
    if(this->state_ == State::Writing) this->state_ = State::Idle;
}

bool GbRomFlash::readAt(uint32_t off, uint8_t* p, uint32_t n){
    if(!this->file_.seekSet(off)) return false;
    uint32_t done = 0;
    while(done < n){
        const int got = this->file_.read(p + done, n - done);
        if(got <= 0) return false;
        done += (uint32_t)got;
    }
    return true;
}

// 頭と末尾 kSampleBytes のハッシュ(重なる小さなROMは全体を1回)
bool GbRomFlash::computeSample(){
    uint32_t h = kFnvBasis;
    const uint32_t head = this->size_ < kSampleBytes ? this->size_ : kSampleBytes;
    const uint32_t tail_start = this->size_ > 2 * kSampleBytes ? this->size_ - kSampleBytes : head;
    for(uint32_t off = 0; off < head; ){
        const uint32_t n = (head - off) < kChunkBytes ? (head - off) : kChunkBytes;
        if(!this->readAt(off, this->buf_, n)) return false;
        h = Fnv(h, this->buf_, n);
        off += n;
    }
    for(uint32_t off = tail_start; off < this->size_; ){
        const uint32_t n = (this->size_ - off) < kChunkBytes ? (this->size_ - off) : kChunkBytes;
        if(!this->readAt(off, this->buf_, n)) return false;
        h = Fnv(h, this->buf_, n);
        off += n;
    }
    this->sample_hash_ = h;
    return this->readAt(kRomHeaderStart, this->rom_header_, sizeof(this->rom_header_));
}

bool GbRomFlash::readHeader(Header& h) const {
    if(!this->area_->holdsRawData()) return false;
    memcpy(&h, this->area_->data(), sizeof(h));
    return memcmp(h.kind, kKind, sizeof(kKind)) == 0 && h.version == kVersion
        && h.data_offset == kDataOffset && h.header_hash == HeaderHash(h)
        && h.rom_size > 0 && h.rom_size <= Capacity(this->area_);
}

GbRomFlash::State GbRomFlash::begin(const char* rom_path){
    this->cancel();
    this->state_ = State::Idle;
    this->reused_ = false;
    this->message_.clear();
    this->size_ = this->pos_ = this->erased_to_ = 0;
    this->full_hash_ = kFnvBasis;

    const uint32_t cap = Capacity(this->area_);
    if(cap == 0){ this->fail("ROMを置く内蔵フラッシュの領域がありません"); return this->state_; }
    if(!OSData::SD_usable){ this->fail("SDカードが使えません"); return this->state_; }
    if(!rom_path || !rom_path[0]){ this->fail("ROMを開けません"); return this->state_; }

    this->file_ = OSData::SD.open(rom_path, O_RDONLY);
    if(!this->file_ || this->file_.isDirectory()){ this->fail("ROMを開けません"); return this->state_; }
    const uint64_t file_size = this->file_.fileSize();
    if(file_size < kRomHeaderEnd){ this->fail("ROMとして短すぎます"); return this->state_; }
    if(file_size > cap){
        char text[96];
        snprintf(text, sizeof(text), "ROMが大きすぎます(内蔵フラッシュに置けるのは%luKBまで)", (unsigned long)(cap / 1024));
        this->fail(text);
        return this->state_;
    }
    this->size_ = (uint32_t)file_size;
    if(!this->computeSample()){ this->fail("ROMの読み込みに失敗しました"); return this->state_; }

    Header h;
    if(this->readHeader(h) && h.rom_size == this->size_ && h.sample_hash == this->sample_hash_
       && memcmp(h.rom_header, this->rom_header_, sizeof(h.rom_header)) == 0
       && Fnv(kFnvBasis, this->area_->data() + kDataOffset, this->size_) == h.full_hash){
        this->file_.close();
        this->reused_ = true;
        this->pos_ = this->size_;
        this->state_ = State::Done;
        LOG_APP_MSG("GbRomFlash: 内蔵フラッシュに置いてある %s(%luB)をそのまま使います", rom_path, (unsigned long)this->size_);
        return this->state_;
    }

    // 書き始める前に見出しを消す(途中で止まっても古い見出しが新しい中身を名乗らないように)
    if(!this->area_->erase(0, FlashArea::kSectorBytes)){ this->fail("内蔵フラッシュを消せません"); return this->state_; }
    if(!this->file_.seekSet(0)){ this->fail("ROMの読み込みに失敗しました"); return this->state_; }
    this->state_ = State::Writing;
    LOG_APP_MSG("GbRomFlash: %s(%luB)を内蔵フラッシュへ書きます", rom_path, (unsigned long)this->size_);
    return this->state_;
}

// 次に書く所までを消す。64KB境界に揃っていて残りが64KB以上なら64KB、それ以外は4KB
bool GbRomFlash::eraseNext(){
    const uint32_t need = (this->size_ + FlashArea::kSectorBytes - 1) / FlashArea::kSectorBytes * FlashArea::kSectorBytes;
    const uint32_t off = kDataOffset + this->erased_to_;
    uint32_t len = FlashArea::kSectorBytes;
    if((this->area_->baseAddress() + off) % FlashArea::kBlockBytes == 0 && need - this->erased_to_ >= FlashArea::kBlockBytes){
        len = FlashArea::kBlockBytes;
    }
    if(!this->area_->erase(off, len)) return false;
    this->erased_to_ += len;
    return true;
}

bool GbRomFlash::writeChunk(){
    const uint32_t n = (this->size_ - this->pos_) < kChunkBytes ? (this->size_ - this->pos_) : kChunkBytes;
    uint32_t got_all = 0;
    while(got_all < n){
        const int got = this->file_.read(this->buf_ + got_all, n - got_all);
        if(got <= 0){ this->fail("ROMの読み込みに失敗しました"); return false; }
        got_all += (uint32_t)got;
    }
    this->full_hash_ = Fnv(this->full_hash_, this->buf_, n);
    // ページ(256B)に揃える。余りは消したままの0xFF
    const uint32_t padded = (n + FlashArea::kPageBytes - 1) / FlashArea::kPageBytes * FlashArea::kPageBytes;
    if(padded > n) memset(this->buf_ + n, 0xFF, padded - n);
    const uint32_t off = kDataOffset + this->pos_;
    if(!this->area_->program(off, this->buf_, padded)){ this->fail("内蔵フラッシュへ書けません"); return false; }
    if(memcmp(this->area_->data() + off, this->buf_, n) != 0){
        this->fail("内蔵フラッシュに書いた内容が合いません");
        return false;
    }
    this->pos_ += n;
    return true;
}

bool GbRomFlash::writeHeader(){
    Header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, FlashArea::kRawMagic, sizeof(h.magic));
    memcpy(h.kind, kKind, sizeof(h.kind));
    h.version = kVersion;
    h.data_offset = kDataOffset;
    h.rom_size = this->size_;
    h.sample_hash = this->sample_hash_;
    h.full_hash = this->full_hash_;
    memcpy(h.rom_header, this->rom_header_, sizeof(h.rom_header));
    h.header_hash = HeaderHash(h);

    static_assert(sizeof(Header) <= FlashArea::kPageBytes, "見出しは1ページに収める");
    memset(this->buf_, 0xFF, FlashArea::kPageBytes);
    memcpy(this->buf_, &h, sizeof(h));
    if(!this->area_->program(0, this->buf_, FlashArea::kPageBytes)
       || memcmp(this->area_->data(), this->buf_, FlashArea::kPageBytes) != 0){
        this->fail("内蔵フラッシュへ見出しを書けません");
        return false;
    }
    return true;
}

void GbRomFlash::step(uint32_t budget_ms){
    if(this->state_ != State::Writing) return;
    if(!OSData::SD_usable){ this->fail("SDカードが使えません"); return; }
    const uint32_t start = millis();
    do{
        if(this->pos_ >= this->size_){
            this->file_.close();
            if(!this->writeHeader()) return;
            this->state_ = State::Done;
            LOG_APP_OK("GbRomFlash: 内蔵フラッシュへ %luB 書きました", (unsigned long)this->size_);
            return;
        }
        if(this->pos_ + kChunkBytes > this->erased_to_ && this->erased_to_ < this->size_){
            if(!this->eraseNext()){ this->fail("内蔵フラッシュを消せません"); return; }
        }else if(!this->writeChunk()){
            return;
        }
    }while(millis() - start < budget_ms);
}
