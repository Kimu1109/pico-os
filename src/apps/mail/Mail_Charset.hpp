#pragma once

#include <cstddef>
#include <cstdint>

// メールの文字コードをUTF-8へ直す(本文とヘッダーの両方が使う)。
//
// 対応: UTF-8 / US-ASCII / ISO-2022-JP / Shift_JIS(cp932)/ EUC-JP / ISO-8859-1(windows-1252も同じ扱い)。
// 日本語の3つは JIS X 0208 の区点 → Unicode の表(Mail_Jis_Data.hpp、約17KB、フラッシュ)を引く。
// 表に無い文字(JIS X 0212・IBM拡張・外字)と、壊れたバイト列は 〓(U+3013)にする。
// UTF-8 も素通しにはせず、壊れた並びを 〓 に置き換える(フォント側へ不正な列を渡さないため)。
//
// 状態を持つ(ISO-2022-JPのエスケープ、2バイト文字の1バイト目、UTF-8の途中)ので、
// 本文を何回に分けて feed() しても同じ結果になる。最後に finish() で書きかけを吐き出す。
namespace MailCharset {

    enum class Charset : uint8_t { Utf8, Latin1, Iso2022Jp, ShiftJis, EucJp };

    // MIMEの charset 名(大文字小文字は問わない)。知らない名前は Utf8 にする
    // (今のメールの大半はUTF-8で、ASCIIの部分はどれでも読めるため)
    Charset Parse(const char* name, size_t len);
    Charset Parse(const char* name);

    // 変換した結果の渡し先
    class Sink {
        public:
            virtual ~Sink() = default;
            virtual void put(const char* s, size_t n) = 0;
    };

    // Unicodeの1文字をUTF-8で書く。書いたバイト数(1〜4)を返す
    int EncodeUtf8(uint32_t cp, char out[4]);

    // JIS X 0208 の区点(1〜94)→ Unicode。無ければ 0
    uint16_t JisToUnicode(int row, int cell);

    constexpr uint32_t kReplacement = 0x3013; // 〓

    class Decoder {
        public:
            void begin(Charset cs);
            void feed(const uint8_t* data, size_t n, Sink& out);
            // 書きかけ(2バイト文字の片割れ・UTF-8の途中)を 〓 にして終える
            void finish(Sink& out);
            Charset charset() const { return cs_; }

        private:
            enum class JisMode : uint8_t { Ascii, Kanji, Kana };

            Charset cs_ = Charset::Utf8;
            // ISO-2022-JP
            JisMode jis_ = JisMode::Ascii;
            uint8_t esc_[3] = {};
            uint8_t esc_len_ = 0;
            // 2バイト文字の1バイト目(0なら無し)。EUC-JPの0x8F(3バイト)は esc_ と同じく数える
            uint8_t lead_ = 0;
            uint8_t skip_ = 0;
            // UTF-8 の途中
            uint8_t utf_[4] = {};
            uint8_t utf_len_ = 0;
            uint8_t utf_need_ = 0;

            void emit(uint32_t cp, Sink& out);
            void byteUtf8(uint8_t b, Sink& out);
            void byteJis(uint8_t b, Sink& out);
            void byteSjis(uint8_t b, Sink& out);
            void byteEuc(uint8_t b, Sink& out);
    };

    // 長さの決まったバッファへ書く Sink。入りきらない文字は丸ごと捨てる(UTF-8を途中で切らない)
    class BufSink : public Sink {
        public:
            BufSink(char* buf, size_t cap) : buf_(buf), cap_(cap) { if(cap_) buf_[0] = '\0'; }
            void put(const char* s, size_t n) override;
            size_t length() const { return len_; }
            bool truncated() const { return truncated_; }
            void clear(){ len_ = 0; truncated_ = false; if(cap_) buf_[0] = '\0'; }

        private:
            char* buf_;
            size_t cap_;
            size_t len_ = 0;
            bool truncated_ = false;
    };

    // 1回で変換する(ヘッダーの短い文字列向け)
    void DecodeAll(Charset cs, const uint8_t* data, size_t n, Sink& out);
}
