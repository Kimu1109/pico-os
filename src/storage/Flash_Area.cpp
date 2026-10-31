#include "storage/Flash_Area.hpp"

#include <cstring>

#if !defined(PICOOS_PC)

#include <Arduino.h>
#include <hardware/flash.h>

// arduino-picoのリンカスクリプトが決めるLittleFSの範囲(LittleFS.cppと同じもの)
extern uint8_t _FS_start;
extern uint8_t _FS_end;

namespace {
    class DeviceFlashArea : public FlashArea {
        public:
            uint32_t size() const override { return (uint32_t)(&_FS_end - &_FS_start); }
            uint32_t baseAddress() const override { return (uint32_t)(uintptr_t)&_FS_start; }
            const uint8_t* data() const override { return &_FS_start; }
            bool erase(uint32_t off, uint32_t len) override {
                if(!this->inRange(off, len, kSectorBytes)) return false;
                // LittleFS.cpp の lfs_flash_erase() と同じ手順
                noInterrupts();
                rp2040.idleOtherCore();
                flash_range_erase(this->flashOffset(off), len);
                rp2040.resumeOtherCore();
                interrupts();
                return true;
            }
            bool program(uint32_t off, const uint8_t* p, uint32_t len) override {
                if(!p || !this->inRange(off, len, kPageBytes)) return false;
                noInterrupts();
                rp2040.idleOtherCore();
                flash_range_program(this->flashOffset(off), p, len);
                rp2040.resumeOtherCore();
                interrupts();
                return true;
            }
        private:
            uint32_t flashOffset(uint32_t off) const { return (uint32_t)((uintptr_t)&_FS_start - (uintptr_t)XIP_BASE) + off; }
            bool inRange(uint32_t off, uint32_t len, uint32_t align) const {
                const uint32_t sz = this->size();
                return len > 0 && off % align == 0 && len % align == 0 && off <= sz && len <= sz - off;
            }
    };
}

FlashArea* DefaultFlashArea(){
    static DeviceFlashArea area;
    if(&_FS_end <= &_FS_start) return nullptr; // filesystem_size = 0
    return &area;
}

#else // PICOOS_PC

#include "apps/firmware/Fw_Store.hpp"

#include <cstdlib>
#include <string>

#if !defined(__EMSCRIPTEN__)
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace {
    // 実機の割り当て(Fw_Store.cpp の PcFwStore と同じ値)
    constexpr uint32_t kBase = 0x1025F000u;
    constexpr uint32_t kSize = 0x103FF000u - 0x1025F000u;

    class PcFlashArea : public FlashArea {
        public:
            uint32_t size() const override { return kSize; }
            uint32_t baseAddress() const override { return kBase; }
            const uint8_t* data() const override { return mem_; }
            bool erase(uint32_t off, uint32_t len) override {
                if(!this->inRange(off, len, kSectorBytes)) return false;
                memset(mem_ + off, 0xFF, len);
                return true;
            }
            bool program(uint32_t off, const uint8_t* p, uint32_t len) override {
                if(!p || !this->inRange(off, len, kPageBytes)) return false;
                for(uint32_t i = 0; i < len; i++) mem_[off + i] &= p[i]; // NORフラッシュは1→0しか書けない
                return true;
            }

            bool open(){
                if(mem_) return true;
#if !defined(__EMSCRIPTEN__)
                if(!FwStorePc::dir) return false;
                mkdir(FwStorePc::dir, 0755);
                const std::string path = std::string(FwStorePc::dir) + "/gb_flash.bin";
                const int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
                if(fd < 0) return false;
                struct stat st{};
                const bool fresh = fstat(fd, &st) == 0 && st.st_size == 0;
                if(ftruncate(fd, kSize) != 0){ ::close(fd); return false; }
                void* p = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                ::close(fd);
                if(p == MAP_FAILED) return false;
                mem_ = static_cast<uint8_t*>(p);
                if(fresh) memset(mem_, 0xFF, kSize); // 消してあるフラッシュと同じにする
#else
                mem_ = static_cast<uint8_t*>(malloc(kSize));
                if(!mem_) return false;
                memset(mem_, 0xFF, kSize);
#endif
                return true;
            }
        private:
            bool inRange(uint32_t off, uint32_t len, uint32_t align) const {
                return mem_ && len > 0 && off % align == 0 && len % align == 0 && off <= kSize && len <= kSize - off;
            }
            uint8_t* mem_ = nullptr;
    };
}

FlashArea* DefaultFlashArea(){
    static PcFlashArea area;
    return area.open() ? &area : nullptr;
}

#endif
