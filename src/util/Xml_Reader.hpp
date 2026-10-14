#pragma once

#include <cstddef>
#include <cstdint>

// XMLを好きな切れ目のバイト列で受け取り、要素・属性・文字を順に知らせる読み取り係(SAX型)。
//
// JsonReader と同じ作り: 文書全体をRAMへ載せない・木を作らない・確保をしない。feed() へ受信した
// バイト列をそのまま渡すだけでよく、1バイトずつ食わせても同じ結果になる。
// Google News のRSSのように「数十KBの応答から、<item> の <title> と <link> だけ拾う」ための作り。
//
// 読めるもの: 要素/属性(' と " の引用符)/自己終了タグ/文字/CDATA/コメント/処理命令(<?xml ?>)/
//   DOCTYPE(内部サブセットごと読み飛ばす。サブセット内のコメントに [ ] を書くと数え違える)/先頭のBOM
// 読まないもの(読み飛ばす・そのまま渡す): 名前空間の解決(`media:content` のように書かれたまま渡す)、
//   DTDの実体宣言(&nbsp; のような未定義の実体は `&nbsp;` のまま文字へ入れる)、検証
//
// - 実体参照は &lt; &gt; &amp; &quot; &apos; と &#N; &#xH; を UTF-8 へ戻す(U+0000・範囲外・サロゲートは U+FFFD)
// - 文字は「ひとまとまり」ずつ onText で渡す。子要素の直前・要素の終わりで区切り、前後の空白は落とし、
//   空白だけなら渡さない。CDATAも同じ文字として混ぜる(`<![CDATA[<b>x</b>]]>` は `<b>x</b>` という文字)
// - 文字・属性値は kValueBytes-1 バイトまで(超えた分は UTF-8 の文字の途中で切らないように捨て、truncated を立てる)
// - 名前は kNameBytes-1 バイトまで(超えたら切り詰める。開きと閉じは同じ切り詰め方で比べる)
// - 入れ子は kMaxDepth 段まで。超えたら誤り。開きと閉じが合わなくても誤り
class XmlReader {
    public:
        static constexpr int kMaxDepth = 16;
        static constexpr size_t kValueBytes = 256;
        static constexpr size_t kNameBytes = 32;

        class Handler {
            public:
                virtual ~Handler() = default;
                // 要素が開いた(開始タグの名前まで読んだ。属性はこの後 onAttribute で届く)。
                // depth は開いた後の深さ(ルート要素が1)
                virtual void onOpen(const char* name, int depth){ (void)name; (void)depth; }
                // 属性。depth は属性を持つ要素の深さ。value は NUL 終端(len バイト)
                virtual void onAttribute(const char* name, const char* value, size_t len,
                                         bool truncated, int depth){
                    (void)name; (void)value; (void)len; (void)truncated; (void)depth;
                }
                // 文字。name は文字を直接含んでいる要素の名前、depth はその要素の深さ。text は NUL 終端(len バイト)
                virtual void onText(const char* name, const char* text, size_t len,
                                    bool truncated, int depth){
                    (void)name; (void)text; (void)len; (void)truncated; (void)depth;
                }
                // 要素が閉じた(自己終了タグでは onOpen の直後)。depth は閉じる要素の深さ
                virtual void onClose(const char* name, int depth){ (void)name; (void)depth; }
        };

        XmlReader() = default;

        void reset(Handler* handler);
        // 読み進める。誤りを見つけたら false(以降は何もしない)
        bool feed(const void* data, size_t len);
        // ルート要素を閉じ終えたか(後ろの空白・コメントは許す)
        bool complete() const { return root_done_ && state_ == State::Text; }
        bool failed() const { return state_ == State::Error; }
        // 誤りの理由(テスト・ログ用)
        const char* error() const { return error_; }

    private:
        enum class State : uint8_t {
            Text,          // 文字(要素の外では空白だけ許す)
            TagOpen,       // '<' の直後
            TagName,       // 開始タグの名前
            InTag,         // 開始タグの中(属性の前後)
            AttrName,
            AfterAttrName, // 属性名の後(= を待つ)
            BeforeValue,   // = の後(引用符を待つ)
            AttrValue,
            SelfClose,     // '/' の直後('>' を待つ)
            CloseName,     // </名前
            CloseAfter,    // </名前 の後('>' を待つ)
            Bang,          // '<!' の直後(コメント/CDATA/DOCTYPE の見分け)
            Comment,
            CData,
            Pi,            // <? ... ?>
            Doctype,
            Entity,        // '&' の直後
            Error,
        };

        Handler* handler_ = nullptr;
        State state_ = State::Text;
        State entity_return_ = State::Text;
        const char* error_ = "";
        bool root_done_ = false;

        int depth_ = 0;
        char names_[kMaxDepth][kNameBytes] = {};

        char name_[kNameBytes] = {};        // 読んでいる最中のタグ名
        size_t name_len_ = 0;
        char attr_name_[kNameBytes] = {};
        size_t attr_name_len_ = 0;
        char quote_ = '"';

        char value_[kValueBytes] = {};      // 文字 / 属性値
        size_t value_len_ = 0;
        bool truncated_ = false;

        char bang_[8] = {};
        uint8_t bang_len_ = 0;
        uint8_t dashes_ = 0;                // コメント末尾の '-' の数 / CDATA末尾の ']' の数 / 処理命令の '?'
        int doctype_depth_ = 0;
        char doctype_quote_ = 0;

        char entity_[12] = {};
        uint8_t entity_len_ = 0;

        bool step(char c);
        bool fail(const char* why);
        void appendRaw(char c);
        void appendText(char c);
        void appendCodepoint(uint32_t cp);
        void flushText();
        bool openTag();
        bool closeTag();
        void closeTop();
        bool finishEntity();
        void emitAttribute();
        bool stepEntity(char c);
};
