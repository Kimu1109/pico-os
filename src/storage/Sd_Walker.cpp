#include "storage/Sd_Walker.hpp"
#include "OS_Data.hpp"

#include <string.h>

bool SdWalker::begin(const char* root){
    this->end();
    if(!OSData::SD_usable || !root) return false;
    size_t n = strlen(root);
    while(n > 1 && root[n - 1] == '/') n--;   // 末尾の/を落とす(ルートの"/"はそのまま)
    if(n == 0 || n >= sizeof(this->path_)) return false;
    memcpy(this->path_, root, n);
    this->path_[n] = '\0';
    this->dirs_[0] = OSData::SD.open(this->path_, O_RDONLY);
    if(!this->dirs_[0] || !this->dirs_[0].isDirectory()){
        this->dirs_[0].close();
        return false;
    }
    // ルートは"/"なので、子を足すときに"//"にならないよう長さ0として扱う
    this->lens_[0] = (n == 1 && this->path_[0] == '/') ? 0 : (uint16_t)n;
    this->depth_ = 0;
    return true;
}

void SdWalker::end(){
    for(int i = 0; i <= this->depth_ && i < kMaxDepth; i++) this->dirs_[i].close();
    this->depth_ = -1;
}

bool SdWalker::next(Entry& out){
    while(this->depth_ >= 0){
        FsFile f;
        if(!f.openNext(&this->dirs_[this->depth_], O_RDONLY)){
            this->dirs_[this->depth_].close();
            this->depth_--;
            continue;
        }
        char name[128];
        const bool named = f.getName(name, sizeof(name));
        const bool is_dir = f.isDirectory();
        const uint64_t size = is_dir ? 0 : (uint64_t)f.fileSize();
        f.close();
        if(!named) continue;

        // パス = 今の段のフォルダ + "/" + 名前
        const size_t base = this->lens_[this->depth_];
        const size_t nl = strlen(name);
        if(base + 1 + nl >= sizeof(this->path_)) continue;   // 長すぎるパスは飛ばす
        this->path_[base] = '/';
        memcpy(this->path_ + base + 1, name, nl + 1);

        out = Entry();
        out.path = this->path_;
        out.name = this->path_ + base + 1;
        out.is_dir = is_dir;
        out.size = size;
        out.depth = this->depth_;

        if(is_dir){
            if(this->depth_ + 1 < kMaxDepth){
                this->dirs_[this->depth_ + 1] = OSData::SD.open(this->path_, O_RDONLY);
                if(this->dirs_[this->depth_ + 1]){
                    this->depth_++;
                    this->lens_[this->depth_] = (uint16_t)(base + 1 + nl);
                }
            }else{
                out.too_deep = true;
            }
        }
        return true;
    }
    return false;
}
