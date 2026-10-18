#include "sound/Wav_Writer.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    void PutLe32(uint8_t* p, uint32_t v){
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    }
    void PutLe16(uint8_t* p, uint16_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

    void BuildHeader(uint8_t* h, uint32_t rate, uint32_t data_bytes){
        memcpy(h, "RIFF", 4);
        PutLe32(h + 4, 36 + data_bytes);
        memcpy(h + 8, "WAVEfmt ", 8);
        PutLe32(h + 16, 16);            // fmtの大きさ
        PutLe16(h + 20, 1);             // 整数PCM
        PutLe16(h + 22, 1);             // モノラル
        PutLe32(h + 24, rate);
        PutLe32(h + 28, rate * 2);      // 1秒のバイト数
        PutLe16(h + 32, 2);             // 1サンプルのバイト数
        PutLe16(h + 34, 16);            // bit数
        memcpy(h + 36, "data", 4);
        PutLe32(h + 40, data_bytes);
    }
}

bool WavWriter::begin(const char* path, uint32_t sample_rate){
    finish();
    failed_ = false;
    data_bytes_ = 0;
    rate_ = sample_rate;
    if(!path || sample_rate == 0) return false;
    file_ = OSData::SD.open(path, O_RDWR | O_CREAT | O_TRUNC);
    if(!file_) return false;
    uint8_t h[kHeaderBytes];
    BuildHeader(h, rate_, 0);
    if(file_.write(h, kHeaderBytes) != kHeaderBytes){
        file_.close();
        return false;
    }
    open_ = true;
    return true;
}

bool WavWriter::write(const int16_t* samples, uint32_t n){
    if(!open_ || failed_) return false;
    if(n == 0) return true;
    const uint32_t bytes = n * 2;
    if(bytes > kMaxDataBytes - data_bytes_){
        failed_ = true;
        return false;
    }
    //WAVはリトルエンディアン。RP2350もPCもリトルエンディアンなのでそのまま書く
    const size_t w = file_.write(samples, bytes);
    data_bytes_ += (uint32_t)w;
    if(w != bytes){
        failed_ = true;
        return false;
    }
    return true;
}

bool WavWriter::updateHeader(){
    if(!open_) return false;
    uint8_t h[8];
    bool ok = true;
    PutLe32(h, 36 + data_bytes_);
    ok &= file_.seekSet(4) && file_.write(h, 4) == 4;
    PutLe32(h, data_bytes_);
    ok &= file_.seekSet(40) && file_.write(h, 4) == 4;
    ok &= file_.seekSet(kHeaderBytes + data_bytes_);
    file_.sync();
    return ok;
}

bool WavWriter::finish(){
    if(!open_) return true;
    const bool ok = updateHeader();
    file_.close();
    open_ = false;
    return ok;
}
