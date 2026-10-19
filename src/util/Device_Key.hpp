// src/util/Device_Key.hpp
//
// 端末ごとの鍵(RP2350のOTPに焼いた32バイト)から作った、用途ごとの鍵の置き場。
// 中身を入れるのは DeviceKeyFunctions(src/services/DeviceKey_Functions)だけで、ここは
// 「今使える鍵があるか」と鍵そのものを持つだけ(描画にもSDにも暗号ライブラリにも依存しない)。
// Secret_Cipher.hpp・Secret_Aead・Lock_Functions はこれを見て、鍵があれば端末の鍵で、
// 無ければ今までどおりファームの固定鍵(PICO_Secret::kKey)で暗号化する。
//
// ホストテストは DeviceKeyFunctions をリンクしなくても、Install() を直接呼べば
// 「鍵がある」状態を作れる(Install()しなければ今までの動きのまま)。
//
// 守れるもの(正直に): SDカードだけを盗まれた/見られた場合に、enc3:・lock.cfgのv2・Luaの
// 暗号(パスワード無し)を読まれない。本体ごと持ち去られた場合は守れない(Secure Bootを使って
// いないので、BOOTSELで別のファームを書けばOTPを読める)。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace DeviceKey {

    // 用途ごとの鍵(どれも端末の鍵から BLAKE2b で別々に作る。DeviceKeyFunctions::Derive())
    inline bool available = false;
    inline uint32_t cipher_key[4] = {};   // Secret_Cipher.hpp の enc3:(XTEA 128bit)
    inline uint8_t aead_key[32] = {};     // Secret_Aead の version 3(パスワード無し)
    inline uint8_t pin_key[32] = {};      // Lock_Functions の v2(PINのハッシュの鍵)

    inline void Install(const uint32_t cipher[4], const uint8_t aead[32], const uint8_t pin[32]){
        memcpy(cipher_key, cipher, sizeof(cipher_key));
        memcpy(aead_key, aead, sizeof(aead_key));
        memcpy(pin_key, pin, sizeof(pin_key));
        available = true;
    }

    inline void Clear(){
        volatile uint8_t* p = (volatile uint8_t*)cipher_key;
        for(size_t i = 0; i < sizeof(cipher_key); i++) p[i] = 0;
        p = aead_key;
        for(size_t i = 0; i < sizeof(aead_key); i++) p[i] = 0;
        p = pin_key;
        for(size_t i = 0; i < sizeof(pin_key); i++) p[i] = 0;
        available = false;
    }
}
