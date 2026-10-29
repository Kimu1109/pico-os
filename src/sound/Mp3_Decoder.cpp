#include "sound/Mp3_Decoder.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    uint32_t BE32(const uint8_t* p){ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

    // ID3v2 の大きさ(7bitずつの4バイト)。タグでなければ0
    uint32_t Id3v2Size(const uint8_t* h){
        if(memcmp(h, "ID3", 3) != 0) return 0;
        if((h[6] | h[7] | h[8] | h[9]) & 0x80) return 0;
        const uint32_t body = ((uint32_t)h[6] << 21) | ((uint32_t)h[7] << 14) | ((uint32_t)h[8] << 7) | h[9];
        return 10 + body + ((h[5] & 0x10) ? 10 : 0);     //フッタ付き
    }
}

const char* Mp3Decoder::ErrorText(Error e){
    switch(e){
        case Error::None:       return "";
        case Error::NoSd:       return "SDカードがありません";
        case Error::OpenFailed: return "ファイルを開けません";
        case Error::NotMp3:     return "MP3ファイルではありません(MPEG Layer IIIのみ)";
        case Error::NoData:     return "音のデータがありません";
    }
    return "";
}

bool Mp3Decoder::fail(Error e){
    this->close();
    this->error_ = e;
    return false;
}

void Mp3Decoder::close(){
    if(this->open_) this->f_.close();
    this->open_ = false;
    this->finished_ = true;
}

uint32_t Mp3Decoder::durationMs() const {
    if(this->rate_ == 0) return 0;
    if(this->total_frames_ > 0){
        return (uint32_t)((uint64_t)this->total_frames_ * this->frame_samples_ * 1000 / this->rate_);
    }
    //見出しが無ければCBRとみなす(バイト数×8 / kbps = ms)
    if(this->bitrate_kbps_ == 0 || this->data_end_ <= this->data_start_) return 0;
    return (uint32_t)((uint64_t)(this->data_end_ - this->data_start_) * 8 / this->bitrate_kbps_);
}

bool Mp3Decoder::open(const char* path, uint32_t out_rate){
    this->close();
    this->error_ = Error::None;
    this->channels_ = 0;
    this->rate_ = 0;
    this->bitrate_kbps_ = this->frame_samples_ = 0;
    this->data_start_ = this->data_end_ = this->total_frames_ = 0;
    this->has_toc_ = false;
    if(!OSData::SD_usable) return this->fail(Error::NoSd);
    //minimp3の作業場所が足りなければ読めない(実機・PCとも足りる大きさにしてある)
    if(minimp3_pico_scratch_bytes > sizeof(this->scratch_)) return this->fail(Error::OpenFailed);

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

    this->restartAt(this->data_start_);
    this->finished_ = false;
    this->lanes_ = (this->stereo_out_ && this->channels_ >= 2) ? 2 : 1;
    this->rs_.setup(this->rate_, this->out_rate_, this->lanes_, &Mp3Decoder::Source, this);
    return true;
}

bool Mp3Decoder::parseHeader(){
    const uint32_t file_size = (uint32_t)this->f_.fileSize();
    uint8_t h[10];

    //頭のID3v2(続けて2つ以上あることもある)
    uint32_t pos = 0;
    for(int i = 0; i < 4; i++){
        if(pos + 10 > file_size || !this->f_.seek(pos) || this->f_.read(h, 10) != 10) break;
        const uint32_t size = Id3v2Size(h);
        if(size == 0) break;
        pos += size;
    }
    //終わりのID3v1(128バイト、"TAG"で始まる)
    this->data_end_ = file_size;
    if(file_size >= pos + 128 && this->f_.seek(file_size - 128) && this->f_.read(h, 3) == 3 && memcmp(h, "TAG", 3) == 0){
        this->data_end_ = file_size - 128;
    }
    if(pos >= this->data_end_) return this->fail(Error::NotMp3);

    //最初のフレームを探す
    mp3dec_frame_info_t info;
    int samples = 0;
    uint32_t frame_at = 0;
    if(!this->findFrame(pos, info, samples, frame_at)) return this->fail(Error::NotMp3);
    if(info.layer != 3) return this->fail(Error::NotMp3);
    this->rate_ = (uint32_t)info.hz;
    this->channels_ = (uint16_t)info.channels;
    this->bitrate_kbps_ = (uint16_t)info.bitrate_kbps;
    this->frame_samples_ = (uint16_t)samples;
    this->data_start_ = frame_at;
    //Xing/Info/VBRI のフレームは音を持たないので鳴らさない。ビットレートも本体と違うことがあるので次のフレームで取り直す
    const size_t frame_len = (size_t)(info.frame_bytes - info.frame_offset);
    if(this->parseVbrHeader(this->buf_ + this->buf_pos_ + info.frame_offset, frame_len)){
        this->data_start_ = frame_at + (uint32_t)frame_len;
        if(this->findFrame(this->data_start_, info, samples, frame_at)){
            this->bitrate_kbps_ = (uint16_t)info.bitrate_kbps;
            this->data_start_ = frame_at;
        }
    }
    if(this->data_start_ >= this->data_end_) return this->fail(Error::NoData);
    return true;
}

