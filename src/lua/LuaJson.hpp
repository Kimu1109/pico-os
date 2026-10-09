#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>

#include "lua.hpp"

// LuaとJSONの相互変換(pico.json_decode / pico.json_encode と、シーン間で渡す引数・結果・状態の直列化)。
//
// ヘッダだけで完結させてある(LuaEngine.cppが取り込む)。ホストテストのビルド一覧に足さずに済ませるため。
//
// 設計:
//   - Decode はLuaの値を直接組み立てる(中間の木を作らない)。確保はLuaのアロケータだけ=Luaの予算に乗る。
//     メモリ不足はlongjmpで抜けるので、この中ではデストラクタを持つC++の値を使わない。
//   - Encode は逆にLuaの確保を一切しない関数(lua_next/lua_rawgeti/lua_tolstring(文字列のみ))だけで読み、
//     結果は std::string に溜める。longjmpが起きないのでデストラクタを持つ値を使ってよい。
//   - 入れ子は kMaxDepth 段まで(再帰で使うスタックを一定に抑える。循環参照の検出も兼ねる)
//   - null は nil へ(オブジェクトのキーは消え、配列は穴になる)。keep_null=true なら NullValue() の
//     目印(lightuserdata)にする。Encode は nil を null として書く(配列の途中のnilは書けない)
//   - 空のテーブルは "[]" になる(Luaでは配列と区別できないため)
namespace LuaJson {

    constexpr int kMaxDepth = 16;
    constexpr size_t kMaxInput = 64 * 1024;
    constexpr size_t kMaxOutput = 64 * 1024;

    // pico.json_null の実体(lightuserdataの指す先。中身は使わない)
    inline void* NullValue() {
        static char marker = 0;
        return &marker;
    }

    // ------------------------------------------------------------------ Encode

