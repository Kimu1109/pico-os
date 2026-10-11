#include "functions/Clipboard_Functions.hpp"

#include <cstdlib>
#include <cstring>

namespace {
    char* data = nullptr;
    size_t data_len = 0;
    uint32_t revision = 0;
}

bool ClipboardFunctions::Set(const char* text, size_t n){
    if(!text || n == 0){
        Clear();
        return true;
    }

    //捨てる文字を除いた長さを数え、上限で切る(UTF-8の文字の途中では切らない)
    bool whole = true;
    size_t kept = 0;
    size_t src_end = n;
    for(size_t i = 0; i < n; i++){
        const char c = text[i];
        if(c == '\r' || c == '\0') continue;
        if(kept + 1 > kMaxBytes){
            src_end = i;
            whole = false;
            break;
        }
        kept++;
    }
    if(!whole){
        //src_end は切った位置。継続バイトの途中なら文字の頭まで戻す
        while(src_end > 0 && ((unsigned char)text[src_end] & 0xC0) == 0x80){
            src_end--;
            if(text[src_end] != '\r' && text[src_end] != '\0') kept--;
        }
    }

    char* p = static_cast<char*>(malloc(kept + 1));
    if(!p) return false;
    size_t w = 0;
    for(size_t i = 0; i < src_end && w < kept; i++){
        const char c = text[i];
        if(c == '\r' || c == '\0') continue;
        p[w++] = c;
    }
    p[w] = '\0';

    free(data);
    data = p;
    data_len = w;
    revision++;
    return whole;
}

bool ClipboardFunctions::Set(const char* text){
    return Set(text, text ? strlen(text) : 0);
}

const char* ClipboardFunctions::Get(){ return data ? data : ""; }
size_t ClipboardFunctions::Length(){ return data_len; }
bool ClipboardFunctions::Empty(){ return data_len == 0; }

void ClipboardFunctions::Clear(){
    if(!data) return;
    free(data);
    data = nullptr;
    data_len = 0;
    revision++;
}

uint32_t ClipboardFunctions::Revision(){ return revision; }
