#pragma once

#include "task/Http_Get.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// 検索結果1件(PROTOCOL.md「3. 検索」のTSV 1行)。
// 3列目以降(version / snippet)は今の画面では使わないので持たない。
// 知らない列を読み飛ばすのは前方互換のため(discoveryと同じ扱い)。
struct SearchHit {
    FixedString<PICO_STR_L> path;  // "/" 始まりのサーバ絶対パス。そのままGETできる
    FixedString<PICO_STR_M> title; // 表示用
};

// 検索APIを叩いて結果を受け取る。
//
// **Doc_Cacheを通さないのが Doc_Fetch との違い。** キャッシュ上の置き場所は
// URLの「パス」だけで決まりクエリを見ないため、検索の応答を通してしまうと
// 検索語違いの結果が同じファイルに重なり、条件付きGETで別の検索語の304まで
// 起きてしまう。応答は高々20行なのでRAMへ載せて構わない。
//
// 受信は Doc_Fetch と同じく1フレームずつ進むので、検索中もループは止まらない
// (接続だけは同期的。Http_Get の注意書きを参照)。
class DocSearch {
    public:
        enum class State : uint8_t {
            Idle,
            Fetching,
            Ready,
            Failed,
        };

        // 1回に要求する件数。PROTOCOL.mdはクライアントへ20以下を求めている。
        // 1件で約150B使うので、10件なら1.5KB弱
        constexpr static int kMaxHits = 10;

        // server      … 相手のサーバ(host/portだけ使う)
        // search_path … discoveryの search 行(例 "/v1/search")
        // query       … 検索語(生のUTF-8。パーセントエンコードはこちらで行う)
        // offset      … 何件目から返してもらうか(0始まり)
        bool begin(const Url& server, const char* search_path, const char* query, int offset = 0);

        void update();
        void cancel();

        State state() const { return state_; }
        int count() const { return count_; }
        const SearchHit& hit(int index) const { return hits_[index]; }
        int offset() const { return offset_; }

        // 要求ちょうどの件数が返ってきた = 続きがあるかもしれない(PROTOCOL.md)。
        // 総件数は返ってこないので、続きの有無はこれで推し量るしかない
        bool mayHaveMore() const { return count_ >= kMaxHits; }

        const char* message() const { return message_; }

    private:
        // 受信したバイト列をそのまま1行ずつ拾う。
        // 応答全体をバッファへ溜めないで済むよう行指向にしてある(PROTOCOL.md)
        class Sink : public IHttpSink {
            public:
                DocSearch* owner = nullptr;
                bool write(const void* data, size_t len) override;
        };

        // line_ に溜まった1行をTSVとして解釈し、hits_ へ積む
        void takeLine();
        void finish(State next, const char* message);

        HttpGet http;
        Sink sink;

        SearchHit hits_[kMaxHits];
        int count_ = 0;
        int offset_ = 0;

        // 1行ぶんの組み立て先。必要な2列(path/title)は行の先頭にあるので、
        // snippetが長くて溢れても結果は拾える
        char line_[PICO_STR_256B];
        size_t line_len_ = 0;

        State state_ = State::Idle;
        const char* message_ = "";
};
