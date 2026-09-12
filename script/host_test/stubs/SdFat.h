// ホストテスト(script/host_test/run.sh)専用のダミーヘッダ。実機ビルドでは使われない。
#pragma once
#include <cstdint>
#include <cstddef>
struct FsFile {
    bool isDir(){ return false; }
    bool isOpen(){ return false; }
    void close(){}
    int read(){ return -1; }
    int read(void*, size_t){ return 0; }
    int available(){ return 0; }
    size_t size(){ return 0; }
    size_t fileSize(){ return 0; }
    bool seek(uint32_t){ return false; }
    size_t position(){ return 0; }
    bool getName(char*, size_t){ return false; }
    FsFile openNextFile(){ return FsFile(); }
    void rewindDirectory(){}
    operator bool() const { return false; }
    size_t write(const void*, size_t){ return 0; }
    int fgets(char*, int){ return 0; }
};
struct SdFat {
    bool begin(...){ return false; }
    FsFile open(const char*, int = 0){ return FsFile(); }
    bool exists(const char*){ return false; }
    bool mkdir(const char*){ return false; }
    bool remove(const char*){ return false; }
    bool rmdir(const char*){ return false; }
};
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 4
#define O_TRUNC 8
#define O_APPEND 16
#define O_AT_END 32
