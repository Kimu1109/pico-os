#include "functions/Pad_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <cstring>

namespace {
    // ---- 公開する状態 ----
    uint16_t buttons  = 0;
    uint16_t pressed  = 0;     // このフレームで押されたもの
    uint16_t released = 0;     // このフレームで離されたもの
    PadFunctions::Source source = PadFunctions::Source::None;

    // ---- USBシリアル ----
    char   line[PadFunctions::kLineMax];
    size_t line_len = 0;
    bool   line_overflow = false;   // 長すぎる行は改行まで読み捨てる
    unsigned long serial_last_ms = 0;
    uint16_t serial_buttons = 0;

    // Lua等へ見せる名前(Buttonのビット順)
    const char* const kNames[PadFunctions::kButtonCount] = {
        "up", "down", "left", "right",
        "a", "b", "x", "y",
        "l", "r", "zl", "zr",
        "start", "select", "home",
    };

    int HexDigit(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // 1行読み終えた
    void OnSerialLine(unsigned long now){
        line[line_len] = '\0';
        uint16_t value = 0;
        if(!PadFunctions::ParseLine(line, value)) return;

        serial_buttons = value;
        serial_last_ms = now;
        if(source != PadFunctions::Source::Serial){
            source = PadFunctions::Source::Serial;
            LOG_SYS_OK("コントローラー: USBシリアルからの入力を受け付けました");
        }
    }

    void ReadSerial(unsigned long now){
        size_t budget = PadFunctions::kMaxBytesPerUpdate;
        while(budget > 0 && Serial.available() > 0){
            const int c = Serial.read();
            if(c < 0) break;
            budget--;

            if(c == '\n' || c == '\r'){
                if(!line_overflow && line_len > 0) OnSerialLine(now);
                line_len = 0;
                line_overflow = false;
                continue;
            }
            if(line_overflow) continue;
            if(line_len + 1 >= sizeof(line)){
                line_overflow = true;
                continue;
            }
            line[line_len++] = (char)c;
        }

        if(source == PadFunctions::Source::Serial && now - serial_last_ms > PadFunctions::kSerialTimeoutMs){
            source = PadFunctions::Source::None;
            serial_buttons = 0;
            LOG_SYS_MSG("コントローラー: USBシリアルからの入力が途絶えました");
        }
    }
}

bool PadFunctions::ParseLine(const char* s, uint16_t& out){
    if(!s) return false;
    if(strncmp(s, "pad ", 4) != 0) return false;
    s += 4;
    while(*s == ' ') s++;

    uint32_t value = 0;
    int digits = 0;
    for(; *s; s++){
        if(*s == ' ') break;
        const int d = HexDigit(*s);
        if(d < 0) return false;
        if(++digits > 4) return false;
        value = (value << 4) | (uint32_t)d;
    }
    while(*s == ' ') s++;
    if(*s != '\0' || digits == 0) return false;

    out = (uint16_t)(value & kAllButtons);
    return true;
}

void PadFunctions::Setup(){
    buttons = pressed = released = 0;
    source = Source::None;
    line_len = 0;
    line_overflow = false;
    serial_buttons = 0;
    serial_last_ms = 0;
}

void PadFunctions::Update(){
    UpdateAt(millis());
}

void PadFunctions::UpdateAt(unsigned long now_ms){
    ReadSerial(now_ms);

    const uint16_t next = (source == Source::Serial) ? serial_buttons : 0;
    pressed  = next & ~buttons;
    released = buttons & ~next;
    buttons  = next;
}

bool PadFunctions::IsConnected(){ return source != Source::None; }
PadFunctions::Source PadFunctions::GetSource(){ return source; }

uint16_t PadFunctions::Buttons(){ return buttons; }
bool PadFunctions::IsDown(uint16_t b){ return (buttons & b) != 0; }
bool PadFunctions::Pressed(uint16_t b){ return (pressed & b) != 0; }
bool PadFunctions::Released(uint16_t b){ return (released & b) != 0; }

uint16_t PadFunctions::ButtonFromName(const char* name){
    if(!name) return 0;
    for(int i = 0; i < kButtonCount; i++){
        if(strcmp(name, kNames[i]) == 0) return (uint16_t)(1u << i);
    }
    return 0;
}

const char* PadFunctions::ButtonName(int bit_index){
    if(bit_index < 0 || bit_index >= kButtonCount) return nullptr;
    return kNames[bit_index];
}
