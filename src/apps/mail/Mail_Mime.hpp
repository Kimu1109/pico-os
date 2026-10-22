#pragma once

#include "apps/mail/Mail_Charset.hpp"

#include <cstddef>
#include <cstdint>

// メール(RFC 5322 / MIME)を読む部品。通信は持たない(Imap_Client が受け取ったバイト列を渡す)。
//
//   MailMime::DecodeHeader()   … ヘッダーの値を表示用のUTF-8にする(=?ISO-2022-JP?B?...?= 等のRFC 2047、折り返し)
//   MailMime::DisplayAddress() … From の値から見せる名前を取る("山田 太郎" <taro@example.com> → 山田 太郎)
//   MailMime::ParseInternalDate() / ParseDate() … IMAPの INTERNALDATE / Date ヘッダー → UNIX時刻(UTC)
//   MailMime::TextExtractor    … メール1通を流しながら読み、本文の文字だけを取り出す
//
// TextExtractor は**メール全体をRAMへ載せない**。届いた分から行に切って読み、
//   - multipart は境界(boundary)で部分に分け、入れ子(mixed の中の alternative 等)も4段まで追う
//   - 本文に使うのは最初の text/plain。無ければ text/html をタグを落として文字にする
//   - base64 / quoted-printable を解き、charset(Mail_Charset)をUTF-8へ直す
//   - 添付は中身を読み捨て、数と名前だけ覚える
// 持つのは1行(1KB)とヘッダー1つ(1KB)ぶんの作業場と、呼び出し側が渡す本文の置き場だけ。
namespace MailMime {

    // ヘッダーの値(折り返しを含んでよい)をUTF-8にして out へ書く(NUL終端)。書いた長さを返す。
    // 入りきらない分は文字の切れ目で落とす
    size_t DecodeHeader(const char* raw, size_t len, char* out, size_t cap);

    // From / To の値(DecodeHeader 済み)から見せる名前を取る。名前が無ければアドレス。
    // 複数の宛先("a@x, b@y")は最初の1人だけ
    size_t DisplayAddress(const char* decoded, char* out, size_t cap);

    // "17-Jul-1996 02:44:25 -0700"(引用符は無くてもよい)→ UTC の UNIX時刻。読めなければfalse
    bool ParseInternalDate(const char* s, int64_t& epoch);
    // "Tue, 20 Oct 2026 10:00:00 +0900"(曜日・秒・時差は無くてもよい)→ UTC の UNIX時刻
    bool ParseDate(const char* s, int64_t& epoch);

    // 1970-01-01 からの日数(グレゴリオ暦)
    int64_t DaysFromCivil(int y, int m, int d);

    // ヘッダーの値の引数(Content-Type の charset= / boundary= 等)を取る。名前の大文字小文字は問わない。
    // name*=utf-8''%E3%81%82 の形(RFC 2231)も解く。見つからなければfalse
    bool GetParam(const char* value, const char* name, char* out, size_t cap);

    class TextExtractor : private MailCharset::Sink {
        public:
            static constexpr size_t kLineCap = 1024;
            static constexpr size_t kHeaderCap = 1024;
            static constexpr int kMaxDepth = 4;
            static constexpr size_t kBoundaryCap = 76;   // RFC 2046 では70文字まで
            static constexpr size_t kFieldCap = 192;
            static constexpr size_t kAttachNamesCap = 160;

            // 本文の置き場(呼び出し側が持つ)を渡して読み始める
            void begin(char* out, size_t cap);
            void feed(const uint8_t* data, size_t n);
            // 最後の行と書きかけを吐き出す。本文の末尾の空行は落とす
            void finish();

            size_t length() const { return out_len_; }
            // 本文の置き場が足りず、途中で切った
            bool truncated() const { return out_truncated_; }
            // 本文に使った部分が HTML だった
            bool fromHtml() const { return captured_ == Capture::Html; }

            // 一番外側のヘッダー(DecodeHeader 済み)
            const char* from() const { return from_; }
            const char* to() const { return to_; }
            const char* cc() const { return cc_; }
            const char* subject() const { return subject_; }
            const char* date() const { return date_; }

            int attachmentCount() const { return attach_count_; }
            // 添付の名前を ", " でつないだもの(入りきらない分は落とす)
            const char* attachmentNames() const { return attach_names_; }

        private:
            enum class State : uint8_t { Headers, Body, Skip };
            enum class Encoding : uint8_t { Plain, Base64, QuotedPrintable };
            enum class Capture : uint8_t { None, Plain, Html };

            // ---- 本文の置き場 ----
            char* out_ = nullptr;
            size_t out_cap_ = 0;
            size_t out_len_ = 0;
            bool out_truncated_ = false;
            int newlines_ = 0;        // 末尾に続いている改行の数(3つ以上は詰める)
            bool at_line_start_ = true;

            // ---- 行 ----
            char line_[kLineCap];
            size_t line_len_ = 0;
            bool line_cont_ = false;  // 前の行が長すぎて途中で区切った(続きの行)

            // ---- 今の部分 ----
            State state_ = State::Headers;
            int depth_ = 0;           // 0 = メールのヘッダー
            char header_[kHeaderCap];
            size_t header_len_ = 0;
            char type_[48];
            char part_boundary_[kBoundaryCap + 1];
            char part_name_[96];
            bool part_attachment_ = false;
            Encoding enc_ = Encoding::Plain;
            MailCharset::Charset part_charset_ = MailCharset::Charset::Utf8;
            Capture part_capture_ = Capture::None;

            char boundaries_[kMaxDepth][kBoundaryCap + 1];
            int boundary_count_ = 0;

            // ---- 本文に使った部分 ----
            Capture captured_ = Capture::None;
            MailCharset::Decoder charset_;
            // base64
            uint32_t b64_acc_ = 0;
            int b64_bits_ = 0;
            // quoted-printable(= の後の16進を待っている数と、1桁目)
            uint8_t qp_pending_ = 0;
            uint8_t qp_hi_ = 0;
            // HTML
            enum class HtmlState : uint8_t { Text, Tag, Entity, Comment };
            HtmlState html_ = HtmlState::Text;
            char tag_[16];
            uint8_t tag_len_ = 0;
            bool tag_name_done_ = false;
            bool tag_closing_ = false;
            char entity_[12];
            uint8_t entity_len_ = 0;
            uint8_t comment_dashes_ = 0;
            bool html_skip_ = false;  // <style> <script> <head> の中
            bool html_space_ = false; // 空白を1つ出す予定

            // ---- 一番外側のヘッダー ----
            char from_[kFieldCap];
            char to_[kFieldCap];
            char cc_[kFieldCap];
            char subject_[kFieldCap + 64];
            char date_[64];

            int attach_count_ = 0;
            char attach_names_[kAttachNamesCap];

            void onLine(const char* s, size_t n, bool full);
            void onHeaderLine(const char* s, size_t n);
            void finishHeader();
            void endHeaders();
            bool checkBoundary(const char* s, size_t n);
            void startPart();
            void endPart();
            void bodyBytes(const char* s, size_t n, bool newline);
            void decodedBytes(const uint8_t* s, size_t n);

            // MailCharset::Sink(文字コードを直した後)
            void put(const char* s, size_t n) override;
            void htmlChar(char c);
            void htmlTag();
            void htmlEntity();
            // 本文へ1文字(改行は空行を1行までに詰める)。Html版は手前に保留した空白を出してから
            void textUnit(const char* s, size_t n);
            void textUnitHtml(const char* s, size_t n);
            void outRaw(const char* s, size_t n);
    };
}
