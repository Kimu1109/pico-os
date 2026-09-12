// ホストテスト(script/host_test/*.sh)専用のダミーヘッダ。実機ビルドでは使われない。
//
// 既定では「SDが無い」状態を模す(open()は常に失敗する)が、HostSd::files へ
// パスと内容を登録しておくとそのファイルだけは読めるようになる。
// MarkdownView::load() のようなSD読み込みを伴う経路をホストで検証するための仕組み。
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>

namespace HostSd {
    // パス -> 内容。テスト側から登録する
    inline std::map<std::string, std::string> files;
}

struct FsFile {
    const std::string* data_ = nullptr;
    size_t pos_ = 0;

    bool isDir(){ return false; }
    bool isOpen(){ return data_ != nullptr; }
    void close(){ data_ = nullptr; pos_ = 0; }

    int read(){
        if(!data_ || pos_ >= data_->size()) return -1;
        return (unsigned char)(*data_)[pos_++];
    }
    int read(void* dst, size_t n){
        if(!data_) return 0;
        const size_t avail = data_->size() - pos_;
        const size_t got = (n < avail) ? n : avail;
        memcpy(dst, data_->data() + pos_, got);
        pos_ += got;
        return (int)got;
    }
    int available(){ return data_ ? (int)(data_->size() - pos_) : 0; }
    size_t size(){ return data_ ? data_->size() : 0; }
    size_t fileSize(){ return size(); }
    bool seek(uint32_t p){
        if(!data_ || p > data_->size()) return false;
        pos_ = p;
        return true;
    }
    size_t position(){ return pos_; }
    bool getName(char*, size_t){ return false; }
    FsFile openNextFile(){ return FsFile(); }
    void rewindDirectory(){}
    operator bool() const { return data_ != nullptr; }
    size_t write(const void*, size_t){ return 0; }
    int fgets(char*, int){ return 0; }
};

struct SdFat {
    bool begin(...){ return false; }
    FsFile open(const char* path, int = 0){
        FsFile f;
        if(path){
            auto it = HostSd::files.find(path);
            if(it != HostSd::files.end()) f.data_ = &it->second;
        }
        return f;
    }
    bool exists(const char* path){
        return path && HostSd::files.count(path) > 0;
    }
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
