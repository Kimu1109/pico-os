// PCビルド用のSPI代替。
//
// SD_Functions / Touch_Functions が SPIClassRP2040 を実体として持っているため、
// 型と最低限のメソッドだけを用意して受け流す(PCでは実際の通信は起きない)。
#pragma once

#include <cstdint>

#define MSBFIRST   1
#define SPI_MODE0  0

//実機のspi0/spi1に相当するダミーのハンドル
inline int spi0 = 0;
inline int spi1 = 1;

struct SPISettings {
    SPISettings(uint32_t = 0, int = MSBFIRST, int = SPI_MODE0){}
};

class SPIClassRP2040 {
public:
    SPIClassRP2040(int, int, int, int, int){}
    void begin(bool = false){}
    void end(){}
    void beginTransaction(SPISettings){}
    void endTransaction(){}
};
