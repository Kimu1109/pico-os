#include "functions/WiiClassic_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/I2C_Bus.hpp"

#include <Arduino.h>
#include <Wire.h>

namespace {
    bool connected = false;
    bool first_update = true;
    int  misses = 0;
    unsigned long last_probe_ms = 0;
    unsigned long last_poll_ms  = 0;
    uint16_t buttons = 0;
    int center_x = -1, center_y = -1;     // つながった直後の左スティック(-1 = まだ)

    bool WriteBytes(const uint8_t* data, size_t len){
        Wire.beginTransmission(WiiClassicFunctions::kAddress);
        for(size_t i = 0; i < len; i++) Wire.write(data[i]);
        return Wire.endTransmission() == 0;
    }

    // 暗号化しない初期化 + 最初の変換の開始
    bool Init(){
        static const uint8_t kInit1[] = { 0xF0, 0x55 };
        static const uint8_t kInit2[] = { 0xFB, 0x00 };
        static const uint8_t kStart[] = { 0x00 };
        return WriteBytes(kInit1, sizeof(kInit1)) && WriteBytes(kInit2, sizeof(kInit2))
            && WriteBytes(kStart, sizeof(kStart));
    }

    bool ReadSample(uint8_t data[6]){
        if(Wire.requestFrom(WiiClassicFunctions::kAddress, (uint8_t)6) < 6) return false;
        for(int i = 0; i < 6; i++){
            const int v = Wire.read();
            if(v < 0) return false;
            data[i] = (uint8_t)v;
        }
        //次の変換を始めておく(読むのは次の回)
        static const uint8_t kStart[] = { 0x00 };
        WriteBytes(kStart, sizeof(kStart));
        return true;
    }

    void Lost(unsigned long now){
        connected = false;
        buttons = 0;
        misses = 0;
        last_probe_ms = now;
        PadFunctions::SetDevice(0, false);
        LOG_SYS_MSG("Wiiクラシックコントローラー: 接続が途絶えました");
    }
}

bool WiiClassicFunctions::Decode(const uint8_t d[6], int cx, int cy, uint16_t& out){
    out = 0;
    bool all_ff = true;
    for(int i = 0; i < 6; i++) if(d[i] != 0xFF) all_ff = false;
    if(all_ff) return false;                                //初期化されていない/読めていない
    const uint8_t b4 = (uint8_t)~d[4], b5 = (uint8_t)~d[5];  //1 = 押している
    if((b4 & 0xFE) == 0xFE && b5 == 0xFF) return false;     //全部押している = 読めていない

    using namespace PadFunctions;
    if(b4 & 0x80) out |= Right;
    if(b4 & 0x40) out |= Down;
    if(b4 & 0x20) out |= L;
    if(b4 & 0x10) out |= Select;
    if(b4 & 0x08) out |= Home;
    if(b4 & 0x04) out |= Start;
    if(b4 & 0x02) out |= R;
    if(b5 & 0x80) out |= ZL;
    if(b5 & 0x40) out |= B;
    if(b5 & 0x20) out |= Y;
    if(b5 & 0x10) out |= A;
    if(b5 & 0x08) out |= X;
    if(b5 & 0x04) out |= ZR;
    if(b5 & 0x02) out |= Left;
    if(b5 & 0x01) out |= Up;

    //左スティック(0〜63。上へ倒すと大きくなる)を十字キーとしても読む
    if(cx >= 0 && cy >= 0){
        const int lx = d[0] & 0x3F;
        const int ly = d[1] & 0x3F;
        if(lx - cx >= kStickThreshold) out |= Right;
        if(cx - lx >= kStickThreshold) out |= Left;
        if(ly - cy >= kStickThreshold) out |= Up;
        if(cy - ly >= kStickThreshold) out |= Down;
    }
    return true;
}

void WiiClassicFunctions::Setup(){
    connected = false;
    first_update = true;
    misses = 0;
    buttons = 0;
    center_x = center_y = -1;
    I2cBus::Begin();
}

void WiiClassicFunctions::Update(){
    UpdateAt(millis());
}

void WiiClassicFunctions::UpdateAt(unsigned long now){
    if(!I2cBus::started) return;

    if(!connected){
        if(!first_update && now - last_probe_ms < kProbeIntervalMs) return;
        first_update = false;
        last_probe_ms = now;
        if(Init()){
            connected = true;
            misses = 0;
            buttons = 0;
            center_x = center_y = -1;
            last_poll_ms = now;     //変換が済むまで1回待つ
            LOG_SYS_OK("Wiiクラシックコントローラー: 接続を検出しました");
            PadFunctions::SetDevice(0, true);
        }
        return;
    }

    if(now - last_poll_ms < kPollIntervalMs) return;
    last_poll_ms = now;

    uint8_t data[6];
    uint16_t next = 0;
    if(!ReadSample(data)){
        if(++misses >= kMissLimit) Lost(now);
        return;
    }
    if(center_x < 0){
        //つながって最初に読めた値を左スティックの中心にする(触っていない前提)
        center_x = data[0] & 0x3F;
        center_y = data[1] & 0x3F;
    }
    if(!Decode(data, center_x, center_y, next)){
        //抜けかけ・初期化が外れた。初期化し直して様子を見る
        if(++misses >= kMissLimit){
            Lost(now);
        }else{
            Init();
        }
        return;
    }
    misses = 0;
    buttons = next;
    PadFunctions::SetDevice(buttons, true);
}

bool WiiClassicFunctions::IsConnected(){ return connected; }
uint16_t WiiClassicFunctions::Buttons(){ return buttons; }
