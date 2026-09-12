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

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 4
#define O_TRUNC 8
#define O_APPEND 16
#define O_AT_END 32
// SdFatが使っている別名
#define O_READ  O_RDONLY
#define O_WRITE O_WRONLY

namespace HostSd {
    // パス -> 内容。テスト側から登録する
    inline std::map<std::string, std::string> files;
}

struct FsFile {
    const std::string* data_ = nullptr;  // 読み込み元(nullptrなら読み込み不可)
    std::string* wdata_ = nullptr;       // 書き込み先(nullptrなら書き込み不可)
    size_t pos_ = 0;

    bool isDir(){ return false; }
    bool isOpen(){ return data_ != nullptr || wdata_ != nullptr; }
    void close(){ data_ = nullptr; wdata_ = nullptr; pos_ = 0; }

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
    operator bool() const { return data_ != nullptr || wdata_ != nullptr; }

    size_t write(const void* src, size_t n){
        if(!wdata_ || !src) return 0;
        wdata_->append((const char*)src, n);
        return n;
    }

    // SdFatと同じく改行文字まで(改行を含めて)読み、読み取ったバイト数を返す
    int fgets(char* buf, int size){
        if(!buf || size <= 0) return 0;
        if(!data_ || pos_ >= data_->size()) return 0;
        int n = 0;
        while(pos_ < data_->size() && n < size - 1){
            const char c = (*data_)[pos_++];
            buf[n++] = c;
            if(c == '\n') break;
        }
        buf[n] = '\0';
        return n;
    }
};

struct SdFat {
    bool begin(...){ return false; }

    FsFile open(const char* path, int flags = O_RDONLY){
        FsFile f;
        if(!path) return f;

        const bool writing = (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0;
        if(writing){
            auto it = HostSd::files.find(path);
            if(it == HostSd::files.end()){
                if((flags & O_CREAT) == 0) return f; //新規作成を許していないので開けない
                it = HostSd::files.emplace(path, std::string()).first;
            }
            if(flags & O_TRUNC) it->second.clear();
            //std::mapの要素はノード単位なので、他の要素を足してもこのポインタは無効にならない
            f.wdata_ = &it->second;
            return f;
        }

        auto it = HostSd::files.find(path);
        if(it != HostSd::files.end()) f.data_ = &it->second;
        return f;
    }

    bool exists(const char* path){
        return path && HostSd::files.count(path) > 0;
    }
    bool mkdir(const char*){ return false; }
    bool remove(const char* path){
        return path && HostSd::files.erase(path) > 0;
    }
    bool rename(const char* oldPath, const char* newPath){
        if(!oldPath || !newPath) return false;
        auto it = HostSd::files.find(oldPath);
        if(it == HostSd::files.end()) return false;
        HostSd::files[newPath] = it->second;
        HostSd::files.erase(it);
        return true;
    }
    bool rmdir(const char*){ return false; }
};
