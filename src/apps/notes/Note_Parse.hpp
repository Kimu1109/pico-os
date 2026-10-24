#pragma once

#include "util/FixedString.hpp"

#include <stdint.h>

// メモ1件(テキストファイル)の中身から、一覧に出す題名とタグを拾う。ファイルを小分けに読みながら feed() へ流す。
//
// 題名: 最初の空でない行。頭の空白と、Markdownの見出しの印("# " "## " …)は落とす。kTitleBytes まで
// タグ: 本文に書いた "#言葉"(Bear・Obsidianと同じ書き方。別のファイルや設定は持たない)
//   - "#" の前が行頭・空白・全角の空白/句読点/括弧のときだけ。"C#" や URL の "page#top" はタグにしない
//   - 言葉に使えるのは ASCII の英数字と "_" "-" "/"、それと ASCII 以外(日本語)。句読点(、。,.)・括弧・空白で終わる
//   - "# 見出し"(すぐ後ろが空白)、"##"、数字だけ("#1")、kMaxTagBytes を超える長さはタグにしない
//   - 同じタグが何度出ても、そのたびに知らせる(重ねないのは受け取る側)
// 確保はしない。
class NoteParser {
    public:
        static constexpr int kTitleBytes = 63;
        static constexpr int kMaxTagBytes = 31;

        using TagFn = void (*)(void* ctx, const char* tag);

        void begin(TagFn on_tag, void* ctx);
        void feed(const char* p, int n);
        // 流し終えたら呼ぶ(ファイルの終わりで切れたタグ・題名を締める)
        void finish();

        const char* title() const { return this->title_.c_str(); }

        // 1つの言葉(# を除いたもの)がタグとして使えるか。後ろの句読点を落とした長さを len に返す
        static bool TrimTag(const char* s, int& len);

    private:
        enum class TitleState : uint8_t { Leading, Hashes, Body, Done };

        TagFn on_tag_ = nullptr;
        void* ctx_ = nullptr;

        FixedString<kTitleBytes + 1> title_;
        TitleState title_state_ = TitleState::Leading;
        int title_hashes_ = 0;

        // タグ
        bool in_tag_ = false;
        bool tag_overflow_ = false;
        char tag_[kMaxTagBytes + 4] = {};
        int tag_len_ = 0;
        // 直前の文字が区切り(行頭・空白・全角の記号)か。全角の記号を見分けるため直前の2バイトも持つ
        bool at_boundary_ = true;
        uint8_t prev1_ = 0, prev2_ = 0;

        void feedTitle(char c);
        void endTag();
};
