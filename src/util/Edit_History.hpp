#pragma once
#include <cstdint>
#include <cstring>

// 「元に戻す / やり直し」の記録(テキストエディタ用。描画にもSDにも依存しない)。
//
// 文書のスナップショットは持たず、**1回の編集 = 「posから del を ins に置き換えた」**だけを
// 固定長のバイト列(kBytes)へ順に積む。元に戻すときは ins を del へ、やり直すときは del を ins へ戻す。
//
//   1件 = [pos:4][del_n:2][ins_n:2][flags:2][delのバイト][insのバイト][1件の大きさ:2]
//
// - 末尾の「1件の大きさ」で後ろからたどる。[0, pos_) が元に戻せる件、[pos_, end_) がやり直せる件
// - 新しく記録すると、やり直せる件は捨てる
// - 溢れたら古い件から捨てる(元に戻せる回数が減るだけ。文書の中身は壊れない)
// - 続けて打った文字(同じ所へ1秒以内・改行を含まない・64バイトまで)は1件にまとめる。
//   続けてBackspace/Deleteで消した分も同じ
// - kChained の件は直前の件とひとまとまり(すべて置換)で、元に戻す/やり直すときはまとめて扱う
// - 保存した時点を覚えておき、そこへ戻ったら「未保存でない」と分かる(atSaved())
class EditHistory {
    public:
        static constexpr int kBytes = 4096;
        static constexpr uint16_t kChained = 1;
        static constexpr uint32_t kMergeMs = 1000;
        static constexpr int kMergeMaxBytes = 64;

        struct Step {
            int pos = 0;
            const char* del = nullptr;
            int del_n = 0;
            const char* ins = nullptr;
            int ins_n = 0;
            bool chained = false;
        };

    private:
        static constexpr int kHeader = 10;
        static constexpr int kTrailer = 2;

        uint8_t buf_[kBytes];
        int end_ = 0;     // 記録の終わり
        int pos_ = 0;     // ここまでが元に戻せる
        int saved_ = 0;   // 保存した時点のpos_(-1 = もう戻れない)
        bool can_merge_ = false;
        uint32_t last_ms_ = 0;

        static uint32_t rd32(const uint8_t* p){ uint32_t v; memcpy(&v, p, 4); return v; }
        static uint16_t rd16(const uint8_t* p){ uint16_t v; memcpy(&v, p, 2); return v; }
        static void wr32(uint8_t* p, uint32_t v){ memcpy(p, &v, 4); }
        static void wr16(uint8_t* p, uint16_t v){ memcpy(p, &v, 2); }

        void readAt(int at, Step& s) const {
            s.pos = (int)rd32(buf_ + at);
            s.del_n = rd16(buf_ + at + 4);
            s.ins_n = rd16(buf_ + at + 6);
            s.chained = (rd16(buf_ + at + 8) & kChained) != 0;
            s.del = (const char*)buf_ + at + kHeader;
            s.ins = s.del + s.del_n;
        }
        static int sizeOf(int del_n, int ins_n){ return kHeader + del_n + ins_n + kTrailer; }

        void dropOldest(){
            const int s0 = sizeOf(rd16(buf_ + 4), rd16(buf_ + 6));
            memmove(buf_, buf_ + s0, end_ - s0);
            end_ -= s0;
            pos_ -= s0;
            if(saved_ >= 0) saved_ = (saved_ >= s0) ? saved_ - s0 : -1;
        }

        static bool hasNewline(const char* s, int n){ return memchr(s, '\n', n) != nullptr; }

