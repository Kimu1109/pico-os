#include "net/Discovery.hpp"

#include "OS_Data.hpp"
#include "SdFat.h"

#include <cstdlib>
#include <cstring>

void Discovery::ParseLine(char* line, ServerInfo& out){
    if(!line) return;

    //行末のCR/LFを落とす
    size_t n = strlen(line);
    while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

    if(line[0] == '\0' || line[0] == '#') return; //空行とコメントは読み飛ばす

    char* tab = strchr(line, '\t');
    if(!tab) return; //区切りが無い行は無視する
    *tab = '\0';

    const char* key = line;
    const char* value = tab + 1;

    //前後の空白を落とす(サーバが揃えのために入れている場合がある)
    while(*value == ' ' || *value == '\t') value++;

    if(strcmp(key, "version") == 0){
        out.version = (int)strtol(value, nullptr, 10);
    }else if(strcmp(key, "name") == 0){
        out.name.assign(value);
    }else if(strcmp(key, "home") == 0){
        out.home.assign(value);
    }else if(strcmp(key, "search") == 0){
        out.search.assign(value);
    }else if(strcmp(key, "manifest") == 0){
        out.manifest.assign(value);
    }
    //それ以外は無視する。将来サーバがキーを足しても古いクライアントが壊れないため
}

bool Discovery::ParseFile(const char* path, ServerInfo& out){
    if(!path) return false;

    FsFile f = OSData::SD.open(path);
    if(!f) return false;

    //hostとcheckedは呼び出し側が管理するので触らない
    out.version = 0;
    out.name.clear();
    out.home.clear();
    out.search.clear();
    out.manifest.clear();

    char line[kMaxLineLen];
    while(f.fgets(line, sizeof(line)) > 0){
        ParseLine(line, out);
    }

    f.close();
    return true;
}