// from から最初のフレームを探す(pcm を渡さなければ見出しだけ読む)。見つかれば buf_ + buf_pos_ + info.frame_offset がその頭
bool Mp3Decoder::findFrame(uint32_t from, mp3dec_frame_info_t& info, int& samples, uint32_t& frame_at){
    this->restartAt(from);
    uint32_t scanned = 0;
    while(true){
        this->topUp();
        const size_t avail = this->buf_len_ - this->buf_pos_;
        if(avail == 0) return false;
        samples = mp3dec_decode_frame(&this->dec_, this->buf_ + this->buf_pos_, (int)avail, nullptr, &info);
        if(samples > 0){
            //buf_[buf_pos_] のファイル上の位置は、次に読む位置から残りを引いたところ
            frame_at = this->file_pos_ - (uint32_t)avail + (uint32_t)info.frame_offset;
            return true;
        }
        //見つからなければ探した分を捨てて続きを読む
        const size_t skip = info.frame_bytes > 0 ? (size_t)info.frame_bytes : avail;
        this->buf_pos_ += skip < avail ? skip : avail;
        scanned += (uint32_t)skip;
        if(scanned > kMaxScanBytes) return false;
    }
}

// 最初のフレームの中の Xing/Info(LAME等)か VBRI(Fraunhofer)の見出しを読む。あれば true
bool Mp3Decoder::parseVbrHeader(const uint8_t* frame, size_t len){
    const bool mpeg1 = (frame[1] & 0x18) == 0x18;
    const bool mono = (frame[3] & 0xC0) == 0xC0;
    const size_t side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);   //見出し(4バイト)の後のサイド情報
    size_t p = 4 + side;
    if(len >= p + 8 && (memcmp(frame + p, "Xing", 4) == 0 || memcmp(frame + p, "Info", 4) == 0)){
        const uint32_t flags = BE32(frame + p + 4);
        p += 8;
        if(flags & 1){
            if(len < p + 4) return true;
            this->total_frames_ = BE32(frame + p);
            p += 4;
        }
        if(flags & 2) p += 4;       //バイト数(長さは data_start_〜data_end_ で数える)
        if((flags & 4) && len >= p + 100){
            memcpy(this->toc_, frame + p, 100);
            this->has_toc_ = true;
        }
        return true;
    }
    if(len >= 4 + 32 + 18 && memcmp(frame + 4 + 32, "VBRI", 4) == 0){
        this->total_frames_ = BE32(frame + 4 + 32 + 14);
        return true;
    }
    return false;
}

// 読み込みバッファを詰め直す(残りを頭へ寄せ、空いた分を音のデータの終わりまで読む)
void Mp3Decoder::topUp(){
    const size_t remain = this->buf_len_ - this->buf_pos_;
    if(remain > 0 && this->buf_pos_ > 0) memmove(this->buf_, this->buf_ + this->buf_pos_, remain);
    this->buf_pos_ = 0;
    this->buf_len_ = remain;
    if(this->eof_) return;

    size_t want = sizeof(this->buf_) - remain;
    const uint32_t left = this->data_end_ > this->file_pos_ ? this->data_end_ - this->file_pos_ : 0;
    if(want > left) want = left;
    if(want > 0){
        const int got = this->f_.read(this->buf_ + remain, want);
        if(got <= 0) this->eof_ = true;     //読めなくなったら(SDを抜いた等)そこで終わり
        else{
            this->buf_len_ += (size_t)got;
            this->file_pos_ += (uint32_t)got;
        }
    }
    if(this->file_pos_ >= this->data_end_) this->eof_ = true;
}

