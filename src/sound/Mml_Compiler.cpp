#include "sound/Mml_Compiler.hpp"
#include "sound/Chip_Synth.hpp"
#include "OS_Data.hpp"

#include <cstdarg>
#include <cstring>

using namespace MusicData;

// ================================================================
// 行の読み手
// ================================================================

int MmlTextSource::next(char* buf, size_t cap){
    if(pos_ >= len_) return -1;
    size_t n = 0;
    bool too_long = false;
    while(pos_ < len_ && text_[pos_] != '\n'){
        if(n + 1 < cap) buf[n++] = text_[pos_];
        else too_long = true;
        pos_++;
    }
    if(pos_ < len_) pos_++;     // '\n'
    if(n > 0 && buf[n - 1] == '\r') n--;
    buf[n] = '\0';
    return too_long ? -2 : (int)n;
}

struct MmlFileSource::Impl {
    FsFile f;
};

MmlFileSource::MmlFileSource(const char* path){
    if(!OSData::SD_usable) return;
    impl_ = new Impl();
    impl_->f = OSData::SD.open(path, O_RDONLY);
    opened_ = (bool)impl_->f;
}

MmlFileSource::~MmlFileSource(){
    if(impl_){
        if(opened_) impl_->f.close();
        delete impl_;
    }
}

bool MmlFileSource::rewind(){
    return opened_ && impl_->f.seek(0);
}

int MmlFileSource::next(char* buf, size_t cap){
    if(!opened_) return -1;
    const int n = impl_->f.fgets(buf, (int)cap);
    if(n <= 0) return -1;
    int len = n;
    const bool has_newline = (buf[len - 1] == '\n');
    if(has_newline) len--;
    else if(len >= (int)cap - 1 && impl_->f.available() > 0){
        //改行に届かないまま詰まった = 行が長すぎる(残りは読み捨てる)
        int c;
        while((c = impl_->f.read()) >= 0 && c != '\n'){}
        return -2;
    }
    if(len > 0 && buf[len - 1] == '\r') len--;
    buf[len] = '\0';
    return len;
}

// ================================================================
// 下請け
// ================================================================

namespace {
    const char* const kWaveNames[] = {
        "pulse12", "pulse25", "pulse50", "pulse75", "triangle", "saw", "noise", "noise_short",
    };
    static_assert(sizeof(kWaveNames) / sizeof(kWaveNames[0]) == (size_t)ChipSynth::Wave::kCount,
                  "kWaveNamesをChipSynth::Waveと揃えること");

    bool IsSpace(char c){ return c == ' ' || c == '\t'; }
    bool IsDigit(char c){ return c >= '0' && c <= '9'; }
    bool IsNameChar(char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || IsDigit(c) || c == '_';
    }

    // 行頭の空白を飛ばし、末尾の空白を落とす
    void Trim(const char*& s, int& len){
        while(len > 0 && IsSpace(*s)){ s++; len--; }
        while(len > 0 && IsSpace(s[len - 1])) len--;
    }

    // ;から後ろ(注釈)を落とした長さ
    int StripComment(const char* s, int len){
        for(int i = 0; i < len; i++) if(s[i] == ';') return i;
        return len;
    }

    // 行頭のチャンネル文字(A〜D)を読む。チャンネルの行でなければ0
    int ChannelMask(const char* s, int len, int& consumed){
        int mask = 0, i = 0;
        while(i < len && s[i] >= 'A' && s[i] <= 'D'){
            mask |= 1 << (s[i] - 'A');
            i++;
        }
        consumed = i;
        return mask;
    }

    // 音名 → 半音(ドを0)
    int NoteBase(char c){
        switch(c){
            case 'c': return 0; case 'd': return 2; case 'e': return 4; case 'f': return 5;
            case 'g': return 7; case 'a': return 9; case 'b': return 11;
            default: return -1;
        }
    }
}

