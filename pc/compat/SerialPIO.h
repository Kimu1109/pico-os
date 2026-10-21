// PCビルド/ホストテスト用のSerialPIO(arduino-picoのPIOで作るUART)代替。
//
// 実機のピンの先から届くバイトを PicoPcSerialPio::rx で表す(既定は何も届かない)。
// 送信は捨てる。使っているのはCardKB2のUARTモードの受信だけ(hal/CardKB_Functions.cpp)。
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#ifndef NOPIN
#define NOPIN ((uint8_t)0xff)
#endif

namespace PicoPcSerialPio {
    inline std::deque<uint8_t> rx;
    // falseにすると begin() が失敗した扱い(PIOの空きが無い場合)
    inline bool begin_ok = true;
}

class SerialPIO {
public:
    SerialPIO(uint8_t tx, uint8_t rx_pin, size_t fifo = 32) : tx_(tx), rx_(rx_pin), fifo_(fifo) {}
    void begin(unsigned long baud = 115200){ baud_ = baud; running_ = PicoPcSerialPio::begin_ok; }
    void end(){ running_ = false; }
    explicit operator bool() const { return running_; }
    int available(){ return running_ ? (int)PicoPcSerialPio::rx.size() : 0; }
    int read(){
        if(!running_ || PicoPcSerialPio::rx.empty()) return -1;
        const uint8_t b = PicoPcSerialPio::rx.front();
        PicoPcSerialPio::rx.pop_front();
        return b;
    }
    size_t write(uint8_t){ return 1; }
    unsigned long baud() const { return baud_; }
    uint8_t rxPin() const { return rx_; }
private:
    uint8_t tx_, rx_;
    size_t fifo_;
    unsigned long baud_ = 0;
    bool running_ = false;
};
