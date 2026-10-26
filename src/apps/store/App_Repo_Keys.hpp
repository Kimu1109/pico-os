#pragma once

#include <cstdint>

// アプリストアのカタログ(catalog.tsv)の署名に使う公開鍵(Ed25519、32バイト)のうち、
// ファームに焼き込んで信頼するもの。
//
// 鍵を作るのは script/build_app_repo.py keygen(秘密鍵は手元のPCにだけ置く。リポジトリにも
// 配布先のサーバにも置かないこと)。表示された1行をここへ足す。
//
// ここが空でも、SDの /sys/store.cfg に `key = <公開鍵の16進64桁>` を書けば信頼される
// (SDへ書ける人はアプリを直接置けるので、SDの鍵を信頼しても守りは弱くならない)。
namespace AppRepoKeys {
    struct Key { uint8_t pub[32]; };

    // 例: { { 0x12, 0x34, ... } },
    // d7ab6e22… は投稿用リポジトリ kimu1109/pico-os-apps の CI のカタログ鍵(Environment「catalog」の Secret。
    // その README「管理者の準備」)。鍵を替えたら、ここと pico-os-apps の変数 CATALOG_PUBKEY を揃える。
    // 移す前に手元で sign した目録(pico-os の store/)の鍵 cdae7ba2… は外した(古いファームだけが読むため)。
    // 公式アプリの作者の鍵(files.sig)は今も cdae7ba2… だが、そちらは端末が覚える鍵で、ここには要らない。
    inline constexpr Key kBuiltin[] = {
        { { 0xd7, 0xab, 0x6e, 0x22, 0xfa, 0x88, 0x0c, 0xd1, 0x56, 0x56, 0xec, 0xc1, 0xc3, 0xb2, 0x34, 0x0c, 0xdb, 0xe2, 0xa2, 0x7d, 0x28, 0x0f, 0xa4, 0xb8, 0x22, 0x3f, 0x8b, 0xab, 0x38, 0x03, 0x86, 0xff } },
    };
    inline constexpr int kBuiltinCount = sizeof(kBuiltin) / sizeof(kBuiltin[0]);
}