bool MmlCompiler::fail(int line, int col, const char* fmt, ...){
    if(!r_->message.empty()) return false;   //最初の誤りだけを残す
    //マクロの中の誤りは、マクロを呼んだ位置で報告する
    if(macro_name_){
        r_->line = err_line_;
        r_->col = err_col_;
        r_->message.appendFormat("マクロ$%s: ", macro_name_);
    }else{
        r_->line = line;
        r_->col = col;
    }
    va_list ap;
    va_start(ap, fmt);
    r_->message.appendFormatV(fmt, ap);
    va_end(ap);
    return false;
}

void MmlCompiler::warn(const char* fmt, ...){
    if(!r_->warning.empty()) return;
    va_list ap;
    va_start(ap, fmt);
    r_->warning.appendFormatV(fmt, ap);
    va_end(ap);
}

bool MmlCompiler::emit(uint8_t b){
    if(pos_ >= cap_) return false;
    out_[pos_++] = b;
    return true;
}

bool MmlCompiler::emit16(uint16_t v){
    return emit((uint8_t)(v & 0xFF)) && emit((uint8_t)(v >> 8));
}

void MmlCompiler::patch16(uint32_t pos, uint16_t v){
    out_[pos] = (uint8_t)(v & 0xFF);
    out_[pos + 1] = (uint8_t)(v >> 8);
}

const MmlCompiler::Macro* MmlCompiler::findMacro(const char* name, size_t len) const {
    for(int i = 0; i < macro_count_; i++){
        const auto& m = macros_[i];
        if(m.name.length() == len && memcmp(m.name.c_str(), name, len) == 0) return &m;
    }
    return nullptr;
}

bool MmlCompiler::parseInt(const char*& p, const char* end, int& v, bool allow_sign){
    bool neg = false;
    if(allow_sign && p < end && (*p == '-' || *p == '+')){
        neg = (*p == '-');
        p++;
    }
    if(p >= end || !IsDigit(*p)) return false;
    long n = 0;
    while(p < end && IsDigit(*p)){
        n = n * 10 + (*p - '0');
        if(n > 100000) n = 100000;  //桁あふれ防止(範囲外として弾かれる)
        p++;
    }
    v = (int)(neg ? -n : n);
    return true;
}

// 長さ(数字と付点)。数字を省いたら l の長さ。required なら数字が必須
bool MmlCompiler::parseLength(const char*& p, const char* end, int& ticks, bool required){
    const char* start = p;
    int n = 0;
    if(p < end && IsDigit(*p)){
        parseInt(p, end, n, false);
        if(n <= 0 || kTicksPerWhole % n != 0){
            return fail(0, (int)(start - p), "使えない長さです(%d)", n);
        }
        ticks = kTicksPerWhole / n;
    }else{
        if(required) return fail(0, 0, "長さがありません");
        ticks = ch_.deflen;
    }
    int add = ticks;
    while(p < end && *p == '.'){
        if(add % 2 != 0) return fail(0, 0, "付点が細かすぎます");
        add /= 2;
        ticks += add;
        p++;
    }
    return true;
}

