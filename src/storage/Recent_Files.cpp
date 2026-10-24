#include "storage/Recent_Files.hpp"

#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

namespace RecentFiles {

namespace {
    // 行末の改行を落とす
    void Chomp(char* line){
        size_t n = strlen(line);
        while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    }

    bool WriteLine(FsFile& f, const char* s){
        const size_t n = strlen(s);
        return f.write(s, n) == n && f.write("\n", 1) == 1;
    }

    // 一覧を写し直す。first が非nullなら先頭に書き、map(行) が false の行は落とす。
    // map は行を書き換えてよい(Rename)
    template<typename Map>
    bool Rewrite(const char* first, Map&& map){
        if(!OSData::SD_usable) return false;
        const char* path = PICO_Path::FILE::SYS_RECENT_FILES;
        char tmp[PICO_PATH_LEN + 1];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        if(!OSData::SD.exists(PICO_Path::DIR::SYS)) OSData::SD.mkdir(PICO_Path::DIR::SYS);
        FsFile dst = OSData::SD.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        if(!dst) return false;
        int count = 0;
        bool ok = true;
        if(first){
            ok = WriteLine(dst, first);
            count++;
        }
        FsFile src = OSData::SD.open(path, O_RDONLY);
        if(src){
            char line[PICO_PATH_LEN + 2];
            while(ok && count < kMaxFiles){
                const int n = src.fgets(line, sizeof(line));
                if(n <= 0) break;
                // 長すぎて途中で切れた行は捨てる(続きは次の fgets で来て、"/" で始まらないので落ちる)
                if(line[n - 1] != '\n' && n == (int)sizeof(line) - 1) continue;
                Chomp(line);
                if(line[0] != '/') continue;
                if(first && strcmp(line, first) == 0) continue;
                if(!map(line, sizeof(line))) continue;
                ok = WriteLine(dst, line);
                count++;
            }
            src.close();
        }
        dst.close();
        if(!ok){
            OSData::SD.remove(tmp);
            return false;
        }
        if(OSData::SD.exists(path)) OSData::SD.remove(path);
        return OSData::SD.rename(tmp, path);
    }
}

bool Touch(const char* path){
    if(!path || path[0] != '/' || strlen(path) > PICO_PATH_LEN) return false;
    // 自分の記録を記録しない
    if(strncmp(path, PICO_Path::DIR::SYS, strlen(PICO_Path::DIR::SYS)) == 0) return false;
    const bool ok = Rewrite(path, [](char*, size_t){ return true; });
    if(!ok) LOG_APP_WARN("最近のファイル: %s を書けませんでした", PICO_Path::FILE::SYS_RECENT_FILES);
    return ok;
}

int List(FixedString<PICO_PATH_LEN>* out, int max){
    if(!out || max <= 0 || !OSData::SD_usable) return 0;
    FsFile f = OSData::SD.open(PICO_Path::FILE::SYS_RECENT_FILES, O_RDONLY);
    if(!f) return 0;
    int count = 0;
    char line[PICO_PATH_LEN + 2];
    while(count < max){
        const int n = f.fgets(line, sizeof(line));
        if(n <= 0) break;
        if(line[n - 1] != '\n' && n == (int)sizeof(line) - 1) continue;
        Chomp(line);
        if(line[0] != '/') continue;
        out[count++].assign(line);
    }
    f.close();
    return count;
}

bool Forget(const char* path){
    if(!path) return false;
    return Rewrite(nullptr, [path](char* line, size_t){ return strcmp(line, path) != 0; });
}

bool Rename(const char* old_path, const char* new_path){
    if(!old_path || !new_path || !OSData::SD_usable) return false;
    if(!OSData::SD.exists(PICO_Path::FILE::SYS_RECENT_FILES)) return true;
    const size_t old_len = strlen(old_path);
    return Rewrite(nullptr, [&](char* line, size_t cap){
        if(!PICO_IO::isSameOrInside(line, old_path)) return true;
        // old_path(またはその中)を new_path の下へ付け替える
        FixedString<PICO_PATH_LEN> moved(new_path);
        if(!moved.append(line + old_len)) return false;   // 長すぎるものは忘れる
        if(moved.length() + 1 > cap) return false;
        memcpy(line, moved.c_str(), moved.length() + 1);
        return true;
    });
}

}
