#include "sound/Wav_Decoder.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    uint16_t U16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
    uint32_t U32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

    constexpr uint16_t kFormatPcm        = 1;
    constexpr uint16_t kFormatFloat      = 3;
    constexpr uint16_t kFormatExtensible = 0xFFFE;
}

const char* WavDecoder::ErrorText(Error e){
    switch(e){
        case Error::None:        return "";
        case Error::NoSd:        return "SDカードがありません";
        case Error::OpenFailed:  return "ファイルを開けません";
        case Error::NotWav:      return "WAVファイルではありません";
        case Error::Unsupported: return "対応していない形式です(PCM 8/16/24/32bit・float 32bitのみ)";
        case Error::NoData:      return "音のデータがありません";
    }
    return "";
}

bool WavDecoder::fail(Error e){
    this->close();
    this->error_ = e;
    return false;
}

void WavDecoder::close(){
    if(this->open_) this->f_.close();
    this->open_ = false;
    this->finished_ = true;
}

uint32_t WavDecoder::durationMs() const {
    if(this->block_ == 0 || this->rate_ == 0) return 0;
    return (uint32_t)((uint64_t)(this->data_size_ / this->block_) * 1000 / this->rate_);
}

bool WavDecoder::open(const char* path, uint32_t out_rate){
    this->close();
    this->error_ = Error::None;
    this->format_ = this->channels_ = this->bits_ = this->block_ = 0;
    this->rate_ = this->data_start_ = this->data_size_ = this->data_left_ = 0;
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

    if(!this->parseHeader()) return false;

    this->step_ = (uint32_t)(((uint64_t)this->rate_ << 16) / (out_rate ? out_rate : 22050));
    if(this->step_ == 0) this->step_ = 1;
    this->frac_ = 0;
    this->primed_ = false;
    this->ending_ = false;
    this->buf_pos_ = this->buf_len_ = 0;
    this->data_left_ = this->data_size_;
    this->finished_ = false;
    return true;
}

bool WavDecoder::seekMs(uint32_t ms){
    if(!this->open_ && !this->reopen()) return false;
    const uint32_t frames = this->data_size_ / this->block_;
    uint64_t frame = (uint64_t)ms * this->rate_ / 1000;
    if(frame >= frames) frame = frames > 0 ? frames - 1 : 0;
    const uint32_t offset = (uint32_t)frame * this->block_;
    if(!this->f_.seek(this->data_start_ + offset)) return false;
    this->data_left_ = this->data_size_ - offset;
    this->buf_pos_ = this->buf_len_ = 0;
    this->frac_ = 0;
    this->primed_ = false;
    this->ending_ = false;
    this->finished_ = false;
    return true;
}