bool MmlCompiler::parseHeader(const char* s, int len, int line){
    //"#key value"
    int i = 1;
    while(i < len && !IsSpace(s[i])) i++;
    const char* key = s + 1;
    const int key_len = i - 1;
    const char* val = s + i;
    int val_len = len - i;
    Trim(val, val_len);

    auto is = [&](const char* k){ return (int)strlen(k) == key_len && memcmp(key, k, key_len) == 0; };

    if(is("title")){
        r_->title.assign(val, (size_t)val_len);
    }else if(is("composer")){
        r_->composer.assign(val, (size_t)val_len);
    }else if(is("tempo")){
        const char* p = val;
        int t = 0;
        if(!parseInt(p, val + val_len, t, false) || p != val + val_len || t < 30 || t > 300){
            return fail(line, (int)(val - s) + 1, "#tempo は30〜300です");
        }
        tempo_ = t;
    }else if(is("macro")){
        //"#macro name = body"
        int j = 0;
        while(j < val_len && IsNameChar(val[j])) j++;
        if(j == 0 || j > kMaxMacroName){
            return fail(line, (int)(val - s) + 1, "マクロの名前は英数字と_で1〜%d文字です", kMaxMacroName);
        }
        const char* name = val;
        const int name_len = j;
        while(j < val_len && IsSpace(val[j])) j++;
        if(j >= val_len || val[j] != '='){
            return fail(line, (int)(val - s) + j + 1, "#macro 名前 = 内容 の形で書いてください");
        }
        j++;
        const char* body = val + j;
        int body_len = val_len - j;
        Trim(body, body_len);
        if(body_len >= kMaxMacroBody){
            return fail(line, (int)(body - s) + 1, "マクロの中身が長すぎます(%dバイトまで)", kMaxMacroBody - 1);
        }
        if(findMacro(name, (size_t)name_len)){
            return fail(line, (int)(name - s) + 1, "マクロ$%.*sが2回定義されています", name_len, name);
        }
        if(macro_count_ >= kMaxMacros){
            return fail(line, 1, "マクロが多すぎます(%d個まで)", kMaxMacros);
        }
        Macro& m = macros_[macro_count_++];
        m.name.assign(name, (size_t)name_len);
        m.body.assign(body, (size_t)body_len);
    }
    //知らないヘッダは読み飛ばす(前方互換)
    return true;
}

// ================================================================
// トラックの中身
// ================================================================

