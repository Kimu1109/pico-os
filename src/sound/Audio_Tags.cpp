#include "sound/Audio_Tags.hpp"
#include "apps/mail/Mail_Charset.hpp"
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

bool AudioTagReader::Read(const char* path, AudioTags& out){
    out = AudioTags();
    if(!path || !OSData::SD_usable) return false;
    const size_t n = strlen(path);
    if(n < 4 || strcasecmp(path + n - 4, ".mp3") != 0) return false;
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return false;
    ReadId3v2(f, out);
    if(out.title.empty() || out.artist.empty() || out.album.empty() || out.track == 0) ReadId3v1(f, out);
    f.close();
    return !out.empty();
}
