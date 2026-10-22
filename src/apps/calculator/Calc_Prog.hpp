// src/apps/calculator/Calc_Prog.hpp
//
// プログラマ電卓(進数・ビット演算)の式の評価。Calc_Evalと同じく、キーパッドが組み立てた
// 文字列を先頭から読む素朴な再帰下降パーサで、ヒープもFixedStringも使わない。
//
// 値は整数だけ。語長(8/16/32/64bit)の2の補数で持ち、演算のたびに語長へ丸める
// (あふれたら折り返す。Windowsの電卓のプログラマーモードと同じ)。10進の表示だけ符号付き。
//
// 対応するもの(弱い順。C言語と同じ並び):
//   OR NOR / XOR / AND NAND / << >>(算術シフト) RoL RoR(語長の中で回す) / + - / × ÷ mod / 単項 - NOT
//   数値は今の基数で読む(16進は 0-9 A-F の大文字だけ。小文字は"mod"等の名前とぶつかるため)。Ans・括弧
//
// キーパッドは演算子の名前を前後に空白を付けて入れる(" AND ")。16進の数字の"A""B"と
// 名前の頭がくっついて"BAND"にならないようにするため。読むときは空白を読み飛ばす。
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace CalcProg {

    enum class Error {
        None,
        Syntax,     // 数値になっていない/括弧の対応が取れていない/知らない名前等
        BadDigit,   // 今の基数で使えない数字(2進で"2"等)
        DivByZero,
        Overflow,   // 64bitに収まらない数値
        TooComplex, // 入れ子が深すぎる
    };

    struct Result {
        int64_t value = 0;
        Error error = Error::None;
        bool ok() const { return error == Error::None; }
    };

    // 語長ぶんの下位ビットだけ立てた値
    inline uint64_t Mask(int bits){
        return bits >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << bits) - 1);
    }

    // 語長へ丸めて符号拡張する(8bitの0xFFは-1)
    inline int64_t Wrap(uint64_t v, int bits){
        if (bits >= 64) return (int64_t)v;
        v &= Mask(bits);
        const uint64_t sign = (uint64_t)1 << (bits - 1);
        if (v & sign) v |= ~Mask(bits);
        return (int64_t)v;
    }

    // '0'-'9' 'A'-'F' の値。数字でなければ-1
    inline int DigitValue(char c){
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // 値を基数で書く。10進は符号付き、それ以外は語長ぶんの符号なし(2の補数のまま)。
    // 先頭の0は付けない(0は"0")。outは最低66バイト(64桁+符号+終端)
    inline void Format(int64_t v, int base, int bits, char* out, size_t cap){
        if (!out || cap == 0) return;
        char tmp[72];
        int n = 0;
        bool neg = false;
        uint64_t u;
        if (base == 10) {
            const int64_t s = Wrap((uint64_t)v, bits);
            neg = s < 0;
            u = neg ? (uint64_t)0 - (uint64_t)s : (uint64_t)s; // INT64_MINでも未定義動作にしない
        } else {
            u = (uint64_t)v & Mask(bits);
        }
        do {
            const int d = (int)(u % (uint64_t)base);
            tmp[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
            u /= (uint64_t)base;
        } while (u != 0 && n < 70);
        size_t o = 0;
        if (neg && o + 1 < cap) out[o++] = '-';
        while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
        out[o] = '\0';
    }

    namespace detail {
        constexpr int kMaxDepth = 24;

        struct Parser {
            const char* p;
            int base;
            int bits;
            int64_t ans;
            Error error = Error::None;
            int depth = 0;

            bool failed() const { return error != Error::None; }
            void fail(Error e){ if (error == Error::None) error = e; }

            void skipSpaces(){ while (*p == ' ' || *p == '\t') p++; }

            bool eat(const char* token){
                skipSpaces();
                const size_t len = strlen(token);
                if (strncmp(p, token, len) != 0) return false;
                p += len;
                return true;
            }

            int64_t wrap(uint64_t v) const { return Wrap(v, bits); }

            int64_t parseNumber(){
                skipSpaces();
                uint64_t v = 0;
                bool digits = false;
                while (true) {
                    const int d = DigitValue(*p);
                    if (d < 0) break;
                    if (d >= base) { fail(Error::BadDigit); return 0; }
                    // 64bitを超える桁はあふれ(語長より大きいだけなら折り返す)
                    if (v > (~(uint64_t)0 - (uint64_t)d) / (uint64_t)base) { fail(Error::Overflow); return 0; }
                    v = v * (uint64_t)base + (uint64_t)d;
                    digits = true;
                    p++;
                }
                if (!digits) { fail(Error::Syntax); return 0; }
                return wrap(v);
            }

            int64_t parsePrimary(){
                if (failed()) return 0;
                if (++depth > kMaxDepth) { fail(Error::TooComplex); depth--; return 0; }
                int64_t v = 0;
                if (eat("(")) {
                    v = parseOr();
                    if (!eat(")")) fail(Error::Syntax);
                } else if (eat("Ans")) { // "A"(16進の10)より先に見る
                    v = wrap((uint64_t)ans);
                } else {
                    v = parseNumber();
                }
                depth--;
                return v;
            }

            int64_t parseUnary(){
                if (failed()) return 0;
                if (++depth > kMaxDepth) { fail(Error::TooComplex); depth--; return 0; }
                int64_t v;
                if (eat("-"))        v = wrap((uint64_t)0 - (uint64_t)parseUnary());
                else if (eat("+"))   v = parseUnary();
                else if (eat("NOT")) v = wrap(~(uint64_t)parseUnary());
                else                 v = parsePrimary();
                depth--;
                return v;
            }

            int64_t parseMul(){
                int64_t v = parseUnary();
                while (!failed()) {
                    if (eat("×")) {
                        const int64_t r = parseUnary();
                        v = wrap((uint64_t)v * (uint64_t)r);
                        continue;
                    }
                    const bool is_div = eat("÷");
                    const bool is_mod = !is_div && eat("mod");
                    if (is_div || is_mod) {
                        const int64_t r = parseUnary();
                        if (failed()) break;
                        if (r == 0) { fail(Error::DivByZero); break; }
                        // INT64_MIN ÷ -1 はC++では未定義動作。語長で折り返すと元の値に戻る
                        if (r == -1) { v = is_mod ? 0 : wrap((uint64_t)0 - (uint64_t)v); continue; }
                        v = wrap((uint64_t)(is_mod ? v % r : v / r));
                    } else {
                        break;
                    }
                }
                return v;
            }

            int64_t parseAdd(){
                int64_t v = parseMul();
                while (!failed()) {
                    if (eat("+"))      v = wrap((uint64_t)v + (uint64_t)parseMul());
                    else if (eat("-")) v = wrap((uint64_t)v - (uint64_t)parseMul());
                    else break;
                }
                return v;
            }

            // シフト量は語長以上なら全部押し出したのと同じ。負の量は逆向き
            int64_t shiftLeft(int64_t v, int64_t n) const {
                if (n < 0) return shiftRight(v, n == INT64_MIN ? bits : -n);
                if (n >= bits) return 0;
                return wrap((uint64_t)v << n);
            }
            int64_t shiftRight(int64_t v, int64_t n) const {
                if (n < 0) return shiftLeft(v, n == INT64_MIN ? bits : -n);
                if (n >= bits) return v < 0 ? -1 : 0;
                // 算術シフト(符号を保つ)。符号拡張済みなので64bitのまま右へずらせばよい
                return v < 0 ? (int64_t)~(~(uint64_t)v >> n) : (int64_t)((uint64_t)v >> n);
            }
            int64_t rotate(int64_t v, int64_t n) const {
                const uint64_t m = Mask(bits);
                const uint64_t u = (uint64_t)v & m;
                int64_t k = n % bits;
                if (k < 0) k += bits;
                if (k == 0) return wrap(u);
                return wrap(((u << k) | (u >> (bits - k))) & m);
            }

            int64_t parseShift(){
                int64_t v = parseAdd();
                while (!failed()) {
                    if (eat("<<"))       v = shiftLeft(v, parseAdd());
                    else if (eat(">>"))  v = shiftRight(v, parseAdd());
                    else if (eat("RoL")) v = rotate(v, parseAdd());
                    else if (eat("RoR")) { const int64_t n = parseAdd(); v = rotate(v, -(n % bits)); }
                    else break;
                }
                return v;
            }

            int64_t parseAnd(){
                int64_t v = parseShift();
                while (!failed()) {
                    if (eat("NAND"))     v = wrap(~((uint64_t)v & (uint64_t)parseShift()));
                    else if (eat("AND")) v = wrap((uint64_t)v & (uint64_t)parseShift());
                    else break;
                }
                return v;
            }

            int64_t parseXor(){
                int64_t v = parseAnd();
                while (!failed() && eat("XOR")) v = wrap((uint64_t)v ^ (uint64_t)parseAnd());
                return v;
            }

            int64_t parseOr(){
                int64_t v = parseXor();
                while (!failed()) {
                    if (eat("NOR"))     v = wrap(~((uint64_t)v | (uint64_t)parseXor()));
                    else if (eat("OR")) v = wrap((uint64_t)v | (uint64_t)parseXor());
                    else break;
                }
                return v;
            }
        };
    }

    // base: 2/8/10/16、bits: 8/16/32/64。ans は直前の答え
    inline Result Evaluate(const char* expr, int base, int bits, int64_t ans = 0){
        Result out;
        if (!expr || (base != 2 && base != 8 && base != 10 && base != 16) || bits < 1 || bits > 64) {
            out.error = Error::Syntax;
            return out;
        }
        detail::Parser parser{ expr, base, bits, ans };
        parser.skipSpaces();
        if (*parser.p == '\0') { out.error = Error::Syntax; return out; }

        const int64_t v = parser.parseOr();
        if (!parser.failed()) {
            parser.skipSpaces();
            if (*parser.p != '\0') parser.fail(Error::Syntax);
        }
        if (parser.failed()) { out.error = parser.error; return out; }
        out.value = Wrap((uint64_t)v, bits);
        return out;
    }
}
