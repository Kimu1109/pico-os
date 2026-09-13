// PCビルド用のSdFat代替。
//
// SDカードの代わりにホストのディレクトリ(既定 pc/sdcard/)を使う。
// 実機のSDに置くファイルをそのままそこへ置けば、PCでも同じパスで読める。
// ルートは PICOOS_SD_ROOT 環境変数で差し替えられる。
//
// SdFatのAPIをそのまま真似るのではなく、pico-osが実際に使っている分だけを実装している
// (open/close/read/write/fgets/seek/fileSize/openNextFile/getName/isDir/preAllocate 等)。
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// ---- open フラグ(SdFat互換) ----
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR   0x02
#define O_CREAT  0x04
#define O_TRUNC  0x08
#define O_APPEND 0x10
#define O_AT_END 0x20
#define O_READ   O_RDONLY
#define O_WRITE  O_WRONLY

// ---- SdSpiConfig(PCでは中身を持たない) ----
#define SHARED_SPI 0
#define DEDICATED_SPI 1
inline uint32_t SD_SCK_MHZ(uint32_t mhz){ return mhz; }

struct SdSpiConfig {
    template<typename... Args>
    SdSpiConfig(Args&&...){}
};

// SDのルートになるホスト側ディレクトリ。
// 既定はビルド時に pc/sdcard の絶対パスが埋め込まれる(どこから起動しても動くように)。
// 実行時は PICOOS_SD_ROOT 環境変数で差し替えられる
#ifndef PICOOS_SD_ROOT_DEFAULT
    #define PICOOS_SD_ROOT_DEFAULT "sdcard"
#endif

namespace PicoOsSdHost {
    inline std::string root = PICOOS_SD_ROOT_DEFAULT;

    inline std::string toHostPath(const char* path){
        std::string p = path ? path : "";
        //先頭の'/'はSD上の絶対パスを意味するので、ルート配下へ寄せる
        while(!p.empty() && p.front() == '/') p.erase(p.begin());
        return root + "/" + p;
    }

    // 親ディレクトリを順に作る(SdFat::mkdir相当)
    inline bool makeDirs(const std::string& host_path){
        std::string cur;
        for(size_t i = 0; i <= host_path.size(); i++){
            if(i == host_path.size() || host_path[i] == '/'){
                if(!cur.empty()){
                    struct stat st;
                    if(stat(cur.c_str(), &st) != 0){
                        if(::mkdir(cur.c_str(), 0755) != 0) return false;
                    }
                }
            }
            if(i < host_path.size()) cur.push_back(host_path[i]);
        }
        return true;
    }
}

class FsFile {
public:
    FsFile() = default;
    FsFile(const FsFile&) = delete;
    FsFile& operator=(const FsFile&) = delete;

    FsFile(FsFile&& other) noexcept { moveFrom(other); }
    FsFile& operator=(FsFile&& other) noexcept {
        if(this != &other){ close(); moveFrom(other); }
        return *this;
    }
    ~FsFile(){ close(); }

    // ---- 実体を開く(SdFat::open から呼ばれる) ----
    bool openHost(const std::string& host_path, int flags){
        close();

        struct stat st;
        if(stat(host_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)){
            //ディレクトリはopenNextFile()で辿るためだけに開く
            dir_ = opendir(host_path.c_str());
            if(!dir_) return false;
            is_dir_ = true;
            path_ = host_path;
            return true;
        }

        const char* mode = "rb";
        if(flags & O_APPEND)      mode = (flags & O_RDWR) ? "a+b" : "ab";
        else if(flags & O_TRUNC)  mode = (flags & O_RDWR) ? "w+b" : "wb";
        else if(flags & (O_WRONLY | O_RDWR)){
            //既存を保ったまま書き込む。無ければ作る
            mode = (stat(host_path.c_str(), &st) == 0) ? "r+b" : "w+b";
        }

        if((flags & O_CREAT) && stat(host_path.c_str(), &st) != 0){
            //書き込み先の親ディレクトリを用意しておく
            const size_t slash = host_path.find_last_of('/');
            if(slash != std::string::npos) PicoOsSdHost::makeDirs(host_path.substr(0, slash));
        }

        fp_ = fopen(host_path.c_str(), mode);
        if(!fp_) return false;
        path_ = host_path;
        return true;
    }

    // SdFat互換: SDのパスを直接渡して開く
    bool open(const char* path, int flags = O_RDONLY){
        if(!path) return false;
        return openHost(PicoOsSdHost::toHostPath(path), flags);
    }

    explicit operator bool() const { return fp_ != nullptr || dir_ != nullptr; }
    bool isOpen(){ return fp_ != nullptr || dir_ != nullptr; }
    bool isDir(){ return is_dir_; }
    bool isDirectory(){ return is_dir_; } //SdFatには両方の綴りがある

    void close(){
        if(fp_){ fclose(fp_); fp_ = nullptr; }
        if(dir_){ closedir(dir_); dir_ = nullptr; }
        is_dir_ = false;
        path_.clear();
    }

