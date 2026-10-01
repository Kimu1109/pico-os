#pragma once

#include <cstddef>
#include <cstdint>

// JSONを好きな切れ目のバイト列で受け取り、値を1つずつ知らせる読み取り係(SAX型)。
//
// 文書全体をRAMへ載せない・木を作らない・確保をしない。HttpResponse/Ical::Parser と同じく
// feed() へ受信したバイト列をそのまま渡すだけでよく、1バイトずつ食わせても同じ結果になる。
// Todoist のタスク一覧のように「数十KBの応答から、必要な数個のキーだけ拾う」ための作り。
//
// - 文字列は kValueBytes-1 バイトまで(超えた分は UTF-8 の文字の途中で切らないように捨て、truncated を立てる)
// - キーは kKeyBytes-1 バイトまで(超えたら同様に切り詰める。長いキーを見たい使い方はしない前提)
// - エスケープ(\" \\ \/ \b \f \n \r \t \uXXXX、サロゲートペア)は UTF-8 へ戻して渡す。
//   対になっていないサロゲートと \u0000 は U+FFFD(�)にする(C文字列で扱うため)
// - 数値は検査を緩くし、文字列のまま渡す(必要な側が strtol 等で読む)
// - 入れ子は kMaxDepth 段まで。超えたら誤り
class JsonReader {
    public:
        static constexpr int kMaxDepth = 16;
        static constexpr size_t kValueBytes = 256;
        static constexpr size_t kKeyBytes = 48;

        enum class Type : uint8_t { String, Number, True, False, Null };

        class Handler {
            public:
                virtual ~Handler() = default;
                // オブジェクト/配列に入った。key は親がオブジェクトならそのキー、そうでなければ nullptr。
                // depth は入った後の深さ(一番外の {..} の中が1)
                virtual void onBegin(const char* key, bool is_array, int depth){ (void)key; (void)is_array; (void)depth; }
                virtual void onEnd(bool is_array, int depth){ (void)is_array; (void)depth; }
                // 値(文字列/数値/真偽/null)。text は NUL 終端(len バイト)。
                // depth は値が置かれている入れ物の深さ(一番外の {..} の直下のキーなら1)
                virtual void onValue(const char* key, Type type, const char* text, size_t len,
                                     bool truncated, int depth){
                    (void)key; (void)type; (void)text; (void)len; (void)truncated; (void)depth;
                }
        };

        JsonReader() = default;

        void reset(Handler* handler);
        // 読み進める。誤りを見つけたら false(以降は何もしない)
        bool feed(const void* data, size_t len);
        // 一番外の値を最後まで読み終えたか(前後の空白は許す)
        bool complete() const { return state_ == State::Done; }
        bool failed() const { return state_ == State::Error; }
        // 誤りの理由(テスト・ログ用)
        const char* error() const { return error_; }

    private:
        enum class State : uint8_t {
            Value,        // 値を待つ(先頭・':' の後・配列の ',' の後)
            ValueOrEnd,   // '[' の直後(値か ']')
            KeyOrEnd,     // '{' の直後(キーか '}')
            Key,          // オブジェクトの ',' の後(キー)
            Colon,        // キーの後
            AfterValue,   // 値の後(',' か閉じ括弧)
            String,       // 文字列の中
            Escape,       // '\' の直後
            Unicode,      // \u の4桁を読んでいる
            Literal,      // 数値/true/false/null
            Done,
            Error,
        };

        Handler* handler_ = nullptr;
        State state_ = State::Value;
        const char* error_ = "";

        bool stack_is_array_[kMaxDepth] = {};
        int depth_ = 0;

        bool str_is_key_ = false;
        char key_[kKeyBytes] = {};
        size_t key_len_ = 0;
        bool has_key_ = false;      // 今の値に対応するキーがある(親がオブジェクト)

        char value_[kValueBytes] = {};
        size_t value_len_ = 0;
        bool truncated_ = false;

        uint32_t unicode_ = 0;      // \uXXXX の読みかけ
        int unicode_digits_ = 0;
        uint32_t high_surrogate_ = 0; // 対になる下位を待っている上位サロゲート

        bool step(char c);
        bool fail(const char* why);
        bool beginValue(char c);
        void afterValue();
        void appendByte(char c);
        void appendCodepoint(uint32_t cp);
        void flushHighSurrogate();
        bool finishString();
        bool finishLiteral();
        const char* currentKey() const { return has_key_ ? key_ : nullptr; }
};
