#include "apps/firmware/Fw_Store.hpp"

#include <cstdio>
#include <cstring>

// 実機: 内蔵フラッシュのLittleFS(platformio.ini の board_build.filesystem_size)。
//       arduino-picoのOTAのローダー(フラッシュの先頭の約12KB、どのファームにも入っている)が、
//       起動のたびに otacommand.bin を探し、あれば中身(gzip)を解きながら本体へ写して再起動する。
// PC/Web: 置き場をディレクトリで真似る(FwStorePc::dir。main_pc.cppが決める)。本体は書き換わらない。

#if !defined(PICOOS_PC)

#include <LittleFS.h>
#include <PicoOTA.h>

namespace {
    class DeviceFwStore : public FwStore {
        public:
            bool mount() override {
                if(mounted_) return true;
                // このファームにLittleFSの領域が無い(filesystem_size = 0)
                if(&_FS_end <= &_FS_start) return false;
                LittleFSConfig cfg(true); // 読めなければ作り直す(置き場は更新専用なので消えて困るものは無い)
                LittleFS.setConfig(cfg);
                mounted_ = LittleFS.begin();
                return mounted_;
            }
            void unmount() override {
                if(file_) file_.close();
                if(mounted_) LittleFS.end();
                mounted_ = false;
            }
            uint32_t fsStart() const override { return (uint32_t)(uintptr_t)&_FS_start; }
            uint32_t fsEnd() const override { return (uint32_t)(uintptr_t)&_FS_end; }
            void clear() override {
                if(file_) file_.close();
                LittleFS.remove(kPayloadName);
                LittleFS.remove(kNoteName);
                LittleFS.remove(_OTA_COMMAND_FILE);
            }
            uint32_t capacity() override {
                FSInfo info;
                if(!LittleFS.info(info) || info.totalBytes <= info.usedBytes) return 0;
                // ファイルの管理に使うブロックと、指示・控えのぶんを残す
                const uint64_t margin = (uint64_t)info.blockSize * 6;
                const uint64_t free_bytes = info.totalBytes - info.usedBytes;
                return free_bytes > margin ? (uint32_t)(free_bytes - margin) : 0;
            }
            bool openWrite() override { file_ = LittleFS.open(kPayloadName, "w"); return (bool)file_; }
            bool write(const uint8_t* p, size_t n) override { return file_ && file_.write(p, n) == n; }
            bool closeWrite() override {
                if(!file_) return false;
                file_.close();
                return true;
            }
            bool openRead() override { file_ = LittleFS.open(kPayloadName, "r"); return (bool)file_; }
            int read(uint8_t* p, size_t n) override {
                if(!file_) return -1;
                return (int)file_.read(p, n);
            }
            void closeRead() override { if(file_) file_.close(); }
            bool commit(const char* note) override {
                File f = LittleFS.open(kNoteName, "w");
                if(!f) return false;
                const size_t len = strlen(note);
                const bool ok = f.write((const uint8_t*)note, len) == len;
                f.close();
                if(!ok) return false;
                picoOTA.begin();
                if(!picoOTA.addFile(kPayloadName)) return false;
                return picoOTA.commit();
            }
            bool readNote(char* out, size_t cap) override {
                if(!cap) return false;
                File f = LittleFS.open(kNoteName, "r");
                if(!f) return false;
                const size_t n = f.read((uint8_t*)out, cap - 1);
                f.close();
                out[n] = '\0';
                return n > 0;
            }
        private:
            bool mounted_ = false;
            File file_;
    };
}

FwStore& DefaultFwStore(){
    static DeviceFwStore store;
    return store;
}

#else // PICOOS_PC

#include "storage/Flash_Area.hpp"

#include <string>
#include <sys/stat.h>

namespace FwStorePc {
    const char* dir = nullptr;
}

namespace {
    // 実機の割り当て(フラッシュ4MB、filesystem_size = 1728k)と同じ値を名乗る。
    // PCで作った更新ファイル(firmware_tool.py pack --pc)の見出しもこの値になる
    constexpr uint32_t kFsStart = 0x1024F000u;
    constexpr uint32_t kFsEnd   = 0x103FF000u;

    class PcFwStore : public FwStore {
        public:
            bool mount() override {
                if(!FwStorePc::dir) return false;
                mkdir(FwStorePc::dir, 0755);
                // 実機ではLittleFSのマウントに失敗してフォーマットされ、間借りしているゲームボーイのROMが消える。
                // 同じことを真似る(storage/Flash_Area)
                if(FlashArea* area = DefaultFlashArea()){
                    if(area->holdsRawData()) area->erase(0, FlashArea::kSectorBytes);
                }
                return true;
            }
            void unmount() override { closeFile(); }
            uint32_t fsStart() const override { return kFsStart; }
            uint32_t fsEnd() const override { return kFsEnd; }
            void clear() override {
                closeFile();
                remove(path(kPayloadName).c_str());
                remove(path(kNoteName).c_str());
                remove(path("otacommand.bin").c_str());
            }
            uint32_t capacity() override { return (kFsEnd - kFsStart) - 6 * 4096; }
            bool openWrite() override { closeFile(); f_ = fopen(path(kPayloadName).c_str(), "wb"); return f_ != nullptr; }
            bool write(const uint8_t* p, size_t n) override { return f_ && fwrite(p, 1, n, f_) == n; }
            bool closeWrite() override {
                if(!f_) return false;
                const bool ok = fclose(f_) == 0;
                f_ = nullptr;
                return ok;
            }
            bool openRead() override { closeFile(); f_ = fopen(path(kPayloadName).c_str(), "rb"); return f_ != nullptr; }
            int read(uint8_t* p, size_t n) override {
                if(!f_) return -1;
                const size_t r = fread(p, 1, n, f_);
                if(r == 0 && ferror(f_)) return -1;
                return (int)r;
            }
            void closeRead() override { closeFile(); }
            bool commit(const char* note) override {
                FILE* f = fopen(path(kNoteName).c_str(), "wb");
                if(!f) return false;
                fputs(note, f);
                fclose(f);
                // 実機のローダーへの指示の代わり(PCでは本体を書き換えない。あることだけ示す)
                f = fopen(path("otacommand.bin").c_str(), "wb");
                if(!f) return false;
                fputs("Pico OTA (PCでは書き換えない)\n", f);
                fclose(f);
                return true;
            }
            bool readNote(char* out, size_t cap) override {
                if(!cap) return false;
                FILE* f = fopen(path(kNoteName).c_str(), "rb");
                if(!f) return false;
                const size_t n = fread(out, 1, cap - 1, f);
                fclose(f);
                out[n] = '\0';
                return n > 0;
            }
        private:
            std::string path(const char* name) const { return std::string(FwStorePc::dir ? FwStorePc::dir : ".") + "/" + name; }
            void closeFile(){ if(f_){ fclose(f_); f_ = nullptr; } }
            FILE* f_ = nullptr;
    };
}

FwStore& DefaultFwStore(){
    static PcFwStore store;
    return store;
}

#endif
