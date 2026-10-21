#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// IMAP4rev1(RFC 3501)の応答を読む部品。通信は持たない(Imap_Client が使う)。
//
// IMAPの応答は「行」だが、途中に {123}CRLF で始まるリテラル(123バイトの生のデータ。改行を含んでよい)を
// 挟める。メールのヘッダーや本文はこのリテラルで届く。Imap::Reader はバイト列を受け取って、
// リテラルを含む1つの応答に組み立てる:
//   - 小さいリテラル(ヘッダー)は応答の文字列の中へ "{n}\n" + 中身 の形で残す(ParseFetch() が読む)
//   - 大きいリテラル(本文)は Handler が望めば中身を貯めずにそのまま流す(応答の中には "" が残る)
// 組み立ての場所は kCap バイトの固定長。溢れた応答は overflow 付きで渡す(中身は途中まで)。
namespace Imap {

    class Reader {
        public:
            static constexpr size_t kCap = 4096;

            class Handler {
                public:
                    virtual ~Handler() = default;
                    // リテラルが始まる。line はそこまでの応答。trueを返すと中身を onLiteralData() へ流す
                    virtual bool onLiteralStart(const char* line, size_t len, uint32_t size) = 0;
                    virtual void onLiteralData(const uint8_t* data, size_t n) = 0;
                    virtual void onLiteralEnd() = 0;
                    // 応答が1つ揃った(末尾の CRLF は含まない。NUL終端)
                    virtual void onResponse(const char* text, size_t len, bool overflow) = 0;
            };

            void reset();
            void feed(const uint8_t* data, size_t n, Handler& h);

        private:
            enum class Mode : uint8_t { Line, Literal };

            char buf_[kCap + 1];
            size_t len_ = 0;
            bool overflow_ = false;
            // 溢れた後も行末の {n} を見つけられるよう、行の最後の数文字だけは覚えておく
            char tail_[16];
            size_t tail_len_ = 0;

            Mode mode_ = Mode::Line;
            uint32_t lit_left_ = 0;
            uint32_t lit_store_ = 0;  // 貯める残り(超えた分は捨てる)
            bool lit_stream_ = false;

            void putLineChar(char c);
            void endLine(Handler& h);
    };

    // 応答の種類
    enum class Kind : uint8_t { Untagged, Tagged, Continuation, Other };
    enum class Status : uint8_t { None, Ok, No, Bad, Bye, PreAuth };

    struct Response {
        Kind kind = Kind::Other;
        uint32_t tag = 0;          // "a12" なら 12(このクライアントの付け方)
        Status status = Status::None;
        uint32_t number = 0;       // "* 3 EXISTS" の 3
        bool has_number = false;
        const char* keyword = "";  // EXISTS / FETCH / CAPABILITY …(大文字小文字はそのまま)
        size_t keyword_len = 0;
        const char* text = "";     // 状態の後ろの説明("[AUTHENTICATIONFAILED] Invalid credentials" 等)
    };

    // 1つの応答を分類する。s は NUL終端であること
    bool ParseResponse(const char* s, Response& out);
    bool KeywordIs(const Response& r, const char* word);

    // FETCH 応答の中身(使うものだけ)
    struct Fetch {
        uint32_t seq = 0;
        bool has_uid = false;
        uint32_t uid = 0;
        bool has_flags = false;
        bool seen = false;
        bool answered = false;
        bool flagged = false;
        bool deleted = false;
        bool has_size = false;
        uint32_t size = 0;
        bool has_date = false;
        int64_t date = 0;
        // BODY[HEADER.FIELDS (...)] / RFC822.HEADER の中身(応答の文字列の中を指す)
        const char* header = nullptr;
        size_t header_len = 0;
    };

    // "* N FETCH (...)" を読む。形が違えばfalse
    bool ParseFetch(const char* s, size_t len, Fetch& out);

    // 一覧に出す1通ぶん
    struct Summary {
        uint32_t uid = 0;
        uint32_t seq = 0;
        uint32_t size = 0;
        int64_t date = 0;   // UTC の UNIX時刻(分からなければ0)
        bool seen = false;
        bool answered = false;
        bool flagged = false;
        FixedString<PICO_STR_L> from;     // 見せる名前(DisplayAddress 済み)
        FixedString<PICO_STR_LL> subject;
    };

    // FETCH 応答で届いた分だけ Summary へ写す(ヘッダーは From / Subject / Date を読む)
    void ApplyFetch(const Fetch& f, Summary& out);

    // 文字列をIMAPの quoted string("..." と \ のエスケープ)にする。
    // CR/LF/NUL と8bitの文字は quoted string に入れられないのでfalse
    bool Quote(const char* s, char* out, size_t cap);
}
