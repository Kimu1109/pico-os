#pragma once

#include <cstddef>
#include <cstdint>
#include "board/HwConfig.hpp"

// HwConfig(board/HwConfig.hpp)の名前つきの読み書き・検査・内蔵フラッシュに置く形への変換。
// ハードウェアに触らない(ホストテスト hw_config_test で確かめる)。
//
// 内蔵フラッシュに置く形(先頭から):
//   'P' 'H' 'W' 'C' / 版(1バイト) / 予備(1バイト) / 本文の長さ(2バイト、LE) / 本文のCRC32(4バイト、LE) / 本文
// 本文は "lcd.cs=17\n" の並びで、既定と違うキーだけを書く(既定が変わったファームでは新しい既定に従う)。
// 知らないキーは読み飛ばす(新しいファームで足したキーを古いファームが読んでも壊れない)。
namespace HwConfigCodec {
    enum class Kind : uint8_t { Pin, Hz, Rotation, Offset, Tri };
    // フォールバックの単位。液晶とタッチは1枚の基板なので一緒に扱う
    enum class Group : uint8_t { Lcd, Sd };

    struct Field {
        const char* key;
        Kind kind;
        Group group;
        bool optional_pin; // ピンで -1(つながっていない)を許す
        const char* help; // 使える値("hwcfg keys" に出す)
    };

    size_t FieldCount();
    const Field& FieldAt(size_t i);
    // 名前からFieldの番号を引く。無ければ -1
    int Find(const char* key);

    int32_t Get(const HwConfig& c, size_t i);
    void Put(HwConfig& c, size_t i, int32_t v);

    // 値の文字を読む(形と大まかな範囲だけ。組み合わせの検査は Validate())。
    //   Pin: 0〜29 か -1/none   Hz: 数字に k/M(Hz)を付けてよい   Rotation: 0〜7
    //   Offset: -480〜480   Tri: auto/-1・on/1/true・off/0/false
    bool ParseValue(Kind kind, const char* text, int32_t& out);
    void FormatValue(Kind kind, int32_t v, char* out, size_t cap);

    // key=value の1つを当てる。だめなら false と理由(静的な文字列)
    bool Set(HwConfig& c, const char* key, const char* value, const char** why);

    // 組み合わせまで含めて使える値か。だめなら false と理由を why へ
    bool Validate(const HwConfig& c, char* why, size_t cap);

    bool SameGroup(const HwConfig& a, const HwConfig& b, Group g);
    void CopyGroup(HwConfig& dst, const HwConfig& src, Group g);

    uint32_t Crc32(const uint8_t* p, size_t len);

    constexpr size_t kHeaderBytes = 12;
    constexpr uint8_t kVersion = 1;

    // 置く形にする。書いたバイト数(入らなければ0)
    size_t Encode(const HwConfig& c, uint8_t* out, size_t cap);

    enum class DecodeResult : uint8_t {
        Ok,      // 読めた(out は既定+書いてあったキー)
        Empty,   // 消したまま(0xFF)か印が無い。out は既定
        Broken,  // 印はあるが長さ・CRC・値が合わない。out は既定
    };
    DecodeResult Decode(const uint8_t* data, size_t len, HwConfig& out);
}
