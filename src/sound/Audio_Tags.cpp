#include "sound/Audio_Tags.hpp"
#include "apps/mail/Mail_Charset.hpp"
#include "sound/Audio_Format.hpp"
#include "OS_Data.hpp"

#include <SdFat.h>
#include <cstring>
#include <strings.h>

namespace {
    // 1つの項目の中身を読む最大(これより長い曲名は、ここで切って縮める)
    constexpr size_t kMaxFrameRead = 256;

    uint32_t Syncsafe(const uint8_t* p){
        return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) | ((uint32_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
    }
    uint32_t BE32(const uint8_t* p){ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
    uint32_t BE24(const uint8_t* p){ return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }

    // 書いた結果の前後の空白を落として out へ(UTF-8を途中で切らない BufSink の結果をそのまま)
    void Finish(const char* buf, size_t len, FixedString<PICO_STR_L>& out){
        size_t s = 0, e = len;
        while(s < e && (buf[s] == ' ' || buf[s] == '\t')) s++;
        while(e > s && (buf[e - 1] == ' ' || buf[e - 1] == '\t' || buf[e - 1] == '\0')) e--;
        if(!out.assign(buf + s, e - s)) out.clear();
    }

    // Shift_JIS として最後まで正しく、2バイト文字を1つ以上含むか
    bool LooksSjis(const uint8_t* p, size_t n){
        bool wide = false;
        for(size_t i = 0; i < n; i++){
            const uint8_t b = p[i];
            if(b < 0x80) continue;
            if(b >= 0xA1 && b <= 0xDF) continue;    //半角カナ
            const bool lead = (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
            if(!lead || i + 1 >= n) return false;
            const uint8_t t = p[i + 1];
            if(!((t >= 0x40 && t <= 0x7E) || (t >= 0x80 && t <= 0xFC))) return false;
            wide = true;
            i++;
        }
        return wide;
    }

    // UTF-16 を UTF-8 へ。big=ビッグエンディアン。0x0000 で終わる
    void Utf16(const uint8_t* p, size_t n, bool big, MailCharset::Sink& sink){
        for(size_t i = 0; i + 1 < n; i += 2){
            uint32_t c = big ? ((uint32_t)p[i] << 8 | p[i + 1]) : ((uint32_t)p[i + 1] << 8 | p[i]);
            if(c == 0) break;
            if(c >= 0xD800 && c <= 0xDBFF && i + 3 < n){
                const uint32_t lo = big ? ((uint32_t)p[i + 2] << 8 | p[i + 3]) : ((uint32_t)p[i + 3] << 8 | p[i + 2]);
                if(lo >= 0xDC00 && lo <= 0xDFFF){
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }else{
                    c = MailCharset::kReplacement;
                }
            }else if(c >= 0xD800 && c <= 0xDFFF){
                c = MailCharset::kReplacement;
            }
            char u[4];
            const int len = MailCharset::EncodeUtf8(c, u);
            sink.put(u, (size_t)len);
        }
    }

    // v2.4 のフレームの非同期化を戻す(0xFF 0x00 → 0xFF)。戻した長さを返す
    size_t Resync(uint8_t* p, size_t n){
        size_t w = 0;
        for(size_t r = 0; r < n; r++){
            p[w++] = p[r];
            if(p[r] == 0xFF && r + 1 < n && p[r + 1] == 0x00) r++;
        }
        return w;
    }

    enum Field : uint8_t { kTitle, kArtist, kAlbum, kTrack, kNone };

    Field FieldOf(const char* id, bool v22){
        if(v22){
            if(memcmp(id, "TT2", 3) == 0) return kTitle;
            if(memcmp(id, "TP1", 3) == 0) return kArtist;
            if(memcmp(id, "TAL", 3) == 0) return kAlbum;
            if(memcmp(id, "TRK", 3) == 0) return kTrack;
            return kNone;
        }
        if(memcmp(id, "TIT2", 4) == 0) return kTitle;
        if(memcmp(id, "TPE1", 4) == 0) return kArtist;
        if(memcmp(id, "TALB", 4) == 0) return kAlbum;
        if(memcmp(id, "TRCK", 4) == 0) return kTrack;
        return kNone;
    }

    void Store(Field f, const uint8_t* data, size_t n, AudioTags& out){
        FixedString<PICO_STR_L> s;
        AudioTagReader::DecodeText(data, n, s);
        if(s.empty()) return;
        switch(f){
            case kTitle:  out.title = s; break;
            case kArtist: out.artist = s; break;
            case kAlbum:  out.album = s; break;
            case kTrack:  out.track = AudioTagReader::ParseTrackNumber(s.c_str()); break;
            default: break;
        }
    }

    // 頭の ID3v2。読めた項目は out へ
    void ReadId3v2(FsFile& f, AudioTags& out){
        uint8_t h[10];
        if(!f.seek(0) || f.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) return;
        const uint8_t ver = h[3];
        if(ver < 2 || ver > 4) return;
        if((h[6] | h[7] | h[8] | h[9]) & 0x80) return;
        const uint8_t flags = h[5];
        //v2.2/v2.3 のタグ全体の非同期化は、フレームの大きさの数え方が変わるので読まない
        if(ver < 4 && (flags & 0x80)) return;
        const uint32_t end = 10 + Syncsafe(h + 6);
        uint32_t pos = 10;
        if(ver >= 3 && (flags & 0x40)){
            uint8_t e[4];
            if(f.read(e, 4) != 4) return;
            pos += ver == 4 ? Syncsafe(e) : 4 + BE32(e);
        }
        const bool v22 = ver == 2;
        const uint32_t hdr = v22 ? 6 : 10;
        uint8_t need = 0x0F;    //要る4つ(ビット = Field)
        uint8_t buf[kMaxFrameRead];
        while(need && pos + hdr <= end){
            uint8_t fh[10];
            if(!f.seek(pos) || f.read(fh, hdr) != (int)hdr) return;
            if(fh[0] == 0) return;      //埋め草(パディング)
            const uint32_t size = v22 ? BE24(fh + 3) : (ver == 4 ? Syncsafe(fh + 4) : BE32(fh + 4));
            if(size == 0 || pos + hdr + size > end) return;
            const Field field = FieldOf((const char*)fh, v22);
            if(field != kNone && (need & (1u << field))){
                const uint8_t fmt = v22 ? 0 : fh[9];
                //v2.4: 圧縮0x08・暗号化0x04は読まない。非同期化0x02は戻す。長さの印0x01は4バイト飛ばす
                //v2.3: 圧縮0x80・暗号化0x40は読まない
                const bool skip = ver == 4 ? (fmt & 0x0C) != 0 : (ver == 3 && (fmt & 0xC0) != 0);
                if(!skip){
                    size_t n = size < sizeof(buf) ? size : sizeof(buf);
                    if(f.read(buf, n) != (int)n) return;
                    size_t off = 0;
                    if(ver == 4 && (fmt & 0x01) && n >= 4) off = 4;
                    if(ver == 4 && (fmt & 0x02)) n = off + Resync(buf + off, n - off);
                    Store(field, buf + off, n - off, out);
                    need &= (uint8_t)~(1u << field);
                }
            }
            pos += hdr + size;
        }
    }

    // 終わりの ID3v1(128バイト)。まだ無い項目だけ埋める
    void ReadId3v1(FsFile& f, AudioTags& out){
        const uint32_t size = (uint32_t)f.fileSize();
        if(size < 128) return;
        uint8_t t[128];
        if(!f.seek(size - 128) || f.read(t, 128) != 128 || memcmp(t, "TAG", 3) != 0) return;
        if(out.title.empty())  AudioTagReader::DecodeLegacy(t + 3, 30, out.title);
        if(out.artist.empty()) AudioTagReader::DecodeLegacy(t + 33, 30, out.artist);
        if(out.album.empty())  AudioTagReader::DecodeLegacy(t + 63, 30, out.album);
        //v1.1: 注釈の29バイト目が0なら30バイト目が曲番号
        if(out.track == 0 && t[125] == 0 && t[126] != 0) out.track = t[126];
    }
}

void AudioTagReader::DecodeLegacy(const uint8_t* p, size_t n, FixedString<PICO_STR_L>& out){
    //終端の0より後ろは見ない
    size_t len = 0;
    while(len < n && p[len] != 0) len++;
    char buf[PICO_STR_L];
    MailCharset::BufSink sink(buf, sizeof(buf));
    MailCharset::Decoder dec;
    dec.begin(LooksSjis(p, len) ? MailCharset::Charset::ShiftJis : MailCharset::Charset::Latin1);
    dec.feed(p, len, sink);
    dec.finish(sink);
    Finish(buf, sink.length(), out);
}

void AudioTagReader::DecodeText(const uint8_t* p, size_t n, FixedString<PICO_STR_L>& out){
    out.clear();
    if(n < 1) return;
    const uint8_t enc = p[0];
    p++; n--;
    if(enc == 0){ DecodeLegacy(p, n, out); return; }
    char buf[PICO_STR_L];
    MailCharset::BufSink sink(buf, sizeof(buf));
    if(enc == 1){
        //BOMで向きを決める(無ければリトルエンディアン)
        bool big = false;
        if(n >= 2 && p[0] == 0xFE && p[1] == 0xFF){ big = true; p += 2; n -= 2; }
        else if(n >= 2 && p[0] == 0xFF && p[1] == 0xFE){ p += 2; n -= 2; }
        Utf16(p, n, big, sink);
    }else if(enc == 2){
        Utf16(p, n, true, sink);
    }else if(enc == 3){
        size_t len = 0;
        while(len < n && p[len] != 0) len++;
        MailCharset::Decoder dec;
        dec.begin(MailCharset::Charset::Utf8);
        dec.feed(p, len, sink);
        dec.finish(sink);
    }else{
        return;
    }
    Finish(buf, sink.length(), out);
}

uint16_t AudioTagReader::ParseTrackNumber(const char* s){
    if(!s) return 0;
    while(*s == ' ') s++;
    uint32_t v = 0;
    int digits = 0;
    while(*s >= '0' && *s <= '9' && digits < 5){ v = v * 10 + (uint32_t)(*s - '0'); s++; digits++; }
    return (digits == 0 || v > 65535) ? 0 : (uint16_t)v;
}

void AudioTagReader::DecodeVorbisComment(const uint8_t* p, size_t n, AudioTags& out){
    //「キー=値」。キーは大小を区別しない。値はUTF-8
    const uint8_t* eq = static_cast<const uint8_t*>(memchr(p, '=', n));
    if(!eq) return;
    const size_t klen = (size_t)(eq - p);
    const uint8_t* val = eq + 1;
    const size_t vlen = n - klen - 1;
    auto is = [&](const char* key){ return strlen(key) == klen && strncasecmp((const char*)p, key, klen) == 0; };
    FixedString<PICO_STR_L>* dst = nullptr;
    if(is("TITLE"))        dst = &out.title;
    else if(is("ARTIST"))  dst = &out.artist;
    else if(is("ALBUM"))   dst = &out.album;
    else if(is("TRACKNUMBER")){
        char num[16];
        const size_t m = vlen < sizeof(num) - 1 ? vlen : sizeof(num) - 1;
        memcpy(num, val, m);
        num[m] = '\0';
        const uint16_t t = ParseTrackNumber(num);
        if(t) out.track = t;
        return;
    }
    if(!dst) return;
    char buf[kMaxFrameRead + 1];
    size_t len = vlen < kMaxFrameRead ? vlen : kMaxFrameRead;
    //UTF-8の途中で切れていたら、その文字を落とす
    while(len > 0 && len < vlen && (val[len] & 0xC0) == 0x80) len--;
    memcpy(buf, val, len);
    Finish(buf, len, *dst);
}

namespace {
    uint32_t LE32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

    // FLAC の VORBIS_COMMENT(メタデータブロックの種類4)。頭の ID3v2 は飛ばす。アルバムアート(PICTURE)などは飛ばして読まない
    void ReadFlac(FsFile& f, AudioTags& out){
        uint8_t h[10];
        uint32_t pos = 0;
        for(int i = 0; i < 4; i++){
            if(!f.seek(pos) || f.read(h, 10) != 10) return;
            if(memcmp(h, "ID3", 3) != 0) break;
            if((h[6] | h[7] | h[8] | h[9]) & 0x80) return;
            pos += 10 + Syncsafe(h + 6) + ((h[5] & 0x10) ? 10 : 0);
        }
        if(!f.seek(pos) || f.read(h, 4) != 4 || memcmp(h, "fLaC", 4) != 0) return;
        pos += 4;
        for(int i = 0; i < 64; i++){
            if(!f.seek(pos) || f.read(h, 4) != 4) return;
            const bool last = (h[0] & 0x80) != 0;
            const uint8_t type = h[0] & 0x7F;
            const uint32_t len = BE24(h + 1);
            const uint32_t body = pos + 4;
            if(type == 4 && len >= 8){
                uint8_t w[4];
                uint32_t at = body;
                if(f.read(w, 4) != 4) return;
                at += 4 + LE32(w);                      //ベンダー文字列を飛ばす
                if(at + 4 > body + len || !f.seek(at) || f.read(w, 4) != 4) return;
                const uint32_t count = LE32(w);
                at += 4;
                for(uint32_t c = 0; c < count && c < 256; c++){
                    if(at + 4 > body + len || !f.seek(at) || f.read(w, 4) != 4) return;
                    const uint32_t clen = LE32(w);
                    at += 4;
                    if(clen > body + len - at) return;
                    if(clen > 0 && clen <= 2 * kMaxFrameRead){
                        uint8_t buf[2 * kMaxFrameRead];
                        if(f.read(buf, clen) != (int)clen) return;
                        AudioTagReader::DecodeVorbisComment(buf, clen, out);
                    }
                    at += clen;
                }
                return;
            }
            pos = body + len;
            if(last) return;
        }
    }
}

namespace {
    // MP4の atom の見出し(大きさ4+種類4。大きさが1なら続く8バイトが本当の大きさ)。body=中身の始め、end=atomの終わり(limit で切る)
    bool AtomAt(FsFile& f, uint32_t pos, uint32_t limit, uint32_t& body, uint32_t& end, char type[4]){
        uint8_t h[16];
        if(pos + 8 > limit || !f.seek(pos) || f.read(h, 8) != 8) return false;
        uint64_t size = BE32(h);
        memcpy(type, h + 4, 4);
        uint32_t hdr = 8;
        if(size == 1){
            if(pos + 16 > limit || f.read(h + 8, 8) != 8) return false;
            size = ((uint64_t)BE32(h + 8) << 32) | BE32(h + 12);
            hdr = 16;
        }else if(size == 0){
            size = limit - pos;
        }
        if(size < hdr) return false;
        body = pos + hdr;
        end = (pos + size > limit) ? limit : (uint32_t)(pos + size);
        return true;
    }

    bool FindAtom(FsFile& f, uint32_t from, uint32_t limit, const char* type, uint32_t& body, uint32_t& end){
        uint32_t pos = from;
        for(int i = 0; i < 256 && pos + 8 <= limit; i++){
            uint32_t b, e;
            char t[4];
            if(!AtomAt(f, pos, limit, b, e, t)) return false;
            if(memcmp(t, type, 4) == 0){ body = b; end = e; return true; }
            if(e <= pos) return false;
            pos = e;
        }
        return false;
    }

    // iTunes形式のタグ: moov → udta → meta → ilst の ©nam(曲名)・©ART・©alb・trkn(曲番号)。それぞれ中の data atom を読む
    void ReadM4a(FsFile& f, AudioTags& out){
        const uint32_t size = (uint32_t)f.fileSize();
        uint32_t b, e;
        if(!FindAtom(f, 0, size, "moov", b, e)) return;
        if(!FindAtom(f, b, e, "udta", b, e)) return;
        if(!FindAtom(f, b + 0, e, "meta", b, e)) return;
        if(!FindAtom(f, b + 4, e, "ilst", b, e)) return;      //meta は版/フラグ4バイトの後ろに子が続く
        const uint32_t ilst_end = e;
        uint32_t pos = b;
        for(int i = 0; i < 64 && pos + 8 <= ilst_end; i++){
            uint32_t ib, ie;
            char t[4];
            if(!AtomAt(f, pos, ilst_end, ib, ie, t) || ie <= pos) return;
            const char* key = nullptr;
            bool track = false;
            if(memcmp(t, "\xA9" "nam", 4) == 0)      key = "TITLE=";
            else if(memcmp(t, "\xA9" "ART", 4) == 0) key = "ARTIST=";
            else if(memcmp(t, "\xA9" "alb", 4) == 0) key = "ALBUM=";
            else if(memcmp(t, "trkn", 4) == 0)     track = true;
            if(key || track){
                uint32_t db, de;
                if(FindAtom(f, ib, ie, "data", db, de) && de >= db + 8){
                    //data: 種類(4)・地域(4)の後ろが中身
                    uint32_t len = de - db - 8;
                    if(track){
                        uint8_t tr[8];
                        if(len >= 4 && f.seek(db + 8) && f.read(tr, 4) == 4){
                            const uint16_t n = (uint16_t)((tr[2] << 8) | tr[3]);
                            if(n) out.track = n;
                        }
                    }else{
                        if(len > kMaxFrameRead) len = kMaxFrameRead;
                        uint8_t buf[kMaxFrameRead + 8];
                        const size_t klen = strlen(key);
                        memcpy(buf, key, klen);
                        if(f.seek(db + 8) && f.read(buf + klen, len) == (int)len){
                            AudioTagReader::DecodeVorbisComment(buf, klen + len, out);
                        }
                    }
                }
            }
            pos = ie;
        }
    }
}

bool AudioTagReader::Read(const char* path, AudioTags& out){
    out = AudioTags();
    if(!path || !OSData::SD_usable) return false;
    const bool mp3 = AudioFormat::IsMp3(path);
    const bool flac = AudioFormat::IsFlac(path);
    const bool m4a = AudioFormat::IsM4a(path);
    if(!mp3 && !flac && !m4a) return false;
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return false;
    if(flac){
        ReadFlac(f, out);
    }else if(m4a){
        ReadM4a(f, out);
    }else{
        ReadId3v2(f, out);
        if(out.title.empty() || out.artist.empty() || out.album.empty() || out.track == 0) ReadId3v1(f, out);
    }
    f.close();
    return !out.empty();
}