    inline void EscapeString(std::string& out, const char* s, size_t n) {
        out.push_back('"');
        for (size_t i = 0; i < n; i++) {
            const unsigned char c = (unsigned char)s[i];
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back((char)c);
                    }
            }
        }
        out.push_back('"');
    }

    // idx の値をJSONで out に追記する。失敗時は err に理由(静的文字列)を入れて false。
    // 失敗したときLuaスタックには何か残っていることがある(呼び出し側が lua_settop で戻す)
    inline bool EncodeValue(lua_State* L, int idx, std::string& out, int depth, const char*& err) {
        idx = lua_absindex(L, idx);
        if (out.size() > kMaxOutput) { err = "出力が大きすぎます"; return false; }

        switch (lua_type(L, idx)) {
            case LUA_TNIL:
                out += "null";
                return true;
            case LUA_TBOOLEAN:
                out += lua_toboolean(L, idx) ? "true" : "false";
                return true;
            case LUA_TNUMBER: {
                char buf[40];
                if (lua_isinteger(L, idx)) {
                    snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, idx));
                } else {
                    const double d = (double)lua_tonumber(L, idx);
                    if (!std::isfinite(d)) { err = "NaN/無限大はJSONにできません"; return false; }
                    // 単精度(LUA_32BITS)なら7桁(Luaの tostring と同じ。0.1 を 0.10000000149012 と書かない)
                    snprintf(buf, sizeof(buf), sizeof(lua_Number) == 4 ? "%.7g" : "%.14g", d);
                }
                out += buf;
                return true;
            }
            case LUA_TSTRING: {
                size_t n = 0;
                const char* s = lua_tolstring(L, idx, &n); // 文字列型なので変換(確保)は起きない
                EscapeString(out, s, n);
                return true;
            }
            case LUA_TLIGHTUSERDATA:
                if (lua_touserdata(L, idx) == NullValue()) { out += "null"; return true; }
                err = "JSONにできない値です(userdata)";
                return false;
            case LUA_TTABLE:
                break;
            default:
                err = "JSONにできない値です(function/thread/userdata)";
                return false;
        }

        if (depth >= kMaxDepth) { err = "入れ子が深すぎます(循環参照の可能性)"; return false; }
        if (!lua_checkstack(L, 6)) { err = "スタックが足りません"; return false; }

        // 配列か(キーが正の整数だけ)を調べる。穴(nil)があってもキーの最大値が個数の2倍+8以内なら
        // 配列とみなし、穴は null で書く({1,nil,3} → [1,null,3])。まばらなもの({[1000]=1})は配列にしない
        size_t n = 0;
        bool is_array = true;
        size_t count = 0;
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            count++;
            if (!lua_isinteger(L, -2)) {
                is_array = false;
            } else {
                const lua_Integer k = lua_tointeger(L, -2);
                if (k < 1) is_array = false;
                else if ((size_t)k > n) n = (size_t)k;
            }
            lua_pop(L, 1);
        }
        if (n > count * 2 + 8) is_array = false;

        if (count == 0) { out += "[]"; return true; }

        if (is_array) {
            out.push_back('[');
            for (size_t i = 1; i <= n; i++) {
                if (i > 1) out.push_back(',');
                lua_rawgeti(L, idx, (lua_Integer)i);
                const bool ok = EncodeValue(L, -1, out, depth + 1, err);
                lua_pop(L, 1);
                if (!ok) return false;
            }
            out.push_back(']');
            return true;
        }

        out.push_back('{');
        bool first = true;
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
            // キーは文字列だけ(数値のまま lua_tolstring すると、キー自体が文字列へ書き換わって lua_next が壊れる)
            if (lua_type(L, -2) != LUA_TSTRING) { err = "オブジェクトのキーは文字列だけです"; return false; }
            if (!first) out.push_back(',');
            first = false;
            size_t kn = 0;
            const char* ks = lua_tolstring(L, -2, &kn);
            EscapeString(out, ks, kn);
            out.push_back(':');
            if (!EncodeValue(L, -1, out, depth + 1, err)) return false;
            lua_pop(L, 1);
        }
        out.push_back('}');
        return true;
    }

    // ------------------------------------------------------------------ Decode

    struct Parser {
        const char* p;
        const char* end;
        const char* start;
        const char* err = nullptr;
        bool keep_null = false;
    };

    inline bool Fail(Parser& ps, const char* why) {
        if (!ps.err) ps.err = why;
        return false;
    }

    inline void SkipWs(Parser& ps) {
        while (ps.p < ps.end && (*ps.p == ' ' || *ps.p == '\t' || *ps.p == '\n' || *ps.p == '\r')) ps.p++;
    }

    inline int HexVal(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    inline bool Hex4(const char* s, const char* end, uint32_t& out) {
        if (end - s < 4) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            const int h = HexVal(s[i]);
            if (h < 0) return false;
            v = (v << 4) | (uint32_t)h;
        }
        out = v;
        return true;
    }

    inline size_t PutUtf8(char* dst, uint32_t cp) {
        if (cp < 0x80) { dst[0] = (char)cp; return 1; }
        if (cp < 0x800) {
            dst[0] = (char)(0xC0 | (cp >> 6));
            dst[1] = (char)(0x80 | (cp & 0x3F));
            return 2;
        }
        if (cp < 0x10000) {
            dst[0] = (char)(0xE0 | (cp >> 12));
            dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[2] = (char)(0x80 | (cp & 0x3F));
            return 3;
        }
        dst[0] = (char)(0xF0 | (cp >> 18));
        dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }

    // ps.p が開き引用符の次。文字列をLuaスタックに積む
    inline bool ParseString(lua_State* L, Parser& ps) {
        // 閉じ引用符までの生の長さ(エスケープは必ず縮むので、これだけ確保すれば書き出しが溢れない)
        const char* q = ps.p;
        while (q < ps.end && *q != '"') {
            if ((unsigned char)*q < 0x20) return Fail(ps, "文字列に制御文字があります");
            if (*q == '\\') {
                q++;
                if (q >= ps.end) return Fail(ps, "文字列が閉じていません");
            }
            q++;
        }
        if (q >= ps.end) return Fail(ps, "文字列が閉じていません");
        const size_t raw = (size_t)(q - ps.p);

        luaL_Buffer b;
        char* dst = luaL_buffinitsize(L, &b, raw ? raw : 1);
        size_t n = 0;
        const char* s = ps.p;
        while (s < q) {
            if (*s != '\\') { dst[n++] = *s++; continue; }
            s++;
            const char e = *s++;
            switch (e) {
                case '"':  dst[n++] = '"'; break;
                case '\\': dst[n++] = '\\'; break;
                case '/':  dst[n++] = '/'; break;
                case 'b':  dst[n++] = '\b'; break;
                case 'f':  dst[n++] = '\f'; break;
                case 'n':  dst[n++] = '\n'; break;
                case 'r':  dst[n++] = '\r'; break;
                case 't':  dst[n++] = '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!Hex4(s, q, cp)) {
                        luaL_pushresultsize(&b, 0);
                        lua_pop(L, 1);
                        return Fail(ps, "\\u の後ろが不正です");
                    }
                    s += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t lo = 0;
                        if (q - s >= 6 && s[0] == '\\' && s[1] == 'u' && Hex4(s + 2, q, lo)
                            && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            s += 6;
                        } else {
                            cp = 0xFFFD; // 対になっていない上位サロゲート
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    n += PutUtf8(dst + n, cp);
                    break;
                }
                default:
                    luaL_pushresultsize(&b, 0);
                    lua_pop(L, 1);
                    return Fail(ps, "不正なエスケープです");
            }
        }
        luaL_pushresultsize(&b, n);
        ps.p = q + 1;
        return true;
    }

    inline bool ParseNumber(lua_State* L, Parser& ps) {
        const char* s = ps.p;
        const char* q = s;
        bool is_float = false;
        if (q < ps.end && *q == '-') q++;
        const char* digits = q;
        while (q < ps.end && *q >= '0' && *q <= '9') q++;
        if (q == digits) return Fail(ps, "数値が不正です");
        if (*digits == '0' && q - digits > 1) return Fail(ps, "数値が不正です(先頭の0)");
        if (q < ps.end && *q == '.') {
            is_float = true;
            q++;
            const char* f = q;
            while (q < ps.end && *q >= '0' && *q <= '9') q++;
            if (q == f) return Fail(ps, "数値が不正です(小数点の後)");
        }
        if (q < ps.end && (*q == 'e' || *q == 'E')) {
            is_float = true;
            q++;
            if (q < ps.end && (*q == '+' || *q == '-')) q++;
            const char* x = q;
            while (q < ps.end && *q >= '0' && *q <= '9') q++;
            if (q == x) return Fail(ps, "数値が不正です(指数)");
        }
        const size_t len = (size_t)(q - s);
        if (len >= 40) return Fail(ps, "数値が長すぎます");
        char buf[40];
        memcpy(buf, s, len);
        buf[len] = '\0';
        ps.p = q;
        if (!is_float) {
            errno = 0;
            char* e = nullptr;
            const long long v = strtoll(buf, &e, 10);
            // Luaの整数(LUA_32BITS なので32bit)の範囲を超えた整数は浮動小数点数にする
            if (errno == 0 && v >= (long long)LUA_MININTEGER && v <= (long long)LUA_MAXINTEGER) {
                lua_pushinteger(L, (lua_Integer)v);
                return true;
            }
        }
        lua_pushnumber(L, (lua_Number)strtod(buf, nullptr));
        return true;
    }

    inline bool ParseValue(lua_State* L, Parser& ps, int depth) {
        SkipWs(ps);
        if (ps.p >= ps.end) return Fail(ps, "値がありません");
        if (!lua_checkstack(L, 8)) return Fail(ps, "スタックが足りません");

        const char c = *ps.p;
        if (c == '"') {
            ps.p++;
            return ParseString(L, ps);
        }
        if (c == '{') {
            if (depth >= kMaxDepth) return Fail(ps, "入れ子が深すぎます");
            ps.p++;
            lua_createtable(L, 0, 4);
            SkipWs(ps);
            if (ps.p < ps.end && *ps.p == '}') { ps.p++; return true; }
            for (;;) {
                SkipWs(ps);
                if (ps.p >= ps.end || *ps.p != '"') return Fail(ps, "キー(文字列)がありません");
                ps.p++;
                if (!ParseString(L, ps)) return false;
                SkipWs(ps);
                if (ps.p >= ps.end || *ps.p != ':') return Fail(ps, "':' がありません");
                ps.p++;
                if (!ParseValue(L, ps, depth + 1)) return false;
                if (lua_isnil(L, -1)) lua_pop(L, 2);   // null はキーごと省く
                else lua_rawset(L, -3);
                SkipWs(ps);
                if (ps.p >= ps.end) return Fail(ps, "オブジェクトが閉じていません");
                if (*ps.p == ',') { ps.p++; continue; }
                if (*ps.p == '}') { ps.p++; return true; }
                return Fail(ps, "',' か '}' がありません");
            }
        }
        if (c == '[') {
            if (depth >= kMaxDepth) return Fail(ps, "入れ子が深すぎます");
            ps.p++;
            lua_createtable(L, 4, 0);
            SkipWs(ps);
            if (ps.p < ps.end && *ps.p == ']') { ps.p++; return true; }
            lua_Integer i = 1;
            for (;;) {
                if (!ParseValue(L, ps, depth + 1)) return false;
                if (lua_isnil(L, -1)) lua_pop(L, 1);   // null は穴にする
                else lua_rawseti(L, -2, i);
                i++;
                SkipWs(ps);
                if (ps.p >= ps.end) return Fail(ps, "配列が閉じていません");
                if (*ps.p == ',') { ps.p++; continue; }
                if (*ps.p == ']') { ps.p++; return true; }
                return Fail(ps, "',' か ']' がありません");
            }
        }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(L, ps);

        auto lit = [&](const char* word) {
            const size_t n = strlen(word);
            if ((size_t)(ps.end - ps.p) >= n && memcmp(ps.p, word, n) == 0) { ps.p += n; return true; }
            return false;
        };
        if (lit("true"))  { lua_pushboolean(L, 1); return true; }
        if (lit("false")) { lua_pushboolean(L, 0); return true; }
        if (lit("null")) {
            if (ps.keep_null) lua_pushlightuserdata(L, NullValue());
            else lua_pushnil(L);
            return true;
        }
        return Fail(ps, "不正な文字です");
    }

    // s[0..len) をJSONとして読み、値を1つ積んで true。失敗は false(積まない)。
    // err_pos に誤りの位置(バイト)を入れる
    inline bool Decode(lua_State* L, const char* s, size_t len, bool keep_null,
                       const char*& err, size_t& err_pos) {
        err = nullptr;
        err_pos = 0;
        if (len > kMaxInput) { err = "入力が大きすぎます"; return false; }
        Parser ps;
        ps.p = s;
        ps.start = s;
        ps.end = s + len;
        ps.keep_null = keep_null;
        const int base = lua_gettop(L);
        bool ok = ParseValue(L, ps, 0);
        if (ok) {
            SkipWs(ps);
            if (ps.p != ps.end) ok = Fail(ps, "余分な文字があります");
        }
        if (!ok) {
            lua_settop(L, base);
            err = ps.err ? ps.err : "不正なJSONです";
            err_pos = (size_t)(ps.p - ps.start);
            return false;
        }
        return true;
    }

}
