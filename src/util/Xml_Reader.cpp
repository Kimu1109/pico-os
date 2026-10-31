#include "util/Xml_Reader.hpp"

#include <cstring>
#include "system/I18n_Functions.hpp"

namespace {
    bool IsSpace(char c){ return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

    bool IsNameStart(char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' || (uint8_t)c >= 0x80;
    }

    // 末尾が UTF-8 の文字の途中で切れていたら、その文字ごと落とした長さを返す
    size_t TrimPartialUtf8(const char* s, size_t len){
        size_t i = len;
        int back = 0;
        while(i > 0 && back < 4){
            const uint8_t b = (uint8_t)s[i - 1];
            if((b & 0xC0) != 0x80){
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

    int HexValue(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    void AddName(char* buf, size_t& len, char c){
        if(len < XmlReader::kNameBytes - 1){
            buf[len++] = c;
            buf[len] = '\0';
        }
    }
}

void XmlReader::reset(Handler* handler){
    handler_ = handler;
    state_ = State::Text;
    entity_return_ = State::Text;
    error_ = "";
    root_done_ = false;
    depth_ = 0;
    name_[0] = '\0';
    name_len_ = 0;
    attr_name_[0] = '\0';
    attr_name_len_ = 0;
    value_len_ = 0;
    truncated_ = false;
    bang_len_ = 0;
    dashes_ = 0;
    doctype_depth_ = 0;
    doctype_quote_ = 0;
    entity_len_ = 0;
}

bool XmlReader::fail(const char* why){
    state_ = State::Error;
    error_ = why;
    return false;
}

bool XmlReader::feed(const void* data, size_t len){
    if(state_ == State::Error) return false;
    const char* p = (const char*)data;
    for(size_t i = 0; i < len; i++){
        if(!step(p[i])) return false;
    }
    return true;
}

void XmlReader::appendRaw(char c){
    if(value_len_ < kValueBytes - 1) value_[value_len_++] = c;
    else truncated_ = true;
}

// 文字の先頭の空白は捨てる(末尾の空白は flushText が落とす)
void XmlReader::appendText(char c){
    if(value_len_ == 0 && IsSpace(c)) return;
    appendRaw(c);
}

void XmlReader::appendCodepoint(uint32_t cp){
    if(cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if(cp < 0x80){
        appendRaw((char)cp);
    }else if(cp < 0x800){
        appendRaw((char)(0xC0 | (cp >> 6)));
        appendRaw((char)(0x80 | (cp & 0x3F)));
    }else if(cp < 0x10000){
        appendRaw((char)(0xE0 | (cp >> 12)));
        appendRaw((char)(0x80 | ((cp >> 6) & 0x3F)));
        appendRaw((char)(0x80 | (cp & 0x3F)));
    }else{
        appendRaw((char)(0xF0 | (cp >> 18)));
        appendRaw((char)(0x80 | ((cp >> 12) & 0x3F)));
        appendRaw((char)(0x80 | ((cp >> 6) & 0x3F)));
        appendRaw((char)(0x80 | (cp & 0x3F)));
    }
}

void XmlReader::flushText(){
    if(depth_ > 0 && value_len_ > 0){
        size_t n = value_len_;
        while(n > 0 && IsSpace(value_[n - 1])) n--;
        n = TrimPartialUtf8(value_, n);
        if(n > 0){
            value_[n] = '\0';
            if(handler_) handler_->onText(names_[depth_ - 1], value_, n, truncated_, depth_);
        }
    }
    value_len_ = 0;
    truncated_ = false;
}

bool XmlReader::openTag(){
    if(depth_ >= kMaxDepth) return fail(TR("入れ子が深すぎます"));
    memcpy(names_[depth_], name_, name_len_ + 1);
    depth_++;
    if(handler_) handler_->onOpen(names_[depth_ - 1], depth_);
    return true;
}

void XmlReader::closeTop(){
    if(handler_) handler_->onClose(names_[depth_ - 1], depth_);
    depth_--;
    if(depth_ == 0) root_done_ = true;
}

bool XmlReader::closeTag(){
    if(depth_ == 0) return fail(TR("開いていない要素を閉じています"));
    if(strcmp(name_, names_[depth_ - 1]) != 0) return fail(TR("閉じタグが開きタグと合いません"));
    closeTop();
    return true;
}

void XmlReader::emitAttribute(){
    const size_t n = TrimPartialUtf8(value_, value_len_);
    value_[n] = '\0';
    if(handler_) handler_->onAttribute(attr_name_, value_, n, truncated_, depth_);
    value_len_ = 0;
    truncated_ = false;
}

// &...; の続きを読む。実体として解けたら文字を足し、解けなければ書かれたまま足す。
// ; より前に名前に使えない文字が来たら、&以降を書かれたまま足して、その文字を元の状態で読み直す
bool XmlReader::stepEntity(char c){
    if(c == ';'){
        entity_[entity_len_] = '\0';
        uint32_t cp = 0;
        bool ok = false;
        if(strcmp(entity_, "lt") == 0){ cp = '<'; ok = true; }
        else if(strcmp(entity_, "gt") == 0){ cp = '>'; ok = true; }
        else if(strcmp(entity_, "amp") == 0){ cp = '&'; ok = true; }
        else if(strcmp(entity_, "quot") == 0){ cp = '"'; ok = true; }
        else if(strcmp(entity_, "apos") == 0){ cp = '\''; ok = true; }
        else if(entity_[0] == '#' && entity_len_ >= 2){
            const bool hex = (entity_[1] == 'x' || entity_[1] == 'X');
            size_t i = hex ? 2 : 1;
            ok = (i < entity_len_);
            for(; ok && i < entity_len_; i++){
                const int v = hex ? HexValue(entity_[i]) : ((entity_[i] >= '0' && entity_[i] <= '9') ? entity_[i] - '0' : -1);
                if(v < 0){ ok = false; break; }
                cp = cp * (hex ? 16 : 10) + (uint32_t)v;
                if(cp > 0x10FFFF) cp = 0x110000;   // 桁あふれを防ぐ(範囲外は U+FFFD になる)
            }
        }
        if(ok){
            appendCodepoint(cp);
        }else{
            appendRaw('&');
            for(uint8_t i = 0; i < entity_len_; i++) appendRaw(entity_[i]);
            appendRaw(';');
        }
        state_ = entity_return_;
        return true;
    }
    const bool name_char = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '#';
    if(name_char && entity_len_ < sizeof(entity_) - 1){
        entity_[entity_len_++] = c;
        return true;
    }
    // 実体ではなかった
    appendRaw('&');
    for(uint8_t i = 0; i < entity_len_; i++) appendRaw(entity_[i]);
    state_ = entity_return_;
    return step(c);
}

bool XmlReader::step(char c){
    switch(state_){
        case State::Text:
            if(c == '<'){ state_ = State::TagOpen; return true; }
            if(depth_ == 0){
                // 要素の外は空白(とBOM)だけ許す
                if(IsSpace(c) || (uint8_t)c == 0xEF || (uint8_t)c == 0xBB || (uint8_t)c == 0xBF) return true;
                return fail(TR("ルート要素の外に文字があります"));
            }
            if(c == '&'){
                entity_len_ = 0;
                entity_return_ = State::Text;
                state_ = State::Entity;
                return true;
            }
            appendText(c);
            return true;

        case State::TagOpen:
            if(c == '/'){
                flushText();
                name_len_ = 0;
                name_[0] = '\0';
                state_ = State::CloseName;
                return true;
            }
            if(c == '!'){ bang_len_ = 0; state_ = State::Bang; return true; }
            if(c == '?'){ dashes_ = 0; state_ = State::Pi; return true; }
            if(IsNameStart(c)){
                if(root_done_) return fail(TR("ルート要素が2つあります"));
                flushText();
                name_len_ = 0;
                name_[0] = '\0';
                AddName(name_, name_len_, c);
                state_ = State::TagName;
                return true;
            }
            return fail(TR("'<' の後ろが読めません"));

        case State::TagName:
            if(IsSpace(c)){ if(!openTag()) return false; state_ = State::InTag; return true; }
            if(c == '>'){ if(!openTag()) return false; state_ = State::Text; return true; }
            if(c == '/'){ if(!openTag()) return false; state_ = State::SelfClose; return true; }
            AddName(name_, name_len_, c);
            return true;

        case State::InTag:
            if(IsSpace(c)) return true;
            if(c == '>'){ state_ = State::Text; return true; }
            if(c == '/'){ state_ = State::SelfClose; return true; }
            if(IsNameStart(c)){
                attr_name_len_ = 0;
                attr_name_[0] = '\0';
                AddName(attr_name_, attr_name_len_, c);
                state_ = State::AttrName;
                return true;
            }
            return fail(TR("属性名が読めません"));

        case State::AttrName:
            if(c == '='){ state_ = State::BeforeValue; return true; }
            if(IsSpace(c)){ state_ = State::AfterAttrName; return true; }
            if(c == '>' || c == '/') return fail(TR("値のない属性です"));
            AddName(attr_name_, attr_name_len_, c);
            return true;

        case State::AfterAttrName:
            if(IsSpace(c)) return true;
            if(c == '='){ state_ = State::BeforeValue; return true; }
            return fail(TR("属性に = がありません"));

        case State::BeforeValue:
            if(IsSpace(c)) return true;
            if(c == '"' || c == '\''){
                quote_ = c;
                value_len_ = 0;
                truncated_ = false;
                state_ = State::AttrValue;
                return true;
            }
            return fail(TR("属性値が引用符で囲まれていません"));

        case State::AttrValue:
            if(c == quote_){ emitAttribute(); state_ = State::InTag; return true; }
            if(c == '<') return fail(TR("属性値に '<' があります"));
            if(c == '&'){
                entity_len_ = 0;
                entity_return_ = State::AttrValue;
                state_ = State::Entity;
                return true;
            }
            // 属性値の中の改行・タブは空白1つとして扱う(XMLの規則)
            appendRaw((c == '\n' || c == '\t' || c == '\r') ? ' ' : c);
            return true;

        case State::SelfClose:
            if(c != '>') return fail(TR("'/' の後ろに '>' がありません"));
            closeTop();
            state_ = State::Text;
            return true;

        case State::CloseName:
            if(c == '>'){ if(!closeTag()) return false; state_ = State::Text; return true; }
            if(IsSpace(c)){ if(!closeTag()) return false; state_ = State::CloseAfter; return true; }
            AddName(name_, name_len_, c);
            return true;

        case State::CloseAfter:
            if(IsSpace(c)) return true;
            if(c == '>'){ state_ = State::Text; return true; }
            return fail(TR("閉じタグの中に余分な文字があります"));

        case State::Bang:
            if(bang_len_ < sizeof(bang_)) bang_[bang_len_++] = c;
            if(bang_len_ == 1){
                if(c != '-' && c != '[' && c != 'D') return fail(TR("'<!' の後ろが読めません"));
            }else if(bang_len_ == 2 && bang_[0] == '-'){
                if(bang_[1] != '-') return fail(TR("コメントの始まりが読めません"));
                dashes_ = 0;
                state_ = State::Comment;
            }else if(bang_len_ == 7){
                if(memcmp(bang_, "[CDATA[", 7) == 0){
                    if(depth_ == 0) return fail(TR("ルート要素の外にCDATAがあります"));
                    dashes_ = 0;
                    state_ = State::CData;
                }else if(memcmp(bang_, "DOCTYPE", 7) == 0){
                    doctype_depth_ = 0;
                    doctype_quote_ = 0;
                    state_ = State::Doctype;
                }else{
                    return fail(TR("'<!' の後ろが読めません"));
                }
            }
            return true;

        case State::Comment:
            if(c == '-'){ if(dashes_ < 2) dashes_++; return true; }
            if(c == '>' && dashes_ >= 2){ state_ = State::Text; return true; }
            dashes_ = 0;
            return true;

        case State::CData:
            // ']' は ']]>' の一部かもしれないので、次の文字が分かるまで数えるだけにする
            if(c == ']'){
                if(dashes_ < 255) dashes_++;
                return true;
            }
            if(c == '>' && dashes_ >= 2){
                for(uint8_t i = 2; i < dashes_; i++) appendText(']');
                dashes_ = 0;
                state_ = State::Text;
                return true;
            }
            for(uint8_t i = 0; i < dashes_; i++) appendText(']');
            dashes_ = 0;
            appendText(c);
            return true;

        case State::Pi:
            if(c == '>' && dashes_ == 1){ state_ = State::Text; return true; }
            dashes_ = (c == '?') ? 1 : 0;
            return true;

        case State::Doctype:
            if(doctype_quote_){
                if(c == doctype_quote_) doctype_quote_ = 0;
            }else if(c == '"' || c == '\''){
                doctype_quote_ = c;
            }else if(c == '['){
                doctype_depth_++;
            }else if(c == ']'){
                if(doctype_depth_ > 0) doctype_depth_--;
            }else if(c == '>' && doctype_depth_ == 0){
                state_ = State::Text;
            }
            return true;

        case State::Entity:
            return stepEntity(c);

        case State::Error:
            return false;
    }
    return false;
}
