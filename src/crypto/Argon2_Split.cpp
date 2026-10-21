// Monocypher 4.0.2(BSD-2-Clause / CC0)の crypto_argon2() を写したもの。説明は Argon2_Split.hpp
#include "crypto/Argon2_Split.hpp"

#include <cstdlib>

namespace {
    using u8 = uint8_t;
    using u32 = uint32_t;
    using u64 = uint64_t;

    struct Blk { u64 a[128]; };

    u64 Rotr64(u64 x, u64 n) { return (x >> n) ^ (x << (64 - n)); }

    void Store32Le(u8 out[4], u32 in) {
        out[0] = (u8)in; out[1] = (u8)(in >> 8); out[2] = (u8)(in >> 16); out[3] = (u8)(in >> 24);
    }

    // バイト列(リトルエンディアン)を同じ場所の64bit語へ並べ直す/その逆
    void BytesToWords(Blk* b) {
        const u8* s = (const u8*)b->a;
        for (int i = 0; i < 128; i++) {
            u64 v = 0;
            for (int k = 7; k >= 0; k--) v = (v << 8) | s[i * 8 + k];
            b->a[i] = v;
        }
    }
    void WordsToBytes(Blk* b) {
        u8* d = (u8*)b->a;
        for (int i = 0; i < 128; i++) {
            const u64 v = b->a[i];
            for (int k = 0; k < 8; k++) d[i * 8 + k] = (u8)(v >> (k * 8));
        }
    }

    void Wipe(Blk* b) {
        volatile u64* p = b->a;
        for (int i = 0; i < 128; i++) p[i] = 0;
    }

    void BlakeUpdate32(crypto_blake2b_ctx* ctx, u32 input) {
        u8 buf[4];
        Store32Le(buf, input);
        crypto_blake2b_update(ctx, buf, 4);
        crypto_wipe(buf, sizeof(buf));
    }

    void BlakeUpdate32Buf(crypto_blake2b_ctx* ctx, const u8* buf, u32 size) {
        BlakeUpdate32(ctx, size);
        crypto_blake2b_update(ctx, buf, size);
    }

    void CopyBlock(Blk* o, const Blk* in) { for (int i = 0; i < 128; i++) o->a[i] = in->a[i]; }
    void XorBlock(Blk* o, const Blk* in) { for (int i = 0; i < 128; i++) o->a[i] ^= in->a[i]; }

    // 長さに上限の無いハッシュ(H')。digestとinputは重なってよい(inputを先に読み切る)
    void ExtendedHash(u8* digest, u32 digest_size, const u8* input, u32 input_size) {
        crypto_blake2b_ctx ctx;
        crypto_blake2b_init(&ctx, digest_size < 64 ? digest_size : 64);
        BlakeUpdate32(&ctx, digest_size);
        crypto_blake2b_update(&ctx, input, input_size);
        crypto_blake2b_final(&ctx, digest);

        if (digest_size > 64) {
            u32 r = (u32)(((u64)digest_size + 31) >> 5) - 2;
            u32 i = 1;
            u32 in = 0;
            u32 out = 32;
            while (i < r) {
                crypto_blake2b(digest + out, 64, digest + in, 64);
                i += 1;
                in += 32;
                out += 32;
            }
            crypto_blake2b(digest + out, digest_size - (32 * r), digest + in, 64);
        }
    }

#define LSB(x) ((u64)(u32)x)
#define G(a, b, c, d)	\
	a += b + ((LSB(a) * LSB(b)) << 1);  d ^= a;  d = Rotr64(d, 32); \
	c += d + ((LSB(c) * LSB(d)) << 1);  b ^= c;  b = Rotr64(b, 24); \
	a += b + ((LSB(a) * LSB(b)) << 1);  d ^= a;  d = Rotr64(d, 16); \
	c += d + ((LSB(c) * LSB(d)) << 1);  b ^= c;  b = Rotr64(b, 63)
#define ROUND(v0,  v1,  v2,  v3,  v4,  v5,  v6,  v7,	\
              v8,  v9, v10, v11, v12, v13, v14, v15)	\
	G(v0, v4,  v8, v12);  G(v1, v5,  v9, v13); \
	G(v2, v6, v10, v14);  G(v3, v7, v11, v15); \
	G(v0, v5, v10, v15);  G(v1, v6, v11, v12); \
	G(v2, v7,  v8, v13);  G(v3, v4,  v9, v14)

    void GRounds(Blk* b) {
        for (int i = 0; i < 128; i += 16) {
            ROUND(b->a[i   ], b->a[i+ 1], b->a[i+ 2], b->a[i+ 3],
                  b->a[i+ 4], b->a[i+ 5], b->a[i+ 6], b->a[i+ 7],
                  b->a[i+ 8], b->a[i+ 9], b->a[i+10], b->a[i+11],
                  b->a[i+12], b->a[i+13], b->a[i+14], b->a[i+15]);
        }
        for (int i = 0; i < 16; i += 2) {
            ROUND(b->a[i   ], b->a[i+ 1], b->a[i+ 16], b->a[i+ 17],
                  b->a[i+32], b->a[i+33], b->a[i+ 48], b->a[i+ 49],
                  b->a[i+64], b->a[i+65], b->a[i+ 80], b->a[i+ 81],
                  b->a[i+96], b->a[i+97], b->a[i+112], b->a[i+113]);
        }
    }

