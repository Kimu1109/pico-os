#include "sound/Flac_Decoder.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstring>

namespace {
    uint32_t BE32(const uint8_t* p){ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
    uint32_t BE24(const uint8_t* p){ return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }

    // ID3v2 の大きさ(7bitずつの4バイト)。タグでなければ0
    uint32_t Id3v2Size(const uint8_t* h){
        if(memcmp(h, "ID3", 3) != 0) return 0;
        if((h[6] | h[7] | h[8] | h[9]) & 0x80) return 0;
        const uint32_t body = ((uint32_t)h[6] << 21) | ((uint32_t)h[7] << 14) | ((uint32_t)h[8] << 7) | h[9];
        return 10 + body + ((h[5] & 0x10) ? 10 : 0);     //フッタ付き
    }

    // フレームの見出しのCRC-8(多項式 x^8+x^2+x+1、初期値0)
    uint8_t Crc8(const uint8_t* p, size_t n){
        uint8_t c = 0;
        for(size_t i = 0; i < n; i++){
            c ^= p[i];
            for(int k = 0; k < 8; k++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
        }
        return c;
    }

    constexpr uint32_t kRateTable[16] = {
        0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, 0, 0, 0, 0
    };
    constexpr uint8_t kBpsTable[8] = {0, 8, 12, 0, 16, 20, 24, 32};

    // 符号なしでの足し引き(壊れたデータで符号付きの桁あふれ=未定義動作にしない)
    inline int32_t Add(int32_t a, int32_t b){ return (int32_t)((uint32_t)a + (uint32_t)b); }
    inline int32_t Mul(int32_t a, int32_t k){ return (int32_t)((uint32_t)a * (uint32_t)k); }
    inline int32_t Neg(int32_t a){ return (int32_t)(0u - (uint32_t)a); }

    inline int16_t Clamp16(int32_t v){ return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }
}

const char* FlacDecoder::ErrorText(Error e){
    switch(e){
        case Error::None:        return "";
        case Error::NoSd:        return "SDカードがありません";
        case Error::OpenFailed:  return "ファイルを開けません";
        case Error::NotFlac:     return "FLACファイルではありません";
        case Error::Unsupported: return "このFLACは再生できません(2ch以下・24bit以下・96kHz以下・ブロック長4608以下のみ)";
        case Error::NoData:      return "音のデータがありません";
    }
    return "";
}

bool FlacDecoder::fail(Error e){
    this->close();
    this->error_ = e;
    return false;
}

void FlacDecoder::close(){
    if(this->open_) this->f_.close();
    this->open_ = false;
    this->finished_ = true;
}

uint32_t FlacDecoder::durationMs() const {
    if(this->rate_ == 0 || this->total_samples_ == 0) return 0;
    return (uint32_t)(this->total_samples_ * 1000 / this->rate_);
}

// ================================================================
// ファイルとビットの読み取り
// ================================================================

bool FlacDecoder::seekFile(uint32_t pos){
    this->buf_base_ = pos;
    this->buf_pos_ = this->buf_len_ = 0;
    this->acc_ = 0;
    this->nbits_ = 0;
    this->exhausted_ = false;
    return pos <= this->file_size_ && this->f_.seek(pos);
}

uint32_t FlacDecoder::bytePos() const {
    return this->buf_base_ + (uint32_t)this->buf_pos_ - this->nbits_ / 8;
}

bool FlacDecoder::refill(){
    if(this->buf_pos_ < this->buf_len_) return true;
    this->buf_base_ += (uint32_t)this->buf_len_;
    const int n = this->f_.read(this->buf_, kBufferBytes);
    this->buf_pos_ = 0;
    this->buf_len_ = n > 0 ? (size_t)n : 0;
    return this->buf_len_ > 0;
}

void FlacDecoder::loadMore(){
    if(this->buf_pos_ + 4 <= this->buf_len_ && this->nbits_ <= 32){
        this->acc_ = (this->acc_ << 32) | BE32(this->buf_ + this->buf_pos_);
        this->buf_pos_ += 4;
        this->nbits_ += 32;
        return;
    }
    if(this->buf_pos_ < this->buf_len_ || this->refill()){
        this->acc_ = (this->acc_ << 8) | this->buf_[this->buf_pos_++];
        this->nbits_ += 8;
        return;
    }
    //ファイルの終わり。0のビットを足して読み進められるようにし、呼んだ側が exhausted_ を見て打ち切る
    this->exhausted_ = true;
    this->acc_ <<= 8;
    this->nbits_ += 8;
}

uint32_t FlacDecoder::readBits(unsigned n){
    if(n == 0) return 0;
    while(this->nbits_ < n) this->loadMore();
    this->nbits_ -= n;
    return (uint32_t)((this->acc_ >> this->nbits_) & ((1ull << n) - 1));
}

int32_t FlacDecoder::readSigned(unsigned n){
    if(n == 0) return 0;
    const uint32_t v = this->readBits(n);
    return (int32_t)(v << (32 - n)) >> (32 - n);
}

uint32_t FlacDecoder::readUnary(){
    uint32_t q = 0;
    for(;;){
        if(this->nbits_ == 0) this->loadMore();
        const uint64_t x = this->acc_ << (64 - this->nbits_);
        if(x){
            const unsigned lz = (unsigned)__builtin_clzll(x);
            q += lz;
            this->nbits_ -= lz + 1;
            return q;
        }
        q += this->nbits_;
        this->nbits_ = 0;
        if(this->exhausted_ || q > (1u << 24)) return q;     //壊れていて終わりが来ない
    }
}

// ================================================================
// 見出し
// ================================================================

bool FlacDecoder::open(const char* path, uint32_t out_rate){
    this->close();
    this->error_ = Error::None;
    this->channels_ = this->bps_ = 0;
    this->rate_ = 0;
    this->min_block_ = this->max_block_ = 0;
    this->total_samples_ = 0;
    this->audio_start_ = this->file_size_ = this->fixed_block_ = 0;
    this->seek_pos_ = this->seek_count_ = 0;
    this->block_len_ = this->block_pos_ = 0;
    this->block_first_ = this->next_sample_ = this->skip_until_ = 0;
    this->buf_base_ = 0;
    this->buf_pos_ = this->buf_len_ = 0;
    this->acc_ = 0;
    this->nbits_ = 0;
    this->exhausted_ = false;
    this->stats_ = DecodeStats();
    if(!OSData::SD_usable) return this->fail(Error::NoSd);

    //reopen()は覚えているパスそのものを渡してくる(自分自身へのコピーは避ける)
    if(path != this->path_){
        this->path_[0] = '\0';
        if(strlen(path) < sizeof(this->path_)) strcpy(this->path_, path);
    }
    this->out_rate_ = out_rate ? out_rate : 22050;

    this->f_ = OSData::SD.open(path, O_RDONLY);
    if(!this->f_) return this->fail(Error::OpenFailed);
    this->open_ = true;
    this->file_size_ = (uint32_t)this->f_.fileSize();

    if(!this->parseMetadata()) return false;

    //最初のフレームがあるか確かめる(メタデータの後の詰め物は飛ばす)
    FrameHeader h;
    uint32_t at = 0;
    if(!this->probeFrame(this->audio_start_, h, at)) return this->fail(Error::NoData);
    this->audio_start_ = at;

    this->startAt(this->audio_start_, 0);
    this->finished_ = false;
    this->lanes_ = (this->stereo_out_ && this->channels_ >= 2) ? 2 : 1;
    this->rs_.setup(this->rate_, this->out_rate_, this->lanes_, &FlacDecoder::Source, this);
    return true;
}

bool FlacDecoder::parseMetadata(){
    uint8_t h[34];
    //頭のID3v2(続けて2つ以上あることもある)
    uint32_t pos = 0;
    for(int i = 0; i < 4; i++){
        if(pos + 10 > this->file_size_ || !this->f_.seek(pos) || this->f_.read(h, 10) != 10) break;
        const uint32_t size = Id3v2Size(h);
        if(size == 0) break;
        pos += size;
    }
    if(pos + 8 > this->file_size_ || !this->f_.seek(pos) || this->f_.read(h, 4) != 4 || memcmp(h, "fLaC", 4) != 0){
        return this->fail(Error::NotFlac);
    }
    pos += 4;

    bool got_info = false;
    for(int i = 0; i < 64; i++){
        if(pos + 4 > this->file_size_ || !this->f_.seek(pos) || this->f_.read(h, 4) != 4) return this->fail(Error::NotFlac);
        const bool last = (h[0] & 0x80) != 0;
        const uint8_t type = h[0] & 0x7F;
        const uint32_t len = BE24(h + 1);
        const uint32_t body = pos + 4;
        if(type == 0){
            if(len < 34 || i != 0 || body + 34 > this->file_size_) return this->fail(Error::NotFlac);
            uint8_t si[34];
            if(this->f_.read(si, 34) != 34) return this->fail(Error::NotFlac);
            this->min_block_ = (uint16_t)((si[0] << 8) | si[1]);
            this->max_block_ = (uint16_t)((si[2] << 8) | si[3]);
            this->rate_ = ((uint32_t)si[10] << 12) | ((uint32_t)si[11] << 4) | (si[12] >> 4);
            this->channels_ = (uint16_t)(((si[12] >> 1) & 7) + 1);
            this->bps_ = (uint16_t)((((si[12] & 1) << 4) | (si[13] >> 4)) + 1);
            this->total_samples_ = ((uint64_t)(si[13] & 0x0F) << 32) | ((uint64_t)si[14] << 24)
                                 | ((uint64_t)si[15] << 16) | ((uint64_t)si[16] << 8) | si[17];
            got_info = true;
        }else if(type == 3 && !this->seek_count_){
            this->seek_pos_ = body;
            this->seek_count_ = len / 18;
        }
        pos = body + len;
        if(last) break;
    }
    if(!got_info) return this->fail(Error::NotFlac);

    if(this->rate_ == 0) return this->fail(Error::NotFlac);
    if(this->channels_ > 2 || this->bps_ < 4 || this->bps_ > 24 || this->rate_ > kMaxRate || this->max_block_ > kMaxBlock){
        return this->fail(Error::Unsupported);
    }
    if(pos >= this->file_size_) return this->fail(Error::NoData);     //見出しだけで音が無い
    this->fixed_block_ = (this->min_block_ == this->max_block_) ? this->max_block_ : 0;
    this->audio_start_ = pos;
    return true;
}

bool FlacDecoder::readFrameHeader(FrameHeader& h){
    uint8_t b[16];
    size_t n = 0;
    for(int i = 0; i < 4; i++) b[n++] = (uint8_t)this->readBits(8);
    if(b[0] != 0xFF || (b[1] & 0xFE) != 0xF8 || (b[3] & 1)) return false;
    h.variable = (b[1] & 1) != 0;

    //標本(フレーム)番号。UTF-8と同じ符号化で最大7バイト
    uint8_t c = (uint8_t)this->readBits(8);
    b[n++] = c;
    unsigned extra;
    if(c < 0x80) extra = 0;
    else if((c & 0xE0) == 0xC0) extra = 1;
    else if((c & 0xF0) == 0xE0) extra = 2;
    else if((c & 0xF8) == 0xF0) extra = 3;
    else if((c & 0xFC) == 0xF8) extra = 4;
    else if((c & 0xFE) == 0xFC) extra = 5;
    else if(c == 0xFE) extra = 6;
    else return false;
    uint64_t num = extra == 0 ? c : (c & ((1u << (6 - extra)) - 1));
    for(unsigned i = 0; i < extra; i++){
        const uint8_t d = (uint8_t)this->readBits(8);
        b[n++] = d;
        if((d & 0xC0) != 0x80) return false;
        num = (num << 6) | (d & 0x3F);
    }
    h.number = num;

    const unsigned bs_code = b[2] >> 4;
    const unsigned sr_code = b[2] & 0x0F;
    if(bs_code == 0) return false;
    if(bs_code == 1) h.blocksize = 192;
    else if(bs_code <= 5) h.blocksize = 576u << (bs_code - 2);
    else if(bs_code >= 8) h.blocksize = 256u << (bs_code - 8);
    else if(bs_code == 6){
        const uint8_t v = (uint8_t)this->readBits(8);
        b[n++] = v;
        h.blocksize = (uint32_t)v + 1;
    }else{
        const uint8_t v0 = (uint8_t)this->readBits(8);
        const uint8_t v1 = (uint8_t)this->readBits(8);
        b[n++] = v0;
        b[n++] = v1;
        h.blocksize = (((uint32_t)v0 << 8) | v1) + 1;
    }

    if(sr_code == 0) h.rate = this->rate_;
    else if(sr_code <= 11) h.rate = kRateTable[sr_code];
    else if(sr_code == 12){
        const uint8_t v = (uint8_t)this->readBits(8);
        b[n++] = v;
        h.rate = (uint32_t)v * 1000;
    }else if(sr_code == 13 || sr_code == 14){
        const uint8_t v0 = (uint8_t)this->readBits(8);
        const uint8_t v1 = (uint8_t)this->readBits(8);
        b[n++] = v0;
        b[n++] = v1;
        h.rate = (((uint32_t)v0 << 8) | v1) * (sr_code == 14 ? 10 : 1);
    }else{
        return false;
    }

    const unsigned assign = b[3] >> 4;
    if(assign < 8){ h.channels = (uint8_t)(assign + 1); h.assign = (uint8_t)assign; }
    else if(assign <= 10){ h.channels = 2; h.assign = (uint8_t)assign; }
    else return false;

    const unsigned size_code = (b[3] >> 1) & 7;
    if(size_code == 0) h.bps = (uint8_t)this->bps_;
    else if(kBpsTable[size_code] != 0) h.bps = kBpsTable[size_code];
    else return false;

    const uint8_t crc = (uint8_t)this->readBits(8);
    if(this->exhausted_) return false;
    return crc == Crc8(b, n);
}

bool FlacDecoder::matchesStream(const FrameHeader& h) const {
    return h.channels == this->channels_ && h.rate == this->rate_ && h.bps == this->bps_
        && h.blocksize >= 1 && h.blocksize <= kMaxBlock;
}

// ================================================================
// 展開
// ================================================================

bool FlacDecoder::decodeResidual(int32_t* out, uint32_t n, unsigned order){
    const uint32_t method = this->readBits(2);
    if(method > 1) return false;
    const unsigned pbits = method ? 5 : 4;
    const uint32_t esc = method ? 31 : 15;
    const unsigned po = this->readBits(4);
    const uint32_t parts = 1u << po;
    if((n & (parts - 1)) != 0 || (n >> po) < order) return false;

    uint32_t idx = order;
    for(uint32_t p = 0; p < parts; p++){
        const uint32_t cnt = (n >> po) - (p == 0 ? order : 0);
        const uint32_t param = this->readBits(pbits);
        if(param == esc){
            const unsigned raw = this->readBits(5);
            for(uint32_t i = 0; i < cnt; i++) out[idx++] = raw ? this->readSigned(raw) : 0;
        }else{
            for(uint32_t i = 0; i < cnt; i++){
                const uint32_t q = this->readUnary();
                const uint32_t r = param ? this->readBits(param) : 0;
                const uint32_t u = (q << param) | r;
                out[idx++] = (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
            }
        }
        if(this->exhausted_) return false;
    }
    return true;
}

bool FlacDecoder::decodeSubframe(int32_t* out, uint32_t n, unsigned bps){
    if(this->readBits(1)) return false;                 //最初のビットは0
    const unsigned type = this->readBits(6);
    unsigned wasted = 0;
    if(this->readBits(1)){
        wasted = this->readUnary() + 1;
        if(wasted >= bps) return false;
    }
    bps -= wasted;

    if(type == 0){
        const int32_t v = this->readSigned(bps);
        for(uint32_t i = 0; i < n; i++) out[i] = v;
    }else if(type == 1){
        for(uint32_t i = 0; i < n; i++) out[i] = this->readSigned(bps);
    }else if(type >= 8 && type <= 12){
        const unsigned order = type - 8;
        if(n <= order) return false;
        for(unsigned i = 0; i < order; i++) out[i] = this->readSigned(bps);
        if(!this->decodeResidual(out, n, order)) return false;
        switch(order){
            case 1:
                for(uint32_t i = 1; i < n; i++) out[i] = Add(out[i], out[i - 1]);
                break;
            case 2:
                for(uint32_t i = 2; i < n; i++) out[i] = Add(out[i], Add(Mul(out[i - 1], 2), Neg(out[i - 2])));
                break;
            case 3:
                for(uint32_t i = 3; i < n; i++) out[i] = Add(out[i], Add(Add(Mul(out[i - 1], 3), Mul(out[i - 2], -3)), out[i - 3]));
                break;
            case 4:
                for(uint32_t i = 4; i < n; i++){
                    out[i] = Add(out[i], Add(Add(Mul(out[i - 1], 4), Mul(out[i - 2], -6)), Add(Mul(out[i - 3], 4), Neg(out[i - 4]))));
                }
                break;
            default: break;
        }
    }else if(type >= 32){
        const unsigned order = type - 31;
        if(n <= order) return false;
        for(unsigned i = 0; i < order; i++) out[i] = this->readSigned(bps);
        const unsigned prec_code = this->readBits(4);
        if(prec_code == 15) return false;
        const unsigned prec = prec_code + 1;
        const int32_t shift = this->readSigned(5);
        if(shift < 0) return false;
        int32_t coef[32];
        for(unsigned j = 0; j < order; j++) coef[j] = this->readSigned(prec);
        if(!this->decodeResidual(out, n, order)) return false;
        //予測(係数の合計は64bitで足す。Cortex-M33は32x32→64の積和が1命令)
        for(uint32_t i = order; i < n; i++){
            int64_t sum = 0;
            const int32_t* s = out + i - 1;
            for(unsigned j = 0; j < order; j++) sum += (int64_t)coef[j] * s[-(int)j];
            out[i] = Add(out[i], (int32_t)(sum >> shift));
        }
    }else{
        return false;
    }
    if(this->exhausted_) return false;

    if(wasted){
        for(uint32_t i = 0; i < n; i++) out[i] = (int32_t)((uint32_t)out[i] << wasted);
    }
    return true;
}

bool FlacDecoder::decodeFrameBody(const FrameHeader& h){
    const uint32_t n = h.blocksize;
    const unsigned bps = h.bps;
    switch(h.assign){
        case 8:     //左 + 差(右 = 左 - 差)
            if(!this->decodeSubframe(this->ch_[0], n, bps) || !this->decodeSubframe(this->ch_[1], n, bps + 1)) return false;
            for(uint32_t i = 0; i < n; i++) this->ch_[1][i] = Add(this->ch_[0][i], Neg(this->ch_[1][i]));
            break;
        case 9:     //差 + 右(左 = 右 + 差)
            if(!this->decodeSubframe(this->ch_[0], n, bps + 1) || !this->decodeSubframe(this->ch_[1], n, bps)) return false;
            for(uint32_t i = 0; i < n; i++) this->ch_[0][i] = Add(this->ch_[0][i], this->ch_[1][i]);
            break;
        case 10:    //中 + 差
            if(!this->decodeSubframe(this->ch_[0], n, bps) || !this->decodeSubframe(this->ch_[1], n, bps + 1)) return false;
            for(uint32_t i = 0; i < n; i++){
                const int32_t side = this->ch_[1][i];
                const int32_t mid = (int32_t)(((uint32_t)this->ch_[0][i] << 1) | ((uint32_t)side & 1));
                this->ch_[0][i] = (Add(mid, side)) >> 1;
                this->ch_[1][i] = (Add(mid, Neg(side))) >> 1;
            }
            break;
        default:
            for(uint8_t c = 0; c < h.channels; c++){
                if(!this->decodeSubframe(this->ch_[c], n, bps)) return false;
            }
            break;
    }
    this->alignByte();
    this->readBits(16);     //フレーム全体のCRC-16(確かめない。ヘッダのコメント参照)
    return !this->exhausted_;
}

bool FlacDecoder::probeFrame(uint32_t from, FrameHeader& h, uint32_t& at){
    const uint32_t limit = (from + kMaxResync < this->file_size_) ? from + kMaxResync : this->file_size_;
    while(from + 8 < this->file_size_ && from < limit){
        if(!this->seekFile(from)) return false;
        //次の同期語(0xFF, 0xF8/0xF9)を探す
        uint32_t cand = 0xFFFFFFFFu;
        int prev = -1;
        for(;;){
            if(this->buf_pos_ >= this->buf_len_ && !this->refill()) break;
            const uint8_t b = this->buf_[this->buf_pos_++];
            const uint32_t here = this->buf_base_ + (uint32_t)this->buf_pos_;     //b の次の位置
            if(prev == 0xFF && (b & 0xFE) == 0xF8){ cand = here - 2; break; }
            prev = b;
            if(here >= limit) break;
        }
        if(cand == 0xFFFFFFFFu) return false;
        if(!this->seekFile(cand)) return false;
        if(this->readFrameHeader(h) && this->matchesStream(h)){
            at = cand;
            return true;
        }
        from = cand + 1;
    }
    return false;
}

bool FlacDecoder::resync(uint32_t from){
    FrameHeader h;
    uint32_t at = 0;
    if(!this->probeFrame(from, h, at)) return false;
    return this->seekFile(at);
}

bool FlacDecoder::decodeBlock(){
    for(int fails = 0; fails < 64;){
        if(this->total_samples_ && this->next_sample_ >= this->total_samples_) return false;
        const uint32_t start = this->bytePos();
        if(start >= this->file_size_) return false;
        FrameHeader h;
        const bool ok = this->readFrameHeader(h) && this->matchesStream(h) && this->decodeFrameBody(h);
        if(!ok){
            //壊れたフレームは捨て、次の同期語まで進む(見つからなければここで終わり)
            if(!this->resync(start + 1)) return false;
            fails++;
            continue;
        }
        const uint64_t first = h.firstSample(this->fixed_block_ ? this->fixed_block_ : h.blocksize);
        uint32_t len = h.blocksize;
        if(this->total_samples_){
            if(first >= this->total_samples_) return false;
            if(first + len > this->total_samples_) len = (uint32_t)(this->total_samples_ - first);
        }
        this->block_first_ = first;
        this->block_len_ = len;
        this->next_sample_ = first + len;
        this->block_pos_ = 0;
        if(this->skip_until_ > first){
            if(this->skip_until_ >= first + len) continue;      //シーク先より前のブロックは丸ごと捨てる
            this->block_pos_ = (uint32_t)(this->skip_until_ - first);
        }
        this->skip_until_ = 0;
        return true;
    }
    return false;
}

bool FlacDecoder::advance(){
    const uint32_t t0 = micros();
    bool ok = this->decodeBlock();
    if(!ok && this->loop_){
        this->startAt(this->audio_start_, 0);
        ok = this->decodeBlock();
    }
    if(ok){
        const uint32_t dt = micros() - t0;
        this->stats_.blocks++;
        this->stats_.total_us += dt;
        this->stats_.total_samples += this->block_len_ - this->block_pos_;
        if(dt > this->stats_.max_us) this->stats_.max_us = dt;
    }
    return ok;
}

void FlacDecoder::startAt(uint32_t pos, uint64_t sample){
    this->seekFile(pos);
    this->block_len_ = this->block_pos_ = 0;
    this->block_first_ = sample;
    this->next_sample_ = sample;
    this->skip_until_ = 0;
}

// ================================================================
// 出力・シーク
// ================================================================

bool FlacDecoder::rawSource(Frame& v){
    if(this->block_pos_ >= this->block_len_ && !this->advance()) return false;
    const uint32_t i = this->block_pos_++;
    int32_t l = this->ch_[0][i];
    int32_t r = this->channels_ == 2 ? this->ch_[1][i] : l;
    const int bps = this->bps_;
    if(bps > 16){
        const int sh = bps - 16;
        const int32_t half = 1 << (sh - 1);
        l = Add(l, half) >> sh;
        r = Add(r, half) >> sh;
    }else if(bps < 16){
        l = (int32_t)((uint32_t)l << (16 - bps));
        r = (int32_t)((uint32_t)r << (16 - bps));
    }
    l = Clamp16(l);
    r = Clamp16(r);
    if(this->lanes_ == 2){
        v.v[0] = (int16_t)l;
        v.v[1] = (int16_t)r;
    }else if(this->channels_ == 2){
        v.v[0] = v.v[1] = (int16_t)((l + r) / 2);
    }else{
        v.v[0] = v.v[1] = (int16_t)l;
    }
    return true;
}

size_t FlacDecoder::read(int16_t* out, size_t max){
    if(!this->open_ || this->finished_) return 0;
    const size_t n = this->rs_.read(out, max);
    if(this->rs_.finished()) this->finished_ = true;
    return n;
}

// 標本番号 sample 以前で一番近い SEEKTABLE の点へ飛ぶ。表が読めない・合わなければ頭から
bool FlacDecoder::seekBySeekTable(uint64_t sample){
    uint64_t best_sample = 0;
    uint64_t best_offset = 0;
    for(uint32_t i = 0; i < this->seek_count_; i++){
        uint8_t e[18];
        if(!this->f_.seek(this->seek_pos_ + i * 18) || this->f_.read(e, 18) != 18) break;
        const uint64_t s = ((uint64_t)BE32(e) << 32) | BE32(e + 4);
        const uint64_t o = ((uint64_t)BE32(e + 8) << 32) | BE32(e + 12);
        if(s == 0xFFFFFFFFFFFFFFFFull) break;       //空き(以降は全部空き)
        if(s > sample) break;
        if(this->audio_start_ + o < this->file_size_){ best_sample = s; best_offset = o; }
    }
    this->startAt(this->audio_start_ + (uint32_t)best_offset, best_sample);
    this->skip_until_ = sample;
    return true;
}

// SEEKTABLE が無いとき。バイト位置を補間と二分で絞り、見つけたフレームの標本番号を見る
bool FlacDecoder::seekByBisect(uint64_t sample){
    if(this->total_samples_ == 0){
        this->startAt(this->audio_start_, 0);
        this->skip_until_ = sample;
        return true;
    }
    uint32_t lo_pos = this->audio_start_;
    uint64_t lo_sample = 0;
    uint32_t hi_pos = this->file_size_;
    uint64_t hi_sample = this->total_samples_;
    for(int iter = 0; iter < 24 && hi_pos - lo_pos > 2 * kBufferBytes; iter++){
        uint32_t guess;
        if((iter & 1) || hi_sample <= lo_sample){
            guess = lo_pos + (hi_pos - lo_pos) / 2;
        }else{
            guess = lo_pos + (uint32_t)((uint64_t)(hi_pos - lo_pos) * (sample - lo_sample) / (hi_sample - lo_sample));
        }
        if(guess <= lo_pos) guess = lo_pos + 1;
        if(guess >= hi_pos) guess = hi_pos - 1;
        FrameHeader h;
        uint32_t at = 0;
        if(!this->probeFrame(guess, h, at) || at >= hi_pos){
            hi_pos = guess;
            continue;
        }
        const uint64_t s = h.firstSample(this->fixed_block_ ? this->fixed_block_ : h.blocksize);
        if(s <= sample){
            if(s + h.blocksize > sample){
                this->startAt(at, s);
                this->skip_until_ = sample;
                return true;
            }
            lo_pos = at;
            lo_sample = s;
        }else{
            hi_pos = at;
            hi_sample = s;
        }
    }
    this->startAt(lo_pos, lo_sample);
    this->skip_until_ = sample;
    return true;
}

bool FlacDecoder::seekSample(uint64_t sample){
    if(this->total_samples_ && sample >= this->total_samples_) sample = this->total_samples_ - 1;
    if(sample == 0){
        this->startAt(this->audio_start_, 0);
        return true;
    }
    return this->seek_count_ > 0 ? this->seekBySeekTable(sample) : this->seekByBisect(sample);
}

bool FlacDecoder::seekMs(uint32_t ms){
    if(!this->open_ && !this->reopen()) return false;
    if(!this->seekSample((uint64_t)ms * this->rate_ / 1000)) return false;
    this->rs_.reset();
    this->finished_ = false;
    return true;
}
