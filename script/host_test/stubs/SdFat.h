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

// 常に <fcntl.h> の値を使う。以前は「先に取り込まれていればそちら、無ければ自前の値」
// にしていたが、それだと翻訳単位ごとに O_CREAT 等の値が変わり、inline の open() が
// どちらの値で作られたものがリンクされるか次第で「書き込み用に開けない」ことがあった
// (TLSのヘッダ経由で <fcntl.h> を取り込むファイルが増えて実際に踏んだ)
#include <fcntl.h>
#ifndef O_RDONLY
    #define O_RDONLY 0
#endif
#ifndef O_WRONLY
    #define O_WRONLY 1
#endif
#ifndef O_RDWR
    #define O_RDWR 2
#endif
#ifndef O_CREAT
    #define O_CREAT 4
#endif
#ifndef O_TRUNC
    #define O_TRUNC 8
#endif
#ifndef O_APPEND
    #define O_APPEND 16
#endif
#ifndef O_AT_END
    #define O_AT_END 32
#endif
// SdFatが使っている別名
#define O_READ  O_RDONLY
#define O_WRITE O_WRONLY

namespace HostSd {
    // パス -> 内容。テスト側から登録する
    inline std::map<std::string, std::string> files;
    // trueのときだけ、filesのパスからフォルダがあることにする(open()でフォルダを開け、openNext()で
    // 直下を名前順に列挙できる)。既定はfalse(以前からのテストの「フォルダは無い」前提を変えないため)
    inline bool dirs_enabled = false;

    // pathの直下にある名前(ファイル/フォルダ)を、afterより後の最初のものを返す。無ければ空
    inline std::string NextChild(const std::string& dir, const std::string& after, bool& is_dir){
        const std::string prefix = (dir == "/") ? "/" : dir + "/";
        std::string best;
        for(const auto& kv : files){
            const std::string& p = kv.first;
            if(p.compare(0, prefix.size(), prefix) != 0 || p.size() == prefix.size()) continue;
            const size_t slash = p.find('/', prefix.size());
            const std::string name = p.substr(prefix.size(), slash == std::string::npos ? std::string::npos : slash - prefix.size());
            if(name <= after) continue;
            if(best.empty() || name < best){ best = name; is_dir = slash != std::string::npos; }
        }
        return best;
    }
    inline bool IsDir(const std::string& path){
        if(!dirs_enabled) return false;
        if(path == "/") return true;
        const std::string prefix = path + "/";
        for(const auto& kv : files) if(kv.first.compare(0, prefix.size(), prefix) == 0) return true;
        return false;
    }
}

namespace HostSdCapacity {
    inline uint32_t clusters = 1000;
    inline int32_t free_clusters = 400;
    inline uint32_t bytes_per_cluster = 32768;
}

struct FsFile {
    const std::string* data_ = nullptr;  // 読み込み元(nullptrなら読み込み不可)
    std::string* wdata_ = nullptr;       // 書き込み先(nullptrなら書き込み不可)
    size_t pos_ = 0;
    // HostSd::dirs_enabled のときのフォルダ
    bool is_dir_ = false;
    std::string path_;       // 開いているフォルダ/ファイルのパス
    std::string last_child_; // openNext() で最後に返した名前
    bool wseek_ = false;     // 書き込み用で seekSet() した(以降は wpos_ へ上書き)
    size_t wpos_ = 0;

    bool isDir(){ return is_dir_; }
    bool isDirectory(){ return is_dir_; } // SdFatには両方の綴りがある(pc/compat/SdFat.hと同じ)
    bool isOpen(){ return data_ != nullptr || wdata_ != nullptr || is_dir_; }
    void close(){ data_ = nullptr; wdata_ = nullptr; pos_ = 0; wseek_ = false; wpos_ = 0; is_dir_ = false; path_.clear(); last_child_.clear(); }

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
    bool getName(char* out, size_t cap){
        if(path_.empty() || !out || cap == 0) return false;
        const size_t slash = path_.rfind('/');
        const std::string name = path_.substr(slash + 1);
        if(name.size() + 1 > cap) return false;
        memcpy(out, name.c_str(), name.size() + 1);
        return true;
    }
    FsFile openNextFile(){ return FsFile(); }
    // このスタブはパス->内容のフラットなmapで、ディレクトリという実体が無い。
    // ディレクトリの走査(PICO_IO::removeRecursiveが使う)は再現できないので、
    // 「中身が無い」ことにして常にfalseを返す。
    // ディレクトリ操作を伴う経路はPCビルド(pc/compat/SdFat.hは実ファイルシステム)で確かめること
    // HostSd::dirs_enabled のときだけ、フォルダの直下を名前順に列挙する
    bool openNext(FsFile* dir, int = O_RDONLY){
        close();
        if(!dir || !dir->is_dir_) return false;
        bool child_dir = false;
        const std::string name = HostSd::NextChild(dir->path_, dir->last_child_, child_dir);
        if(name.empty()) return false;
        dir->last_child_ = name;
        path_ = (dir->path_ == "/" ? "/" : dir->path_ + "/") + name;
        if(child_dir){
            is_dir_ = true;
        }else{
            data_ = &HostSd::files[path_];
        }
        return true;
    }
    void rewindDirectory(){ last_child_.clear(); }
    operator bool() const { return data_ != nullptr || wdata_ != nullptr || is_dir_; }

    size_t write(const void* src, size_t n){
        if(!wdata_ || !src) return 0;
        if(wseek_){
            //seekSet()した後は、その位置へ上書きする(足りなければ伸ばす)
            if(wpos_ + n > wdata_->size()) wdata_->resize(wpos_ + n);
            memcpy(&(*wdata_)[wpos_], src, n);
            wpos_ += n;
            return n;
        }
        wdata_->append((const char*)src, n);
        return n;
    }
    bool sync(){ return wdata_ != nullptr || data_ != nullptr; }

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
    bool seekSet(uint64_t pos){
        if(wdata_ && !data_){
            if(pos > wdata_->size()) return false;
            wseek_ = true;
            wpos_ = (size_t)pos;
            return true;
        }
        if(!data_ || pos > data_->size()) return false;
        pos_ = (size_t)pos;
        return true;
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
        if(it != HostSd::files.end()){
            f.data_ = &it->second;
            f.path_ = path;
        }else{
            //フォルダは末尾の / があっても開ける(本物のSdFatと同じ)
            std::string dir = path;
            while(dir.size() > 1 && dir.back() == '/') dir.pop_back();
            if(HostSd::IsDir(dir)){
                f.is_dir_ = true;
                f.path_ = dir;
            }
        }
        return f;
    }

    // 容量(SdFatのFsVolumeと同じ名前)。テストから値を差し替えられる
    uint32_t clusterCount(){ return HostSdCapacity::clusters; }
    int32_t freeClusterCount(){ return HostSdCapacity::free_clusters; }
    uint32_t bytesPerCluster(){ return HostSdCapacity::bytes_per_cluster; }

    bool exists(const char* path){
        return path && HostSd::files.count(path) > 0;
    }
    // このスタブはパスをキーにしたフラットなmapなので、ディレクトリという実体が無い。
    // 「作った」ことにしておけば、その配下のファイルはそのまま開けるため成功を返す
    // (実機のSdFat::mkdirは既定で親ディレクトリも作る)
    bool mkdir(const char*){ return true; }
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
