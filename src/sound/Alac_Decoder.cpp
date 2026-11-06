#include "sound/Alac_Decoder.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstring>

namespace {
    uint32_t BE32(const uint8_t* p){ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
    uint16_t BE16(const uint8_t* p){ return (uint16_t)((p[0] << 8) | p[1]); }

    // 符号なしでの足し引き(壊れたデータで符号付きの桁あふれ=未定義動作にしない)
    inline int32_t Add(int32_t a, int32_t b){ return (int32_t)((uint32_t)a + (uint32_t)b); }
    inline int32_t Mul(int32_t a, int32_t b){ return (int32_t)((uint32_t)a * (uint32_t)b); }
    inline int16_t Clamp16(int32_t v){ return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }

    // 下位 bps ビットを符号付きとして読み直す
    inline int32_t SignExt(int32_t v, unsigned bps){ return (int32_t)((uint32_t)v << (32 - bps)) >> (32 - bps); }
    inline unsigned Log2(uint32_t v){ return v ? 31u - (unsigned)__builtin_clz(v) : 0u; }
    inline int Sgn(int32_t v){ return (v > 0) - (v < 0); }

    // atom の種類(4文字)が一致するか
    inline bool Is(const char* a, const char* b){ return memcmp(a, b, 4) == 0; }

    constexpr uint32_t kMaxAtomDepthIter = 4096;     // 兄弟の atom を数える上限(壊れたファイルで回り続けない)
    constexpr uint32_t kMaxSttsEntries = 4096;
}

const char* AlacDecoder::ErrorText(Error e){
    switch(e){
        case Error::None:        return "";
        case Error::NoSd:        return "SDカードがありません";
        case Error::OpenFailed:  return "ファイルを開けません";
        case Error::NotAlac:     return "ALACのM4Aファイルではありません";
        case Error::Aac:         return "AACのM4Aは再生できません(ALACのみ)";
        case Error::Unsupported: return "このALACは再生できません(2ch以下・24bit以下・96kHz以下・フレーム長4096以下のみ)";
        case Error::NoData:      return "音のデータがありません";
    }
    return "";
}

bool AlacDecoder::fail(Error e){
    this->close();
    this->error_ = e;
    return false;
}

void AlacDecoder::close(){
    if(this->open_) this->f_.close();
    this->open_ = false;
    this->finished_ = true;
}

uint32_t AlacDecoder::durationMs() const {
    if(this->rate_ == 0 || this->total_samples_ == 0) return 0;
    return (uint32_t)(this->total_samples_ * 1000 / this->rate_);
}

// ================================================================
// ファイルとビットの読み取り(FlacDecoder と同じ作り)
// ================================================================

bool AlacDecoder::seekFile(uint32_t pos){
    this->buf_base_ = pos;
    this->buf_pos_ = this->buf_len_ = 0;
    this->acc_ = 0;
    this->nbits_ = 0;
    this->exhausted_ = false;
    return pos <= this->file_size_;
}

uint32_t AlacDecoder::bytePos() const {
    return this->buf_base_ + (uint32_t)this->buf_pos_ - this->nbits_ / 8;
}

bool AlacDecoder::rd(uint32_t pos, void* buf, size_t n){
    return pos + n <= this->file_size_ && this->f_.seek(pos) && this->f_.read(buf, n) == (int)n;
}

bool AlacDecoder::refill(){
    if(this->buf_pos_ < this->buf_len_) return true;
    this->buf_base_ += (uint32_t)this->buf_len_;
    this->buf_pos_ = this->buf_len_ = 0;
    //表の読み取り(rd)でファイルの読み込み位置が動いているので、毎回合わせ直す
    if(this->buf_base_ >= this->file_size_ || !this->f_.seek(this->buf_base_)) return false;
    const int n = this->f_.read(this->buf_, kBufferBytes);
    this->buf_len_ = n > 0 ? (size_t)n : 0;
    return this->buf_len_ > 0;
}

void AlacDecoder::loadMore(){
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

uint32_t AlacDecoder::readBits(unsigned n){
    if(n == 0) return 0;
    while(this->nbits_ < n) this->loadMore();
    this->nbits_ -= n;
    return (uint32_t)((this->acc_ >> this->nbits_) & ((1ull << n) - 1));
}

int32_t AlacDecoder::readSigned(unsigned n){
    if(n == 0) return 0;
    return SignExt((int32_t)this->readBits(n), n);
}

uint32_t AlacDecoder::readUnary9(){
    while(this->nbits_ < 9) this->loadMore();
    const uint32_t top = (uint32_t)((this->acc_ >> (this->nbits_ - 9)) & 0x1FF);
    const uint32_t zeros = (~top) & 0x1FF;
    if(zeros == 0){
        this->nbits_ -= 9;
        return 9;
    }
    const unsigned ones = 8 - Log2(zeros);
    this->nbits_ -= ones + 1;
    return ones;
}

// 可変長の1つの値(Apple の dyn_get)。k は余りのビット数、bps は「9個以上1が続いたとき」に生で読むビット数
uint32_t AlacDecoder::readScalar(unsigned k, unsigned bps){
    uint32_t x = this->readUnary9();
    if(x > 8) return this->readBits(bps);
    if(k > 1){
        while(this->nbits_ < k) this->loadMore();
        const uint32_t extra = (uint32_t)((this->acc_ >> (this->nbits_ - k)) & ((1ull << k) - 1));
        x = (x << k) - x;
        if(extra > 1){
            x += extra - 1;
            this->nbits_ -= k;
        }else{
            this->nbits_ -= k - 1;
        }
    }
    return x;
}

// ================================================================
// MP4
// ================================================================

// pos の atom の見出しを読む。type に4文字、a に位置(大きさが limit を越えるものは limit で切る)
bool AlacDecoder::atomAt(uint32_t pos, uint32_t limit, Atom& a, char type[4]){
    uint8_t h[16];
    if(pos + 8 > limit || !this->rd(pos, h, 8)) return false;
    uint64_t size = BE32(h);
    memcpy(type, h + 4, 4);
    uint32_t hdr = 8;
    if(size == 1){
        if(pos + 16 > limit || !this->rd(pos + 8, h + 8, 8)) return false;
        size = ((uint64_t)BE32(h + 8) << 32) | BE32(h + 12);
        hdr = 16;
    }else if(size == 0){
        size = limit - pos;
    }
    if(size < hdr) return false;
    a.start = pos;
    a.body = pos + hdr;
    a.end = (pos + size > limit) ? limit : (uint32_t)(pos + size);
    return true;
}

bool AlacDecoder::findAtom(uint32_t from, uint32_t limit, const char* type, Atom& out){
    uint32_t pos = from;
    for(uint32_t i = 0; i < kMaxAtomDepthIter && pos + 8 <= limit; i++){
        Atom a;
        char t[4];
        if(!this->atomAt(pos, limit, a, t)) return false;
        if(Is(t, type)){ out = a; return true; }
        if(a.end <= pos) return false;
        pos = a.end;
    }
    return false;
}

// 'alac' の設定(24バイト。大きさ・種類・版/フラグの後ろ)
bool AlacDecoder::parseCookie(uint32_t pos, uint32_t end){
    uint8_t c[24];
    if(pos + 24 > end || !this->rd(pos, c, 24)) return false;
    this->frame_length_ = BE32(c);
    const uint8_t compat = c[4];
    this->bps_ = c[5];
    this->rice_pb_ = c[6];
    this->rice_mb_ = c[7];
    this->rice_kb_ = c[8];
    this->channels_ = c[9];
    this->rate_ = BE32(c + 20);
    return compat == 0;
}

bool AlacDecoder::parseTrack(const Atom& trak, bool& saw_aac){
    Atom mdia, mdhd, minf, stbl, stsd;
    if(!this->findAtom(trak.body, trak.end, "mdia", mdia)) return false;
    if(!this->findAtom(mdia.body, mdia.end, "minf", minf)) return false;
    if(!this->findAtom(minf.body, minf.end, "stbl", stbl)) return false;
    if(!this->findAtom(stbl.body, stbl.end, "stsd", stsd)) return false;

    //最初の標本記述(stsd: 版/フラグ4 + 個数4 の後ろ)の種類
    Atom entry;
    char type[4];
    if(!this->atomAt(stsd.body + 8, stsd.end, entry, type)) return false;
    if(Is(type, "mp4a")){ saw_aac = true; return false; }
    if(!Is(type, "alac")) return false;

    //標本記述の中: 8バイトの見出しの後ろに 予約6+参照2 と音声の記述(版0は20バイト、QuickTimeの版1は+16、版2は+36)、その後ろに子の atom
    uint8_t ver[2];
    if(!this->rd(entry.body + 8, ver, 2)) return false;
    uint32_t child = entry.body + 8 + 20;
    const unsigned qt = BE16(ver);
    if(qt == 1) child += 16;
    else if(qt == 2) child += 36;
    Atom ca;
    if(!this->findAtom(child, entry.end, "alac", ca)) return false;
    if(!this->parseCookie(ca.body + 4, ca.end)) return false;

    //時間の単位(mdhd)
    if(!this->findAtom(mdia.body, mdia.end, "mdhd", mdhd)) return false;
    uint8_t m[4];
    if(!this->rd(mdhd.body, m, 1)) return false;
    const uint32_t ts_pos = mdhd.body + 4 + (m[0] == 1 ? 16 : 8);
    if(!this->rd(ts_pos, m, 4)) return false;
    this->timescale_ = BE32(m);

    //表(位置と個数だけ覚える)
    Atom a;
    uint8_t h[12];
    if(!this->findAtom(stbl.body, stbl.end, "stts", a) || !this->rd(a.body, h, 8)) return false;
    this->stts_count_ = BE32(h + 4);
    this->stts_pos_ = a.body + 8;
    if(!this->findAtom(stbl.body, stbl.end, "stsc", a) || !this->rd(a.body, h, 8)) return false;
    this->stsc_count_ = BE32(h + 4);
    this->stsc_pos_ = a.body + 8;
    if(!this->findAtom(stbl.body, stbl.end, "stsz", a) || !this->rd(a.body, h, 12)) return false;
    this->stsz_fixed_ = BE32(h + 4);
    this->frame_count_ = BE32(h + 8);
    this->stsz_pos_ = a.body + 12;
    this->co64_ = false;
    if(this->findAtom(stbl.body, stbl.end, "stco", a)){
    }else if(this->findAtom(stbl.body, stbl.end, "co64", a)){
        this->co64_ = true;
    }else{
        return false;
    }
    if(!this->rd(a.body, h, 8)) return false;
    this->stco_count_ = BE32(h + 4);
    this->stco_pos_ = a.body + 8;
    return true;
}

bool AlacDecoder::parseContainer(){
    uint8_t h[8];
    if(this->file_size_ < 16 || !this->rd(0, h, 8) || memcmp(h + 4, "ftyp", 4) != 0) return this->fail(Error::NotAlac);
    Atom moov;
    if(!this->findAtom(0, this->file_size_, "moov", moov)) return this->fail(Error::NotAlac);

    bool saw_aac = false;
    bool found = false;
    uint32_t pos = moov.body;
    for(uint32_t i = 0; i < 64; i++){
        Atom trak;
        if(!this->findAtom(pos, moov.end, "trak", trak)) break;
        if(this->parseTrack(trak, saw_aac)){ found = true; break; }
        pos = trak.end;
    }
    if(!found) return this->fail(saw_aac ? Error::Aac : Error::NotAlac);

    if(this->rate_ == 0 || this->frame_length_ == 0) return this->fail(Error::NotAlac);
    if(this->channels_ < 1 || this->channels_ > 2 || (this->bps_ != 16 && this->bps_ != 20 && this->bps_ != 24)
       || this->rate_ > kMaxRate || this->frame_length_ > kMaxFrame){
        return this->fail(Error::Unsupported);
    }
    if(this->frame_count_ == 0 || this->stco_count_ == 0 || this->stsc_count_ == 0) return this->fail(Error::NoData);

    //総サンプル数(stts の 個数×長さ の合計。時間の単位が周波数と違えば直す)
    uint64_t units = 0;
    const uint32_t n = this->stts_count_ < kMaxSttsEntries ? this->stts_count_ : kMaxSttsEntries;
    for(uint32_t i = 0; i < n; i++){
        uint8_t e[8];
        if(!this->rd(this->stts_pos_ + i * 8, e, 8)) break;
        units += (uint64_t)BE32(e) * BE32(e + 4);
    }
    if(units == 0) units = (uint64_t)this->frame_count_ * this->frame_length_;
    this->total_samples_ = (this->timescale_ && this->timescale_ != this->rate_) ? units * this->rate_ / this->timescale_ : units;
    return true;
}

// ================================================================
// フレームの表
// ================================================================

uint32_t AlacDecoder::sizeOf(uint32_t index){
    if(this->stsz_fixed_) return this->stsz_fixed_;
    if(index >= this->frame_count_) return 0;
    if(index < this->size_base_ || index >= this->size_base_ + this->size_n_){
        const uint32_t base = index - index % kSizeWindow;
        uint32_t n = this->frame_count_ - base;
        if(n > kSizeWindow) n = kSizeWindow;
        uint8_t raw[kSizeWindow * 4];
        this->size_n_ = 0;
        this->size_base_ = base;
        if(!this->rd(this->stsz_pos_ + base * 4, raw, n * 4)) return 0;
        for(uint32_t i = 0; i < n; i++) this->size_cache_[i] = BE32(raw + i * 4);
        this->size_n_ = n;
    }
    return this->size_cache_[index - this->size_base_];
}

bool AlacDecoder::stscEntry(uint32_t i, uint32_t& first, uint32_t& spc){
    uint8_t e[8];
    if(i >= this->stsc_count_ || !this->rd(this->stsc_pos_ + i * 12, e, 8)) return false;
    first = BE32(e);
    spc = BE32(e + 4);
    return first >= 1;
}

bool AlacDecoder::chunkOffset(uint32_t chunk, uint32_t& off){
    if(chunk >= this->stco_count_) return false;
    uint8_t e[8];
    if(this->co64_){
        if(!this->rd(this->stco_pos_ + chunk * 8, e, 8) || BE32(e) != 0) return false;     //4GB以上の位置は読まない
        off = BE32(e + 4);
    }else{
        if(!this->rd(this->stco_pos_ + chunk * 4, e, 4)) return false;
        off = BE32(e);
    }
    return off < this->file_size_;
}

// チャンク chunk(0始まり)の頭へ。stsc の組を今のチャンクに合わせる(前へ戻るときは最初から数える)
bool AlacDecoder::setupChunk(uint32_t chunk){
    uint32_t first = 0, spc = 0;
    if(chunk < this->chunk_ || this->stsc_spc_ == 0){
        this->stsc_i_ = 0;
        if(!this->stscEntry(0, first, spc)) return false;
        this->stsc_spc_ = spc;
        this->stsc_next_first_ = this->stco_count_ + 1;
        uint32_t nf = 0, ns = 0;
        if(this->stsc_count_ > 1 && this->stscEntry(1, nf, ns)) this->stsc_next_first_ = nf;
    }
    //次の組の先頭チャンクへ来たら組を進める
    while(this->stsc_i_ + 1 < this->stsc_count_ && chunk + 1 >= this->stsc_next_first_){
        this->stsc_i_++;
        uint32_t f = 0, s = 0;
        if(!this->stscEntry(this->stsc_i_, f, s)) return false;
        this->stsc_spc_ = s;
        this->stsc_next_first_ = this->stco_count_ + 1;
        uint32_t nf = 0, ns = 0;
        if(this->stsc_i_ + 1 < this->stsc_count_ && this->stscEntry(this->stsc_i_ + 1, nf, ns)) this->stsc_next_first_ = nf;
    }
    this->chunk_ = chunk;
    this->spc_ = this->stsc_spc_;
    this->in_chunk_ = 0;
    return this->spc_ > 0 && this->chunkOffset(chunk, this->chunk_off_);
}

// frame 番号のフレームを次に読むフレームにする
bool AlacDecoder::locate(uint32_t frame){
    if(frame >= this->frame_count_) return false;
    //frame を含むチャンクを、stsc の組を順にたどって探す
    uint32_t acc = 0;
    uint32_t chunk = 0, in_chunk = 0;
    bool found = false;
    for(uint32_t i = 0; i < this->stsc_count_; i++){
        uint32_t first = 0, spc = 0, next = this->stco_count_ + 1;
        if(!this->stscEntry(i, first, spc)) return false;
        if(i + 1 < this->stsc_count_){
            uint32_t nf = 0, ns = 0;
            if(!this->stscEntry(i + 1, nf, ns)) return false;
            next = nf;
        }
        if(spc == 0 || next < first) return false;
        const uint64_t run = (uint64_t)(next - first) * spc;
        if((uint64_t)frame < (uint64_t)acc + run){
            chunk = (first - 1) + (frame - acc) / spc;
            in_chunk = (frame - acc) % spc;
            found = true;
            break;
        }
        acc += (uint32_t)run;
    }
    if(!found) return false;
    this->stsc_spc_ = 0;        //組を最初から合わせ直す
    this->chunk_ = 0xFFFFFFFFu;
    if(!this->setupChunk(chunk)) return false;
    //チャンクの頭から、frame の手前までの大きさを足す
    for(uint32_t k = 0; k < in_chunk; k++){
        const uint32_t s = this->sizeOf(frame - in_chunk + k);
        if(s == 0) return false;
        this->chunk_off_ += s;
    }
    this->in_chunk_ = in_chunk;
    this->frame_idx_ = frame;
    return true;
}

bool AlacDecoder::nextFrame(uint32_t& off){
    if(this->frame_idx_ >= this->frame_count_) return false;
    if(this->in_chunk_ >= this->spc_ && !this->setupChunk(this->chunk_ + 1)) return false;
    const uint32_t size = this->sizeOf(this->frame_idx_);
    if(size == 0) return false;
    off = this->chunk_off_;
    this->chunk_off_ += size;
    this->in_chunk_++;
    this->frame_idx_++;
    return true;
}

// ================================================================
// 展開
// ================================================================

bool AlacDecoder::open(const char* path, uint32_t out_rate){
    this->close();
    this->error_ = Error::None;
    this->channels_ = this->bps_ = 0;
    this->rate_ = this->frame_length_ = this->timescale_ = 0;
    this->rice_pb_ = this->rice_mb_ = this->rice_kb_ = 0;
    this->total_samples_ = 0;
    this->file_size_ = 0;
    this->stsz_pos_ = this->stsz_fixed_ = this->frame_count_ = 0;
    this->stsc_pos_ = this->stsc_count_ = this->stco_pos_ = this->stco_count_ = 0;
    this->stts_pos_ = this->stts_count_ = 0;
    this->co64_ = false;
    this->size_base_ = this->size_n_ = 0;
    this->frame_idx_ = this->chunk_ = this->in_chunk_ = this->spc_ = this->chunk_off_ = 0;
    this->stsc_i_ = this->stsc_next_first_ = this->stsc_spc_ = 0;
    this->buf_base_ = 0;
    this->buf_pos_ = this->buf_len_ = 0;
    this->acc_ = 0;
    this->nbits_ = 0;
    this->exhausted_ = false;
    this->block_len_ = this->block_pos_ = 0;
    this->block_first_ = this->skip_until_ = 0;
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

    if(!this->parseContainer()) return false;
    this->startAt(0);
    if(!this->locate(0)) return this->fail(Error::NoData);
    this->finished_ = false;
    this->lanes_ = (this->stereo_out_ && this->channels_ >= 2) ? 2 : 1;
    this->rs_.setup(this->rate_, this->out_rate_, this->lanes_, &AlacDecoder::Source, this);
    return true;
}

void AlacDecoder::startAt(uint32_t frame){
    this->seekFile(0);
    this->block_len_ = this->block_pos_ = 0;
    this->block_first_ = (uint64_t)frame * this->frame_length_;
    this->skip_until_ = 0;
}

// 予測の係数で、残差の列 buf をその場で音の列へ直す(ffmpeg の lpc_prediction と同じ手順。coefs は書き換わる)
void AlacDecoder::predict(int32_t* buf, uint32_t n, unsigned bps, int16_t* coefs, unsigned order, unsigned quant){
    if(n <= 1 || order == 0) return;
    if(order == 31){
        //1次の予測だけ
        for(uint32_t i = 1; i < n; i++) buf[i] = SignExt(Add(buf[i - 1], buf[i]), bps);
        return;
    }
    //頭の order 個は、1つ前との差分
    for(unsigned i = 0; i < order && i + 1 < n; i++) buf[i + 1] = SignExt(Add(buf[i], buf[i + 1]), bps);
    for(uint32_t i = order + 1; i < n; i++){
        const int32_t d = buf[i - order - 1];
        int32_t err = buf[i];
        const int32_t* pred = buf + (i - order);
        int32_t sum = 0;
        for(unsigned j = 0; j < order; j++) sum = Add(sum, Mul(Add(pred[j], -d), (int32_t)coefs[j]));
        sum = (quant ? Add(sum, (int32_t)(1u << (quant - 1))) : sum) >> quant;
        buf[i] = SignExt(Add(Add(sum, d), err), bps);

        //係数を残差の向きへ寄せる
        const int es = Sgn(err);
        if(es){
            for(unsigned j = 0; j < order && Mul(err, es) > 0; j++){
                int32_t v = Add(d, -pred[j]);
                const int sg = Sgn(v) * es;
                coefs[j] = (int16_t)(coefs[j] - sg);
                v = Mul(v, sg);
                err = Add(err, -Mul(v >> quant, (int32_t)(j + 1)));
            }
        }
    }
}

// 残差を適応ゴロム符号から読む(ffmpeg の rice_decompress と同じ手順)
bool AlacDecoder::decodeResidual(int32_t* out, uint32_t n, unsigned bps, int32_t hist_mult){
    int32_t history = this->rice_mb_;
    unsigned sign_mod = 0;
    uint32_t i = 0;
    while(i < n){
        unsigned k = Log2((uint32_t)(history >> 9) + 3);
        if(k > this->rice_kb_) k = this->rice_kb_;
        const uint32_t x = this->readScalar(k, bps) + sign_mod;
        sign_mod = 0;
        out[i++] = (int32_t)(x >> 1) ^ -(int32_t)(x & 1);
        if(x > 0xFFFF) history = 0xFFFF;
        else history = (int32_t)((uint32_t)history + x * (uint32_t)hist_mult - (uint32_t)(((int64_t)history * hist_mult) >> 9));
        //0が続く区間(history が小さいとき、0の個数をまとめて持っている)
        if(history < 128 && i < n){
            sign_mod = 1;
            int kk = 7 - (int)Log2((uint32_t)history) + ((history + 16) >> 6);
            if(kk < 0) kk = 0;
            if((unsigned)kk > this->rice_kb_) kk = this->rice_kb_;
            const uint32_t run = this->readScalar((unsigned)kk, 16);
            if(run > 0){
                if(run > n - i) return false;
                for(uint32_t j = 0; j < run; j++) out[i + j] = 0;
                i += run;
            }
            if(run > 0xFFFF) sign_mod = 0;
            history = 0;
        }
        if(this->exhausted_) return false;
    }
    return true;
}

// ビット読み取りの位置から1フレーム(1つの要素 + 終わりの印)を展開して ch_ へ。expect はフレームの標準のサンプル数
bool AlacDecoder::decodeFrame(uint32_t expect){
    const unsigned tag = this->readBits(3);
    const unsigned ch = tag == 1 ? 2 : (tag == 0 ? 1 : 0);        //1つの要素(SCE=0・CPE=1)だけ扱う
    if(ch == 0 || ch != this->channels_) return false;
    this->readBits(4);                                            //要素の番号
    if(this->readBits(12) != 0) return false;
    const unsigned hdr = this->readBits(4);
    const bool partial = (hdr & 8) != 0;
    const unsigned shifted = (hdr >> 1) & 3;
    const bool escape = (hdr & 1) != 0;
    uint32_t n = expect;
    if(partial) n = this->readBits(32);
    if(n == 0 || n > this->frame_length_ || n > kMaxFrame) return false;
    if(shifted > 1) return false;       //24bit以下なら下位は多くて8bit
    const unsigned chan_bits = this->bps_ - shifted * 8 + (ch - 1);

    if(escape){
        //圧縮せず、そのまま並べてある
        if(shifted) return false;
        for(uint32_t i = 0; i < n; i++){
            for(unsigned c = 0; c < ch; c++) this->ch_[c][i] = this->readSigned(this->bps_);
        }
    }else{
        const unsigned mix_bits = this->readBits(8);
        const int mix_res = (int8_t)this->readBits(8);
        if(mix_bits > 31) return false;
        unsigned mode[2], quant[2], order[2];
        int32_t mult[2];
        int16_t coefs[2][32];
        for(unsigned c = 0; c < ch; c++){
            mode[c] = this->readBits(4);
            quant[c] = this->readBits(4);
            mult[c] = (int32_t)((this->readBits(3) * (uint32_t)this->rice_pb_) / 4);
            order[c] = this->readBits(5);
            for(int i = (int)order[c] - 1; i >= 0; i--) coefs[c][i] = (int16_t)this->readBits(16);
        }
        if(shifted){
            for(uint32_t i = 0; i < n; i++){
                for(unsigned c = 0; c < ch; c++) this->low_[c][i] = (uint8_t)this->readBits(shifted * 8);
            }
        }
        if(this->exhausted_) return false;
        for(unsigned c = 0; c < ch; c++){
            if(!this->decodeResidual(this->ch_[c], n, chan_bits, mult[c])) return false;
        }
        for(unsigned c = 0; c < ch; c++){
            if(mode[c]) this->predict(this->ch_[c], n, chan_bits, nullptr, 31, 0);
            this->predict(this->ch_[c], n, chan_bits, coefs[c], order[c], quant[c]);
        }
        //左右の混ぜ戻し
        if(ch == 2 && mix_res != 0){
            for(uint32_t i = 0; i < n; i++){
                int32_t a = this->ch_[0][i];
                int32_t b = this->ch_[1][i];
                a = Add(a, -(int32_t)(((int64_t)b * mix_res) >> mix_bits));
                b = Add(b, a);
                this->ch_[0][i] = b;
                this->ch_[1][i] = a;
            }
        }
        if(shifted){
            for(unsigned c = 0; c < ch; c++){
                for(uint32_t i = 0; i < n; i++) this->ch_[c][i] = (int32_t)(((uint32_t)this->ch_[c][i] << (shifted * 8)) | this->low_[c][i]);
            }
        }
    }
    if(this->exhausted_) return false;
    this->block_len_ = n;
    return true;
}

bool AlacDecoder::decodeNext(){
    for(int fails = 0; fails < 16;){
        if(this->frame_idx_ >= this->frame_count_) return false;
        const uint64_t first = (uint64_t)this->frame_idx_ * this->frame_length_;
        if(this->total_samples_ && first >= this->total_samples_) return false;
        uint32_t off = 0;
        if(!this->nextFrame(off)) return false;
        //続けて読むなら読み込みを続ける(前のフレームの終わりの位置と同じ)。違えば読み直す
        if(this->buf_len_ == 0 || this->bytePos() != off) this->seekFile(off);
        uint32_t expect = this->frame_length_;
        if(this->total_samples_ && first + expect > this->total_samples_) expect = (uint32_t)(this->total_samples_ - first);
        this->block_first_ = first;
        const bool ok = this->decodeFrame(expect);
        if(ok){
            this->readBits(3);      //終わりの印(ID_END)。値は見ない
            this->alignByte();
        }else{
            //壊れたフレームは、同じ長さの無音で置き換える(表から次の位置が分かるので、長さがずれない)
            this->block_len_ = expect;
            for(unsigned c = 0; c < this->channels_; c++) memset(this->ch_[c], 0, expect * sizeof(int32_t));
            this->seekFile(0);
            this->buf_len_ = 0;
            fails++;
        }
        if(this->total_samples_ && first + this->block_len_ > this->total_samples_){
            this->block_len_ = (uint32_t)(this->total_samples_ - first);
        }
        this->block_pos_ = 0;
        if(this->skip_until_ > first){
            if(this->skip_until_ >= first + this->block_len_) continue;     //シーク先より前のフレームは丸ごと捨てる
            this->block_pos_ = (uint32_t)(this->skip_until_ - first);
        }
        this->skip_until_ = 0;
        return this->block_len_ > 0;
    }
    return false;
}

bool AlacDecoder::advance(){
    const uint32_t t0 = micros();
    bool ok = this->decodeNext();
    if(!ok && this->loop_){
        this->startAt(0);
        ok = this->locate(0) && this->decodeNext();
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

// ================================================================
// 出力・シーク
// ================================================================

bool AlacDecoder::rawSource(Frame& v){
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

size_t AlacDecoder::read(int16_t* out, size_t max){
    if(!this->open_ || this->finished_) return 0;
    const size_t n = this->rs_.read(out, max);
    if(this->rs_.finished()) this->finished_ = true;
    return n;
}

bool AlacDecoder::seekSample(uint64_t sample){
    if(this->total_samples_ && sample >= this->total_samples_) sample = this->total_samples_ - 1;
    uint32_t frame = (uint32_t)(sample / this->frame_length_);
    if(frame >= this->frame_count_) frame = this->frame_count_ - 1;
    this->startAt(frame);
    if(!this->locate(frame)) return false;
    this->skip_until_ = sample;
    return true;
}

bool AlacDecoder::seekMs(uint32_t ms){
    if(!this->open_ && !this->reopen()) return false;
    if(!this->seekSample((uint64_t)ms * this->rate_ / 1000)) return false;
    this->rs_.reset();
    this->finished_ = false;
    return true;
}
