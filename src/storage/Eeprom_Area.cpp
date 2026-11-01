#include "storage/Eeprom_Area.hpp"

#include <cstring>

#if !defined(PICOOS_PC)

#include <Arduino.h>
#include <hardware/flash.h>

// arduino-picoのリンカスクリプトが決める EEPROM の1セクタ(EEPROM.cpp と同じもの)
extern "C" uint8_t _EEPROM_start;

namespace {
    uint32_t FlashOffset(){ return (uint32_t)((uintptr_t)&_EEPROM_start - (uintptr_t)XIP_BASE); }

    bool EraseSector(){
        // storage/Flash_Area.cpp と同じ手順(フラッシュを触る間はXIPで読めないので両コアとも止める)
        noInterrupts();
        rp2040.idleOtherCore();
        flash_range_erase(FlashOffset(), EepromArea::kBytes);
        rp2040.resumeOtherCore();
        interrupts();
        return true;
    }

    bool ProgramPage(size_t off, const uint8_t* page){
        noInterrupts();
        rp2040.idleOtherCore();
        flash_range_program(FlashOffset() + (uint32_t)off, page, EepromArea::kPageBytes);
        rp2040.resumeOtherCore();
        interrupts();
        return true;
    }
}

const uint8_t* EepromArea::Data(){ return &_EEPROM_start; }

bool EepromArea::Erase(){ return EraseSector(); }

bool EepromArea::Write(const uint8_t* p, size_t len){
    if(!p || len > kBytes) return false;
    EraseSector();
    uint8_t page[kPageBytes];
    for(size_t off = 0; off < len; off += kPageBytes){
        const size_t n = len - off < kPageBytes ? len - off : kPageBytes;
        memset(page, 0xFF, sizeof(page));
        memcpy(page, p + off, n);
        ProgramPage(off, page);
    }
    return memcmp(Data(), p, len) == 0;
}

#else // PICOOS_PC

#include "apps/firmware/Fw_Store.hpp"

#include <cstdio>
#include <string>
#if !defined(__EMSCRIPTEN__)
    #include <sys/stat.h>
#endif

namespace {
    uint8_t mem[EepromArea::kBytes];
    bool loaded = false;

    std::string FilePath(){
#if !defined(__EMSCRIPTEN__)
        if(FwStorePc::dir) return std::string(FwStorePc::dir) + "/eeprom.bin";
#endif
        return std::string();
    }

    void Load(){
        if(loaded) return;
        loaded = true;
        memset(mem, 0xFF, sizeof(mem)); // 消してあるフラッシュと同じ
        const std::string path = FilePath();
        if(path.empty()) return;
        FILE* f = fopen(path.c_str(), "rb");
        if(!f) return;
        const size_t n = fread(mem, 1, sizeof(mem), f);
        (void)n;
        fclose(f);
    }

    bool Store(){
        const std::string path = FilePath();
        if(path.empty()) return true; // Web・置き場なし: メモリの中だけ
#if !defined(__EMSCRIPTEN__)
        mkdir(FwStorePc::dir, 0755);
#endif
        FILE* f = fopen(path.c_str(), "wb");
        if(!f) return false;
        const bool ok = fwrite(mem, 1, sizeof(mem), f) == sizeof(mem);
        return fclose(f) == 0 && ok;
    }
}

const uint8_t* EepromArea::Data(){
    Load();
    return mem;
}

bool EepromArea::Erase(){
    Load();
    memset(mem, 0xFF, sizeof(mem));
    return Store();
}

bool EepromArea::Write(const uint8_t* p, size_t len){
    if(!p || len > kBytes) return false;
    Load();
    memset(mem, 0xFF, sizeof(mem));
    memcpy(mem, p, len);
    return Store();
}

#endif
