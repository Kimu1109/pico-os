#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// SHA-1(FIPS 180-4)。**WebSocketの開始の確認(Sec-WebSocket-Accept)だけに使う。**
// RFC 6455 がこの計算を決めているので使うだけで、署名・鍵の導出には使わないこと(衝突が作れる)。
// 確保はしない(状態は約100バイト)。
class Sha1 {
    public:
        static constexpr size_t kDigestBytes = 20;

        Sha1(){ reset(); }

        void reset(){
            h_[0] = 0x67452301; h_[1] = 0xEFCDAB89; h_[2] = 0x98BADCFE;
            h_[3] = 0x10325476; h_[4] = 0xC3D2E1F0;
            total_ = 0;
            used_ = 0;
        }

        void update(const void* data, size_t len){
            const uint8_t* p = (const uint8_t*)data;
            total_ += len;
            while(len > 0){
                size_t n = 64 - used_;
                if(n > len) n = len;
                memcpy(buf_ + used_, p, n);
                used_ += n;
                p += n;
                len -= n;
                if(used_ == 64){
                    block(buf_);
                    used_ = 0;
                }
            }
        }

        void finish(uint8_t out[kDigestBytes]){
            const uint64_t bits = total_ * 8;
            const uint8_t pad = 0x80;
            update(&pad, 1);
            const uint8_t zero = 0;
            while(used_ != 56) update(&zero, 1);
            uint8_t len[8];
            for(int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
            update(len, 8);
            for(int i = 0; i < 5; i++){
                out[i * 4 + 0] = (uint8_t)(h_[i] >> 24);
                out[i * 4 + 1] = (uint8_t)(h_[i] >> 16);
                out[i * 4 + 2] = (uint8_t)(h_[i] >> 8);
                out[i * 4 + 3] = (uint8_t)(h_[i]);
            }
        }

    private:
        uint32_t h_[5];
        uint64_t total_;
        size_t used_;
        uint8_t buf_[64];

        static uint32_t rol(uint32_t x, int n){ return (x << n) | (x >> (32 - n)); }

        void block(const uint8_t* b){
            uint32_t w[80];
            for(int i = 0; i < 16; i++){
                w[i] = ((uint32_t)b[i * 4] << 24) | ((uint32_t)b[i * 4 + 1] << 16)
                     | ((uint32_t)b[i * 4 + 2] << 8) | (uint32_t)b[i * 4 + 3];
            }
            for(int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            uint32_t a = h_[0], bb = h_[1], c = h_[2], d = h_[3], e = h_[4];
            for(int i = 0; i < 80; i++){
                uint32_t f, k;
                if(i < 20){ f = (bb & c) | (~bb & d); k = 0x5A827999; }
                else if(i < 40){ f = bb ^ c ^ d; k = 0x6ED9EBA1; }
                else if(i < 60){ f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; }
                else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
                const uint32_t t = rol(a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol(bb, 30); bb = a; a = t;
            }
            h_[0] += a; h_[1] += bb; h_[2] += c; h_[3] += d; h_[4] += e;
        }
};
