#include "apps/reader/Reader_Store.hpp"

#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ReaderStore {

namespace {
    constexpr size_t kLineMax = 512;

    // 1行をタブで分ける(行の中身を書き換える)。分けた数を返す
    int SplitTabs(char* line, char** fields, int max){
        // 行末の改行を落とす
        size_t n = strlen(line);
        while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        int count = 0;
        char* p = line;
        while(count < max){
            fields[count++] = p;
            char* tab = strchr(p, '\t');
            if(!tab) break;
            *tab = '\0';
            p = tab + 1;
        }
        return count;
    }

    bool ParseState(const char* s, State& out){
        unsigned long pos = 0;
        int block = 0, para = 0, flags = 0;
        if(sscanf(s, "%lu,%d,%d,%d", &pos, &block, &para, &flags) != 4) return false;
        out = State();
        out.pos = (uint32_t)pos;
        out.block = (int8_t)block;
        out.para = (int8_t)para;
        out.flags = (uint8_t)flags;
        return true;
    }

    void FormatState(const State& s, char* out, size_t n){
        snprintf(out, n, "%lu,%d,%d,%d", (unsigned long)s.pos, (int)s.block, (int)s.para, (int)s.flags);
    }

    // タブ・改行は1行の書式を壊すので空白へ
    void Sanitize(const char* in, char* out, size_t n){
        size_t i = 0;
        for(; in[i] && i + 1 < n; i++){
            const char c = in[i];
            out[i] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
        }
        out[i] = '\0';
    }

    bool WriteLine(FsFile& f, const char* s){
        const size_t n = strlen(s);
        return f.write(s, n) == n;
    }