#undef ROUND
#undef G
#undef LSB

    void FreeBlocks(Blk** blocks, u32 n) {
        for (u32 i = 0; i < n; i++) {
            if (!blocks[i]) continue;
            Wipe(blocks[i]);
            free(blocks[i]);
        }
        free(blocks);
    }
}

bool Argon2Split::Hash(uint8_t* hash, uint32_t hash_size,
                       const crypto_argon2_config& config,
                       const crypto_argon2_inputs& inputs,
                       const crypto_argon2_extras& extras) {
    const u32 segment_size = config.nb_blocks / config.nb_lanes / 4;
    const u32 lane_size = segment_size * 4;
    const u32 nb_blocks = lane_size * config.nb_lanes; // 切り捨て(Monocypherと同じ)
    if (nb_blocks == 0) return false;

    // ブロックごとに確保する(連続した大きな塊を要求しない)
    Blk** blocks = (Blk**)calloc(nb_blocks, sizeof(Blk*));
    if (!blocks) return false;
    for (u32 i = 0; i < nb_blocks; i++) {
        blocks[i] = (Blk*)malloc(sizeof(Blk));
        if (!blocks[i]) {
            FreeBlocks(blocks, nb_blocks);
            return false;
        }
    }

    {
        u8 initial_hash[72]; // 64バイト+後で足す2語
        crypto_blake2b_ctx ctx;
        crypto_blake2b_init(&ctx, 64);
        BlakeUpdate32(&ctx, config.nb_lanes);
        BlakeUpdate32(&ctx, hash_size);
        BlakeUpdate32(&ctx, config.nb_blocks);
        BlakeUpdate32(&ctx, config.nb_passes);
        BlakeUpdate32(&ctx, 0x13);             // 版
        BlakeUpdate32(&ctx, config.algorithm);
        BlakeUpdate32Buf(&ctx, inputs.pass, inputs.pass_size);
        BlakeUpdate32Buf(&ctx, inputs.salt, inputs.salt_size);
        BlakeUpdate32Buf(&ctx, extras.key, extras.key_size);
        BlakeUpdate32Buf(&ctx, extras.ad, extras.ad_size);
        crypto_blake2b_final(&ctx, initial_hash);

        // 各レーンの最初の2ブロック(H'の出力をブロック自身へ書いてから語へ並べ直す)
        for (u32 l = 0; l < config.nb_lanes; l++) {
            for (u32 i = 0; i < 2; i++) {
                Store32Le(initial_hash + 64, i);
                Store32Le(initial_hash + 68, l);
                Blk* b = blocks[l * lane_size + i];
                ExtendedHash((u8*)b->a, 1024, initial_hash, 72);
                BytesToWords(b);
            }
        }
        crypto_wipe(initial_hash, sizeof(initial_hash));
    }

    int constant_time = config.algorithm != CRYPTO_ARGON2_D;

    Blk tmp;
    for (u32 pass = 0; pass < config.nb_passes; pass++) {
        for (u32 slice = 0; slice < 4; slice++) {
            const u32 pass_offset = pass == 0 && slice == 0 ? 2 : 0;
            const u32 slice_offset = slice * segment_size;

            if (slice == 2 && config.algorithm == CRYPTO_ARGON2_ID) {
                constant_time = 0;
            }

            for (u32 segment = 0; segment < config.nb_lanes; segment++) {
                Blk index_block;
                u32 index_ctr = 1;
                for (u32 block = pass_offset; block < segment_size; block++) {
                    const u32 segment_start = segment * lane_size + slice_offset;
                    Blk* current = blocks[segment_start + block];
                    Blk* previous =
                        block == 0 && slice_offset == 0
                        ? blocks[segment_start + lane_size - 1]
                        : blocks[segment_start + block - 1];

                    u64 index_seed;
                    if (constant_time) {
                        if (block == pass_offset || (block % 128) == 0) {
                            for (int i = 0; i < 128; i++) index_block.a[i] = 0;
                            index_block.a[0] = pass;
                            index_block.a[1] = segment;
                            index_block.a[2] = slice;
                            index_block.a[3] = nb_blocks;
                            index_block.a[4] = config.nb_passes;
                            index_block.a[5] = config.algorithm;
                            index_block.a[6] = index_ctr;
                            index_ctr++;

                            CopyBlock(&tmp, &index_block);
                            GRounds(&index_block);
                            XorBlock(&index_block, &tmp);
                            CopyBlock(&tmp, &index_block);
                            GRounds(&index_block);
                            XorBlock(&index_block, &tmp);
                        }
                        index_seed = index_block.a[block % 128];
                    } else {
                        index_seed = previous->a[0];
                    }

                    const u32 next_slice = ((slice + 1) % 4) * segment_size;
                    const u32 window_start = pass == 0 ? 0 : next_slice;
                    const u32 nb_segments = pass == 0 ? slice : 3;
                    const u64 lane =
                        pass == 0 && slice == 0
                        ? segment
                        : (index_seed >> 32) % config.nb_lanes;
                    const u32 window_size =
                        nb_segments * segment_size +
                        (lane == segment ? block - 1 :
                         block == 0 ? (u32)-1 : 0);

                    const u64 j1 = index_seed & 0xffffffff;
                    const u64 x = (j1 * j1) >> 32;
                    const u64 y = (window_size * x) >> 32;
                    const u64 z = (window_size - 1) - y;
                    const u64 ref = (window_start + z) % lane_size;
                    const u32 index = (u32)lane * lane_size + (u32)ref;
                    Blk* reference = blocks[index];

                    CopyBlock(&tmp, previous);
                    XorBlock(&tmp, reference);
                    if (pass == 0) { CopyBlock(current, &tmp); }
                    else           { XorBlock(current, &tmp); }
                    GRounds(&tmp);
                    XorBlock(current, &tmp);
                }
                Wipe(&index_block);
            }
        }
    }
    Wipe(&tmp);

    // 各レーンの最後のブロックをXORで畳む
    Blk* last_block = blocks[lane_size - 1];
    for (u32 lane = 1; lane < config.nb_lanes; lane++) {
        Blk* next_block = blocks[lane * lane_size + lane_size - 1];
        XorBlock(next_block, last_block);
        last_block = next_block;
    }

    // 最後のブロックをその場でバイト列にしてH'へ通す
    WordsToBytes(last_block);
    ExtendedHash(hash, hash_size, (const u8*)last_block->a, 1024);

    FreeBlocks(blocks, nb_blocks);
    return true;
}