// pos から読み直す(展開の途中状態とビットの貯金を捨てる)
void Mp3Decoder::restartAt(uint32_t pos){
    if(!this->f_.seek(pos)) this->eof_ = true;
    else this->eof_ = false;
    this->file_pos_ = pos;
    this->buf_pos_ = this->buf_len_ = 0;
    this->pcm_count_ = this->pcm_pos_ = 0;
    mp3dec_init(&this->dec_);
}

// 次に音の出るフレームまで展開する。終わり(ループしないとき)なら false
bool Mp3Decoder::decodeNext(){
    minimp3_pico_scratch = this->scratch_;
    bool looped = false;
    while(true){
        this->topUp();
        const size_t avail = this->buf_len_ - this->buf_pos_;
        if(avail == 0){
            //1周して何も出なければ(音の出るフレームが無い)終える
            if(!this->loop_ || looped) return false;
            this->restartAt(this->data_start_);
            looped = true;
            continue;
        }
        mp3dec_frame_info_t info;
        const int samples = mp3dec_decode_frame(&this->dec_, this->buf_ + this->buf_pos_, (int)avail, this->pcm_, &info);
        //frame_bytes が0なのは、頭のフレームがバッファに収まりきらないとき(終わりで切れている・大きすぎる)。捨てて先へ
        const size_t used = info.frame_bytes > 0 ? (size_t)info.frame_bytes : avail;
        this->buf_pos_ += used < avail ? used : avail;
        //飛んだ直後はビットの貯金が足りず、音の出ないフレームがある(samples==0)
        if(samples > 0){
            this->pcm_count_ = (uint16_t)samples;
            this->pcm_pos_ = 0;
            this->pcm_ch_ = (uint8_t)(info.channels == 2 ? 2 : 1);
            return true;
        }
    }
}

bool Mp3Decoder::rawSource(Frame& v){
    if(this->pcm_pos_ >= this->pcm_count_ && !this->decodeNext()) return false;
    const int16_t* p = this->pcm_ + (size_t)this->pcm_pos_ * this->pcm_ch_;
    this->pcm_pos_++;
    if(this->pcm_ch_ == 2){
        if(this->lanes_ == 2){
            v.v[0] = p[0];
            v.v[1] = p[1];
        }else{
            v.v[0] = v.v[1] = (int16_t)(((int32_t)p[0] + p[1]) / 2);
        }
    }else{
        v.v[0] = v.v[1] = p[0];
    }
    return true;
}

uint32_t Mp3Decoder::seekOffset(uint32_t ms) const {
    const uint32_t dur = this->durationMs();
    const uint32_t len = this->data_end_ - this->data_start_;
    if(dur == 0 || len == 0) return this->data_start_;
    if(ms >= dur) ms = dur - 1;
    uint64_t off;
    if(this->has_toc_){
        //TOC: i% の時刻の位置が toc_[i]/256。間は直線で埋める
        const uint32_t pct_x1000 = (uint32_t)((uint64_t)ms * 100000 / dur);     //0〜99999
        const uint32_t i = pct_x1000 / 1000;
        const uint32_t fa = this->toc_[i];
        const uint32_t fb = i < 99 ? this->toc_[i + 1] : 256;
        const uint32_t fx_x1000 = fa * 1000 + (fb > fa ? (fb - fa) * (pct_x1000 % 1000) : 0);
        off = (uint64_t)len * fx_x1000 / (256 * 1000);
    }else{
        off = (uint64_t)len * ms / dur;
    }
    if(off >= len) off = len - 1;
    return this->data_start_ + (uint32_t)off;
}

bool Mp3Decoder::seekMs(uint32_t ms){
    if(!this->open_ && !this->reopen()) return false;
    this->restartAt(this->seekOffset(ms));
    this->rs_.reset();
    this->finished_ = false;
    return true;
}

size_t Mp3Decoder::read(int16_t* out, size_t max){
    if(!this->open_ || this->finished_) return 0;
    const size_t n = this->rs_.read(out, max);
    if(this->rs_.finished()) this->finished_ = true;
    return n;
}
