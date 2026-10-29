#include "apps/firmware/Fw_Index.hpp"

#include <cstdlib>
#include <cstring>

namespace {
    constexpr int kColumns = 9;

    int HexValue(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool ParseSha(const char* s, uint8_t out[32]){
        if(strlen(s) != 64) return false;
        for(int i = 0; i < 32; i++){
            const int hi = HexValue(s[i * 2]);
            const int lo = HexValue(s[i * 2 + 1]);
            if(hi < 0 || lo < 0) return false;
            out[i] = (uint8_t)((hi << 4) | lo);
        }
        return true;
    }

    // 10進の数(先頭の0や符号・空白は受け付けない。0だけは可)
    bool ParseU32(const char* s, uint32_t& out){
        if(!s[0] || strlen(s) > 10) return false;
        if(s[0] == '0' && s[1]) return false;
        for(const char* p = s; *p; p++) if(*p < '0' || *p > '9') return false;
        const unsigned long long v = strtoull(s, nullptr, 10);
        if(v > 0xFFFFFFFFull) return false;
        out = (uint32_t)v;
        return true;
    }

    // 表示に出すだけの値(git・日時・構成)。制御文字は受け付けない
    bool PlainText(const char* s){
        for(const char* p = s; *p; p++) if((unsigned char)*p < 0x20 || *p == 0x7F) return false;
        return true;
    }

    // 1行を読む。読めなければfalse(fieldsは行のコピーの中を指す)
    bool ParseLine(char* line, FwIndex::Entry& e){
        char* fields[kColumns] = {};
        int n = 0;
        char* p = line;
        while(n < kColumns){
            fields[n++] = p;
            char* tab = strchr(p, '\t');
            if(!tab) break;
            *tab = '\0';
            p = tab + 1;
        }
        if(n < kColumns) return false;
        // 10列目より後ろ(将来の列)は捨てる
        if(char* tab = strchr(fields[kColumns - 1], '\t')) *tab = '\0';

        if(!ParseU32(fields[0], e.serial)) return false;
        if(!fields[1][0] || strlen(fields[1]) > 16 || !PlainText(fields[1]) || !e.git.assign(fields[1])) return false;
        if(!PlainText(fields[2]) || !e.date.assign(fields[2])) return false;
        if(strcmp(fields[3], "stable") == 0) e.channel = FwIndex::Channel::Stable;
        else if(strcmp(fields[3], "nightly") == 0) e.channel = FwIndex::Channel::Nightly;
        else return false;
        if(!fields[4][0] || !PlainText(fields[4]) || !e.env.assign(fields[4])) return false;
        if(strcmp(fields[5], "1") == 0) e.signed_flag = true;
        else if(strcmp(fields[5], "0") == 0) e.signed_flag = false;
        else return false;
        if(!ParseU32(fields[6], e.bytes) || e.bytes == 0) return false;
        if(!ParseSha(fields[7], e.sha256)) return false;
        if(!FwIndex::SafeFileName(fields[8]) || !e.file.assign(fields[8])) return false;
        return true;
    }
}

const char* FwIndex::ChannelText(Channel c){
    return c == Channel::Stable ? "安定" : "夜間";
}

bool FwIndex::SafeFileName(const char* name){
    if(!name) return false;
    const size_t len = strlen(name);
    if(len < 5 || len >= PICO_STR_L || name[0] == '.') return false;
    for(size_t i = 0; i < len; i++){
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
        if(!ok) return false;
    }
    return strcmp(name + len - 4, ".pfw") == 0;
}

bool FwIndex::Parse(const char* text, size_t len, const Filter& filter, Entry* out, int cap, Result& result){
    result = Result{};
    const char* env = filter.env ? filter.env : "";
    char line[kMaxLineBytes];
    size_t pos = 0;
    bool first = true;
    while(pos < len){
        const char* start = text + pos;
        const void* nl = memchr(start, '\n', len - pos);
        const size_t raw = nl ? (size_t)((const char*)nl - start) : len - pos;
        pos += raw + (nl ? 1 : 0);
        size_t n = raw;
        if(n > 0 && start[n - 1] == '\r') n--;
        const bool too_long = n >= sizeof(line);
        if(!too_long){
            memcpy(line, start, n);
            line[n] = '\0';
        }

        if(first){
            first = false;
            if(too_long || strcmp(line, kMagic) != 0) return false;
            continue;
        }
        if(too_long){ result.bad_lines++; continue; }
        if(n == 0 || line[0] == '#') continue;

        Entry e;
        if(!ParseLine(line, e)){ result.bad_lines++; continue; }
        if(strcmp(e.env.c_str(), env) != 0){ result.hidden_env++; continue; }
        if(!e.signed_flag && !filter.allow_unsigned){ result.hidden_unsigned++; continue; }
        if(result.count >= cap){ result.overflow++; continue; }
        out[result.count++] = e;
    }
    return !first;
}
