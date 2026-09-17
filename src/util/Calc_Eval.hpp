// src/util/Calc_Eval.hpp
//
// 電卓アプリ用の式評価。対応するのは四則演算(+ - × ÷)、丸括弧、平方根(√)、円周率(π)。
// CalculatorKeypadが組み立てる式はUTF-8のまま(×÷√πはマルチバイト文字)渡ってくるので、
// バイト列を先頭から舐める素朴な再帰下降パーサで評価する。
//
// Url.hpp/SD_IO.hppと同じく「純粋な文字列処理」なのでヒープもFixedStringも使わず、
// const char* を受け取って数値+エラー種別だけを返す。結果の文字列整形(FixedStringへの
// フォーマット)は呼び出し側(CalculatorScene)の役目にして、ここでは評価だけに専念する。
// この形にしておくとホスト側で(SDもGUIも無しに)全経路を検証できる。
#pragma once

#include <cmath>
#include <cstring>
#include <cstdlib>

namespace CalcEval {

    enum class Error {
        None,
        Syntax,     // 数値になっていない/括弧の対応が取れていない等
        DivByZero,  // 0除算
        Domain,     // 負の数の平方根等、定義域の外
        TooComplex, // 括弧・単項演算子の入れ子が深すぎる
    };

    struct Result {
        double value = 0.0;
        Error error = Error::None;
        bool ok() const { return error == Error::None; }
    };

    namespace detail {
        // 極端な入れ子("((((((..."等)でスタックを掘り尽くさないための保険。
        // 電卓の式としては十分すぎる深さで、通常の利用では絶対に当たらない
        constexpr int kMaxDepth = 24;
        constexpr double kPi = 3.14159265358979323846;

        struct Parser {
            const char* p;
            Error error = Error::None;
            int depth = 0;

            bool failed() const { return error != Error::None; }

            void skipSpaces() {
                while (*p == ' ' || *p == '\t') p++;
            }

            // 与えたトークン(1バイトのASCII演算子、または×÷√πのマルチバイト文字)が
            // 現在位置にあれば読み進めてtrueを返す
            bool eat(const char* token) {
                skipSpaces();
                const size_t len = strlen(token);
                if (strncmp(p, token, len) != 0) return false;
                p += len;
                return true;
            }

            double parseNumber() {
                skipSpaces();
                char* end = nullptr;
                const double v = strtod(p, &end);
                if (end == p) {
                    error = Error::Syntax;
                    return 0.0;
                }
                p = end;
                return v;
            }

            // 数値/π/括弧式/√式/単項+-の1つぶん。四則演算の最小単位
            double parsePrimary() {
                if (failed()) return 0.0;

                if (++depth > kMaxDepth) {
                    error = Error::TooComplex;
                    depth--;
                    return 0.0;
                }

                double result = 0.0;

                if (eat("(")) {
                    result = parseExpr();
                    if (!eat(")") && !failed()) error = Error::Syntax;
                } else if (eat("√")) {
                    const double inner = parsePrimary();
                    if (!failed()) {
                        if (inner < 0.0) error = Error::Domain;
                        else result = std::sqrt(inner);
                    }
                } else if (eat("π")) {
                    result = kPi;
                } else if (eat("-")) {
                    result = -parsePrimary();
                } else if (eat("+")) {
                    result = parsePrimary();
                } else {
                    result = parseNumber();
                }

                depth--;
                return result;
            }

            // ×÷は+-より結合が強いので、先にこちらでまとめる
            double parseTerm() {
                double v = parsePrimary();
                while (!failed()) {
                    if (eat("×")) {
                        const double rhs = parsePrimary();
                        if (!failed()) v *= rhs;
                    } else if (eat("÷")) {
                        const double rhs = parsePrimary();
                        if (failed()) break;
                        if (rhs == 0.0) { error = Error::DivByZero; break; }
                        v /= rhs;
                    } else {
                        break;
                    }
                }
                return v;
            }

            double parseExpr() {
                double v = parseTerm();
                while (!failed()) {
                    if (eat("+")) {
                        const double rhs = parseTerm();
                        if (!failed()) v += rhs;
                    } else if (eat("-")) {
                        const double rhs = parseTerm();
                        if (!failed()) v -= rhs;
                    } else {
                        break;
                    }
                }
                return v;
            }
        };
    }

    inline Result Evaluate(const char* expr) {
        Result out;
        if (!expr || expr[0] == '\0') {
            out.error = Error::Syntax;
            return out;
        }

        detail::Parser parser{ expr };
        const double v = parser.parseExpr();

        if (!parser.failed()) {
            parser.skipSpaces();
            if (*parser.p != '\0') parser.error = Error::Syntax; // 式の途中で余りが残った
        }

        if (parser.failed()) {
            out.error = parser.error;
            return out;
        }

        if (!std::isfinite(v)) {
            out.error = Error::Domain; // オーバーフロー等もまとめてここへ倒す
            return out;
        }

        out.value = v;
        return out;
    }
}
