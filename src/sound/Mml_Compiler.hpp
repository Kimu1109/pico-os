#pragma once
#include <cstddef>
#include <cstdint>
#include "util/FixedString.hpp"
#include "sound/Music_Data.hpp"
#include "consts.hpp"

// pico-os MML(MUSIC_FORMAT.md)を読み、演奏データ(Music_Data.hpp)へ変換する。1コア目で使う。
//
// - テキストは行ごとに受け取る(MmlLineSource)。チャンネルごとに命令列を続けて書くため、
//   **チャンネルの数だけ頭から読み直す**(SDのファイルなら全体をRAMへ載せずに済む)
// - 出力は呼び出し側の固定長のバッファ。確保はしない
// - 最初の誤りで止め、行・列・理由を返す。気づいた注意は1つだけ警告として返す
class MmlLineSource {
public:
    virtual ~MmlLineSource() = default;
    virtual bool rewind() = 0;
    // 次の1行(改行は含めない)。終わりなら-1、長すぎる行なら-2
    virtual int next(char* buf, size_t cap) = 0;
};

// メモリ上の文字列から読む(pico.music_play_text用)
class MmlTextSource : public MmlLineSource {
public:
    MmlTextSource(const char* text, size_t len) : text_(text), len_(len) {}
    bool rewind() override { pos_ = 0; return true; }
    int next(char* buf, size_t cap) override;
private:
    const char* text_;
    size_t len_;
    size_t pos_ = 0;
};

// SDのファイルから読む。開けなければ ok() が false
class MmlFileSource : public MmlLineSource {
public:
    explicit MmlFileSource(const char* path);
    ~MmlFileSource() override;
    bool ok() const { return opened_; }
    bool rewind() override;
    int next(char* buf, size_t cap) override;
private:
    struct Impl;
    Impl* impl_ = nullptr;   // FsFileを抱える(ヘッダでSdFatを取り込まないため)
    bool opened_ = false;
};

struct MmlResult {
    bool ok = false;
    int line = 0;       // 誤りの位置(1始まり。0なら位置なし)
    int col = 0;
    FixedString<PICO_STR_L> message;    // 誤りの理由
    FixedString<PICO_STR_L> warning;    // 気づいた注意(1つだけ)
    FixedString<PICO_STR_M> title;
    FixedString<PICO_STR_M> composer;
    uint16_t size = 0;                  // 書いた演奏データのバイト数
};

class MmlCompiler {
public:
    static constexpr int kMaxLineBytes = 512;
    static constexpr int kMaxMacros = 16;
    static constexpr int kMaxMacroName = 16;
    static constexpr int kMaxMacroBody = 192;
    static constexpr int kDefaultTempo = 120;

    // src を読み、out(cap バイト)へ演奏データを書く。成否は r.ok
    bool compile(MmlLineSource& src, uint8_t* out, size_t cap, MmlResult& r);

private:
    struct Macro {
        FixedString<kMaxMacroName + 1> name;
        FixedString<kMaxMacroBody> body;
    };
    struct LoopFrame {
        uint32_t count_pos;     // LoopBeginの回数を後から書く位置
        int32_t  break_pos;     // LoopBreakの飛び先を後から書く位置(-1で無し)
        int64_t  ticks_begin;
        int64_t  ticks_break;
        int      octave;
        int      transpose;
        int      line, col;     // 閉じ忘れの報告用
    };
    struct Channel {
        int octave = 4;
        int deflen = MusicData::kTicksPerQuarter;
        int transpose = 0;
        int64_t ticks = 0;
        LoopFrame loops[MusicData::kMaxLoopDepth];
        int depth = 0;
        bool segno = false;
        int64_t ticks_at_segno = 0;
    };

    // 1行の中身を読む。in_macro のとき、誤りの位置は呼び出し元($の位置)にする
    bool parseSeq(const char* s, int len, int line, int col_base, bool in_macro);
    bool parseLength(const char*& p, const char* end, int& ticks, bool required);
    bool parseInt(const char*& p, const char* end, int& v, bool allow_sign);
    bool emit(uint8_t b);
    bool emit16(uint16_t v);
    void patch16(uint32_t pos, uint16_t v);
    bool fail(int line, int col, const char* fmt, ...);
    void warn(const char* fmt, ...);
    const Macro* findMacro(const char* name, size_t len) const;
    bool parseHeader(const char* s, int len, int line);

    uint8_t* out_ = nullptr;
    size_t cap_ = 0;
    size_t pos_ = 0;
    MmlResult* r_ = nullptr;
    Macro macros_[kMaxMacros];
    int macro_count_ = 0;
    int tempo_ = kDefaultTempo;
    Channel ch_;
    // 呼び出し元の位置(マクロの中の誤りをここで報告する)
    int err_line_ = 0, err_col_ = 0;
    const char* macro_name_ = nullptr;
};