bool MmlCompiler::parseSeq(const char* s, int len, int line, int col_base, bool in_macro){
    const char* p = s;
    const char* end = s + len;
    //この関数の中の誤りの列(1始まり)
    #define COL() (col_base + (int)(p - s))
    #define FAIL(...) do{ fail(line, COL(), __VA_ARGS__); return false; }while(0)
    //parseLength()/parseInt()の中の誤りは位置を持たないので、ここで付け直す
    #define FIX_POS(at) do{ if(!macro_name_ && r_->line == 0){ r_->line = line; r_->col = col_base + (int)((at) - s); } }while(0)
    #define OUT_OF_SPACE() FAIL("曲が長すぎます(演奏データが%uバイトを超えます)", (unsigned)cap_)

    while(p < end){
        const char c = *p;
        const char* at = p;

        if(IsSpace(c) || c == '|'){ p++; continue; }
        if(c == ';') break;

        const int base = NoteBase(c);
        if(base >= 0 || c == 'r' || c == 'n'){
            p++;
            int midi = -1;
            if(base >= 0){
                int semi = base;
                while(p < end && (*p == '+' || *p == '#' || *p == '-')){
                    semi += (*p == '-') ? -1 : 1;
                    p++;
                }
                midi = (ch_.octave + 1) * 12 + semi + ch_.transpose;
            }else if(c == 'n'){
                int n = 0;
                if(!parseInt(p, end, n, false) || n < 0 || n > 127){ p = at; FAIL("n の後ろは0〜127のノート番号です"); }
                midi = n + ch_.transpose;
            }
            //長さ。n は番号と区別するため「,長さ」で書く(省けば l の長さ)
            int ticks = ch_.deflen;
            if(c != 'n'){
                if(!parseLength(p, end, ticks, false)){ FIX_POS(at); return false; }
            }else if(p < end && *p == ','){
                p++;
                if(!parseLength(p, end, ticks, false)){ FIX_POS(at); return false; }
            }
            //タイ
            while(p < end && *p == '^'){
                p++;
                int more = 0;
                if(!parseLength(p, end, more, false)){ FIX_POS(at); return false; }
                ticks += more;
            }
            if(ticks > 65535) { p = at; FAIL("音符が長すぎます"); }
            if(c == 'r'){
                if(!emit(Op::Rest) || !emit16((uint16_t)ticks)) OUT_OF_SPACE();
            }else{
                if(midi < 0 || midi > 127){ p = at; FAIL("音が高すぎるか低すぎます(ノート番号%d)", midi); }
                if(!emit(Op::Note) || !emit((uint8_t)midi) || !emit16((uint16_t)ticks)) OUT_OF_SPACE();
            }
            ch_.ticks += ticks;
            continue;
        }

        p++;
        int v = 0;
        switch(c){
            case 'o':
                if(!parseInt(p, end, v, false) || v < 0 || v > 8){ p = at; FAIL("o の後ろは0〜8です"); }
                ch_.octave = v;
                break;
            case '>':
                if(ch_.octave >= 8){ p = at; FAIL("これ以上オクターブを上げられません"); }
                ch_.octave++;
                break;
            case '<':
                if(ch_.octave <= 0){ p = at; FAIL("これ以上オクターブを下げられません"); }
                ch_.octave--;
                break;
            case 'l': {
                int ticks = 0;
                if(!parseLength(p, end, ticks, true)){ FIX_POS(at); return false; }
                ch_.deflen = ticks;
                break;
            }
            case 't':
                if(!parseInt(p, end, v, false) || v < 30 || v > 300){ p = at; FAIL("t の後ろは30〜300です"); }
                if(!emit(Op::Tempo) || !emit16((uint16_t)v)) OUT_OF_SPACE();
                break;
            case 'v':
                if(!parseInt(p, end, v, false) || v < 0 || v > 15){ p = at; FAIL("v の後ろは0〜15です"); }
                if(!emit(Op::Volume) || !emit((uint8_t)v)) OUT_OF_SPACE();
                break;
            case 'E':
                if(!parseInt(p, end, v, true) || v < -7 || v > 7){ p = at; FAIL("E の後ろは-7〜7です"); }
                if(!emit(Op::Envelope) || !emit((uint8_t)(int8_t)v)) OUT_OF_SPACE();
                break;
            case 'q':
                if(!parseInt(p, end, v, false) || v < 1 || v > 8){ p = at; FAIL("q の後ろは1〜8です"); }
                if(!emit(Op::Gate) || !emit((uint8_t)v)) OUT_OF_SPACE();
                break;
            case 'k':
                if(!parseInt(p, end, v, true) || v < -24 || v > 24){ p = at; FAIL("k の後ろは-24〜24です"); }
                ch_.transpose = v;
                break;
            case '@': {
                int wave = -1;
                if(p < end && IsDigit(*p)){
                    parseInt(p, end, v, false);
                    if(v < 0 || v >= (int)ChipSynth::Wave::kCount){ p = at; FAIL("@ の後ろの数字は0〜%dです", (int)ChipSynth::Wave::kCount - 1); }
                    wave = v;
                }else{
                    const char* name = p;
                    while(p < end && IsNameChar(*p)) p++;
                    const size_t name_len = (size_t)(p - name);
                    for(int i = 0; i < (int)ChipSynth::Wave::kCount; i++){
                        if(strlen(kWaveNames[i]) == name_len && memcmp(kWaveNames[i], name, name_len) == 0){
                            wave = i;
                            break;
                        }
                    }
                    if(wave < 0){ p = at; FAIL("知らない波形です(@%.*s)", (int)name_len, name); }
                }
                if(!emit(Op::Wave) || !emit((uint8_t)wave)) OUT_OF_SPACE();
                break;
            }
            case '[': {
                if(ch_.depth >= kMaxLoopDepth){ p = at; FAIL("[ ] の入れ子は%d段までです", kMaxLoopDepth); }
                if(!emit(Op::LoopBegin)) OUT_OF_SPACE();
                LoopFrame& f = ch_.loops[ch_.depth++];
                f.count_pos = (uint32_t)pos_;
                if(!emit(2)) OUT_OF_SPACE();
                f.break_pos = -1;
                f.ticks_begin = ch_.ticks;
                f.ticks_break = -1;
                f.octave = ch_.octave;
                f.transpose = ch_.transpose;
                f.line = line;
                f.col = COL() - 1;
                break;
            }
            case ':': {
                if(ch_.depth == 0){ p = at; FAIL(": は [ ] の中でしか使えません"); }
                LoopFrame& f = ch_.loops[ch_.depth - 1];
                if(f.break_pos >= 0){ p = at; FAIL("1つの [ ] に : は1つまでです"); }
                if(!emit(Op::LoopBreak)) OUT_OF_SPACE();
                f.break_pos = (int32_t)pos_;
                if(!emit16(0)) OUT_OF_SPACE();
                f.ticks_break = ch_.ticks;
                break;
            }
            case ']': {
                if(ch_.depth == 0){ p = at; FAIL("対応する [ がありません"); }
                int n = 2;
                if(p < end && IsDigit(*p)){
                    parseInt(p, end, n, false);
                    if(n < 2 || n > 255){ p = at; FAIL("繰り返しの回数は2〜255です"); }
                }
                LoopFrame& f = ch_.loops[--ch_.depth];
                if(!emit(Op::LoopEnd)) OUT_OF_SPACE();
                out_[f.count_pos] = (uint8_t)n;
                if(f.break_pos >= 0) patch16((uint32_t)f.break_pos, (uint16_t)pos_);
                const int64_t body = ch_.ticks - f.ticks_begin;
                const int64_t last = (f.break_pos >= 0) ? (f.ticks_break - f.ticks_begin) : body;
                ch_.ticks = f.ticks_begin + body * (n - 1) + last;
                if(f.octave != ch_.octave || f.transpose != ch_.transpose){
                    warn("%d行: [ ] の中でオクターブ/移調が元に戻っていません(2回目以降も1回目と同じ高さで鳴ります)", line);
                }
                break;
            }
            case 'L':
                if(in_macro){ p = at; FAIL("L はマクロの中には書けません"); }
                if(ch_.depth > 0){ p = at; FAIL("L は [ ] の中には書けません"); }
                if(ch_.segno){ p = at; FAIL("L は1つのトラックに1つまでです"); }
                if(!emit(Op::Segno)) OUT_OF_SPACE();
                ch_.segno = true;
                ch_.ticks_at_segno = ch_.ticks;
                break;
            case '$': {
                if(in_macro){ p = at; FAIL("マクロの中から別のマクロは呼べません"); }
                const char* name = p;
                while(p < end && IsNameChar(*p)) p++;
                const Macro* m = findMacro(name, (size_t)(p - name));
                if(!m){ p = at; FAIL("定義されていないマクロです($%.*s)", (int)(p - name), name); }

                //マクロの中で変えた状態は抜けたら戻す(波形等は再生側、オクターブ等はここで)
                if(!emit(Op::SaveState)) OUT_OF_SPACE();
                const int octave = ch_.octave, deflen = ch_.deflen, transpose = ch_.transpose;
                const int depth = ch_.depth;
                err_line_ = line;
                err_col_ = col_base + (int)(at - s);
                macro_name_ = m->name.c_str();
                const bool ok = parseSeq(m->body.c_str(), (int)m->body.length(), line, 1, true);
                if(ok && ch_.depth != depth) fail(line, 1, "[ ] が閉じていません");
                macro_name_ = nullptr;
                if(!ok || ch_.depth != depth) return false;
                ch_.octave = octave; ch_.deflen = deflen; ch_.transpose = transpose;
                if(!emit(Op::RestoreState)) OUT_OF_SPACE();
                break;
            }
            default:
                p = at;
                if((unsigned char)c >= 0x80) FAIL("MMLの中に使えない文字があります(注釈は ; の後ろへ)");
                FAIL("知らない命令です(%c)", c);
        }
    }
    return true;
    #undef COL
    #undef FAIL
    #undef FIX_POS
    #undef OUT_OF_SPACE
}

