// PCビルド用のWire(I2C)代替。
//
// 実機のI2Cの先に何かがつながっている状態を PicoPcWire で表す。既定は「何もつながっていない」
// (beginTransmission→endTransmission は NACK、requestFrom は 0 バイト)。
// CardKB2・Wiiクラシックコントローラーのように、あれば接続する程度の機器はこれを差し込んでホストテストできる。
//   - present_addr / keys … 1バイト読むと keys を1つずつ返す簡単な機器(CardKB2)
//   - devices             … 書き込みと読み出しを自分で答える機器(Wiiクラシック等)。アドレス → Device*
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace PicoPcWire {
    // 応答するI2Cアドレス(-1=誰も居ない)。present_addr のデバイスが keys を1バイトずつ返す
    // (keys が空なら 0 を返す。CardKB の「押されていない」と同じ)
    inline int present_addr = -1;
    inline std::deque<uint8_t> keys;

    struct Device {
        virtual ~Device() = default;
        // beginTransmission〜endTransmission の間に書かれたバイト列。falseならNACK
        virtual bool onWrite(const uint8_t* data, size_t len) = 0;
        // requestFrom。返したバイト数(0ならNACK)
        virtual size_t onRead(uint8_t* data, size_t len) = 0;
    };
    inline std::map<int, Device*> devices;
}

class TwoWire {
public:
    void setSDA(int){}
    void setSCL(int){}
    void setClock(uint32_t){}
    void begin(){ begun = true; }
    bool begun = false;

    void beginTransmission(uint8_t addr){ tx_addr = addr; tx.clear(); }
    size_t write(uint8_t b){ tx.push_back(b); return 1; }
    // 0=ACK、2=アドレスにNACK(Arduinoの値に合わせる)
    uint8_t endTransmission(bool = true){
        auto it = PicoPcWire::devices.find((int)tx_addr);
        if(it != PicoPcWire::devices.end() && it->second){
            return it->second->onWrite(tx.data(), tx.size()) ? 0 : 2;
        }
        return (int)tx_addr == PicoPcWire::present_addr ? 0 : 2;
    }

    size_t requestFrom(uint8_t addr, uint8_t count){
        rx.clear();
        auto it = PicoPcWire::devices.find((int)addr);
        if(it != PicoPcWire::devices.end() && it->second){
            uint8_t buf[32];
            const size_t want = count < sizeof(buf) ? count : sizeof(buf);
            const size_t got = it->second->onRead(buf, want);
            for(size_t i = 0; i < got; i++) rx.push_back(buf[i]);
            return rx.size();
        }
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
    std::vector<uint8_t> tx;
    std::deque<uint8_t> rx;
};

inline TwoWire Wire;
