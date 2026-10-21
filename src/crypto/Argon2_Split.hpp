// src/crypto/Argon2_Split.hpp
//
// Argon2(RFC 9106)の作業領域を1KiBのブロックごとに別々に確保する版。出力はMonocypherの
// crypto_argon2() とビット単位で同じ(argon2_split_test で突き合わせている)。
//
// なぜ自前で持つか: crypto_argon2() は作業領域(Luaの暗号は64KiB)を1つの連続した塊で受け取る。
// 実機は空きヒープが全部で80〜100KB程度しかないので、一度その64KiBの穴へ長生きする確保が入ると
// 連続64KiBが二度と取れなくなり、再起動するまで pico.encrypt/decrypt(パスワード付き)が
// 「メモリが足りません」で失敗し続けた。1KiBずつなら穴が散らばっていても取れる。
// 計算はMonocypher 4.0.2 の crypto_argon2() をそのまま写し、ブロックの引き方だけを変えている。
// スタックを節約するため、最初のブロックの元と最後のブロックの直列化はブロック自身の上で行う
// (Monocypherはそれぞれ1KiBの配列をスタックに置く)。

#pragma once

#include <cstddef>
#include <cstdint>

#include "monocypher.h"

namespace Argon2Split {

    // Monocypherの crypto_argon2() と同じ引数(作業領域を除く)。作業領域を確保できなければ false
    //(hashには何も書かない)。使い終わった作業領域は消してから返す
    bool Hash(uint8_t* hash, uint32_t hash_size,
              const crypto_argon2_config& config,
              const crypto_argon2_inputs& inputs,
              const crypto_argon2_extras& extras);
}