    // 一覧を書き直す。skip_path の本の行は落とし、first が非nullならそれを先頭に書く
    template<typename WriteFirst>
    bool Rewrite(const char* skip_path, WriteFirst&& first){
        if(!OSData::SD_usable) return false;
        const char* path = PICO_Path::FILE::SYS_READER_BOOKS;
        char tmp[PICO_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FsFile dst = OSData::SD.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        if(!dst) return false;
        int books = 0;
        bool ok = first(dst, books);

        FsFile src = OSData::SD.open(path, O_RDONLY);
        if(src){
            char line[kLineMax];
            char work[kLineMax];
            bool keep = false;
            while(ok){
                const int n = src.fgets(line, sizeof(line));
                if(n <= 0) break;
                // 長すぎて途中で切れた行は捨てる(残りは次の fgets で別の行として来るが、形が合わないので落ちる)
                if(line[n - 1] != '\n' && n == (int)sizeof(line) - 1) continue;
                memcpy(work, line, (size_t)n + 1);
                char* f[5];
                const int c = SplitTabs(work, f, 5);
                if(c < 3) continue;
                const bool same = (skip_path && strcmp(f[1], skip_path) == 0);
                if(f[0][0] == 'B' && f[0][1] == '\0'){
                    keep = !same && books < kMaxBooks;
                    if(keep) books++;
                }else if(!(f[0][0] == 'M' && f[0][1] == '\0')){
                    continue;
                }
                if(!keep || same) continue;
                ok = WriteLine(dst, line);
                if(ok && line[strlen(line) - 1] != '\n') ok = WriteLine(dst, "\n");
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

bool LoadBook(const char* path, State& at, Mark* marks, int& mark_count){
    at = State();
    mark_count = 0;
    if(!path || !OSData::SD_usable) return false;
    FsFile f = OSData::SD.open(PICO_Path::FILE::SYS_READER_BOOKS, O_RDONLY);
    if(!f) return false;
    bool found = false;
    char line[kLineMax];
    while(true){
        const int n = f.fgets(line, sizeof(line));
        if(n <= 0) break;
        char* fld[5];
        const int c = SplitTabs(line, fld, 5);
        if(c < 3 || strcmp(fld[1], path) != 0) continue;
        if(strcmp(fld[0], "B") == 0){
            if(ParseState(fld[2], at)) found = true;
        }else if(strcmp(fld[0], "M") == 0 && marks && mark_count < kMaxMarks){
            Mark& m = marks[mark_count];
            if(!ParseState(fld[2], m.at)) continue;
            m.label.assign(c >= 4 ? fld[3] : "");
            mark_count++;
        }
    }
    f.close();
    return found;
}

bool SaveBook(const char* path, const char* title, uint32_t size, const State& at, const Mark* marks, int mark_count){
    if(!path || !path[0]) return false;
    const bool ok = Rewrite(path, [&](FsFile& dst, int& books) -> bool {
        char st[48];
        char clean[128];
        char line[kLineMax];
        FormatState(at, st, sizeof(st));
        Sanitize(title ? title : "", clean, 97);
        int n = snprintf(line, sizeof(line), "B\t%s\t%s\t%lu\t%s\n", path, st, (unsigned long)size, clean);
        if(n <= 0 || n >= (int)sizeof(line)) return false;
        if(!WriteLine(dst, line)) return false;
        books++;
        for(int i = 0; i < mark_count && i < kMaxMarks; i++){
            FormatState(marks[i].at, st, sizeof(st));
            Sanitize(marks[i].label.c_str(), clean, sizeof(clean));
            n = snprintf(line, sizeof(line), "M\t%s\t%s\t%s\n", path, st, clean);
            if(n <= 0 || n >= (int)sizeof(line)) continue;
            if(!WriteLine(dst, line)) return false;
        }
        return true;
    });
    if(!ok) LOG_APP_WARN("読書: %s を書けませんでした", PICO_Path::FILE::SYS_READER_BOOKS);
    return ok;
}

int ListBooks(BookRecord* out, int max){
    if(!out || max <= 0 || !OSData::SD_usable) return 0;
    FsFile f = OSData::SD.open(PICO_Path::FILE::SYS_READER_BOOKS, O_RDONLY);
    if(!f) return 0;
    int count = 0;
    char line[kLineMax];
    while(count < max){
        const int n = f.fgets(line, sizeof(line));
        if(n <= 0) break;
        char* fld[5];
        const int c = SplitTabs(line, fld, 5);
        if(c < 4 || strcmp(fld[0], "B") != 0) continue;
        BookRecord& r = out[count];
        if(!ParseState(fld[2], r.at)) continue;
        r.path.assign(fld[1]);
        r.size = (uint32_t)strtoul(fld[3], nullptr, 10);
        r.title.assign(c >= 5 ? fld[4] : "");
        count++;
    }
    f.close();
    return count;
}

bool ForgetBook(const char* path){
    if(!path) return false;
    return Rewrite(path, [](FsFile&, int&) -> bool { return true; });
}

Settings LoadSettings(){
    Settings s;
    if(!OSData::SD_usable || !OSData::SD.exists(PICO_Path::FILE::CFG::SYS_READER_CFG)) return s;
    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_READER_CFG, [&s](const char* key, const char* value){
        if(strcmp(key, "font") == 0) s.large_font = (strcmp(value, "large") == 0);
        else if(strcmp(key, "ruby") == 0) s.ruby = (strcmp(value, "off") != 0);
    });
    return s;
}

void SaveSettings(const Settings& s){
    if(!OSData::SD_usable) return;
    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_READER_CFG, "font", s.large_font ? "large" : "small");
    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_READER_CFG, "ruby", s.ruby ? "on" : "off");
}

void MakeLabel(const char* text, FixedString<64>& out){
    out.clear();
    if(!text) return;
    const char* p = text;
    // 頭の空白(全角・半角)
    for(;;){
        if(*p == ' ') p++;
        else if(memcmp(p, "\xE3\x80\x80", 3) == 0) p += 3;
        else break;
    }
    bool in_ruby = false;
    char buf[64];
    size_t n = 0;
    while(*p){
        const char c = *p;
        if(c == ReaderLayout::kRubyOn){ in_ruby = true; p++; continue; }
        if(c == ReaderLayout::kRubyOff){ in_ruby = false; p++; continue; }
        int len = 1;
        const uint8_t b = (uint8_t)c;
        if(b >= 0xF0) len = 4;
        else if(b >= 0xE0) len = 3;
        else if(b >= 0xC0) len = 2;
        if(!in_ruby){
            if(n + len >= sizeof(buf) - 3) break;
            memcpy(buf + n, p, len);
            n += len;
        }
        for(int k = 0; k < len && *p; k++) p++;
    }
    buf[n] = '\0';
    (void)out.assign(buf);
}

}