// ================================================================
// 全体
// ================================================================

bool MmlCompiler::compile(MmlLineSource& src, uint8_t* out, size_t cap, MmlResult& r){
    r = MmlResult();
    r_ = &r;
    out_ = out;
    cap_ = cap;
    pos_ = kHeaderBytes;
    macro_count_ = 0;
    tempo_ = kDefaultTempo;
    macro_name_ = nullptr;

    if(cap < kHeaderBytes + kChannels){
        fail(0, 0, "演奏データの置き場が小さすぎます");
        return false;
    }

    char line_buf[kMaxLineBytes + 1];
    int used_mask = 0;

    //1周目: ヘッダ(マクロ・テンポ等)を集め、どのチャンネルが使われているかを見る
    if(!src.rewind()){ fail(0, 0, "曲を読めません"); return false; }
    for(int line = 1; ; line++){
        const int n = src.next(line_buf, sizeof(line_buf));
        if(n == -1) break;
        if(n == -2){ fail(line, 1, "行が長すぎます(%dバイトまで)", kMaxLineBytes); return false; }
        const char* s = line_buf;
        int len = StripComment(s, n);
        if(line_buf[0] == '#') len = n;     //ヘッダは値に ; を含めてよいので後で落とす
        Trim(s, len);
        if(len == 0) continue;
        if(*s == '#'){
            int hlen = StripComment(s, len);
            while(hlen > 0 && IsSpace(s[hlen - 1])) hlen--;
            if(!parseHeader(s, hlen, line)) return false;
            continue;
        }
        int consumed = 0;
        const int mask = ChannelMask(s, len, consumed);
        if(mask == 0){
            fail(line, (int)(s - line_buf) + 1, "行の先頭は # (ヘッダ)か A〜D (チャンネル)です");
            return false;
        }
        used_mask |= mask;
    }

    //チャンネルごとに頭から読み直して命令列を書く
    int64_t loop_ticks[kChannels];
    bool has_segno[kChannels] = {};
    uint16_t offsets[kChannels] = {};
    for(int ch = 0; ch < kChannels; ch++){
        if(!(used_mask & (1 << ch))) continue;
        offsets[ch] = (uint16_t)pos_;
        ch_ = Channel();
        if(!src.rewind()){ fail(0, 0, "曲を読めません"); return false; }
        for(int line = 1; ; line++){
            const int n = src.next(line_buf, sizeof(line_buf));
            if(n < 0) break;
            const char* s = line_buf;
            int len = n;
            Trim(s, len);
            if(len == 0 || *s == '#' || *s == ';') continue;
            int consumed = 0;
            const int mask = ChannelMask(s, len, consumed);
            if(!(mask & (1 << ch))) continue;
            if(!parseSeq(s + consumed, len - consumed, line, (int)(s - line_buf) + consumed + 1, false)) return false;
        }
        if(ch_.depth > 0){
            const LoopFrame& f = ch_.loops[ch_.depth - 1];
            fail(f.line, f.col, "[ が閉じていません");
            return false;
        }
        if(ch_.segno && ch_.ticks == ch_.ticks_at_segno){
            fail(0, 0, "チャンネル%c: L の後ろに音符も休符もありません(無限ループになります)", 'A' + ch);
            return false;
        }
        if(!emit(Op::End)){
            fail(0, 0, "曲が長すぎます(演奏データが%uバイトを超えます)", (unsigned)cap_);
            return false;
        }
        has_segno[ch] = ch_.segno;
        loop_ticks[ch] = ch_.ticks - ch_.ticks_at_segno;
    }

    //ループの長さがチャンネルで違えば、周期がずれていく(書く側の責任だが気づけるように)
    int first = -1;
    for(int ch = 0; ch < kChannels; ch++){
        if(!has_segno[ch]) continue;
        if(first < 0){ first = ch; continue; }
        if(loop_ticks[ch] != loop_ticks[first]){
            warn("L から後ろの長さがチャンネルで違います(%c:%ld %c:%ld ティック)",
                 'A' + first, (long)loop_ticks[first], 'A' + ch, (long)loop_ticks[ch]);
            break;
        }
    }

    out_[0] = kMagic0;
    out_[1] = kMagic1;
    out_[2] = kVersion;
    out_[3] = 0;
    patch16(4, (uint16_t)tempo_);
    patch16(6, 0);
    for(int ch = 0; ch < kChannels; ch++) patch16(8 + ch * 2, offsets[ch]);
    r.size = (uint16_t)pos_;
    r.ok = true;
    return true;
}
