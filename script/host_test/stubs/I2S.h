// ホストテスト用のarduino-pico I2Sの代替。書き込まれたワードを溜めるだけで音は出さない。
// バッファの大きさ(setBuffers)を超えては書けない。テストは consume() で「DMAが送った」ことにする。
// 入力(INPUTで作ったもの。マイク)は、テストが rx へ積んだワードを read() で返す
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

class I2S {
public:
    explicit I2S(int direction = 1, int bclk_pin = 26, int data_pin = 28)
        : bclk(bclk_pin), data(data_pin), is_input(direction == 0){
        if(is_input) last_input = this; else last = this;
    }
    //テストが中身を覗くための入口(Sound_Functions.cppの中の実体は外から見えないため)
    inline static I2S* last = nullptr;
    inline static I2S* last_input = nullptr;   // 入力(Mic_Functions.cpp)

    bool setBCLK(int pin){ bclk = pin; return true; }
    bool setDATA(int pin){ data = pin; return true; }
    bool setBitsPerSample(int b){ bps = b; return true; }
    bool setBuffers(size_t n, size_t words, int32_t = 0){ capacity = n * words; return true; }
    bool begin(long rate){
        begin_count++;      //失敗も含めた試行回数
        if(fail_begin) return false;
        sample_rate = rate;
        running = true;
        return true;
    }
    bool end(){ running = false; queued.clear(); rx.clear(); end_count++; return true; }
    bool setDIN(int pin){ data = pin; return true; }

    // 入力: 読めるバイト数(実機と同じく4バイト=1ワード単位)
    int available(){ return (running && is_input) ? (int)(rx.size() * 4) : 0; }
    // 入力: 読めるだけ読む(待たない)。読んだバイト数
    size_t read(uint8_t* buf, size_t size){
        if(!running || !is_input || (size & 3)) return 0;
        size_t n = size / 4;
        if(n > rx.size()) n = rx.size();
        for(size_t i = 0; i < n; i++){
            const uint32_t w = rx.front();
            rx.pop_front();
            memcpy(buf + i * 4, &w, 4);
        }
        return n * 4;
    }
    bool getOverflow(){ const bool f = overflow; overflow = false; return f; }
    size_t write(int32_t v, bool){
        if(!running || queued.size() >= capacity) return 0;
        queued.push_back((uint32_t)v);
        written.push_back((uint32_t)v);
        return 4;
    }

    // テスト用: 先頭からn個送った(空いた)ことにする
    void consume(size_t n){
        if(n > queued.size()) n = queued.size();
        queued.erase(queued.begin(), queued.begin() + n);
    }

    int bclk = -1, data = -1, bps = 0;
    bool is_input = false;
    bool overflow = false;          // 入力: テストが立てると getOverflow() が1回だけ真
    std::deque<uint32_t> rx;        // 入力: まだ読まれていないワード(左,右,左,右…)
    long sample_rate = 0;
    size_t capacity = 0;
    bool running = false;
    bool fail_begin = false;
    int begin_count = 0, end_count = 0;
    std::vector<uint32_t> queued;   //まだ送られていない分
    std::vector<uint32_t> written;  //これまでに書かれた全部
};
