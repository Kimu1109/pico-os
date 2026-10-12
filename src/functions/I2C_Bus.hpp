#pragma once
#include <cstdint>
#include <Wire.h>

// 外部のI2C機器(CardKB2・Wiiクラシックコントローラー)が共有するバス I2C0 の準備。
//
// SDA=GP0 / SCL=GP1(外部コントローラー用に空けてある端子)。100kHz(CardKB2が400kHzに耐えるか
// 分からないため、遅い方に合わせる。Wiiクラシックの6バイトの読み出しは約0.7ms)。
// どの機器も「無いのが普通」なので、begin() はここで1回だけ行い、機器ごとの Setup() から呼ぶ。
namespace I2cBus {
    constexpr int      kSdaPin  = 0;
    constexpr int      kSclPin  = 1;
    constexpr uint32_t kClockHz = 100000;

    inline bool started = false;

    inline void Begin(){
        if(started) return;
        Wire.setSDA(kSdaPin);
        Wire.setSCL(kSclPin);
        Wire.setClock(kClockHz);
        Wire.begin();
        started = true;
    }
}
