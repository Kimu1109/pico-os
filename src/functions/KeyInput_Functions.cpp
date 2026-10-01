#include "functions/KeyInput_Functions.hpp"

#include <cstring>

// 列と行の読み取りだけ(何にも依存しない)。配り先は KeyInput_Dispatch.cpp

namespace {
    KeyInputFunctions::Event queue[KeyInputFunctions::kQueueSize];
    size_t q_head = 0;      // 次に取り出す位置
    size_t q_count = 0;
    uint32_t dropped = 0;
    bool dispatched_this_frame = false;

    struct NamedKey { const char* name; KeyInputFunctions::Key key; };
    const NamedKey kNamedKeys[] = {
        { "enter",     KeyInputFunctions::Key::Enter },
        { "backspace", KeyInputFunctions::Key::Backspace },
        { "tab",       KeyInputFunctions::Key::Tab },
        { "esc",       KeyInputFunctions::Key::Escape },
        { "delete",    KeyInputFunctions::Key::Delete },
        { "left",      KeyInputFunctions::Key::Left },
        { "right",     KeyInputFunctions::Key::Right },
        { "up",        KeyInputFunctions::Key::Up },
        { "down",      KeyInputFunctions::Key::Down },
        { "home",      KeyInputFunctions::Key::Home },
        { "end",       KeyInputFunctions::Key::End },
        { "pageup",    KeyInputFunctions::Key::PageUp },
        { "pagedown",  KeyInputFunctions::Key::PageDown },
    };

    int HexDigit(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
}

void KeyInputFunctions::Setup(){
    q_head = q_count = 0;
    dropped = 0;
    dispatched_this_frame = false;
}

bool KeyInputFunctions::ParseLine(const char* s, Event& out){
    if(!s || strncmp(s, "key ", 4) != 0) return false;
    s += 4;
    while(*s == ' ') s++;

    //修飾キー(16進数1桁)
    const int m = HexDigit(*s);
    if(m < 0 || m > 7) return false;
    s++;
    if(*s != ' ') return false;
    while(*s == ' ') s++;

    //CODE(空白まで)
    const char* code = s;
    while(*s && *s != ' ') s++;
    const size_t code_len = (size_t)(s - code);
    while(*s == ' ') s++;
    if(*s != '\0' || code_len == 0) return false;

    Event ev;
    ev.mods = (uint8_t)m;

    if(code_len >= 3 && (code[0] == 'u' || code[0] == 'U') && code[1] == '+'){
        if(code_len - 2 > 6) return false;
        uint32_t cp = 0;
        for(size_t i = 2; i < code_len; i++){
            const int d = HexDigit(code[i]);
            if(d < 0) return false;
            cp = (cp << 4) | (uint32_t)d;
        }
        if(cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        ev.key = Key::Char;
        ev.cp = cp;
        out = ev;
        return true;
    }

    for(const NamedKey& k : kNamedKeys){
        if(strlen(k.name) == code_len && strncmp(k.name, code, code_len) == 0){
            ev.key = k.key;
            out = ev;
            return true;
        }
    }
    return false;
}

bool KeyInputFunctions::FeedLine(const char* line){
    if(!line || strncmp(line, "key ", 4) != 0) return false;
    Event ev;
    if(ParseLine(line, ev)) Push(ev);
    return true;
}

bool KeyInputFunctions::Push(const Event& ev){
    if(q_count >= kQueueSize){
        dropped++;
        return false;
    }
    queue[(q_head + q_count) % kQueueSize] = ev;
    q_count++;
    return true;
}

size_t KeyInputFunctions::Pending(){ return q_count; }

bool KeyInputFunctions::Pop(Event& out){
    if(q_count == 0) return false;
    out = queue[q_head];
    q_head = (q_head + 1) % kQueueSize;
    q_count--;
    return true;
}

void KeyInputFunctions::DiscardPending(){
    q_head = q_count = 0;
}

uint32_t KeyInputFunctions::DroppedCount(){ return dropped; }

bool KeyInputFunctions::HadInputThisFrame(){ return dispatched_this_frame; }

void KeyInputFunctions::detail::SetDispatchedThisFrame(bool any){ dispatched_this_frame = any; }

int KeyInputFunctions::EncodeUtf8(uint32_t cp, char* out){
    int n;
    if(cp < 0x80){
        out[0] = (char)cp;
        n = 1;
    }else if(cp < 0x800){
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    }else if(cp < 0x10000){
        if(cp >= 0xD800 && cp <= 0xDFFF){ out[0] = '\0'; return 0; }
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    }else if(cp <= 0x10FFFF){
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }else{
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return n;
}
