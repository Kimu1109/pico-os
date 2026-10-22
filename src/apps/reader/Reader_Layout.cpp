#include "apps/reader/Reader_Layout.hpp"

#include <cstring>

namespace ReaderLayout {

namespace {
    const uint16_t kNoStart[] = {
        0x3001, 0x3002, 0xFF0C, 0xFF0E, 0x30FB, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF01, 0x30FC,   // 、。，．・：；？！ー
        0x300D, 0x300F, 0xFF09, 0xFF3D, 0xFF5D, 0x3015, 0x3009, 0x300B, 0x3011, 0x3019,   // 」』）］｝〕〉》】〙
        0x3017, 0x301F, 0x2019, 0x201D,                                                   // 〗〟’”
        0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E,   // ぁぃぅぇぉっゃゅょゎ
        0x3095, 0x3096,
        0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE,   // ァィゥェォッャュョヮ
        0x30F5, 0x30F6, 0x3005, 0x303B, 0x309D, 0x309E, 0x30FD, 0x30FE, 0x2026, 0x2025,   // ヵヶ々〻ゝゞヽヾ…‥
        0xFF61, 0xFF64, 0xFF63, 0xFF9E, 0xFF9F,                                           // 半角の。、」゛゜
        ')', ']', '}', ',', '.', ':', ';', '!', '?',
    };
    const uint16_t kNoEnd[] = {
        0x300C, 0x300E, 0xFF08, 0xFF3B, 0xFF5B, 0x3014, 0x3008, 0x300A, 0x3010, 0x3018,   // 「『（［｛〔〈《【〘
        0x3016, 0x301D, 0x2018, 0x201C, 0xFF62,                                           // 〖〝‘“｢
        '(', '[', '{',
    };

    template<size_t N>
    bool InList(const uint16_t (&list)[N], uint32_t cp){
        for(size_t i = 0; i < N; i++) if(list[i] == cp) return true;
        return false;
    }

    int Measure(const Params& p, uint32_t cp){
        return p.measure ? p.measure(cp, p.ctx) : 8;
    }

