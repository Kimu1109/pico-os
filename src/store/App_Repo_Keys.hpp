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
        { { 0 } }, // 空の配列は作れないので全部0の1件を置く(全部0は鍵として扱わない)
    };
    inline constexpr int kBuiltinCount = sizeof(kBuiltin) / sizeof(kBuiltin[0]);
}
