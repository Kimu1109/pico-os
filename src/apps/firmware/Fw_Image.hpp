#pragma once

#include <cstddef>
#include <cstdint>

// ファームの更新ファイル(.pfw)の見出しを読む係(形式は docs/FIRMWARE_UPDATE.md)。
//
//   [見出し 128バイト][署名 64バイト][中身 = firmware.bin を gzip したもの]
//
// 署名(Ed25519)は kSignContext(16バイト)+ 見出し128バイト に対するもので、見出しに中身の
// SHA-256 が入っているので、署名1つで全体を守る。中身は arduino-pico のOTAのローダー
// (フラッシュの先頭の約12KB)がそのまま読める gzip なので、本体では解かない。
//
// ソケットもSDも持たない(読み取り・検査・署名の確認だけ)ので、ホストテストで全部の経路を確かめられる。
// 作る側は script/firmware_tool.py。値を変えるときは両方を揃えること。
namespace FwImage {
    constexpr size_t kHeaderBytes = 128;
    constexpr size_t kSigBytes    = 64;
    constexpr size_t kPrefixBytes = kHeaderBytes + kSigBytes; // 中身の始まり
    constexpr uint16_t kFormat    = 1;
    constexpr char kMagic[8]      = {'P','I','C','O','-','F','W','1'};
    constexpr char kSignContext[16] = "pico-os-fw-v1\0\0"; // 16バイト(終端のNULまで)
    constexpr size_t kSignMessageBytes = 16 + kHeaderBytes;
    constexpr uint32_t kFlagSigned = 1u << 0;
    constexpr uint32_t kFlashBase  = 0x10000000u;
    // アプリの始まり(OTAのローダーの後ろ)。これより小さいイメージはあり得ない
    constexpr uint32_t kAppOffset  = 0x3000u;
    constexpr const char* kBoard   = "rpipico2w";

    struct Header {
        uint32_t flags = 0;
        char board[17] = {};       // 16バイト+終端
        uint32_t serial = 0;       // 通し番号(git rev-list --count HEAD)
        uint32_t build_time = 0;   // UNIX時刻(参考)
        char git[17] = {};         // 短いコミットのハッシュ(末尾の'+'は手元の変更あり)
        uint32_t image_bytes = 0;  // 解いた後の大きさ(firmware.bin)
        uint32_t payload_bytes = 0;// gzipの大きさ
        uint32_t flash_base = 0;
        uint32_t fs_start = 0;     // そのファームのLittleFSの範囲(今のファームと同じでなければ書けない)
        uint32_t fs_end = 0;
        uint8_t payload_sha256[32] = {};

        bool signedFlag() const { return (flags & kFlagSigned) != 0; }
    };

    enum class Error : uint8_t {
        None,
        BadMagic,       // 更新ファイルではない
        BadFormat,      // 版が違う・見出しが壊れている
        WrongBoard,     // 別の基板向け
        BadSize,        // 大きさがおかしい
        FsMismatch,     // LittleFSの範囲が今のファームと違う(USBで書き直す必要がある)
        TooLarge,       // 置き場(LittleFS)に入らない
    };
    const char* ErrorText(Error e);

    // 128バイトを読む(形だけ。今の本体との突き合わせは Check())
    Error Parse(const uint8_t* bytes, size_t len, Header& out);

    // 今の本体と突き合わせる。capacity = 置き場に入る中身の大きさ
    Error Check(const Header& h, uint32_t fs_start, uint32_t fs_end, uint32_t capacity);

    enum class Sig : uint8_t {
        Trusted,     // 焼き込みの鍵で確かめられた
        Unsigned,    // 署名なし
        UnknownKey,  // 署名はあるが、どの鍵でも確かめられない(鍵が登録されていない・別の人の鍵)
        Bad,         // 署名の印と中身が合わない(壊れている)
    };
    const char* SigText(Sig s);

    // header = 128バイトの生の見出し, sig = 64バイト。keys は32バイトずつ count 個(全部0の鍵は飛ばす)
    Sig VerifySignature(const uint8_t* header, const uint8_t* sig, const uint8_t (*keys)[32], int count);
    // 焼き込みの鍵(Fw_Keys.hpp)で確かめる
    Sig VerifyBuiltin(const uint8_t* header, const uint8_t* sig);
    // 焼き込みの鍵が1つでも登録されているか
    bool HasBuiltinKey();
}