    int read(){
        if(!fp_) return -1;
        const int c = fgetc(fp_);
        return c;
    }
    int read(void* dst, size_t n){
        if(!fp_ || !dst) return 0;
        return (int)fread(dst, 1, n, fp_);
    }
    size_t write(const void* src, size_t n){
        if(!fp_ || !src) return 0;
        return fwrite(src, 1, n, fp_);
    }

    // 改行まで(改行を含めて)読み、読み取ったバイト数を返す(SdFat互換)
    int fgets(char* buf, int size){
        if(!fp_ || !buf || size <= 0) return 0;
        if(!::fgets(buf, size, fp_)) return 0;
        return (int)strlen(buf);
    }

    size_t size(){ return fileSize(); }
    size_t fileSize(){
        if(!fp_) return 0;
        const long cur = ftell(fp_);
        fseek(fp_, 0, SEEK_END);
        const long end = ftell(fp_);
        fseek(fp_, cur, SEEK_SET);
        return (size_t)(end < 0 ? 0 : end);
    }
    bool seek(uint32_t pos){ return fp_ && fseek(fp_, (long)pos, SEEK_SET) == 0; }
    bool seekSet(uint32_t pos){ return seek(pos); }
    size_t position(){ return fp_ ? (size_t)ftell(fp_) : 0; }
    int available(){
        if(!fp_) return 0;
        return (int)(fileSize() - position());
    }
    void flush(){ if(fp_) fflush(fp_); }
    bool sync(){ if(fp_) fflush(fp_); return true; }

    int printf(const char* fmt, ...){
        if(!fp_ || !fmt) return 0;
        va_list ap;
        va_start(ap, fmt);
        const int n = vfprintf(fp_, fmt, ap);
        va_end(ap);
        return n;
    }

    //PCでは事前確保は不要。実機と同じくvalidLengthを伸ばさないので何もしない
    bool preAllocate(uint64_t){ return true; }
    bool truncate(uint64_t len){ return fp_ && ftruncate(fileno(fp_), (off_t)len) == 0; }

    bool getName(char* out, size_t cap){
        if(!out || cap == 0) return false;
        const size_t slash = path_.find_last_of('/');
        const std::string name = (slash == std::string::npos) ? path_ : path_.substr(slash + 1);
        if(name.size() >= cap) return false;
        memcpy(out, name.c_str(), name.size() + 1);
        return true;
    }

    void rewindDirectory(){ if(dir_) rewinddir(dir_); }

    // SdFatのopenNext(dir, flags)相当。dirを親として次のエントリを開く
    bool openNext(FsFile* dir, int flags = O_RDONLY){
        close();
        if(!dir || !dir->dir_) return false;
        while(true){
            struct dirent* e = readdir(dir->dir_);
            if(!e) return false;
            if(strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            return openHost(dir->path_ + "/" + e->d_name, flags);
        }
    }

    FsFile openNextFile(){
        FsFile next;
        if(!dir_) return next;
        while(true){
            struct dirent* e = readdir(dir_);
            if(!e) return next;
            if(strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            next.openHost(path_ + "/" + e->d_name, O_RDONLY);
            return next;
        }
    }

private:
    void moveFrom(FsFile& other){
        fp_ = other.fp_;         other.fp_ = nullptr;
        dir_ = other.dir_;       other.dir_ = nullptr;
        is_dir_ = other.is_dir_; other.is_dir_ = false;
        path_ = std::move(other.path_);
        other.path_.clear();
    }

    FILE* fp_ = nullptr;
    DIR* dir_ = nullptr;
    bool is_dir_ = false;
    std::string path_;
};

class SdFat {
public:
    template<typename... Args>
    bool begin(Args&&...){
        if(const char* env = getenv("PICOOS_SD_ROOT")) PicoOsSdHost::root = env;
        struct stat st;
        if(stat(PicoOsSdHost::root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)){
            printf("[PC] SDのルートが見つかりません: %s\n", PicoOsSdHost::root.c_str());
            return false;
        }
        return true;
    }

    FsFile open(const char* path, int flags = O_RDONLY){
        FsFile f;
        if(path) f.openHost(PicoOsSdHost::toHostPath(path), flags);
        return f;
    }

    bool exists(const char* path){
        if(!path) return false;
        struct stat st;
        return stat(PicoOsSdHost::toHostPath(path).c_str(), &st) == 0;
    }
    bool mkdir(const char* path){
        return path && PicoOsSdHost::makeDirs(PicoOsSdHost::toHostPath(path));
    }
    bool remove(const char* path){
        return path && ::remove(PicoOsSdHost::toHostPath(path).c_str()) == 0;
    }
    bool rmdir(const char* path){
        return path && ::rmdir(PicoOsSdHost::toHostPath(path).c_str()) == 0;
    }
    bool rename(const char* oldPath, const char* newPath){
        if(!oldPath || !newPath) return false;
        return ::rename(PicoOsSdHost::toHostPath(oldPath).c_str(),
                        PicoOsSdHost::toHostPath(newPath).c_str()) == 0;
    }
};