        bool tryMerge(int pos, const char* del, int del_n, const char* ins, int ins_n, uint32_t now_ms){
            if(!can_merge_ || pos_ == 0 || now_ms - last_ms_ > kMergeMs) return false;
            const int size = rd16(buf_ + pos_ - kTrailer);
            const int at = pos_ - size;
            Step l;
            readAt(at, l);
            if(l.chained) return false;

            if(l.del_n == 0 && del_n == 0 && ins_n > 0){
                //続けて打った文字
                if(pos != l.pos + l.ins_n || l.ins_n + ins_n > kMergeMaxBytes) return false;
                if(hasNewline(ins, ins_n) || hasNewline(l.ins, l.ins_n)) return false;
                if(end_ + ins_n > kBytes) return false;
                memcpy(buf_ + pos_ - kTrailer, ins, ins_n);
                wr16(buf_ + at + 6, (uint16_t)(l.ins_n + ins_n));
            }else if(l.ins_n == 0 && ins_n == 0 && del_n > 0){
                if(l.del_n + del_n > kMergeMaxBytes) return false;
                if(hasNewline(del, del_n) || hasNewline(l.del, l.del_n)) return false;
                if(end_ + del_n > kBytes) return false;
                uint8_t* payload = buf_ + at + kHeader;
                if(pos + del_n == l.pos){
                    //Backspace: 前へ伸びる
                    memmove(payload + del_n, payload, l.del_n);
                    memcpy(payload, del, del_n);
                    wr32(buf_ + at, (uint32_t)pos);
                }else if(pos == l.pos){
                    //Delete: 後ろへ伸びる
                    memcpy(payload + l.del_n, del, del_n);
                }else{
                    return false;
                }
                wr16(buf_ + at + 4, (uint16_t)(l.del_n + del_n));
            }else{
                return false;
            }
            const int new_size = size + del_n + ins_n;
            pos_ = end_ = at + new_size;
            wr16(buf_ + pos_ - kTrailer, (uint16_t)new_size);
            last_ms_ = now_ms;
            return true;
        }

    public:
        void clear(){
            end_ = pos_ = 0;
            saved_ = 0;
            can_merge_ = false;
        }

        // 文書の[pos, pos+del_n)(中身はdel)を ins に置き換えた。置き換える前に呼んでも後に呼んでもよい。
        // chained=true なら直前の件とひとまとまりにする(まとめない)
        void record(int pos, const char* del, int del_n, const char* ins, int ins_n, uint32_t now_ms, bool chained = false){
            if(del_n == 0 && ins_n == 0) return;
            //やり直せる件を捨てる
            if(pos_ < end_){
                if(saved_ > pos_) saved_ = -1;
                end_ = pos_;
            }
            if(!chained && tryMerge(pos, del, del_n, ins, ins_n, now_ms)) return;

            const int size = sizeOf(del_n, ins_n);
            if(size > kBytes || del_n > 0xFFFF || ins_n > 0xFFFF){
                //1件でも収まらない。ここより前へは戻れない
                clear();
                saved_ = -1;
                return;
            }
            while(end_ + size > kBytes) dropOldest();

            uint8_t* p = buf_ + end_;
            wr32(p, (uint32_t)pos);
            wr16(p + 4, (uint16_t)del_n);
            wr16(p + 6, (uint16_t)ins_n);
            wr16(p + 8, chained ? kChained : 0);
            if(del_n) memcpy(p + kHeader, del, del_n);
            if(ins_n) memcpy(p + kHeader + del_n, ins, ins_n);
            wr16(p + size - kTrailer, (uint16_t)size);
            end_ += size;
            pos_ = end_;
            can_merge_ = !chained;
            last_ms_ = now_ms;
        }

        bool canUndo() const { return pos_ > 0; }
        bool canRedo() const { return pos_ < end_; }

        // 元に戻す1件。戻した文書は [s.pos, s.pos+s.ins_n) を s.del へ置き換えたもの。
        // s.chained ならもう1件続けて戻すこと
        bool undo(Step& s){
            if(pos_ == 0) return false;
            const int size = rd16(buf_ + pos_ - kTrailer);
            pos_ -= size;
            readAt(pos_, s);
            can_merge_ = false;
            return true;
        }
        // やり直す1件。[s.pos, s.pos+s.del_n) を s.ins へ置き換える。
        // nextRedoChained() ならもう1件続けてやり直すこと
        bool redo(Step& s){
            if(pos_ >= end_) return false;
            readAt(pos_, s);
            pos_ += sizeOf(s.del_n, s.ins_n);
            can_merge_ = false;
            return true;
        }
        bool nextRedoChained() const {
            return pos_ < end_ && (rd16(buf_ + pos_ + 8) & kChained) != 0;
        }

        // 次の記録を前の件とまとめない(カーソルを動かした・保存した等)
        void breakMerge(){ can_merge_ = false; }
        void markSaved(){ saved_ = pos_; can_merge_ = false; }
        bool atSaved() const { return saved_ == pos_; }
        // 使っているバイト数(テスト・表示用)
        int usedBytes() const { return end_; }
};
