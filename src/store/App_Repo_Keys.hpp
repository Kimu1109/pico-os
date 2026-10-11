#pragma once

#include <cstdint>

// アプリストアのカタログ(catalog.tsv)の署名に使う公開鍵(Ed25519、32バイト)のうち、
// ファームに焼き込んで信頼するもの。
//
// 鍵を作るのは script/app_repo_keygen.py(秘密鍵は手元のPCにだけ置く。リポジトリにも
// 配布先のサーバにも置かないこと)。表示された1行をここへ足す。
//
// ここが空でも、SDの /sys/store.cfg に `key = <公開鍵の16進64桁>` を書けば信頼される
// (SDへ書ける人はアプリを直接置けるので、SDの鍵を信頼しても守りは弱くならない)。
namespace AppRepoKeys {
    struct Key { uint8_t pub[32]; };

    // 例: { { 0x12, 0x34, ... } },
    inline constexpr Key kBuiltin[] = {
        { { 0xcd, 0xae, 0x7b, 0xa2, 0x9d, 0x76, 0x14, 0x3c, 0xb1, 0x0e, 0xe4, 0xc5, 0xbf, 0x4c, 0x9c, 0x78, 0xc3, 0xa5, 0x5d, 0xfd, 0x5d, 0x6c, 0xd0, 0xdf, 0x25, 0xff, 0x0c, 0xba, 0xca, 0xd3, 0x0c, 0x5d } },
    };
    inline constexpr int kBuiltinCount = sizeof(kBuiltin) / sizeof(kBuiltin[0]);
}
