// PCビルド用のWire(I2C)代替。
//
// 実機のI2Cの先に何かがつながっている状態を PicoPcWire で表す。既定は「何もつながっていない」
// (beginTransmission→endTransmission は NACK、requestFrom は 0 バイト)。
// CardKB2のようにあれば接続する程度の機器は、これを差し込んでホストテストできる。
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace PicoPcWire {
    // 応答するI2Cアドレス(-1=誰も居ない)。present_addr のデバイスが keys を1バイトずつ返す
    // (keys が空なら 0 を返す。CardKB の「押されていない」と同じ)
    inline int present_addr = -1;
    inline std::deque<uint8_t> keys;
}

class TwoWire {
public:
    void setSDA(int){}
    void setSCL(int){}
    void setClock(uint32_t){}
    void begin(){ begun = true; }
    bool begun = false;

    void beginTransmission(uint8_t addr){ tx_addr = addr; }
    // 0=ACK、2=アドレスにNACK(Arduinoの値に合わせる)
    uint8_t endTransmission(bool = true){ return (int)tx_addr == PicoPcWire::present_addr ? 0 : 2; }

    size_t requestFrom(uint8_t addr, uint8_t count){
        rx.clear();
        if((int)addr != PicoPcWire::present_addr) return 0;
        for(uint8_t i = 0; i < count; i++){
            if(PicoPcWire::keys.empty()){ rx.push_back(0); continue; }
            rx.push_back(PicoPcWire::keys.front());
            PicoPcWire::keys.pop_front();
        }
        return rx.size();
    }
    int available(){ return (int)rx.size(); }
    int read(){
        if(rx.empty()) return -1;
        const int v = rx.front();
        rx.pop_front();
        return v;
    }

private:
    uint8_t tx_addr = 0;
    std::deque<uint8_t> rx;
};

inline TwoWire Wire;
