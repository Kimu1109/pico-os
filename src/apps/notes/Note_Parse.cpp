#include "apps/notes/Note_Parse.hpp"

#include <string.h>

namespace {
    inline bool IsAsciiSpace(char c){ return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

    inline bool IsTagAscii(char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '/';
    }

    // タグの終わりにする全角の記号(UTF-8で3バイト)。空白・句読点・括弧・カンマ/ピリオド
    bool IsWideStop(const uint8_t* s){
        if(s[0] == 0xE3 && s[1] == 0x80){
            // U+3000 全角空白 / 、 / 。 / 〈〉《》「」『』【】〔〕
            const uint8_t c = s[2];
            return c == 0x80 || c == 0x81 || c == 0x82 || (c >= 0x88 && c <= 0x95);
        }
        if(s[0] == 0xEF && s[1] == 0xBC){
            // ！（）＊，．：；？ (U+FF01 U+FF08 U+FF09 U+FF0A U+FF0C U+FF0E U+FF1A U+FF1B U+FF1F)
            const uint8_t c = s[2];
            return c == 0x81 || c == 0x88 || c == 0x89 || c == 0x8A || c == 0x8C || c == 0x8E ||
                   c == 0x9A || c == 0x9B || c == 0x9F;
        }
        return false;
    }
}

void NoteParser::begin(TagFn on_tag, void* ctx){
    this->on_tag_ = on_tag;
    this->ctx_ = ctx;
    this->title_.clear();
    this->title_state_ = TitleState::Leading;
    this->title_hashes_ = 0;
    this->in_tag_ = false;
    this->tag_overflow_ = false;
    this->tag_len_ = 0;
    this->at_boundary_ = true;
    this->prev1_ = this->prev2_ = 0;
}

bool NoteParser::TrimTag(const char* s, int& len){
    // 全角の記号が出たらそこまで
    int n = 0;
    while(n < len){
        const uint8_t b = (uint8_t)s[n];
        if(b >= 0x80 && n + 3 <= len && IsWideStop((const uint8_t*)s + n)) break;
        n++;
    }
    // 後ろの "-" "/" は落とす("#todo-" の "-" は言葉の続きではない)
    while(n > 0 && (s[n - 1] == '-' || s[n - 1] == '/')) n--;
    len = n;
    if(n == 0) return false;
    bool all_digits = true;
    for(int i = 0; i < n && all_digits; i++) all_digits = (s[i] >= '0' && s[i] <= '9');
    return !all_digits;
}

void NoteParser::endTag(){
    if(!this->in_tag_) return;
    this->in_tag_ = false;
    if(this->tag_overflow_ || this->tag_len_ == 0) return;
    int len = this->tag_len_;
    // 末尾で切れた多バイト文字は落とす
    int i = len;
    while(i > 0 && ((uint8_t)this->tag_[i - 1] & 0xC0) == 0x80) i--;
    if(i > 0 && (uint8_t)this->tag_[i - 1] >= 0xC0){
        const int need = Utf8CharBytesFromLeadByte((uint8_t)this->tag_[i - 1]);
        if(len - (i - 1) < need) len = i - 1;
    }
    if(!TrimTag(this->tag_, len)) return;
    if(len > kMaxTagBytes) return;
    this->tag_[len] = '\0';
    if(this->on_tag_) this->on_tag_(this->ctx_, this->tag_);
}

void NoteParser::feedTitle(char c){
    switch(this->title_state_){
        case TitleState::Done:
            return;
        case TitleState::Leading:
            if(IsAsciiSpace(c)) return;
            if(c == '#'){
                this->title_state_ = TitleState::Hashes;
                this->title_hashes_ = 1;
                return;
            }
            this->title_state_ = TitleState::Body;
            break;
        case TitleState::Hashes:
            if(c == '#'){ this->title_hashes_++; return; }
            if(c == ' ' || c == '\t'){
                // 見出しの印だった。続きを題名の本体として読む(頭の空白は Leading で落ちる)
                this->title_state_ = TitleState::Leading;
                return;
            }
            // "#タグ" で始まる行。# も題名に含める
            this->title_state_ = TitleState::Body;
            for(int i = 0; i < this->title_hashes_; i++) (void)this->title_.append('#');
            if(c == '\n' || c == '\r'){ this->title_state_ = TitleState::Done; return; }
            break;
        case TitleState::Body:
            break;
    }
    if(c == '\n' || c == '\r'){
        this->title_state_ = TitleState::Done;
        return;
    }
    if(c == '\t') c = ' ';
    // いっぱいになったら(文字の途中で切らないよう append に任せて)終わり
    if(this->title_.length() >= (size_t)kTitleBytes){
        this->title_state_ = TitleState::Done;
        return;
    }
    (void)this->title_.append(c);
}

void NoteParser::feed(const char* p, int n){
    if(!p) return;
    for(int i = 0; i < n; i++){
        const char c = p[i];
        const uint8_t b = (uint8_t)c;
        this->feedTitle(c);

        if(this->in_tag_){
            // ASCII以外は言葉の続き。全角の記号かどうかは3バイト揃ってから確かめる(下)
            if(b < 0x80 && !IsTagAscii(c)){
                this->endTag();
            }else{
                if(this->tag_len_ < (int)sizeof(this->tag_) - 1) this->tag_[this->tag_len_++] = c;
                else this->tag_overflow_ = true;
                // 全角の記号が揃ったら、それより前で締める
                if(this->tag_len_ >= 3 && (b & 0xC0) == 0x80 &&
                   ((uint8_t)this->tag_[this->tag_len_ - 3] & 0xF0) == 0xE0 &&
                   IsWideStop((const uint8_t*)this->tag_ + this->tag_len_ - 3)){
                    this->tag_len_ -= 3;
                    this->endTag();
                }
            }
        }else if(c == '#' && this->at_boundary_){
            this->in_tag_ = true;
            this->tag_overflow_ = false;
            this->tag_len_ = 0;
        }

        // 区切りかどうか(次の文字のため)
        if(b < 0x80){
            this->at_boundary_ = IsAsciiSpace(c);
        }else if((b & 0xC0) == 0x80 && (this->prev2_ & 0xF0) == 0xE0){
            // 3バイトの文字の終わり。全角の空白・句読点・括弧なら区切り
            const uint8_t w[3] = { this->prev2_, this->prev1_, b };
            this->at_boundary_ = IsWideStop(w);
        }else{
            this->at_boundary_ = false;
        }
        this->prev2_ = this->prev1_;
        this->prev1_ = b;
    }
}

void NoteParser::finish(){
    this->endTag();
    // 題名が "#" だけで終わった(見出しの印だけの行)ときは空のまま
    if(this->title_state_ == TitleState::Hashes) this->title_state_ = TitleState::Done;
}