    // ルビ「(かんじ)」の幅
    int RubyWidth(const Params& p, const char* s, size_t n){
        int w = Measure(p, '(') + Measure(p, ')');
        size_t i = 0;
        while(i < n){
            const uint8_t b = (uint8_t)s[i];
            uint32_t cp;
            int len;
            if(b < 0x80){ cp = b; len = 1; }
            else if(b < 0xE0){ cp = b & 0x1F; len = 2; }
            else if(b < 0xF0){ cp = b & 0x0F; len = 3; }
            else { cp = b & 0x07; len = 4; }
            for(int k = 1; k < len && i + k < n; k++) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
            i += len;
            w += Measure(p, cp);
        }
        return w;
    }
}

bool IsNoLineStart(uint32_t cp){ return InList(kNoStart, cp); }
bool IsKanji(uint32_t cp){
    return (cp >= 0x3400 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x3FFFF) ||
           cp == 0x3005 || cp == 0x3006 || cp == 0x3007 || cp == 0x3013;   // 々〆〇〓
}
bool IsNoLineEnd(uint32_t cp){ return InList(kNoEnd, cp); }

// ---------------------------------------------------------------------------
// Toc
// ---------------------------------------------------------------------------

void Toc::add(const char* text, size_t n, uint32_t pos){
    if(count_ >= kMax || n == 0) return;
    // 同じ位置を2回拾わない(索引を作り直すと同じ見出しをもう一度通る)
    for(int i = 0; i < count_; i++) if(entries_[i].pos == pos) return;
    // 位置の順に並べる
    int at = count_;
    while(at > 0 && entries_[at - 1].pos > pos) at--;
    for(int i = count_; i > at; i--) entries_[i] = entries_[i - 1];
    Entry& e = entries_[at];
    e.pos = pos;
    if(n >= (size_t)kTitleBytes) n = kTitleBytes - 1;
    while(n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80) n--;
    memcpy(e.title, text, n);
    e.title[n] = '\0';
    count_++;
}

void Toc::onQuoted(const char* text, size_t n, uint32_t pos){
    add(text, n, pos);
}

void Toc::onEnd(uint32_t end, Aozora::Reader& r){
    if(!capture_) return;
    capture_ = false;
    char buf[kTitleBytes];
    size_t n = 0;
    uint32_t pos = capture_pos_;
    Aozora::Token t;
    // 見出しの文字だけを拾い直す(ルビ・注記は捨てる)
    while(pos < end){
        r.next(pos, t);
        if(t.kind == Aozora::Tok::Eof || t.kind == Aozora::Tok::Newline) break;
        if(t.kind != Aozora::Tok::Char) continue;
        char u[4];
        const int m = Aozora::EncodeUtf8(t.cp, u);
        if(n + m >= sizeof(buf)) break;
        memcpy(buf + n, u, m);
        n += m;
    }
    // 頭の全角空白は落とす
    size_t from = 0;
    while(n - from >= 3 && memcmp(buf + from, "\xE3\x80\x80", 3) == 0) from += 3;
    add(buf + from, n - from, capture_pos_);
}

// ---------------------------------------------------------------------------
// LayoutPage
// ---------------------------------------------------------------------------

State LayoutPage(Aozora::Reader& r, const State& start, const Params& p, Page* out, bool* at_end, Toc* toc){
    uint32_t pos = start.pos;
    int block = start.block;
    int para = start.para;
    bool mid = (start.flags & kMid) != 0;
    uint32_t para_pos = pos;
    const int max_lines = p.lines < 1 ? 1 : (p.lines > kMaxLines ? kMaxLines : p.lines);
    const bool first_page = (pos <= r.info().body_start);

    if(out) out->count = 0;
    int lines = 0;
    bool eof = false;
    Aozora::Token t;

    while(lines < max_lines && !eof){
        Line* L = out ? &out->lines[lines] : nullptr;
        if(L){ L->len = 0; L->text[0] = '\0'; L->x = 0; }

        int indent = block + (mid ? para : 0);
        if(!mid) para = 0;
        int avail = p.width - indent * p.em;
        int x = 0;
        int bytes = 0;
        int chars = 0;
        // 最後に見つけた「ここで折ってよい所」(その前までをこの行に残す)。溢れた文字の前で折れない
        // (行頭禁則・開き括弧の後・ルビの親文字の途中・漢字の続き)ときはここまで戻る
        bool cand_valid = false;
        uint32_t cand_pos = 0;
        int cand_bytes = 0;
        int cand_x = 0;
        uint32_t prev_cp = 0;
        bool in_base = false;   // ｜の後、ルビまで(親文字の途中では折らない)
        int align = -1;     // 地付き(地から何字上げるか)
        bool para_end = false;
        bool page_break = false;

        for(;;){
            const uint32_t before = pos;
            r.next(pos, t);
            if(t.kind == Aozora::Tok::Eof){
                eof = true;
                para_end = true;
                break;
            }
            if(t.kind == Aozora::Tok::Newline){
                para_end = true;
                break;
            }
            if(t.kind == Aozora::Tok::Note){
                switch(t.note){
                    case Aozora::Note::PageBreak:
                        // ページの頭の改ページは何もしない
                        if(lines == 0 && chars == 0) continue;
                        page_break = true;
                        break;
                    case Aozora::Note::Indent:
                        if(chars == 0) para = t.num;
                        break;
                    case Aozora::Note::BlockIndent:
                        block = t.num;
                        break;
                    case Aozora::Note::BlockIndentEnd:
                        block = 0;
                        break;
                    case Aozora::Note::AlignBottom:
                        align = t.num;
                        break;
                    case Aozora::Note::Heading:
                        if(toc) toc->onQuoted(t.text, t.len, para_pos);
                        break;
                    case Aozora::Note::HeadingBegin:
                        if(toc) toc->onBegin(pos);
                        break;
                    case Aozora::Note::HeadingEnd:
                        if(toc) toc->onEnd(before, r);
                        break;
                    default:
                        break;
                }
                if(page_break) break;
                if(chars == 0){
                    indent = block + para;
                    avail = p.width - indent * p.em;
                }
                continue;
            }
            if(t.kind == Aozora::Tok::Ruby){
                if(!p.ruby || t.len == 0){
                    in_base = false;
                    continue;
                }
                const int w = RubyWidth(p, t.text, t.len);
                const int nb = t.len + 4;
                if(chars > 0 && (x + w > avail || bytes + nb >= kLineBytes)){
                    // ルビだけを次の行へ送らない(親文字ごと送る)
                    if(cand_valid){
                        pos = cand_pos;
                        bytes = cand_bytes;
                        x = cand_x;
                    }else{
                        pos = before;
                    }
                    break;
                }
                if(L){
                    char* d = L->text + bytes;
                    *d++ = kRubyOn;
                    *d++ = '(';
                    memcpy(d, t.text, t.len);
                    d += t.len;
                    *d++ = ')';
                    *d++ = kRubyOff;
                    *d = '\0';
                }
                x += w;
                bytes += nb;
                chars++;
                in_base = false;
                prev_cp = 0;    // ルビの後ろは(行頭禁則の文字でなければ)折ってよい
                continue;
            }
            if(t.kind == Aozora::Tok::RubyStart){
                if(chars > 0 && !in_base && !IsNoLineEnd(prev_cp)){
                    cand_valid = true;
                    cand_pos = before;
                    cand_bytes = bytes;
                    cand_x = x;
                }
                in_base = true;
                continue;
            }

            // 文字
            const uint32_t cp = t.cp;
            const int w = Measure(p, cp);
            char u[4];
            const int nb = Aozora::EncodeUtf8(cp, u);
            // この文字の前で折ってよいか
            const bool breakable = chars > 0 && !in_base && !IsNoLineStart(cp) && !IsNoLineEnd(prev_cp) &&
                                   !(IsKanji(cp) && IsKanji(prev_cp));
            if(chars > 0 && (x + w > avail || bytes + nb >= kLineBytes)){
                if(!breakable && cand_valid){
                    // 追い出し: 折ってよい所まで戻って、そこから次の行へ
                    pos = cand_pos;
                    bytes = cand_bytes;
                    x = cand_x;
                }else{
                    pos = before;
                }
                break;
            }
            if(breakable){
                cand_valid = true;
                cand_pos = before;
                cand_bytes = bytes;
                cand_x = x;
            }
            prev_cp = cp;
            if(L) memcpy(L->text + bytes, u, nb);
            x += w;
            bytes += nb;
            chars++;
        }

        // ページの頭の空行は飛ばす(本の最初のページ以外)
        const bool blank = (chars == 0);
        if(blank && lines == 0 && !first_page && para_end && !eof){
            mid = false;
            para = 0;
            para_pos = pos;
            continue;
        }
        if(!(blank && (page_break || (eof && lines > 0)))){
            if(L){
                L->len = (uint16_t)bytes;
                L->text[bytes] = '\0';
                if(align >= 0) L->x = (int16_t)(p.width - align * p.em - x);
                else L->x = (int16_t)(indent * p.em);
                if(L->x < 0) L->x = 0;
            }
            lines++;
        }
        if(para_end){
            mid = false;
            para = 0;
            para_pos = pos;
        }else if(!(page_break && blank)){
            mid = true;
        }
        if(page_break) break;
    }
    if(out) out->count = lines;

    State next;
    next.pos = pos;
    next.block = (int8_t)block;
    next.para = (int8_t)para;
    next.flags = mid ? kMid : 0;

    if(at_end){
        // 残りが空行と注記だけなら、このページで終わり
        bool end = eof;
        if(!end){
            uint32_t p2 = pos;
            for(;;){
                r.next(p2, t);
                if(t.kind == Aozora::Tok::Eof){ end = true; break; }
                if(t.kind == Aozora::Tok::Newline) continue;
                if(t.kind == Aozora::Tok::Note && t.note != Aozora::Note::Heading && t.note != Aozora::Note::HeadingBegin) continue;
                break;
            }
        }
        *at_end = end;
    }
    return next;
}

// ---------------------------------------------------------------------------
// PageIndex
// ---------------------------------------------------------------------------

void PageIndex::reset(const State& first){
    count_ = 0;
    stride_ = 1;
    pages_ = 0;
    cursor_ = first;
    done_ = false;
}

bool PageIndex::step(Aozora::Reader& r, const Params& p, Toc* toc, int n){
    while(n-- > 0 && !done_){
        if(pages_ % stride_ == 0){
            if(count_ >= kMaxMarks){
                // 1つおきに間引いて、間隔を倍にする
                for(int i = 0; i < count_ / 2; i++) marks_[i] = marks_[i * 2];
                count_ /= 2;
                stride_ *= 2;
            }
            if(pages_ % stride_ == 0) marks_[count_++] = cursor_;
        }
        bool end = false;
        const State nx = LayoutPage(r, cursor_, p, nullptr, &end, toc);
        pages_++;
        if(end || nx.pos <= cursor_.pos){
            done_ = true;
            cursor_.pos = r.size();
        }else{
            cursor_ = nx;
        }
    }
    return done_;
}

bool PageIndex::find(uint32_t pos, Aozora::Reader& r, const Params& p, int& page, State& start) const {
    if(count_ == 0 || !covers(pos)) return false;
    // pos 以下で一番後ろの印
    int lo = 0, hi = count_ - 1;
    while(lo < hi){
        const int mid = (lo + hi + 1) / 2;
        if(marks_[mid].pos <= pos) lo = mid;
        else hi = mid - 1;
    }
    State s = marks_[lo];
    int n = lo * stride_;
    for(;;){
        if(n + 1 >= pages_){
            break;
        }
        bool end = false;
        const State nx = LayoutPage(r, s, p, nullptr, &end, nullptr);
        if(end || nx.pos > pos) break;
        s = nx;
        n++;
    }
    page = n;
    start = s;
    return true;
}

bool PageIndex::startOf(int n, Aozora::Reader& r, const Params& p, State& out) const {
    if(n < 0 || n >= pages_ || count_ == 0) return false;
    int i = n / stride_;
    if(i >= count_) i = count_ - 1;
    State s = marks_[i];
    for(int k = i * stride_; k < n; k++){
        bool end = false;
        s = LayoutPage(r, s, p, nullptr, &end, nullptr);
    }
    out = s;
    return true;
}

}
