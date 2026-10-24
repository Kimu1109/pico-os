#pragma once

#include "consts.hpp"

#include <stdint.h>
#include <string.h>

// 流れてくるバイト列(ファイルを小分けに読んだもの)の中から、検索語を探す。全文検索(FileSearchScene・メモ)で使う。
//
// - 比べ方は大文字/小文字を区別しない(ASCIIだけ)部分一致。日本語はバイト列のまま
//   (検索語がUTF-8として正しければ、文字の途中から一致することはない)
// - KMP法なので、チャンクの境目をまたぐ一致も、前のチャンクを取っておかずに見つかる
// - 数えるのは一致の数・最初の一致のバイト位置と行(1始まり)
// 確保はしない(検索語と表はメンバの固定長)。
class TextMatcher {
    public:
        static constexpr int kMaxNeedle = PICO_STR_M - 1;

        // 検索語を決めて最初から。空・長すぎるなら false(何にも一致しない)
        bool setNeedle(const char* s){
            this->n_ = 0;
            this->reset();
            if(!s) return false;
            const size_t len = strlen(s);
            if(len == 0 || len > (size_t)kMaxNeedle) return false;
            this->n_ = (int)len;
            this->needle_nl_ = 0;
            for(int i = 0; i < this->n_; i++){
                this->needle_[i] = Lower(s[i]);
                if(s[i] == '\n') this->needle_nl_++;
            }
            // 失敗関数(needle_[0..i] の、真の接頭辞かつ接尾辞の長さ)
            this->fail_[0] = 0;
            int k = 0;
            for(int i = 1; i < this->n_; i++){
                while(k > 0 && this->needle_[i] != this->needle_[k]) k = this->fail_[k - 1];
                if(this->needle_[i] == this->needle_[k]) k++;
                this->fail_[i] = (uint8_t)k;
            }
            return true;
        }

        // 同じ検索語で、別のファイル(流れ)を最初から
        void reset(){
            this->k_ = 0;
            this->pos_ = 0;
            this->line_ = 1;
            this->count_ = 0;
            this->first_pos_ = 0;
            this->first_line_ = 0;
        }

        bool hasNeedle() const { return this->n_ > 0; }

        // 一致した所の前後(buf[0..n) の中の at バイト目を含む1行)を、一覧に出す1行の抜粋にして out へ。
        // 行の頭がbufより前にあれば、文字の途中から始まらないよう継続バイトを飛ばす。
        // タブ・制御文字は空白へ、頭の空白は落とす。out は cap バイト(終端込み)まで、文字の途中では切らない
        static void MakeSnippet(const char* buf, int n, int at, char* out, size_t cap){
            if(!out || cap == 0) return;
            out[0] = '\0';
            if(!buf || n <= 0) return;
            if(at < 0) at = 0;
            if(at > n) at = n;
            int a = at;
            while(a > 0 && buf[a - 1] != '\n') a--;
            int b = at;
            while(b < n && buf[b] != '\n') b++;
            while(a < b && ((uint8_t)buf[a] & 0xC0) == 0x80) a++;
            while(a < b && (buf[a] == ' ' || buf[a] == '\t' || buf[a] == '\r')) a++;
            size_t w = 0;
            for(int i = a; i < b; i++){
                const uint8_t c = (uint8_t)buf[i];
                if(c >= 0x80 && (c & 0xC0) != 0x80){
                    // 文字の頭。まるごと入らなければ終わり(bufの終わりで欠けた文字も入れない)
                    const int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
                    if(w + (size_t)len + 1 > cap || i + len > b) break;
                    memcpy(out + w, buf + i, (size_t)len);
                    w += (size_t)len;
                    i += len - 1;
                    continue;
                }
                if(w + 2 > cap) break;
                if((c & 0xC0) == 0x80) continue;   // はぐれた継続バイト
                out[w++] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
            }
            while(w > 0 && out[w - 1] == ' ') w--;
            out[w] = '\0';
        }

        // n バイトを流す。この中で1つでも一致したら true
        bool feed(const char* p, int n){
            if(this->n_ == 0 || !p) return false;
            bool hit = false;
            for(int i = 0; i < n; i++){
                const char c = p[i];
                const char lc = Lower(c);
                while(this->k_ > 0 && lc != this->needle_[this->k_]) this->k_ = this->fail_[this->k_ - 1];
                if(lc == this->needle_[this->k_]) this->k_++;
                if(c == '\n') this->line_++;
                this->pos_++;
                if(this->k_ == this->n_){
                    if(this->count_ == 0){
                        this->first_pos_ = this->pos_ - (uint32_t)this->n_;
                        this->first_line_ = this->line_ - (uint32_t)this->needle_nl_;
                    }
                    if(this->count_ < 0xFFFF) this->count_++;
                    hit = true;
                    this->k_ = this->fail_[this->k_ - 1];
                }
            }
            return hit;
        }

        int count() const { return this->count_; }
        bool matched() const { return this->count_ > 0; }
        // 最初の一致の頭のバイト位置(流れの頭から)と行(1始まり)。一致していなければ0
        uint32_t firstPos() const { return this->first_pos_; }
        uint32_t firstLine() const { return this->first_line_; }

    private:
        static char Lower(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

        char needle_[kMaxNeedle] = {};
        uint8_t fail_[kMaxNeedle] = {};
        int n_ = 0;
        int needle_nl_ = 0;
        int k_ = 0;
        uint32_t pos_ = 0;
        uint32_t line_ = 1;
        int count_ = 0;
        uint32_t first_pos_ = 0;
        uint32_t first_line_ = 0;
};
