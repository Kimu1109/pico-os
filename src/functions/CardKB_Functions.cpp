#include "functions/CardKB_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <Wire.h>

namespace {
    bool wire_started = false;
    bool connected = false;
    int  misses = 0;
    unsigned long last_probe_ms = 0;
    unsigned long last_poll_ms  = 0;
    bool first_update = true;

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
    if(!wire_started){
        Wire.setSDA(kSdaPin);
        Wire.setSCL(kSclPin);
        Wire.setClock(kClockHz);
        Wire.begin();
        wire_started = true;
    }
}

void CardKbFunctions::Update(){
    UpdateAt(millis());
}

void CardKbFunctions::UpdateAt(unsigned long now){
    if(!wire_started) return;

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
