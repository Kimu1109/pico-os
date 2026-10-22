#include "apps/mail/Mail_Charset.hpp"
#include "apps/mail/Mail_Jis_Data.hpp"

#include <cstring>

namespace {
    bool EqualsNoCase(const char* a, size_t alen, const char* b){
        const size_t blen = strlen(b);
        if(alen != blen) return false;
        for(size_t i = 0; i < alen; i++){
            char x = a[i], y = b[i];
            if(x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if(y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if(x != y) return false;
        }
        return true;
    }

    // windows-1252 の 0x80〜0x9F(ISO-8859-1では制御文字の所。実際のメールでは引用符等に使われている)
    constexpr uint16_t kCp1252High[32] = {
        0x20AC, 0x3013, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x3013, 0x017D, 0x3013,
        0x3013, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x3013, 0x017E, 0x0178,
    };

    constexpr uint8_t ESC = 0x1B;
}

namespace MailCharset {

Charset Parse(const char* name, size_t len){
    if(!name) return Charset::Utf8;
    //前後の空白と引用符は落とす(charset="ISO-2022-JP" の形で来る)
    while(len > 0 && (*name == ' ' || *name == '"' || *name == '\t')){ name++; len--; }
    while(len > 0 && (name[len - 1] == ' ' || name[len - 1] == '"' || name[len - 1] == '\t')) len--;

    static const char* const kJis[] = { "iso-2022-jp", "iso-2022-jp-2", "iso-2022-jp-3", "csiso2022jp" };
    static const char* const kSjis[] = { "shift_jis", "shift-jis", "sjis", "x-sjis", "windows-31j",
                                         "cp932", "ms932", "ms_kanji", "csshiftjis" };
    static const char* const kEuc[] = { "euc-jp", "x-euc-jp", "eucjp", "cseucpkdfmtjapanese" };
    static const char* const kLatin[] = { "iso-8859-1", "latin1", "iso_8859-1", "windows-1252", "cp1252",
                                          "us-ascii", "ascii" };
    for(const char* n : kJis) if(EqualsNoCase(name, len, n)) return Charset::Iso2022Jp;
    for(const char* n : kSjis) if(EqualsNoCase(name, len, n)) return Charset::ShiftJis;
    for(const char* n : kEuc) if(EqualsNoCase(name, len, n)) return Charset::EucJp;
    //us-ascii を Latin1 にしておくと、8bitのバイトが紛れ込んでいても文字化けで済む(UTF-8の検査で全部 〓 にならない)
    for(const char* n : kLatin) if(EqualsNoCase(name, len, n)) return Charset::Latin1;
    return Charset::Utf8;
}

Charset Parse(const char* name){
    return Parse(name, name ? strlen(name) : 0);
}

int EncodeUtf8(uint32_t cp, char out[4]){
    if(cp < 0x80){
        out[0] = (char)cp;
        return 1;
    }
    if(cp < 0x800){
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if(cp < 0x10000){
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

uint16_t JisToUnicode(int row, int cell){
    if(row < 1 || row > MailJisData::kRows || cell < 1 || cell > MailJisData::kCells) return 0;
    return MailJisData::kTable[(row - 1) * MailJisData::kCells + (cell - 1)];
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

void Decoder::begin(Charset cs){
    cs_ = cs;
    jis_ = JisMode::Ascii;
    esc_len_ = 0;
    lead_ = 0;
    skip_ = 0;
    utf_len_ = 0;
    utf_need_ = 0;
}

void Decoder::emit(uint32_t cp, Sink& out){
    //タブと改行以外の制御文字は落とす(画面で□になるだけなので)
    if(cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r') return;
    if(cp == 0x7F) return;
    char buf[4];
    const int n = EncodeUtf8(cp, buf);
    out.put(buf, (size_t)n);
}

void Decoder::byteUtf8(uint8_t b, Sink& out){
    if(utf_need_ > 0){
        if((b & 0xC0) == 0x80){
            utf_[utf_len_++] = b;
            if(utf_len_ < utf_need_ + 1) return;
            //揃った。長すぎる表し方・サロゲート・範囲外を弾く
            uint32_t cp;
            if(utf_len_ == 2) cp = ((uint32_t)(utf_[0] & 0x1F) << 6) | (utf_[1] & 0x3F);
            else if(utf_len_ == 3) cp = ((uint32_t)(utf_[0] & 0x0F) << 12) | ((uint32_t)(utf_[1] & 0x3F) << 6) | (utf_[2] & 0x3F);
            else cp = ((uint32_t)(utf_[0] & 0x07) << 18) | ((uint32_t)(utf_[1] & 0x3F) << 12)
                    | ((uint32_t)(utf_[2] & 0x3F) << 6) | (utf_[3] & 0x3F);
            const uint32_t min = (utf_len_ == 2) ? 0x80 : (utf_len_ == 3) ? 0x800 : 0x10000;
            const bool ok = cp >= min && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
            if(ok) out.put((const char*)utf_, utf_len_);
            else emit(kReplacement, out);
            utf_len_ = 0;
            utf_need_ = 0;
            return;
        }
        //続きのはずが来なかった。書きかけを 〓 にして、このバイトは改めて読む
        emit(kReplacement, out);
        utf_len_ = 0;
        utf_need_ = 0;
    }
    if(b < 0x80){
        emit(b, out);
        return;
    }
    if(b >= 0xC2 && b <= 0xDF) utf_need_ = 1;
    else if(b >= 0xE0 && b <= 0xEF) utf_need_ = 2;
    else if(b >= 0xF0 && b <= 0xF4) utf_need_ = 3;
    else{
        emit(kReplacement, out);
        return;
    }
    utf_[0] = b;
    utf_len_ = 1;
}

void Decoder::byteJis(uint8_t b, Sink& out){
    if(esc_len_ > 0){
        esc_[esc_len_ - 1] = b;
        esc_len_++;
        //ESC ( X / ESC $ X / ESC $ ( X
        const uint8_t i0 = esc_[0];
        if(esc_len_ == 2) return; //まだ1バイトだけ
        if(i0 == '(' && esc_len_ == 3){
            const uint8_t f = esc_[1];
            if(f == 'B' || f == 'J' || f == 'H') jis_ = JisMode::Ascii;
            else if(f == 'I') jis_ = JisMode::Kana;
            esc_len_ = 0;
            lead_ = 0;
            return;
        }
        if(i0 == '$' && esc_len_ == 3){
            const uint8_t f = esc_[1];
            if(f == '@' || f == 'B'){ jis_ = JisMode::Kanji; skip_ = 0; esc_len_ = 0; lead_ = 0; return; }
            if(f == '(') return; //ESC $ ( X の X を待つ
            esc_len_ = 0;
            return;
        }
        if(i0 == '$' && esc_len_ == 4){
            const uint8_t f = esc_[2];
            //JIS X 0208 として扱えるのは B(と JIS X 0213 の1面 Q/O)だけ。それ以外は 〓 の並びになる
            jis_ = JisMode::Kanji;
            if(f != 'B' && f != 'Q' && f != 'O') skip_ = 1; //skip_ は「表を引かない」印
            else skip_ = 0;
            esc_len_ = 0;
            lead_ = 0;
            return;
        }
        esc_len_ = 0;
        return;
    }
    if(b == ESC){
        if(lead_){ emit(kReplacement, out); lead_ = 0; }
        esc_len_ = 1;
        return;
    }
    if(b == 0x0E){ jis_ = JisMode::Kana; return; } //SO
    if(b == 0x0F){ jis_ = JisMode::Ascii; return; } //SI
    //行の終わりではASCIIに戻っているはず。戻し忘れのメールでも次の行が化けないよう、ここで戻す
    if(b == '\n' || b == '\r'){
        if(lead_){ emit(kReplacement, out); lead_ = 0; }
        jis_ = JisMode::Ascii;
        skip_ = 0;
        emit(b, out);
        return;
    }
    if(b >= 0x80){
        emit(kReplacement, out);
        return;
    }
    switch(jis_){
        case JisMode::Ascii:
            emit(b, out);
            return;
        case JisMode::Kana:
            if(b >= 0x21 && b <= 0x5F) emit(0xFF61 + (b - 0x21), out);
            else emit(b, out);
            return;
        case JisMode::Kanji:
            if(b < 0x21 || b > 0x7E){
                //空白等。2バイト文字の途中なら片割れは捨てる
                if(lead_){ emit(kReplacement, out); lead_ = 0; }
                emit(b, out);
                return;
            }
            if(!lead_){ lead_ = b; return; }
            {
                const uint16_t cp = skip_ ? 0 : JisToUnicode(lead_ - 0x20, b - 0x20);
                emit(cp ? cp : kReplacement, out);
            }
            lead_ = 0;
            return;
    }
}

void Decoder::byteSjis(uint8_t b, Sink& out){
    if(lead_){
        const uint8_t s1 = lead_;
        lead_ = 0;
        if((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC)){
            int row = ((s1 < 0xA0) ? (s1 - 0x81) : (s1 - 0xC1)) * 2 + 1;
            int cell;
            if(b >= 0x9F){
                row++;
                cell = b - 0x9E;
            }else{
                cell = b - ((b >= 0x80) ? 0x40 : 0x3F);
            }
            const uint16_t cp = JisToUnicode(row, cell);
            emit(cp ? cp : kReplacement, out);
            return;
        }
        emit(kReplacement, out);
        //2バイト目として読めなかったバイトは、1文字目として読み直す
    }
    if(b < 0x80){
        emit(b, out);
        return;
    }
    if(b >= 0xA1 && b <= 0xDF){
        emit(0xFF61 + (b - 0xA1), out);
        return;
    }
    if((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)){
        lead_ = b;
        return;
    }
    emit(kReplacement, out);
}

void Decoder::byteEuc(uint8_t b, Sink& out){
    if(skip_ > 0){
        //JIS X 0212(0x8F で始まる3バイト)は表に無いので読み飛ばす
        skip_--;
        if(skip_ == 0) emit(kReplacement, out);
        return;
    }
    if(lead_){
        const uint8_t l = lead_;
        lead_ = 0;
        if(l == 0x8E){
            if(b >= 0xA1 && b <= 0xDF){ emit(0xFF61 + (b - 0xA1), out); return; }
            emit(kReplacement, out);
        }else if(b >= 0xA1 && b <= 0xFE){
            const uint16_t cp = JisToUnicode(l - 0xA0, b - 0xA0);
            emit(cp ? cp : kReplacement, out);
            return;
        }else{
            emit(kReplacement, out);
        }
    }
    if(b < 0x80){
        emit(b, out);
        return;
    }
    if(b == 0x8F){ skip_ = 2; return; }
    if(b == 0x8E || (b >= 0xA1 && b <= 0xFE)){
        lead_ = b;
        return;
    }
    emit(kReplacement, out);
}

void Decoder::feed(const uint8_t* data, size_t n, Sink& out){
    for(size_t i = 0; i < n; i++){
        const uint8_t b = data[i];
        switch(cs_){
            case Charset::Utf8: byteUtf8(b, out); break;
            case Charset::Iso2022Jp: byteJis(b, out); break;
            case Charset::ShiftJis: byteSjis(b, out); break;
            case Charset::EucJp: byteEuc(b, out); break;
            case Charset::Latin1:
                if(b >= 0x80 && b < 0xA0) emit(kCp1252High[b - 0x80], out);
                else emit(b, out);
                break;
        }
    }
}

void Decoder::finish(Sink& out){
    if(utf_need_ > 0 || lead_ || (cs_ == Charset::EucJp && skip_ > 0)) emit(kReplacement, out);
    utf_len_ = 0;
    utf_need_ = 0;
    lead_ = 0;
    skip_ = 0;
    esc_len_ = 0;
    jis_ = JisMode::Ascii;
}

// ---------------------------------------------------------------------------
// BufSink
// ---------------------------------------------------------------------------

void BufSink::put(const char* s, size_t n){
    if(truncated_ || cap_ == 0) return;
    if(len_ + n + 1 > cap_){
        truncated_ = true;
        return;
    }
    memcpy(buf_ + len_, s, n);
    len_ += n;
    buf_[len_] = '\0';
}

void DecodeAll(Charset cs, const uint8_t* data, size_t n, Sink& out){
    Decoder d;
    d.begin(cs);
    d.feed(data, n, out);
    d.finish(out);
}

}
