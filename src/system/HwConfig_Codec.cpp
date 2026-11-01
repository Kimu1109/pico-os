#include "system/HwConfig_Codec.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    using HwConfigCodec::Field;
    using HwConfigCodec::Group;
    using HwConfigCodec::Kind;

    // 並びはシリアルの "hwcfg show" の表示順。Get()/Put() の switch と揃えること
    constexpr Field kFields[] = {
        { "lcd.sck",      Kind::Pin,      Group::Lcd, false, "SPI0: 2/6/18/22" },
        { "lcd.mosi",     Kind::Pin,      Group::Lcd, false, "SPI0: 3/7/19" },
        { "lcd.miso",     Kind::Pin,      Group::Lcd, false, "SPI0: 0/4/16/20" },
        { "lcd.cs",       Kind::Pin,      Group::Lcd, false, "" },
        { "lcd.dc",       Kind::Pin,      Group::Lcd, false, "" },
        { "lcd.rst",      Kind::Pin,      Group::Lcd, true,  "none可" },
        { "lcd.bl",       Kind::Pin,      Group::Lcd, true,  "none可" },
        { "lcd.hz",       Kind::Hz,       Group::Lcd, false, "1M-80M" },
        { "lcd.rotation", Kind::Rotation, Group::Lcd, false, "0/2/4/6" },
        { "lcd.offset_x", Kind::Offset,   Group::Lcd, false, "-480..480" },
        { "lcd.offset_y", Kind::Offset,   Group::Lcd, false, "-480..480" },
        { "lcd.invert",   Kind::Tri,      Group::Lcd, false, "auto/on/off" },
        { "lcd.bgr",      Kind::Tri,      Group::Lcd, false, "auto/on/off" },
        { "touch.cs",     Kind::Pin,      Group::Lcd, false, "" },
        { "touch.irq",    Kind::Pin,      Group::Lcd, false, "" },
        { "sd.cs",        Kind::Pin,      Group::Sd,  false, "" },
        { "sd.sck",       Kind::Pin,      Group::Sd,  false, "SPI1: 10/14/26" },
        { "sd.mosi",      Kind::Pin,      Group::Sd,  false, "SPI1: 11/15/27" },
        { "sd.miso",      Kind::Pin,      Group::Sd,  false, "SPI1: 8/12/28" },
        { "sd.hz",        Kind::Hz,       Group::Sd,  false, "1M-50M" },
    };
    constexpr size_t kCount = sizeof(kFields) / sizeof(kFields[0]);

    constexpr char kMagic[4] = { 'P', 'H', 'W', 'C' };

    // RP2350A(Pico 2 W)のGPIOは0〜29。23/24/25/29は無線チップ(CYW43439)がつながっていて使えない
    constexpr int kMaxGpio = 29;
    bool ReservedPin(int p){ return p == 23 || p == 24 || p == 25 || p == 29; }

    // SPIの各端子に使えるピン(RP2350Aの機能表。LovyanGFX/arduino-picoの表と同じ)
    constexpr int8_t kSpi0Sck[]  = { 2, 6, 18, 22 };
    constexpr int8_t kSpi0Miso[] = { 0, 4, 16, 20 };
    constexpr int8_t kSpi0Mosi[] = { 3, 7, 19, 23 };
    constexpr int8_t kSpi1Sck[]  = { 10, 14, 26 };
    constexpr int8_t kSpi1Miso[] = { 8, 12, 24, 28 };
    constexpr int8_t kSpi1Mosi[] = { 11, 15, 27 };

    template<size_t N>
    bool InList(int p, const int8_t (&list)[N]){
        for(size_t i = 0; i < N; i++) if(list[i] == p) return true;
        return false;
    }

    bool IEquals(const char* a, const char* b){
        for(; *a && *b; a++, b++){
            char x = *a, y = *b;
            if(x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if(y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if(x != y) return false;
        }
        return *a == *b;
    }

    // 符号つきの10進の整数だけ(前後の空白は呼ぶ側で落とす)
    bool ParseInt(const char* s, long& out, const char** rest){
        char* end = nullptr;
        if(!s || !*s) return false;
        if(!((*s >= '0' && *s <= '9') || *s == '-' || *s == '+')) return false;
        out = strtol(s, &end, 10);
        if(end == s) return false;
        *rest = end;
        return true;
    }

    bool ParseExactInt(const char* s, long& out){
        const char* rest = nullptr;
        return ParseInt(s, out, &rest) && *rest == '\0';
    }

    void Fail(char* why, size_t cap, const char* key, const char* msg){
        if(why && cap) snprintf(why, cap, "%s: %s", key, msg);
    }
}

size_t HwConfigCodec::FieldCount(){ return kCount; }
const HwConfigCodec::Field& HwConfigCodec::FieldAt(size_t i){ return kFields[i < kCount ? i : 0]; }

int HwConfigCodec::Find(const char* key){
    if(!key) return -1;
    for(size_t i = 0; i < kCount; i++) if(strcmp(kFields[i].key, key) == 0) return (int)i;
    return -1;
}

int32_t HwConfigCodec::Get(const HwConfig& c, size_t i){
    switch(i){
        case 0:  return c.lcd_sck;
        case 1:  return c.lcd_mosi;
        case 2:  return c.lcd_miso;
        case 3:  return c.lcd_cs;
        case 4:  return c.lcd_dc;
        case 5:  return c.lcd_rst;
        case 6:  return c.lcd_bl;
        case 7:  return (int32_t)c.lcd_hz;
        case 8:  return c.lcd_rotation;
        case 9:  return c.lcd_offset_x;
        case 10: return c.lcd_offset_y;
        case 11: return c.lcd_invert;
        case 12: return c.lcd_bgr;
        case 13: return c.touch_cs;
        case 14: return c.touch_irq;
        case 15: return c.sd_cs;
        case 16: return c.sd_sck;
        case 17: return c.sd_mosi;
        case 18: return c.sd_miso;
        case 19: return (int32_t)c.sd_hz;
        default: return 0;
    }
}

void HwConfigCodec::Put(HwConfig& c, size_t i, int32_t v){
    switch(i){
        case 0:  c.lcd_sck = (int8_t)v; break;
        case 1:  c.lcd_mosi = (int8_t)v; break;
        case 2:  c.lcd_miso = (int8_t)v; break;
        case 3:  c.lcd_cs = (int8_t)v; break;
        case 4:  c.lcd_dc = (int8_t)v; break;
        case 5:  c.lcd_rst = (int8_t)v; break;
        case 6:  c.lcd_bl = (int8_t)v; break;
        case 7:  c.lcd_hz = (uint32_t)v; break;
        case 8:  c.lcd_rotation = (uint8_t)v; break;
        case 9:  c.lcd_offset_x = (int16_t)v; break;
        case 10: c.lcd_offset_y = (int16_t)v; break;
        case 11: c.lcd_invert = (int8_t)v; break;
        case 12: c.lcd_bgr = (int8_t)v; break;
        case 13: c.touch_cs = (int8_t)v; break;
        case 14: c.touch_irq = (int8_t)v; break;
        case 15: c.sd_cs = (int8_t)v; break;
        case 16: c.sd_sck = (int8_t)v; break;
        case 17: c.sd_mosi = (int8_t)v; break;
        case 18: c.sd_miso = (int8_t)v; break;
        case 19: c.sd_hz = (uint32_t)v; break;
        default: break;
    }
}

bool HwConfigCodec::ParseValue(Kind kind, const char* text, int32_t& out){
    if(!text) return false;
    long v = 0;
    switch(kind){
        case Kind::Pin:
            if(IEquals(text, "none")){ out = -1; return true; }
            if(!ParseExactInt(text, v) || v < -1 || v > kMaxGpio) return false;
            out = (int32_t)v;
            return true;
        case Kind::Hz: {
            const char* rest = nullptr;
            if(!ParseInt(text, v, &rest) || v <= 0) return false;
            long mul = 1;
            if(*rest == 'k' || *rest == 'K'){ mul = 1000; rest++; }
            else if(*rest == 'm' || *rest == 'M'){ mul = 1000000; rest++; }
            if(*rest != '\0' && !IEquals(rest, "hz")) return false;
            if(v > 200000000L / mul) return false;
            out = (int32_t)(v * mul);
            return true;
        }
        case Kind::Rotation:
            if(!ParseExactInt(text, v) || v < 0 || v > 7) return false;
            out = (int32_t)v;
            return true;
        case Kind::Offset:
            if(!ParseExactInt(text, v) || v < -480 || v > 480) return false;
            out = (int32_t)v;
            return true;
        case Kind::Tri:
            if(IEquals(text, "auto") || strcmp(text, "-1") == 0){ out = -1; return true; }
            if(IEquals(text, "on") || IEquals(text, "true") || strcmp(text, "1") == 0){ out = 1; return true; }
            if(IEquals(text, "off") || IEquals(text, "false") || strcmp(text, "0") == 0){ out = 0; return true; }
            return false;
    }
    return false;
}

void HwConfigCodec::FormatValue(Kind kind, int32_t v, char* out, size_t cap){
    if(!out || !cap) return;
    switch(kind){
        case Kind::Pin:
            if(v < 0) snprintf(out, cap, "none");
            else snprintf(out, cap, "%ld", (long)v);
            return;
        case Kind::Hz:
            if(v > 0 && v % 1000000 == 0) snprintf(out, cap, "%ldM", (long)(v / 1000000));
            else if(v > 0 && v % 1000 == 0) snprintf(out, cap, "%ldk", (long)(v / 1000));
            else snprintf(out, cap, "%ld", (long)v);
            return;
        case Kind::Tri:
            snprintf(out, cap, "%s", v < 0 ? "auto" : (v ? "on" : "off"));
            return;
        case Kind::Rotation:
        case Kind::Offset:
            snprintf(out, cap, "%ld", (long)v);
            return;
    }
}

bool HwConfigCodec::Set(HwConfig& c, const char* key, const char* value, const char** why){
    const int i = Find(key);
    if(i < 0){
        if(why) *why = "知らないキーです";
        return false;
    }
    int32_t v = 0;
    if(!ParseValue(kFields[i].kind, value, v)){
        if(why) *why = "値の形が合いません";
        return false;
    }
    if(kFields[i].kind == Kind::Pin && v < 0 && !kFields[i].optional_pin){
        if(why) *why = "このピンは省けません";
        return false;
    }
    Put(c, (size_t)i, v);
    return true;
}

bool HwConfigCodec::Validate(const HwConfig& c, char* why, size_t cap){
    if(why && cap) why[0] = '\0';

    // ピンの1本ずつ
    for(size_t i = 0; i < kCount; i++){
        const Field& f = kFields[i];
        if(f.kind != Kind::Pin) continue;
        const int p = Get(c, i);
        if(p < 0){
            if(p == -1 && f.optional_pin) continue;
            Fail(why, cap, f.key, "このピンは省けません");
            return false;
        }
        if(p > kMaxGpio){
            Fail(why, cap, f.key, "0〜29で指定してください");
            return false;
        }
        if(ReservedPin(p)){
            Fail(why, cap, f.key, "23/24/25/29は無線チップ用です");
            return false;
        }
    }

    // SPIの端子に使えるピンか(使えないピンを渡すとarduino-picoのSPIは止まる)
    struct SpiPin { int8_t pin; const char* key; bool ok; const char* msg; };
    const SpiPin spi[] = {
        { c.lcd_sck,  "lcd.sck",  InList(c.lcd_sck, kSpi0Sck),   "SPIの端子に使えないピンです(hwcfg keys)" },
        { c.lcd_mosi, "lcd.mosi", InList(c.lcd_mosi, kSpi0Mosi), "SPIの端子に使えないピンです(hwcfg keys)" },
        { c.lcd_miso, "lcd.miso", InList(c.lcd_miso, kSpi0Miso), "SPIの端子に使えないピンです(hwcfg keys)" },
        { c.sd_sck,   "sd.sck",   InList(c.sd_sck, kSpi1Sck),    "SPIの端子に使えないピンです(hwcfg keys)" },
        { c.sd_mosi,  "sd.mosi",  InList(c.sd_mosi, kSpi1Mosi),  "SPIの端子に使えないピンです(hwcfg keys)" },
        { c.sd_miso,  "sd.miso",  InList(c.sd_miso, kSpi1Miso),  "SPIの端子に使えないピンです(hwcfg keys)" },
    };
    for(const SpiPin& s : spi){
        if(!s.ok){
            Fail(why, cap, s.key, s.msg);
            return false;
        }
    }

    // 同じピンを2回使っていないか(タッチは液晶のSCK/MOSI/MISOを共有するが、それは別のキーにしていない)
    for(size_t i = 0; i < kCount; i++){
        if(kFields[i].kind != Kind::Pin) continue;
        const int p = Get(c, i);
        if(p < 0) continue;
        for(size_t j = i + 1; j < kCount; j++){
            if(kFields[j].kind != Kind::Pin || Get(c, j) != p) continue;
            if(why && cap) snprintf(why, cap, "%s: %s と同じピン(%d)です", kFields[j].key, kFields[i].key, p);
            return false;
        }
    }

    if(c.lcd_hz < 1000000u || c.lcd_hz > 80000000u){
        Fail(why, cap, "lcd.hz", "1M〜80Mにしてください");
        return false;
    }
    if(c.sd_hz < 1000000u || c.sd_hz > 50000000u){
        Fail(why, cap, "sd.hz", "1M〜50Mにしてください");
        return false;
    }
    if(c.lcd_rotation > 7){
        Fail(why, cap, "lcd.rotation", "0〜7にしてください");
        return false;
    }
    if(c.lcd_rotation & 1){
        Fail(why, cap, "lcd.rotation", "奇数(縦横の入れ替え)は使えません");
        return false;
    }
    if(c.lcd_offset_x < -480 || c.lcd_offset_x > 480 || c.lcd_offset_y < -480 || c.lcd_offset_y > 480){
        Fail(why, cap, "lcd.offset", "-480〜480にしてください");
        return false;
    }
    if(c.lcd_invert < -1 || c.lcd_invert > 1 || c.lcd_bgr < -1 || c.lcd_bgr > 1){
        Fail(why, cap, "lcd.invert/bgr", "auto/on/offのどれかにしてください");
        return false;
    }
    return true;
}

bool HwConfigCodec::SameGroup(const HwConfig& a, const HwConfig& b, Group g){
    for(size_t i = 0; i < kCount; i++){
        if(kFields[i].group == g && Get(a, i) != Get(b, i)) return false;
    }
    return true;
}

void HwConfigCodec::CopyGroup(HwConfig& dst, const HwConfig& src, Group g){
    for(size_t i = 0; i < kCount; i++){
        if(kFields[i].group == g) Put(dst, i, Get(src, i));
    }
}

uint32_t HwConfigCodec::Crc32(const uint8_t* p, size_t len){
    // zlibと同じCRC-32(多項式0xEDB88320)。数百バイトを起動時に1回読むだけなので表は持たない
    uint32_t crc = 0xFFFFFFFFu;
    for(size_t i = 0; i < len; i++){
        crc ^= p[i];
        for(int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

size_t HwConfigCodec::Encode(const HwConfig& c, uint8_t* out, size_t cap){
    if(!out || cap < kHeaderBytes) return 0;
    const HwConfig def = HwConfigDefault();
    size_t len = 0;
    char* body = reinterpret_cast<char*>(out + kHeaderBytes);
    const size_t body_cap = cap - kHeaderBytes;
    for(size_t i = 0; i < kCount; i++){
        const int32_t v = Get(c, i);
        if(v == Get(def, i)) continue;
        char val[16];
        FormatValue(kFields[i].kind, v, val, sizeof(val));
        const int n = snprintf(body + len, body_cap - len, "%s=%s\n", kFields[i].key, val);
        if(n < 0 || (size_t)n >= body_cap - len) return 0;
        len += (size_t)n;
    }
    const uint32_t crc = Crc32(out + kHeaderBytes, len);
    memcpy(out, kMagic, 4);
    out[4] = kVersion;
    out[5] = 0;
    out[6] = (uint8_t)(len & 0xFF);
    out[7] = (uint8_t)(len >> 8);
    out[8] = (uint8_t)(crc & 0xFF);
    out[9] = (uint8_t)(crc >> 8);
    out[10] = (uint8_t)(crc >> 16);
    out[11] = (uint8_t)(crc >> 24);
    return kHeaderBytes + len;
}

HwConfigCodec::DecodeResult HwConfigCodec::Decode(const uint8_t* data, size_t len, HwConfig& out){
    out = HwConfigDefault();
    if(!data || len < kHeaderBytes || memcmp(data, kMagic, 4) != 0) return DecodeResult::Empty;
    if(data[4] != kVersion) return DecodeResult::Broken;
    const size_t body_len = (size_t)data[6] | ((size_t)data[7] << 8);
    if(body_len > len - kHeaderBytes) return DecodeResult::Broken;
    const uint32_t crc = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    const uint8_t* body = data + kHeaderBytes;
    if(Crc32(body, body_len) != crc) return DecodeResult::Broken;

    HwConfig c = HwConfigDefault();
    size_t pos = 0;
    while(pos < body_len){
        size_t end = pos;
        while(end < body_len && body[end] != '\n') end++;
        size_t line_len = end - pos;
        if(line_len > 0 && body[pos + line_len - 1] == '\r') line_len--;
        if(line_len > 0){
            char line[64];
            if(line_len >= sizeof(line)) return DecodeResult::Broken;
            memcpy(line, body + pos, line_len);
            line[line_len] = '\0';
            char* eq = strchr(line, '=');
            if(!eq) return DecodeResult::Broken;
            *eq = '\0';
            const int i = Find(line);
            if(i >= 0){ // 知らないキーは読み飛ばす
                int32_t v = 0;
                if(!ParseValue(kFields[i].kind, eq + 1, v)) return DecodeResult::Broken;
                Put(c, (size_t)i, v);
            }
        }
        pos = end + 1;
    }
    out = c;
    return DecodeResult::Ok;
}
