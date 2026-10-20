#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// アプリストアの配布元(docs/APP_REPO.md)の中身を読む係。
// ソケットもSDも持たない(行の解釈・名前の検査・署名の確認・版の比較だけ)ので、
// ホストテストで全部の経路を確かめられる。通信とインストールは apps/store/App_Store。
namespace AppRepo {
    // ---- 上限(docs/APP_REPO.md「制限値」と揃えること) ----
    constexpr int    kMaxEntries      = 40;           // カタログから読むアプリの数
    constexpr size_t kMaxCatalogBytes = 32 * 1024;
    constexpr size_t kMaxLineBytes    = 512;           // catalog.tsv / files.tsv の1行
    constexpr int    kMaxFiles        = 64;            // 1アプリのファイル数
    constexpr size_t kMaxFilesBytes   = 8 * 1024;      // files.tsv の大きさ
    constexpr uint32_t kMaxFileBytes  = 1024UL * 1024UL;
    constexpr uint32_t kMaxAppBytes   = 4UL * 1024UL * 1024UL;
    constexpr size_t kMaxPathLen      = 95;            // files.tsv のパス
    constexpr int    kMaxPathDepth    = 4;
    constexpr size_t kMaxSigBytes     = 256;           // catalog.sig

    constexpr const char* kCatalogMagic = "pico-os-apps";
    constexpr int kCatalogFormat = 1;
    // 署名する中身 = kSignContext(16バイト。終端のNULまで) + SHA-256(catalog.tsv)
    constexpr char kSignContext[16] = "pico-os-apps-v1";
    constexpr size_t kSignMessageBytes = 16 + 32;

    // 権限のビット(catalog.tsv の perms 列 / app.cfg の permission_*)
    enum Perm : uint8_t {
        PermNetwork = 1 << 0,  // network       / permission_network
        PermNotify  = 1 << 1,  // notify        / permission_notify
        PermSd      = 1 << 2,  // sd            / permission_sd_outside_app_dir
        PermBackground = 1 << 3, // background  / permission_background(閉じている間も定期的に動く)
        PermMicrophone = 1 << 4, // microphone  / permission_microphone(マイクで聞く・録音する)
    };

    struct Entry {
        FixedString<PICO_STR_S> id;          // 英小文字・数字・'-'・'_'(24バイトまで)
        FixedString<PICO_STR_S> version;     // 数字と'.'
        FixedString<PICO_STR_M> dir;         // /lua/apps/ の下のディレクトリ名(UTF-8)
        FixedString<PICO_STR_M> name;        // 表示名(切り詰めてよい)
        uint32_t bytes = 0;                  // 全部のファイルの合計
        uint8_t files_sha256[32] = {};       // files.tsv のSHA-256
        uint8_t perms = 0;
        FixedString<PICO_STR_LL> description; // 切り詰めてよい
    };

    struct FileEntry {
        FixedString<kMaxPathLen + 1> path;   // アプリのディレクトリからの相対パス
        uint32_t bytes = 0;
        uint8_t sha256[32] = {};
    };

    // ---- 名前の検査 ----
    bool ValidId(const char* s);
    bool ValidVersion(const char* s);
    // /lua/apps/ の下のディレクトリ名として使えるか(区切り・FATで使えない文字・制御文字・
    // 先頭の"."・末尾の空白と"."を断る)
    bool ValidDirName(const char* s);
    // files.tsv のパス([A-Za-z0-9._-] の区切り。"." で始まる区切り・空の区切りは不可)
    bool ValidRelPath(const char* s);

    // 版の比較(数字を"."で区切って左から数として比べる。足りない所は0)。a<b なら負
    int CompareVersion(const char* a, const char* b);

    // ---- 16進 ----
    bool HexDecode(const char* hex, size_t hex_len, uint8_t* out, size_t out_len);
    void HexEncode(const uint8_t* in, size_t len, char* out); // out は len*2+1

    // ---- 権限 ----
    // "network,notify" / "-" → ビット。知らない名前があればfalse(新しい権限を古い本体が黙って許さないため)
    bool ParsePerms(const char* s, uint8_t& out);
    // 表示用 "ネットワーク・通知"(無ければ"なし")
    void PermsText(uint8_t perms, FixedString<PICO_STR_L>& out);

    // 空行・'#'で始まる行(どの表でも読み飛ばす)
    bool IsSkipLine(const char* line);

    // ---- catalog.tsv ----
    // 行を読む関数はどれも line をその場で書き換える(タブをNULにする)
    // 1行目 "pico-os-apps<TAB>1<TAB><serial>"
    bool ParseCatalogHeader(char* line, uint32_t& serial);
    // 2行目以降の1件。空行・'#'で始まる行は false(skip=true)
    bool ParseCatalogLine(char* line, Entry& out, bool& skip);

    // ---- files.tsv ----
    bool ParseFilesLine(char* line, FileEntry& out, bool& skip);

    // ---- catalog.sig ----
    // "ed25519<TAB or 空白><公開鍵64桁><TAB or 空白><署名128桁>"
    bool ParseSignature(const char* text, uint8_t pubkey[32], uint8_t sig[64]);
    // catalog.tsv のSHA-256 に対する署名を確かめる
    bool VerifyCatalog(const uint8_t catalog_sha256[32], const uint8_t pubkey[32], const uint8_t sig[64]);

    // ---- 信頼する鍵 ----
    // ファームに焼き込んだ鍵(apps/store/App_Repo_Keys.hpp)と、/sys/store.cfg の key = 行
    // (呼び出し側が渡す)のどちらかに一致すれば信頼する
    bool IsBuiltinKey(const uint8_t pubkey[32]);
}
