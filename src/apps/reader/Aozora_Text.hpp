#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// 青空文庫の形式のテキストを、1文字ずつ(注記を解いた形で)読み出す。読書アプリ(ReaderScene)の土台。
//
// 本を丸ごとRAMへ読まない(長編は1MB近くある)。位置はファイルの中のバイト位置で表し、
// next(pos) がその位置から字句を1つ読んで pos を進める。ページの並べ方(Reader_Layout)は
// この位置だけを覚えておけば、どこからでも読み直せる。
//
//   - 文字コード: 先頭を見て UTF-8 か Shift_JIS(cp932)かを決める(青空文庫の配布物はShift_JIS)。
//     Shift_JIS は メールと同じ JIS X 0208 の表(apps/mail/Mail_Charset)を引く。表に無い文字は 〓
//   - 改行: CRLF / LF / CR
//   - ルビ: 《かんじ》は Ruby、ルビの掛かり始めの ｜ は RubyStart(親文字の途中で行を折らないため)
//   - 注記: ［＃…］ は Note(改ページ・字下げ・地付き・見出し だけ意味を取り、他は Other)
//   - 外字: ※［＃「…」、U+XXXX、…］ は その文字1つ。番号が「第3水準1-85-25」だけのものは 〓
//   - 冒頭の「【テキスト中に現れる記号について】」の区切り線で囲まれた説明は読み飛ばす(Analyze()が見つける)
namespace Aozora {

    enum class Encoding : uint8_t { Utf8, ShiftJis };

    // 本の中身の読み出し口(実機はSDのファイル、テストはメモリ)
    class Source {
        public:
            virtual ~Source() = default;
            virtual uint32_t size() const = 0;
            // off から最大 n バイトを dst へ。読めたバイト数を返す
            virtual size_t readAt(uint32_t off, uint8_t* dst, size_t n) = 0;
    };

    // 本の先頭を見て分かること
    struct Info {
        Encoding enc = Encoding::ShiftJis;
        uint32_t body_start = 0;    // BOMの後
        // 読み飛ばす説明の範囲 [notes_begin, notes_end)。無ければ両方0
        uint32_t notes_begin = 0;
        uint32_t notes_end = 0;
        // 青空文庫の決まり(1行目が題名、2行目が著者)。空なら呼び出し側がファイル名を使う
        FixedString<PICO_STR_L> title;
        FixedString<PICO_STR_L> author;
    };

    // 先頭(最大 kProbeBytes)から文字コード・題名・著者・説明の範囲を決める
    constexpr size_t kProbeBytes = 8192;
    void Analyze(Source& src, Info& out);

    enum class Tok : uint8_t { Eof, Char, Newline, Ruby, RubyStart, Note };

    enum class Note : uint8_t {
        Other,          // 意味を取らない注記(傍点・字の大きさ等)
        PageBreak,      // 改ページ・改丁・改見開き・改段
        Indent,         // ［＃２字下げ］ その段落だけ num 字下げる
        BlockIndent,    // ［＃ここから２字下げ］ 終わりまでの段落を num 字下げる
        BlockIndentEnd, // ［＃ここで字下げ終わり］
        AlignBottom,    // ［＃地付き］(num=0) / ［＃地から２字上げ］(num=2) その行を右へ寄せる
        Heading,        // ［＃「第一章」は中見出し］ text=第一章
        HeadingBegin,   // ［＃中見出し］ / ［＃ここから中見出し］
        HeadingEnd,     // ［＃中見出し終わり］ / ［＃ここで中見出し終わり］
    };

    constexpr size_t kTextMax = 96;             // ルビ・見出しの文字(UTF-8)。超えた分は捨てる
    constexpr uint32_t kReplacement = 0x3013;   // 〓

    struct Token {
        Tok kind = Tok::Eof;
        uint32_t cp = 0;            // Char
        Note note = Note::Other;    // Note
        int8_t num = 0;             // Note の字数
        uint8_t len = 0;            // text のバイト数(Ruby / Heading)
        char text[kTextMax + 1] = {};
    };

    // Unicodeの1文字をUTF-8で書く。書いたバイト数(1〜4)を返す
    int EncodeUtf8(uint32_t cp, char out[4]);

    class Reader {
        public:
            Reader(Source& src, const Info& info) : src_(src), info_(info) {}

            // pos から字句を1つ読み、pos を次の字句の頭へ進める
            void next(uint32_t& pos, Token& out);

            const Info& info() const { return info_; }
            uint32_t size() const { return src_.size(); }

        private:
            static constexpr size_t kWindow = 512;
            // 注記・ルビを探す長さの上限(閉じ括弧が無いときに本の終わりまで読まないため)
            static constexpr int kMaxNoteChars = 160;
            static constexpr int kMaxRubyChars = 48;

            Source& src_;
            const Info& info_;
            uint8_t win_[kWindow];
            uint32_t win_off_ = 0;
            uint32_t win_len_ = 0;

            int byteAt(uint32_t pos);
            // pos の1文字を読む。len に使ったバイト数。終わりなら0を返して len=0
            uint32_t decode(uint32_t pos, int& len);
            // 開き括弧の次(pos)から閉じ括弧までを UTF-8 で buf へ(入りきらない分は捨てる)。
            // 閉じ括弧の次の位置を返す。見つからなければ0
            uint32_t readBracket(uint32_t pos, uint32_t open, uint32_t close, int max_chars, char* buf, size_t cap, size_t& len);
            void interpretNote(Token& t, const char* s, size_t n);
    };
}
