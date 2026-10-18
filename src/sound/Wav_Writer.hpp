#pragma once
#include <cstdint>
#include <SdFat.h>

// 16bitモノラルのWAVをSDへ書く(録音用。1コア目だけ)。
//
// - 見出し(44バイト)の大きさの欄は、最初は0で書き、updateHeader()/finish()で書き直す。
//   録音中も数秒ごとに updateHeader() しておけば、電池が切れても途中までは読めるファイルが残る
// - 書いた量が kMaxDataBytes を超えそうなら write() は断る(WAVの大きさの欄は32bit)
class WavWriter {
public:
    static constexpr uint32_t kHeaderBytes  = 44;
    static constexpr uint32_t kMaxDataBytes = 0x7FFF0000u;     // 2GB弱(FATの4GBより手前で止める)

    ~WavWriter(){ finish(); }

    bool begin(const char* path, uint32_t sample_rate);
    // n個のサンプルを書く。全部書けたらtrue(足りなければ失敗。以降も書かない)
    bool write(const int16_t* samples, uint32_t n);
    // 見出しの大きさの欄を今の量へ書き直し、SDへ書き出す
    bool updateHeader();
    // 見出しを書き直して閉じる。開いていなければ何もしない
    bool finish();

    bool isOpen() const { return open_; }
    bool failed() const { return failed_; }
    uint32_t dataBytes() const { return data_bytes_; }
    uint32_t sampleRate() const { return rate_; }

private:
    FsFile file_;
    bool open_ = false;
    bool failed_ = false;
    uint32_t rate_ = 0;
    uint32_t data_bytes_ = 0;
};
