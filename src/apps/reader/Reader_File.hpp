#pragma once

#include <SdFat.h>

#include "apps/reader/Aozora_Text.hpp"
#include "OS_Data.hpp"

// SDのファイルを本の中身として読む口(読書アプリ)。開いている間はファイルを開いたままにする
class ReaderFile : public Aozora::Source {
    public:
        ~ReaderFile() override { close(); }

        bool open(const char* path){
            close();
            if(!OSData::SD_usable || !path) return false;
            f_ = OSData::SD.open(path, O_RDONLY);
            if(!f_) return false;
            size_ = (uint32_t)f_.size();
            return true;
        }
        void close(){
            if(f_) f_.close();
            size_ = 0;
        }
        bool isOpen(){ return (bool)f_; }

        uint32_t size() const override { return size_; }
        size_t readAt(uint32_t off, uint8_t* dst, size_t n) override {
            if(!f_ || off >= size_) return 0;
            if(!f_.seekSet(off)) return 0;
            const int got = f_.read(dst, n);
            return got > 0 ? (size_t)got : 0;
        }

    private:
        FsFile f_;
        uint32_t size_ = 0;
};
