#include "apps/firmware/Fw_Image.hpp"
#include "apps/firmware/Fw_Keys.hpp"

#include <cstring>

#include "monocypher-ed25519.h"
#include "system/I18n_Functions.hpp"

// 見出しの並び(リトルエンディアン。script/firmware_tool.py と揃える)
//   0  magic[8]        "PICO-FW1"
//   8  u16 header_bytes (=128)
//  10  u16 format       (=1)
//  12  u32 flags        bit0 = 署名あり
//  16  board[16]        "rpipico2w"(NULで埋める)
//  32  u32 serial
//  36  u32 build_time
//  40  git[16]          ASCII(NULで埋める)
//  56  u32 image_bytes
//  60  u32 payload_bytes
//  64  u32 flash_base   (=0x10000000)
//  68  u32 fs_start
//  72  u32 fs_end
//  76  payload_sha256[32]
// 108  reserved[20]     0
namespace {
    uint32_t Le32(const uint8_t* p){
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    uint16_t Le16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }

    // NULで埋めた固定長の文字列を読む(印字できるASCIIだけ。NULの後ろは全部NUL)
    bool ReadText(const uint8_t* p, size_t n, char* out, bool allow_empty){
        size_t len = 0;
        while(len < n && p[len] != 0) len++;
        for(size_t i = len; i < n; i++) if(p[i] != 0) return false;
        if(len == 0 && !allow_empty) return false;
        for(size_t i = 0; i < len; i++){
            if(p[i] < 0x20 || p[i] > 0x7e) return false;
            out[i] = (char)p[i];
        }
        out[len] = '\0';
        return true;
    }

    bool AllZero(const uint8_t* p, size_t n){
        for(size_t i = 0; i < n; i++) if(p[i]) return false;
        return true;
    }
}

const char* FwImage::ErrorText(Error e){
    switch(e){
        case Error::None:       return TR("問題なし");
        case Error::BadMagic:   return TR("ファームの更新ファイルではありません");
        case Error::BadFormat:  return TR("更新ファイルの形式が違うか、壊れています");
        case Error::WrongBoard: return TR("別の基板向けのファームです");
        case Error::BadSize:    return TR("更新ファイルの大きさがおかしいです");
        case Error::FsMismatch: return TR("フラッシュの割り当てが今のファームと違います(USBで書き込んでください)");
        case Error::TooLarge:   return TR("更新ファイルが置き場に入りません");
    }
    return TR("不明な誤り");
}

const char* FwImage::SigText(Sig s){
    switch(s){
        case Sig::Trusted:    return TR("署名を確かめました");
        case Sig::Unsigned:   return TR("署名がありません");
        case Sig::UnknownKey: return TR("知らない鍵の署名です");
        case Sig::Bad:        return TR("署名が合いません(壊れているか、書き換えられています)");
    }
    return "?";
}

FwImage::Error FwImage::Parse(const uint8_t* b, size_t len, Header& h){
    h = Header{};
    if(!b || len < kHeaderBytes) return Error::BadFormat;
    if(memcmp(b, kMagic, sizeof(kMagic)) != 0) return Error::BadMagic;
    if(Le16(b + 8) != kHeaderBytes || Le16(b + 10) != kFormat) return Error::BadFormat;
    h.flags = Le32(b + 12);
    if(h.flags & ~kFlagSigned) return Error::BadFormat; // 知らない印(新しい形式を古い本体が黙って受けない)
    if(!ReadText(b + 16, 16, h.board, false)) return Error::BadFormat;
    h.serial = Le32(b + 32);
    h.build_time = Le32(b + 36);
    if(!ReadText(b + 40, 16, h.git, true)) return Error::BadFormat;
    h.image_bytes = Le32(b + 56);
    h.payload_bytes = Le32(b + 60);
    h.flash_base = Le32(b + 64);
    h.fs_start = Le32(b + 68);
    h.fs_end = Le32(b + 72);
    memcpy(h.payload_sha256, b + 76, 32);
    if(!AllZero(b + 108, 20)) return Error::BadFormat;
    return Error::None;
}

FwImage::Error FwImage::Check(const Header& h, uint32_t fs_start, uint32_t fs_end, uint32_t capacity){
    if(strcmp(h.board, kBoard) != 0) return Error::WrongBoard;
    if(h.flash_base != kFlashBase) return Error::BadFormat;
    if(h.fs_start != fs_start || h.fs_end != fs_end) return Error::FsMismatch;
    // 解いたイメージはアプリの領域(フラッシュの先頭〜LittleFSの手前)に収まらなければならない。
    // はみ出すと書き込みがLittleFS(=中身の置き場そのもの)を壊す
    if(fs_start <= kFlashBase) return Error::BadSize;
    if(h.image_bytes <= kAppOffset || h.image_bytes > fs_start - kFlashBase) return Error::BadSize;
    // gzipの最小(見出し10+末尾8)
    if(h.payload_bytes < 18) return Error::BadSize;
    if(h.payload_bytes > capacity) return Error::TooLarge;
    return Error::None;
}

FwImage::Sig FwImage::VerifySignature(const uint8_t* header, const uint8_t* sig,
                                      const uint8_t (*keys)[32], int count){
    const bool flagged = (Le32(header + 12) & kFlagSigned) != 0;
    const bool zero = AllZero(sig, kSigBytes);
    if(!flagged) return zero ? Sig::Unsigned : Sig::Bad;
    if(zero) return Sig::Bad;

    uint8_t msg[kSignMessageBytes];
    memcpy(msg, kSignContext, 16);
    memcpy(msg + 16, header, kHeaderBytes);
    for(int i = 0; i < count; i++){
        if(AllZero(keys[i], 32)) continue;
        if(crypto_ed25519_check(sig, keys[i], msg, sizeof(msg)) == 0) return Sig::Trusted;
    }
    // どの鍵でも合わない(鍵が1つも無い場合も)。別の人の鍵か、壊れた署名かは見分けられない
    return Sig::UnknownKey;
}

FwImage::Sig FwImage::VerifyBuiltin(const uint8_t* header, const uint8_t* sig){
    uint8_t keys[FwKeys::kBuiltinCount][32];
    for(int i = 0; i < FwKeys::kBuiltinCount; i++) memcpy(keys[i], FwKeys::kBuiltin[i].pub, 32);
    return VerifySignature(header, sig, keys, FwKeys::kBuiltinCount);
}

bool FwImage::HasBuiltinKey(){
    for(int i = 0; i < FwKeys::kBuiltinCount; i++){
        if(!AllZero(FwKeys::kBuiltin[i].pub, 32)) return true;
    }
    return false;
}
