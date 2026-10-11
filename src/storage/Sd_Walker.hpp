#pragma once

#include "SdFat.h"
#include "consts.hpp"

#include <stdint.h>

// SDのフォルダの下を、1件ずつたどる(深さ優先)。ファイル検索・フォルダごとの大きさで使う。
// 1回の next() で1件だけ進むので、呼び出し側が時間で区切って何フレームにも分けられる(フレームを止めない)。
// 開いているフォルダの数だけ FsFile を持つ(深さ kMaxDepth まで。それより深いフォルダは中を見ない)。
// パスは固定長の1本のバッファで持ち、確保はしない。
class SdWalker {
    public:
        static constexpr int kMaxDepth = 8;

        struct Entry {
            const char* path = nullptr;   // 絶対パス(次の next() までだけ有効)
            const char* name = nullptr;   // pathの最後の部分
            bool is_dir = false;
            uint64_t size = 0;            // ファイルの大きさ(フォルダは0)
            int depth = 0;                // rootの直下が0
            bool too_deep = false;        // フォルダが深すぎて中を見なかった
        };

        SdWalker() = default;
        ~SdWalker(){ this->end(); }
        SdWalker(const SdWalker&) = delete;
        SdWalker& operator=(const SdWalker&) = delete;

        // rootの下をたどり始める。開けなければfalse
        bool begin(const char* root);
        // 次の1件。終わったらfalse
        bool next(Entry& out);
        void end();
        bool active() const { return this->depth_ >= 0; }

    private:
        FsFile dirs_[kMaxDepth];
        int depth_ = -1;                     // 今読んでいる dirs_ の段(-1 = 終わり)
        char path_[PICO_PATH_LEN + 1] = "";
        uint16_t lens_[kMaxDepth + 1] = {};  // 段ごとのフォルダのパスの長さ
};
