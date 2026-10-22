#pragma once

#include "apps/reader/Reader_Layout.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// 読書アプリが覚えておくこと: 本ごとの「続きから読む位置」としおり(/sys/reader_books.tsv)と、
// 見た目の設定(/sys/reader.cfg)。
//
// 本の一覧は1行1件のタブ区切り。最近読んだ本が上で、本ごとに B の行の後ろへ M(しおり)の行が続く:
//   B <TAB> パス <TAB> 位置 <TAB> 大きさ <TAB> 題名
//   M <TAB> パス <TAB> 位置 <TAB> 見出し(ページの頭の文字)
// 位置は「バイト位置,ここから字下げ,段落の字下げ,印」(ReaderLayout::State)。組み方(文字の大きさ)が
// 変わっても同じ文の所へ戻れるよう、ページ番号ではなくファイルの中の位置で覚える。
// 書くたびに、開いている本を先頭へ置いて全体を書き直す(覚える本は kMaxBooks 冊まで。古いものから忘れる)。
namespace ReaderStore {

    using State = ReaderLayout::State;

    constexpr int kMaxBooks = 40;
    constexpr int kMaxMarks = 32;

    struct Mark {
        State at;
        FixedString<64> label;
    };

    struct BookRecord {
        FixedString<PICO_PATH_LEN> path;
        FixedString<96> title;
        State at;
        uint32_t size = 0;
    };

    // 本1冊の続きの位置としおり。覚えていなければ false(at は先頭、しおりは0件)
    bool LoadBook(const char* path, State& at, Mark* marks, int& mark_count);
    // 本1冊ぶんを書き、一覧の先頭へ置く
    bool SaveBook(const char* path, const char* title, uint32_t size, const State& at, const Mark* marks, int mark_count);
    // 最近読んだ順に最大 max 冊
    int ListBooks(BookRecord* out, int max);
    // 一覧から忘れる(しおりも消える)
    bool ForgetBook(const char* path);

    // 見た目の設定
    struct Settings {
        bool large_font = false;
        bool ruby = true;
    };
    Settings LoadSettings();
    void SaveSettings(const Settings& s);

    // 行の文字列(ルビの印を含む)から、しおりの見出しにする先頭の文字を作る(ルビと頭の空白は落とす)
    void MakeLabel(const char* line_text, FixedString<64>& out);
}
