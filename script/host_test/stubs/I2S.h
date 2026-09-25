// ホストテスト用のarduino-pico I2Sの代替。書き込まれたワードを溜めるだけで音は出さない。
// バッファの大きさ(setBuffers)を超えては書けない。テストは consume() で「DMAが送った」ことにする
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

class I2S {
public:
    explicit I2S(int = 1){ last = this; }
    //テストが中身を覗くための入口(Sound_Functions.cppの中の実体は外から見えないため)
    inline static I2S* last = nullptr;

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
    bool end(){ running = false; queued.clear(); end_count++; return true; }
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
    long sample_rate = 0;
    size_t capacity = 0;
    bool running = false;
    bool fail_begin = false;
    int begin_count = 0, end_count = 0;
    std::vector<uint32_t> queued;   //まだ送られていない分
    std::vector<uint32_t> written;  //これまでに書かれた全部
};
