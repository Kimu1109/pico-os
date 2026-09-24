#include "chat/Chat_Proto.hpp"

#include <cstring>

namespace ChatProto {

bool ParseUint(const char* s, uint32_t& out){
    if(!s || !*s) return false;
    uint64_t v = 0;
    for(const char* p = s; *p; p++){
        if(*p < '0' || *p > '9') return false;
        v = v * 10 + (uint64_t)(*p - '0');
        if(v > 0xFFFFFFFFull) return false;
    }
    out = (uint32_t)v;
    return true;
}

bool Unescape(const char* src, Text& out){
    out.clear();
    if(!src) return true;

    //1文字ずつappendすると毎回長さを見るので、エスケープの無い区間はまとめて足す
    const char* run = src;
    for(const char* p = src; *p; p++){
        if(*p != '\\') continue;
        if(p > run && !out.append(run, (size_t)(p - run))) return false;
        const char next = p[1];
        if(next == '\0'){
            //末尾の \ はそのまま(壊れた行でも本文は見せる)
            run = p;
            break;
        }
        const char c = (next == 'n') ? '\n' : (next == 't') ? '\t' : next;
        if(!out.append(c)) return false;
        p++;
        run = p + 1;
    }
    if(*run && !out.append(run)) return false;
    return true;
}

namespace {
    // line を タブで最大 max 個へ切る。最後の欄は残り全部(本文にタブは来ないが念のため)
    int SplitTabs(char* line, char** fields, int max){
        int n = 0;
        char* p = line;
        while(n < max){
            fields[n++] = p;
            if(n == max) break;
            char* tab = strchr(p, '\t');
            if(!tab) break;
            *tab = '\0';
            p = tab + 1;
        }
        return n;
    }
}

bool ParseRoom(char* line, Room& out){
    char* f[5];
    //5列目以降(将来の拡張)は f[4] へまとめて入る。読まない
    const int n = SplitTabs(line, f, 5);
    if(n < 4) return false;

    Room r;
    if(!ParseUint(f[0], r.id) || r.id == 0) return false;
    if(!ParseUint(f[2], r.last_id)) return false;
    if(!ParseUint(f[3], r.unread)) return false;
    r.name.assign(f[1]); //表示にしか使わないので切り詰まってよい
    out = r;
    return true;
}

bool ParseMessage(char* line, Message& out){
    char* f[4];
    if(SplitTabs(line, f, 4) < 4) return false;

    uint32_t id = 0, epoch = 0;
    if(!ParseUint(f[0], id) || id == 0) return false;
    if(!ParseUint(f[1], epoch)) return false;

    out.id = id;
    out.epoch = epoch;
    out.name.assign(f[2]);
    //本文が入りきらなければ入ったところまでで見せる(サーバが上限を守っていれば起きない)
    Unescape(f[3], out.text);
    return true;
}

// ---------------------------------------------------------------------------
// LineSink
// ---------------------------------------------------------------------------

void LineSink::resetLines(){
    len_ = 0;
    overflow_ = false;
    dropped_ = 0;
}

bool LineSink::write(const void* data, size_t len){
    const char* p = (const char*)data;
    for(size_t i = 0; i < len; i++){
        const char c = p[i];
        if(c == '\n'){
            if(overflow_){
                dropped_++;
            }else{
                if(len_ > 0 && buf_[len_ - 1] == '\r') len_--;
                buf_[len_] = '\0';
                if(len_ > 0) onLine(buf_, len_);
            }
            len_ = 0;
            overflow_ = false;
            continue;
        }
        if(overflow_) continue;
        if(len_ >= kMaxLineBytes){
            overflow_ = true;
            continue;
        }
        buf_[len_++] = c;
    }
    return true;
}

void LineSink::flush(){
    if(overflow_){
        dropped_++;
    }else if(len_ > 0){
        if(buf_[len_ - 1] == '\r') len_--;
        buf_[len_] = '\0';
        if(len_ > 0) onLine(buf_, len_);
    }
    len_ = 0;
    overflow_ = false;
}

}
