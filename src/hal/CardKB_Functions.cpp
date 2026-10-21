#include "hal/CardKB_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "hal/I2C_Bus.hpp"
#include "hal/CardKB_Uart.hpp"
#include "system/Config_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

#include <Arduino.h>
#include <Wire.h>
#include <SerialPIO.h>
#include <cstring>
#if !defined(PICOOS_PC)
#include <hardware/gpio.h>
#endif

namespace {
    bool connected = false;
    int  misses = 0;
    unsigned long last_probe_ms = 0;
    unsigned long last_poll_ms  = 0;
    bool first_update = true;

    CardKbFunctions::Mode mode = CardKbFunctions::Mode::I2C;
    SerialPIO* uart = nullptr;      // UARTモードのときだけ作る(PIOのステートマシンを1つ使うため)

    void LoadConfig(){
        mode = CardKbFunctions::Mode::I2C;
        if(!OSData::SD_usable) return;
        //無いのが普通(I2Cで読む)。ParseFile()は開けないとFAILを出すので先に確かめる
        if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_CARDKB_CFG)) return;
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_CARDKB_CFG,
            [](const char* key, const char* value){
                if(strcmp(key, "mode") != 0) return;
                if(strcmp(value, "uart") == 0) mode = CardKbFunctions::Mode::Uart;
                else if(strcmp(value, "i2c") == 0) mode = CardKbFunctions::Mode::I2C;
                else LOG_SYS_WARN("cardkb.cfg: mode は i2c/uart です: %s", value);
            });
    }

    void StartUart(){
        CardKbUart::Reset();
        if(!uart) uart = new SerialPIO(NOPIN, CARDKB_UART_RX, CardKbFunctions::kUartFifoBytes);
        uart->begin(CardKbFunctions::kUartBaud);
        if(!*uart){
            //PIOの空きが無い等。打鍵が来ないだけで他には響かない
            LOG_SYS_WARN("CardKB2: UARTの受信を始められませんでした(GP%d、PIOの空きが無い?)", CARDKB_UART_RX);
            return;
        }
#if !defined(PICOOS_PC)
        //つながっていない間に浮いた端子のノイズを拾わないよう、UARTの休み(HIGH)へ引いておく
        gpio_pull_up(CARDKB_UART_RX);
#endif
        LOG_SYS_MSG("CardKB2: UARTモードで受信します(GP%d、%lubps)", CARDKB_UART_RX, CardKbFunctions::kUartBaud);
    }

    void UpdateUart(unsigned long now){
        if(!uart || !*uart) return;
        const bool was_connected = connected;
        for(int i = 0; i < CardKbFunctions::kUartMaxBytesPerUpdate && uart->available() > 0; i++){
            const int b = uart->read();
            if(b < 0) break;
            CardKbUart::Feed((uint8_t)b, now);
        }
        CardKbUart::Tick(now);
        //UARTは押したときにしか届かないので、抜けたことは分からない。正しいフレームが1つでも来たら「つながった」
        connected = CardKbUart::FramesReceived() > 0;
        if(connected && !was_connected) LOG_SYS_OK("CardKB2: UARTで打鍵を受け取りました");
    }

    bool Probe(){
        Wire.beginTransmission(CardKbFunctions::kAddress);
        return Wire.endTransmission() == 0;
    }

    // 1バイト読む。バスの応答が無ければfalse(押していない場合の0はtrueで返る)
    bool ReadByte(uint8_t& raw){
        if(Wire.requestFrom(CardKbFunctions::kAddress, (uint8_t)1) < 1) return false;
        const int v = Wire.read();
        if(v < 0) return false;
        raw = (uint8_t)v;
        return true;
    }
}

bool CardKbFunctions::Decode(uint8_t raw, KeyInputFunctions::Event& out){
    using KeyInputFunctions::Key;
    out = KeyInputFunctions::Event{};
    if(raw >= 0x20 && raw <= 0x7E){
        out.key = Key::Char;
        out.cp  = raw;
        return true;
    }
    switch(raw){
        case 0x08: case 0x7F: out.key = Key::Backspace; return true;
        case 0x09:            out.key = Key::Tab;       return true;
        case 0x0A: case 0x0D: out.key = Key::Enter;     return true;
        case 0x1B:            out.key = Key::Escape;    return true;
        //初代CardKBのカーソルキー(CardKB2のI2Cモードでは出ない)
        case 0xB4: out.key = Key::Left;  return true;
        case 0xB5: out.key = Key::Up;    return true;
        case 0xB6: out.key = Key::Down;  return true;
        case 0xB7: out.key = Key::Right; return true;
        default: return false;  //0(押していない)・読み出しのゴミ(0x01等)
    }
}

void CardKbFunctions::Setup(){
    connected = false;
    misses = 0;
    first_update = true;
    LoadConfig();
    if(mode == Mode::Uart){
        StartUart();
        return;
    }
    I2cBus::Begin();
}

void CardKbFunctions::Update(){
    UpdateAt(millis());
}

void CardKbFunctions::UpdateAt(unsigned long now){
    if(mode == Mode::Uart){
        UpdateUart(now);
        return;
    }
    if(!I2cBus::started) return;

    if(!connected){
        if(!first_update && now - last_probe_ms < kProbeIntervalMs) return;
        first_update = false;
        last_probe_ms = now;
        if(Probe()){
            connected = true;
            misses = 0;
            last_poll_ms = now - kPollIntervalMs;
            LOG_SYS_OK("CardKB2: 接続を検出しました");
        }
        return;
    }

    if(now - last_poll_ms < kPollIntervalMs) return;
    last_poll_ms = now;

    for(int i = 0; i < kMaxReadsPerUpdate; i++){
        uint8_t raw = 0;
        if(!ReadByte(raw)){
            if(++misses >= kMissLimit){
                connected = false;
                last_probe_ms = now;
                LOG_SYS_MSG("CardKB2: 接続が途絶えました");
            }
            return;
        }
        misses = 0;
        if(raw == 0) return;    //押されていない。これ以上読んでも同じ

        KeyInputFunctions::Event ev;
        if(Decode(raw, ev)) KeyInputFunctions::Push(ev);
    }
}

bool CardKbFunctions::IsConnected(){ return connected; }
CardKbFunctions::Mode CardKbFunctions::GetMode(){ return mode; }
bool CardKbFunctions::UartActive(){ return mode == Mode::Uart && uart && *uart; }
uint64_t CardKbFunctions::HeldKeys(){ return UartActive() ? CardKbUart::HeldKeys() : 0; }
void CardKbFunctions::SetGameMode(bool on){ CardKbUart::SetSuppressed(on && UartActive()); }
