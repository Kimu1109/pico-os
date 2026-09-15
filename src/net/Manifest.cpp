#include "net/Manifest.hpp"

#include "OS_Data.hpp"

#include <cstring>

bool Manifest::VersionOf(const char* manifest_file, const char* path,
                         FixedString<PICO_STR_M>& out){
    out.clear();

    if(!manifest_file || manifest_file[0] == '\0') return false;
    if(!path || path[0] == '\0') return false;

    FsFile f = OSData::SD.open(manifest_file, O_RDONLY);
    if(!f) return false;

    char line[PICO_STR_512B];
    bool found = false;
    bool tail_of_long_line = false;

    int n = 0;
    while((n = f.fgets(line, sizeof(line))) > 0){
        //収まりきらなかった行は、続きも含めて丸ごと捨てる。
        //途中で切れた断片をパスとして扱うと、別の文書に一致してしまう
        const bool complete = (line[n - 1] == '\n');
        if(tail_of_long_line){
            tail_of_long_line = !complete;
            continue;
        }
        if(!complete){
            tail_of_long_line = true;
            continue;
        }

        while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if(n == 0 || line[0] == '#') continue;

        char* tab = strchr(line, '\t');
        if(!tab) continue; //versionの無い行は突き合わせようがない
        *tab = '\0';

        const int cmp = strcmp(line, path);
        //pathの昇順に並んでいるので、追い越したらこれ以上は無い(PROTOCOL.md)
        if(cmp > 0) break;
        if(cmp != 0) continue;

        char* version = tab + 1;
        char* version_end = strchr(version, '\t'); //将来列が増えても困らないように
        if(version_end) *version_end = '\0';

        //切り詰まった検証子で比べると別物を同じと見なすので、収まらなければ諦める
        if(version[0] != '\0' && out.assign(version)) found = true;
        break;
    }

    f.close();

    if(!found) out.clear();
    return found;
}