bool WavDecoder::parseHeader(){
    uint8_t h[40];
    const uint32_t file_size = (uint32_t)this->f_.fileSize();

    if(this->f_.read(h, 12) != 12) return this->fail(Error::NotWav);
    if(memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return this->fail(Error::NotWav);

    uint32_t pos = 12;
    bool have_fmt = false;
    while(pos + 8 <= file_size){
        if(!this->f_.seek(pos) || this->f_.read(h, 8) != 8) break;
        const uint32_t size = U32(h + 4);
        const uint32_t body = pos + 8;

        if(memcmp(h, "fmt ", 4) == 0){
            if(size < 16) return this->fail(Error::NotWav);
            const size_t n = size < sizeof(h) ? size : sizeof(h);
            if(this->f_.read(h, n) != (int)n) return this->fail(Error::NotWav);
            this->format_   = U16(h);
            this->channels_ = U16(h + 2);
            this->rate_     = U32(h + 4);
            this->block_    = U16(h + 12);
            this->bits_     = U16(h + 14);
            //拡張形式は、中身の形式がサブフォーマット(GUID)の頭の2バイトにある
            if(this->format_ == kFormatExtensible){
                if(n < 26) return this->fail(Error::Unsupported);
                this->format_ = U16(h + 24);
            }
            have_fmt = true;
        }else if(memcmp(h, "data", 4) == 0){
            if(!have_fmt) return this->fail(Error::NotWav);   //fmtより前のdataは読み方が分からない
            this->data_start_ = body;
            const uint32_t rest = file_size > body ? file_size - body : 0;
            this->data_size_ = size < rest ? size : rest;
            break;
        }
        //チャンクは偶数バイトに揃えてある
        const uint64_t next = (uint64_t)body + size + (size & 1);
        if(next > file_size) break;
        pos = (uint32_t)next;
    }
    if(!have_fmt) return this->fail(Error::NotWav);

    const bool pcm_ok = this->format_ == kFormatPcm &&
        (this->bits_ == 8 || this->bits_ == 16 || this->bits_ == 24 || this->bits_ == 32);
    const bool float_ok = this->format_ == kFormatFloat && this->bits_ == 32;
    if(!pcm_ok && !float_ok) return this->fail(Error::Unsupported);
    if(this->channels_ == 0 || this->channels_ > kMaxChannels) return this->fail(Error::Unsupported);
    if(this->rate_ < 1000 || this->rate_ > 192000) return this->fail(Error::Unsupported);
    if(this->block_ != this->channels_ * (this->bits_ / 8)) return this->fail(Error::Unsupported);
    if(this->data_start_ == 0) return this->fail(Error::NoData);
    //最後の半端なフレームは読まない
    this->data_size_ -= this->data_size_ % this->block_;
    if(this->data_size_ == 0) return this->fail(Error::NoData);
    return this->f_.seek(this->data_start_) || this->fail(Error::OpenFailed);
}

// 読み込みバッファを詰め直す。1フレームも読めなければ false
bool WavDecoder::refill(){
    const size_t remain = this->buf_len_ - this->buf_pos_;
    if(remain > 0) memmove(this->buf_, this->buf_ + this->buf_pos_, remain);
    this->buf_pos_ = 0;
    this->buf_len_ = remain;

    size_t want = sizeof(this->buf_) - remain;
    if(want > this->data_left_) want = this->data_left_;
    if(want > 0){
        const int got = this->f_.read(this->buf_ + remain, want);
        if(got <= 0) this->data_left_ = 0;      //読めなくなったら(SDを抜いた等)そこで終わり
        else{
            this->buf_len_ += (size_t)got;
            this->data_left_ -= (uint32_t)got;
        }
    }
    return this->buf_len_ >= this->block_;
}

int32_t WavDecoder::decodeFrame(const uint8_t* p) const {
    int32_t sum = 0;
    const int bytes = this->bits_ / 8;
    for(int c = 0; c < this->channels_; c++, p += bytes){
        int32_t v;
        if(this->format_ == kFormatFloat){
            float f;
            memcpy(&f, p, 4);
            if(!(f == f)) f = 0;                //NaN
            if(f > 1.0f) f = 1.0f;
            if(f < -1.0f) f = -1.0f;
            v = (int32_t)(f * 32767.0f);
        }else{
            switch(bytes){
                case 1:  v = ((int32_t)p[0] - 128) * 256; break;                 //8bitだけ符号なし
                case 2:  v = (int16_t)U16(p); break;
                case 3:  v = (int16_t)U16(p + 1); break;                        //上位16bit
                default: v = (int16_t)U16(p + 2); break;
            }
        }
        sum += v;
    }
    return sum / this->channels_;
}

bool WavDecoder::nextSource(int16_t& v){
    if(this->buf_len_ - this->buf_pos_ < this->block_){
        if(!this->refill()){
            if(!this->loop_) return false;
            //頭から読み直す
            this->buf_pos_ = this->buf_len_ = 0;
            this->data_left_ = this->data_size_;
            if(!this->f_.seek(this->data_start_) || !this->refill()) return false;
        }
    }
    v = (int16_t)this->decodeFrame(this->buf_ + this->buf_pos_);
    this->buf_pos_ += this->block_;
    return true;
}

size_t WavDecoder::read(int16_t* out, size_t max){
    if(!this->open_ || this->finished_) return 0;

    if(!this->primed_){
        if(!this->nextSource(this->s0_)){ this->finished_ = true; return 0; }
        if(!this->nextSource(this->s1_)){
            this->s1_ = this->s0_;
            this->ending_ = true;
        }
        this->primed_ = true;
    }

    size_t n = 0;
    while(n < max){
        while(this->frac_ >= 0x10000){
            this->frac_ -= 0x10000;
            this->s0_ = this->s1_;
            if(!this->nextSource(this->s1_)){
                //最後の1つも出してから終える(同じ周波数なら入れたのと同じ数だけ出る)
                if(this->ending_){
                    this->finished_ = true;
                    return n;
                }
                this->ending_ = true;
                this->s1_ = this->s0_;
            }
        }
        //差は最大65535、frac>>1 は最大32767 なので積は int32 に収まる
        const int32_t d = (int32_t)this->s1_ - this->s0_;
        out[n++] = (int16_t)(this->s0_ + ((d * (int32_t)(this->frac_ >> 1)) >> 15));
        this->frac_ += this->step_;
    }
    return n;
}
