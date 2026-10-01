#include "util/Json_Reader.hpp"

#include <cstring>

namespace {
    bool IsSpace(char c){ return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

    int HexValue(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool IsLiteralChar(char c){
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || c == '+' || c == '-' || c == '.';
    }

    // 末尾が UTF-8 の文字の途中で切れていたら、その文字ごと落とした長さを返す
    size_t TrimPartialUtf8(const char* s, size_t len){
        size_t i = len;
        int back = 0;
        while(i > 0 && back < 4){
            const uint8_t b = (uint8_t)s[i - 1];
            if((b & 0xC0) != 0x80){
                // 先頭バイト。続きのバイト数が足りているか
                size_t need = 1;
                if((b & 0xE0) == 0xC0) need = 2;
                else if((b & 0xF0) == 0xE0) need = 3;
                else if((b & 0xF8) == 0xF0) need = 4;
                return (len - (i - 1) >= need) ? len : i - 1;
            }
            i--;
            back++;
        }
        return len;
    }
}

void JsonReader::reset(Handler* handler){
    handler_ = handler;
    state_ = State::Value;
    error_ = "";
    depth_ = 0;
    str_is_key_ = false;
    key_[0] = '\0';
    key_len_ = 0;
    has_key_ = false;
    value_len_ = 0;
    truncated_ = false;
    unicode_ = 0;
    unicode_digits_ = 0;
    high_surrogate_ = 0;
}

bool JsonReader::fail(const char* why){
    state_ = State::Error;
    error_ = why;
    return false;
}

bool JsonReader::feed(const void* data, size_t len){
    const char* p = (const char*)data;
    for(size_t i = 0; i < len; i++){
        if(state_ == State::Error) return false;
        if(!step(p[i])) return false;
    }
    return state_ != State::Error;
}

void JsonReader::appendByte(char c){
    if(truncated_) return;
    if(value_len_ + 1 >= kValueBytes){
        truncated_ = true;
        return;
    }
    value_[value_len_++] = c;
}

void JsonReader::appendCodepoint(uint32_t cp){
    if(cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
    char buf[4];
    size_t n;
    if(cp < 0x80){ buf[0] = (char)cp; n = 1; }
    else if(cp < 0x800){
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    }else if(cp < 0x10000){
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    }else{
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    //文字の途中で切らない(入りきらなければ丸ごと捨てる)
    if(truncated_ || value_len_ + n >= kValueBytes){
        truncated_ = true;
        return;
    }
    memcpy(value_ + value_len_, buf, n);
    value_len_ += n;
}

void JsonReader::flushHighSurrogate(){
    if(high_surrogate_ == 0) return;
    high_surrogate_ = 0;
    appendCodepoint(0xFFFD);
}

void JsonReader::afterValue(){
    state_ = (depth_ == 0) ? State::Done : State::AfterValue;
}

bool JsonReader::beginValue(char c){
    switch(c){
        case '{':
        case '[': {
            if(depth_ >= kMaxDepth) return fail("入れ子が深すぎます");
            const bool is_array = (c == '[');
            if(handler_) handler_->onBegin(currentKey(), is_array, depth_ + 1);
            stack_is_array_[depth_++] = is_array;
            has_key_ = false;
            state_ = is_array ? State::ValueOrEnd : State::KeyOrEnd;
            return true;
        }
        case '"':
            str_is_key_ = false;
            value_len_ = 0;
            truncated_ = false;
            high_surrogate_ = 0;
            state_ = State::String;
            return true;
        default:
            if(c == '-' || (c >= '0' && c <= '9') || c == 't' || c == 'f' || c == 'n'){
                value_len_ = 0;
                truncated_ = false;
                appendByte(c);
                state_ = State::Literal;
                return true;
            }
            return fail("値がありません");
    }
}

bool JsonReader::finishString(){
    flushHighSurrogate();
    size_t len = truncated_ ? TrimPartialUtf8(value_, value_len_) : value_len_;
    if(str_is_key_){
        if(len >= kKeyBytes) len = TrimPartialUtf8(value_, kKeyBytes - 1);
        memcpy(key_, value_, len);
        key_[len] = '\0';
        key_len_ = len;
        has_key_ = true;
        state_ = State::Colon;
        return true;
    }
    value_[len] = '\0';
    if(handler_) handler_->onValue(currentKey(), Type::String, value_, len, truncated_, depth_);
    afterValue();
    return true;
}

bool JsonReader::finishLiteral(){
    if(truncated_) return fail("数値が長すぎます");
    value_[value_len_] = '\0';
    Type t;
    if(strcmp(value_, "true") == 0) t = Type::True;
    else if(strcmp(value_, "false") == 0) t = Type::False;
    else if(strcmp(value_, "null") == 0) t = Type::Null;
    else{
        const char c0 = value_[0];
        if(!(c0 == '-' || (c0 >= '0' && c0 <= '9'))) return fail("知らない値です");
        for(size_t i = 0; i < value_len_; i++){
            const char c = value_[i];
            const bool ok = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
            if(!ok) return fail("数値として読めません");
        }
        t = Type::Number;
    }
    if(handler_) handler_->onValue(currentKey(), t, value_, value_len_, false, depth_);
    afterValue();
    return true;
}

bool JsonReader::step(char c){
    switch(state_){
        case State::Value:
        case State::ValueOrEnd:
            if(IsSpace(c)) return true;
            if(state_ == State::ValueOrEnd && c == ']') break; //空の配列は下の閉じ括弧の処理へ
            return beginValue(c);

        case State::KeyOrEnd:
        case State::Key:
            if(IsSpace(c)) return true;
            if(state_ == State::KeyOrEnd && c == '}') break;
            if(c != '"') return fail("キーがありません");
            str_is_key_ = true;
            value_len_ = 0;
            truncated_ = false;
            high_surrogate_ = 0;
            state_ = State::String;
            return true;

        case State::Colon:
            if(IsSpace(c)) return true;
            if(c != ':') return fail("':' がありません");
            state_ = State::Value;
            return true;

        case State::AfterValue:
            if(IsSpace(c)) return true;
            if(c == ','){
                if(stack_is_array_[depth_ - 1]){
                    has_key_ = false;
                    state_ = State::Value;
                }else{
                    state_ = State::Key;
                }
                return true;
            }
            if(c == '}' || c == ']') break;
            return fail("',' がありません");

        case State::String:
            if(high_surrogate_ != 0 && c != '\\') flushHighSurrogate();
            if(c == '"') return finishString();
            if(c == '\\'){ state_ = State::Escape; return true; }
            if((uint8_t)c < 0x20) return fail("文字列に制御文字があります");
            appendByte(c);
            return true;

        case State::Escape: {
            if(c != 'u') flushHighSurrogate();
            char out;
            switch(c){
                case '"':  out = '"'; break;
                case '\\': out = '\\'; break;
                case '/':  out = '/'; break;
                case 'b':  out = '\b'; break;
                case 'f':  out = '\f'; break;
                case 'n':  out = '\n'; break;
                case 'r':  out = '\r'; break;
                case 't':  out = '\t'; break;
                case 'u':
                    unicode_ = 0;
                    unicode_digits_ = 0;
                    state_ = State::Unicode;
                    return true;
                default:
                    return fail("知らないエスケープです");
            }
            appendByte(out);
            state_ = State::String;
            return true;
        }

        case State::Unicode: {
            const int v = HexValue(c);
            if(v < 0) return fail("\\u の後が16進数ではありません");
            unicode_ = (unicode_ << 4) | (uint32_t)v;
            if(++unicode_digits_ < 4) return true;
            state_ = State::String;
            const uint32_t cu = unicode_;
            if(cu >= 0xD800 && cu <= 0xDBFF){
                flushHighSurrogate();
                high_surrogate_ = cu;
            }else if(cu >= 0xDC00 && cu <= 0xDFFF){
                if(high_surrogate_ != 0){
                    const uint32_t cp = 0x10000 + ((high_surrogate_ - 0xD800) << 10) + (cu - 0xDC00);
                    high_surrogate_ = 0;
                    appendCodepoint(cp);
                }else{
                    appendCodepoint(0xFFFD);
                }
            }else{
                flushHighSurrogate();
                appendCodepoint(cu);
            }
            return true;
        }

        case State::Literal:
            if(IsLiteralChar(c)){
                appendByte(c);
                return true;
            }
            if(!finishLiteral()) return false;
            return step(c); //区切りの文字を読み直す

        case State::Done:
            if(IsSpace(c)) return true;
            return fail("値の後に余計なものがあります");

        case State::Error:
            return false;
    }

    // ---- 閉じ括弧('}' / ']') ----
    const bool is_array = (c == ']');
    if(depth_ == 0 || stack_is_array_[depth_ - 1] != is_array) return fail("括弧の対応が合いません");
    const int inner = depth_;
    depth_--;
    if(handler_) handler_->onEnd(is_array, inner);
    has_key_ = false;
    afterValue();
    return true;
}
