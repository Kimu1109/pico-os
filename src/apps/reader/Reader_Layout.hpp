#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/reader/Aozora_Text.hpp"

// 読書アプリのページの組み方(横書き)。Aozora::Reader の字句を、決まった幅と行数のページへ並べる。
//
// ページの頭は「ファイルの中の位置 + そこで効いている字下げ」(State)で表す。同じ State と同じ Params
// から組めば必ず同じページになるので、ページそのものは覚えずに State だけを覚えておけばよい
// (索引 PageIndex も、しおりも、続きから読む位置も State)。
//
//   - 段落(元の1行)を幅で折り返す。溢れた文字の前で折れないとき(行頭禁則の 、。」ゃ 等・開き括弧「（ の後・
//     ルビの親文字の途中・漢字の熟語の途中)は、その前の「折ってよい所」まで戻って次の行へ送る(追い出し)。
//     ルビだけが次の行の頭へ行くことはない(親文字ごと送る)
//   - 字下げ(［＃２字下げ］・［＃ここから２字下げ］…［＃ここで字下げ終わり］)と地付き・地上げ
//   - 改ページの注記でページを終える
//   - ルビは Params::ruby のとき、親文字の後ろへ「(かんじ)」と小さく(色を変えて)出す。行の文字列の中では
//     kRubyOn 〜 kRubyOff で囲む
//   - ページの頭の空行は飛ばす(本の最初のページを除く)
namespace ReaderLayout {

    constexpr int kMaxLines = 16;
    constexpr int kLineBytes = 200;
    constexpr char kRubyOn = '\x01';
    constexpr char kRubyOff = '\x02';

    // State::flags
    constexpr uint8_t kMid = 1;   // 段落の途中から始まる(段落の字下げを引き継ぐ)

    struct State {
        uint32_t pos = 0;
        int8_t block = 0;   // ここから〜字下げ
        int8_t para = 0;    // その段落だけの字下げ
        uint8_t flags = 0;
        uint8_t reserved = 0;

        bool operator==(const State& o) const {
            return pos == o.pos && block == o.block && para == o.para && flags == o.flags;
        }
        bool operator!=(const State& o) const { return !(*this == o); }
    };

    // 1文字の幅(px)を返す関数。ctx は Params::ctx
    using MeasureFn = int (*)(uint32_t cp, void* ctx);

    struct Params {
        int width = 224;    // 行の幅(px)
        int lines = 14;     // 1ページの行数(kMaxLines まで)
        int em = 16;        // 字下げ1字ぶん(px)
        bool ruby = true;
        MeasureFn measure = nullptr;
        void* ctx = nullptr;

        bool operator==(const Params& o) const {
            return width == o.width && lines == o.lines && em == o.em && ruby == o.ruby;
        }
        bool operator!=(const Params& o) const { return !(*this == o); }
    };

    struct Line {
        int16_t x = 0;      // 行の左端(字下げ・地付きを済ませた位置)
        uint16_t len = 0;
        char text[kLineBytes] = {};
    };

    struct Page {
        int count = 0;
        Line lines[kMaxLines];
    };

    // 見出しの一覧(目次)。索引を作るときに注記から拾う
    class Toc {
        public:
            static constexpr int kMax = 64;
            static constexpr int kTitleBytes = 60;
            struct Entry {
                uint32_t pos = 0;
                char title[kTitleBytes] = {};
            };

            void clear(){ count_ = 0; capture_ = false; }
            int count() const { return count_; }
            const Entry& at(int i) const { return entries_[i]; }

            // ［＃「第一章」は中見出し］(pos はその段落の頭)
            void onQuoted(const char* text, size_t n, uint32_t pos);
            // ［＃中見出し］第一章［＃中見出し終わり］(begin は見出しの文字の頭、end は終わりの注記の頭)
            void onBegin(uint32_t pos){ capture_ = true; capture_pos_ = pos; }
            void onEnd(uint32_t end, Aozora::Reader& r);

        private:
            Entry entries_[kMax];
            int count_ = 0;
            bool capture_ = false;
            uint32_t capture_pos_ = 0;

            void add(const char* text, size_t n, uint32_t pos);
    };

    // start から1ページぶん組んで、次のページの頭を返す。
    // out が nullptr なら数えるだけ(索引作り)。*at_end は「このページで本が終わる」。
    // toc を渡すと見出しを拾う
    State LayoutPage(Aozora::Reader& r, const State& start, const Params& p, Page* out, bool* at_end, Toc* toc = nullptr);

    // 行頭に来てはいけない文字 / 行末に残してはいけない文字
    bool IsNoLineStart(uint32_t cp);
    bool IsNoLineEnd(uint32_t cp);
    // 漢字(熟語の途中では折らない。ルビの親文字を1つにまとめるため)
    bool IsKanji(uint32_t cp);

    // ページの頭の索引。全部のページの頭は覚えず、stride ページおきに覚える(あいだは組み直して求める)。
    // 覚える数が kMaxMarks に達したら1つおきに間引いて stride を倍にするので、どんなに長い本でも溢れない
    class PageIndex {
        public:
            static constexpr int kMaxMarks = 1024;

            void reset(const State& first);
            // 索引を最大 n ページぶん進める。終わりまで数え終わったら true
            bool step(Aozora::Reader& r, const Params& p, Toc* toc, int n);
            bool complete() const { return done_; }
            // 数え終わったページ数(complete() なら本の全ページ数)
            int pageCount() const { return pages_; }
            // ここより前の位置なら find() できる
            uint32_t coveredPos() const { return cursor_.pos; }
            bool covers(uint32_t pos) const { return done_ || pos < cursor_.pos; }

            // pos を含むページの番号と頭。索引がまだそこまで無ければ false
            bool find(uint32_t pos, Aozora::Reader& r, const Params& p, int& page, State& start) const;
            // n ページ目(0から)の頭
            bool startOf(int n, Aozora::Reader& r, const Params& p, State& out) const;

        private:
            State marks_[kMaxMarks];
            int count_ = 0;
            int stride_ = 1;
            int pages_ = 0;
            State cursor_;
            bool done_ = false;
    };
}
