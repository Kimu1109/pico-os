// LuaEngine の暗号API(pico.encrypt / decrypt / is_encrypted / hash / random_bytes と、
// pico.store_save/store_load の暗号化オプション)。暗号そのものは util/Secret_Aead.hpp。
// 鍵の決め方と限界はそちらのコメントを読むこと(パスワードを渡さない場合は「ファームの固定鍵+アプリ名」で、
// SDだけを盗まれる場合しか守れない)。

#include "lua/LuaEngine.hpp"

#include <cstring>
#include <string>

#include "util/Secret_Aead.hpp"
#include "util/Base64.hpp"

struct LuaEngineCrypto {
    static LuaEngine* Self(lua_State* L) {
        return *static_cast<LuaEngine**>(lua_getextraspace(L));   // registerFn() の説明参照
    }

    // 引数idxのパスワード(省略/nil/falseならnullptr)。文字列以外はエラー
    static const char* PasswordArg(lua_State* L, int idx) {
        if (lua_isnoneornil(L, idx)) return nullptr;
        if (lua_type(L, idx) != LUA_TSTRING) luaL_error(L, "パスワードは文字列で渡します");
        const char* p = lua_tostring(L, idx);
        return p[0] ? p : nullptr;
    }

    static int l_encrypt(lua_State* L) {
        LuaEngine* self = Self(L);
        size_t n = 0;
        const char* plain = luaL_checklstring(L, 1, &n);
        const char* password = PasswordArg(L, 2);
        if (n > SecretAead::kMaxPlainBytes) {
            lua_pushnil(L);
            lua_pushstring(L, SecretAead::ResultToStr(SecretAead::Result::TooBig));
            return 2;
        }
        SecretAead::Result r;
        {
            std::string out;
            r = SecretAead::Encrypt((const uint8_t*)plain, n, self->app_dir_.c_str(), password, out);
            if (r == SecretAead::Result::Ok) {
                lua_pushlstring(L, out.data(), out.size());
                return 1;
            }
        }
        lua_pushnil(L);
        lua_pushstring(L, SecretAead::ResultToStr(r));
        return 2;
    }

    static int l_decrypt(lua_State* L) {
        LuaEngine* self = Self(L);
        size_t n = 0;
        const char* text = luaL_checklstring(L, 1, &n);
        const char* password = PasswordArg(L, 2);
        SecretAead::Result r;
        {
            std::string out;
            r = SecretAead::Decrypt(text, n, self->app_dir_.c_str(), password, out);
            if (r == SecretAead::Result::Ok) {
                lua_pushlstring(L, out.data(), out.size());
                return 1;
            }
        }
        lua_pushnil(L);
        lua_pushstring(L, SecretAead::ResultToStr(r));
        return 2;
    }

    static int l_is_encrypted(lua_State* L) {
        size_t n = 0;
        const char* text = luaL_checklstring(L, 1, &n);
        lua_pushboolean(L, SecretAead::IsEncrypted(text, n));
        return 1;
    }

    // pico.hash(data [, key]) -> 64桁の16進(BLAKE2b-256)。keyがあれば鍵付き(MAC)
    static int l_hash(lua_State* L) {
        size_t n = 0, kn = 0;
        const char* data = luaL_checklstring(L, 1, &n);
        const char* key = luaL_optlstring(L, 2, nullptr, &kn);
        if (kn > 64) return luaL_error(L, "pico.hash: 鍵は64バイトまでです");
        uint8_t h[32];
        SecretAead::Blake2b(h, sizeof(h), (const uint8_t*)data, n, (const uint8_t*)key, kn);
        static const char* hex = "0123456789abcdef";
        char out[65];
        for (int i = 0; i < 32; i++) { out[i * 2] = hex[h[i] >> 4]; out[i * 2 + 1] = hex[h[i] & 15]; }
        out[64] = '\0';
        lua_pushlstring(L, out, 64);
        return 1;
    }

    // pico.random_bytes(n) -> n バイトの乱数(1〜1024)
    static int l_random_bytes(lua_State* L) {
        const lua_Integer n = luaL_checkinteger(L, 1);
        if (n < 1 || n > 1024) return luaL_error(L, "pico.random_bytes: 1〜1024バイトです");
        // 1KiBをスタックに置くとコア0の小さなスタックを圧迫するので、Luaの文字列バッファへ直接書く
        luaL_Buffer b;
        uint8_t* buf = (uint8_t*)luaL_buffinitsize(L, &b, (size_t)n);
        SecretAead::Random(buf, (size_t)n);
        luaL_pushresultsize(&b, (size_t)n);
        return 1;
    }
};

void LuaEngine::RegisterCryptoApi() {
    registerFn("encrypt", LuaEngineCrypto::l_encrypt);
    registerFn("decrypt", LuaEngineCrypto::l_decrypt);
    registerFn("is_encrypted", LuaEngineCrypto::l_is_encrypted);
    registerFn("hash", LuaEngineCrypto::l_hash);
    registerFn("random_bytes", LuaEngineCrypto::l_random_bytes);
}
