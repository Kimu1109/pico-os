// src/util/Calc_Eval.hpp
//
// 電卓アプリ用の式評価(関数電卓)。CalculatorKeypadが組み立てる式はUTF-8のまま
// (×÷√πはマルチバイト文字)渡ってくるので、バイト列を先頭から舐める素朴な再帰下降パーサで評価する。
//
// 対応するもの:
//   二項演算  + - × ÷ ^(累乗、右結合) mod  C(nCr) P(nPr)
//   単項      + -(-2^2 = -4。累乗の方が強い)
//   後置      !(階乗) %(÷100)
//   関数      sin cos tan asin acos atan sinh cosh tanh asinh acosh atanh
//             log(x)=常用対数 / log(b,x)=底b  ln  abs  cbrt(立方根)  root(n,x)(n乗根)  √
//   定数      π e Ans(直前の答え) x(グラフのときだけ)
//   数値      12  1.5  .5  1.5E-3(EXPキー。Eの後ろは整数)
//   省略した掛け算  2π  3(4+1)  2sin(30)  2x  (1)(2) — ×と同じ強さで左から
//
// 関数の引数は「(」で括るのが基本だが、括弧なしなら直後の1項(累乗を含む)を引数にする
// (√9、sin30、√√16)。三角関数の角度の単位はContext::angleで選ぶ。
//
// Url.hpp/SD_IO.hppと同じく「純粋な文字列処理」なのでヒープもFixedStringも使わず、
// const char* を受け取って数値+エラー種別だけを返す。結果の整形は呼び出し側の役目。
// グラフは1画素の列ごとにこれを呼ぶ(式を中間形式へ翻訳はしない。240列×3本で足りる速さのため)。
#pragma once

#include <cmath>
#include <cstring>
#include <cstdint>

namespace CalcEval {

    enum class Error {
        None,
        Syntax,     // 数値になっていない/括弧の対応が取れていない/知らない名前等
        DivByZero,  // 0除算
        Domain,     // 負の数の平方根・tan(90°)・asin(2)等、定義域の外
        TooComplex, // 括弧・単項演算子の入れ子が深すぎる
        Overflow,   // 結果が大きすぎる(171!等)
        NoVariable, // xを使ったが、グラフ以外で評価した
    };

    enum class AngleMode : uint8_t { Deg = 0, Rad = 1, Grad = 2 };

    struct Context {
        AngleMode angle = AngleMode::Deg;
        double ans = 0.0;   // Ansの値
        bool has_x = false; // xを使ってよいか(グラフのときだけtrue)
        double x = 0.0;
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
        constexpr double kE  = 2.71828182845904523536;

        enum class Fn : uint8_t {
            Sin, Cos, Tan, Asin, Acos, Atan,
            Sinh, Cosh, Tanh, Asinh, Acosh, Atanh,
            Log, Ln, Abs, Cbrt, Root, Sqrt,
        };

        struct FnDef { const char* name; Fn fn; uint8_t min_args; uint8_t max_args; };

        // 前方一致で探すので、長い名前を先に並べる(asinhがasinやsinに食われないように)
        constexpr FnDef kFns[] = {
            { "asinh", Fn::Asinh, 1, 1 }, { "acosh", Fn::Acosh, 1, 1 }, { "atanh", Fn::Atanh, 1, 1 },
            { "sinh",  Fn::Sinh,  1, 1 }, { "cosh",  Fn::Cosh,  1, 1 }, { "tanh",  Fn::Tanh,  1, 1 },
            { "asin",  Fn::Asin,  1, 1 }, { "acos",  Fn::Acos,  1, 1 }, { "atan",  Fn::Atan,  1, 1 },
            { "cbrt",  Fn::Cbrt,  1, 1 }, { "root",  Fn::Root,  2, 2 },
            { "sin",   Fn::Sin,   1, 1 }, { "cos",   Fn::Cos,   1, 1 }, { "tan",   Fn::Tan,   1, 1 },
            { "log",   Fn::Log,   1, 2 }, { "abs",   Fn::Abs,   1, 1 },
            { "ln",    Fn::Ln,    1, 1 }, { "√",     Fn::Sqrt,  1, 1 },
        };

        inline bool IsInteger(double v){ return std::isfinite(v) && v == std::floor(v); }

        struct Parser {
            const char* p;
            const Context& ctx;
            Error error = Error::None;
            int depth = 0;

            bool failed() const { return error != Error::None; }
            void fail(Error e){ if(error == Error::None) error = e; }

            void skipSpaces() {
                while (*p == ' ' || *p == '\t') p++;
            }

            // 与えたトークンが現在位置にあれば読み進めてtrueを返す
            bool eat(const char* token) {
                skipSpaces();
                const size_t len = strlen(token);
                if (strncmp(p, token, len) != 0) return false;
                p += len;
                return true;
            }

            bool peek(const char* token) {
                skipSpaces();
                return strncmp(p, token, strlen(token)) == 0;
            }

            bool enter() {
                if (++depth > kMaxDepth) { fail(Error::TooComplex); return false; }
                return true;
            }

            // ---- 角度 ----
            double toRad(double v) const {
                switch (ctx.angle) {
                    case AngleMode::Deg:  return v * (kPi / 180.0);
                    case AngleMode::Grad: return v * (kPi / 200.0);
                    default:              return v;
                }
            }
            double fromRad(double r) const {
                switch (ctx.angle) {
                    case AngleMode::Deg:  return r * (180.0 / kPi);
                    case AngleMode::Grad: return r * (200.0 / kPi);
                    default:              return r;
                }
            }
            // 直角(90°)の何倍か。sin(180°)が1.2e-16にならないよう、直角のちょうど倍数は表で答える
            double quarterTurns(double v) const {
                switch (ctx.angle) {
                    case AngleMode::Deg:  return v / 90.0;
                    case AngleMode::Grad: return v / 100.0;
                    default:              return v / (kPi / 2.0);
                }
            }
            // ちょうど直角の倍数ならtrueと、何倍目か(0〜3)を返す
            bool exactQuarter(double v, int& k) const {
                const double q = quarterTurns(v);
                const double r = std::round(q);
                const double tol = 1e-12 * (std::fabs(r) > 1.0 ? std::fabs(r) : 1.0);
                if (std::fabs(q - r) > tol) return false;
                long long n = (long long)std::fmod(r, 4.0);
                if (n < 0) n += 4;
                k = (int)n;
                return true;
            }

            // ---- 数値 ----
            // strtodは使わない("inf"や"0x10"を読んでしまう上、"2e"の扱いが定数eとぶつかるため)
            double parseNumber() {
                skipSpaces();
                const char* s = p;
                double v = 0.0;
                bool digits = false;
                while (*s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; digits = true; }
                if (*s == '.') {
                    s++;
                    double scale = 0.1;
                    while (*s >= '0' && *s <= '9') { v += (*s - '0') * scale; scale *= 0.1; s++; digits = true; }
                }
                if (!digits) { fail(Error::Syntax); return 0.0; }
                if (*s == 'E') {
                    s++;
                    int sign = 1;
                    if (*s == '-') { sign = -1; s++; }
                    else if (*s == '+') { s++; }
                    if (!(*s >= '0' && *s <= '9')) { fail(Error::Syntax); return 0.0; }
                    int ex = 0;
                    while (*s >= '0' && *s <= '9') { if (ex < 10000) ex = ex * 10 + (*s - '0'); s++; }
                    v *= std::pow(10.0, sign * ex);
                }
                p = s;
                return v;
            }

            // 関数の引数。"("があれば括弧の中を(カンマ区切りで)読み、無ければ直後の1項
            int parseArgs(double* args, int max_args) {
                if (eat("(")) {
                    int n = 0;
                    do {
                        const double v = parseExpr();
                        if (failed()) return 0;
                        if (n >= max_args) { fail(Error::Syntax); return 0; }
                        args[n++] = v;
                    } while (eat(","));
                    if (!eat(")")) { fail(Error::Syntax); return 0; }
                    return n;
                }
                args[0] = parseUnary();
                return 1;
            }

            double applyFn(Fn fn, const double* a, int n) {
                const double v = a[0];
                int k = 0;
                switch (fn) {
                    case Fn::Sin:
                        if (exactQuarter(v, k)) { static const double t[4] = { 0, 1, 0, -1 }; return t[k]; }
                        return std::sin(toRad(v));
                    case Fn::Cos:
                        if (exactQuarter(v, k)) { static const double t[4] = { 1, 0, -1, 0 }; return t[k]; }
                        return std::cos(toRad(v));
                    case Fn::Tan:
                        if (exactQuarter(v, k)) {
                            if (k == 1 || k == 3) { fail(Error::Domain); return 0.0; }
                            return 0.0;
                        }
                        return std::tan(toRad(v));
                    case Fn::Asin:
                        if (v < -1.0 || v > 1.0) { fail(Error::Domain); return 0.0; }
                        return fromRad(std::asin(v));
                    case Fn::Acos:
                        if (v < -1.0 || v > 1.0) { fail(Error::Domain); return 0.0; }
                        return fromRad(std::acos(v));
                    case Fn::Atan:  return fromRad(std::atan(v));
                    case Fn::Sinh:  return std::sinh(v);
                    case Fn::Cosh:  return std::cosh(v);
                    case Fn::Tanh:  return std::tanh(v);
                    case Fn::Asinh: return std::asinh(v);
                    case Fn::Acosh:
                        if (v < 1.0) { fail(Error::Domain); return 0.0; }
                        return std::acosh(v);
                    case Fn::Atanh:
                        if (v <= -1.0 || v >= 1.0) { fail(Error::Domain); return 0.0; }
                        return std::atanh(v);
                    case Fn::Log:
                        if (n == 2) { // log(底, 真数)
                            const double b = a[0], x = a[1];
                            if (b <= 0.0 || b == 1.0 || x <= 0.0) { fail(Error::Domain); return 0.0; }
                            return std::log(x) / std::log(b);
                        }
                        if (v <= 0.0) { fail(Error::Domain); return 0.0; }
                        return std::log10(v);
                    case Fn::Ln:
                        if (v <= 0.0) { fail(Error::Domain); return 0.0; }
                        return std::log(v);
                    case Fn::Abs:  return std::fabs(v);
                    case Fn::Cbrt: return std::cbrt(v);
                    case Fn::Sqrt:
                        if (v < 0.0) { fail(Error::Domain); return 0.0; }
                        return std::sqrt(v);
                    case Fn::Root: { // root(n, x) = xのn乗根
                        const double nth = a[0], x = a[1];
                        if (nth == 0.0) { fail(Error::Domain); return 0.0; }
                        if (x < 0.0) {
                            // 負の数は奇数乗根だけ(root(3,-8) = -2)
                            if (IsInteger(nth) && std::fmod(std::fabs(nth), 2.0) == 1.0) return -std::pow(-x, 1.0 / nth);
                            fail(Error::Domain); return 0.0;
                        }
                        if (x == 0.0 && nth < 0.0) { fail(Error::DivByZero); return 0.0; }
                        return std::pow(x, 1.0 / nth);
                    }
                }
                fail(Error::Syntax);
                return 0.0;
            }

            // 数値/定数/括弧式/関数の1つぶん
            double parsePrimary() {
                if (failed()) return 0.0;
                if (!enter()) { depth--; return 0.0; }

                double result = 0.0;
                skipSpaces();

                bool matched = false;
                for (const FnDef& f : kFns) {
                    if (!eat(f.name)) continue;
                    matched = true;
                    double args[2] = { 0.0, 0.0 };
                    const int n = parseArgs(args, f.max_args);
                    if (!failed()) {
                        if (n < f.min_args) fail(Error::Syntax);
                        else result = applyFn(f.fn, args, n);
                    }
                    break;
                }

                if (!matched) {
                    if (eat("(")) {
                        result = parseExpr();
                        if (!eat(")")) fail(Error::Syntax);
                    } else if (eat("π")) {
                        result = kPi;
                    } else if (eat("Ans")) {
                        result = ctx.ans;
                    } else if (eat("e")) {
                        result = kE;
                    } else if (eat("x")) {
                        if (!ctx.has_x) fail(Error::NoVariable);
                        result = ctx.x;
                    } else {
                        result = parseNumber();
                    }
                }

                depth--;
                return result;
            }

            // 後置の ! と %
            double parsePostfix() {
                double v = parsePrimary();
                while (!failed()) {
                    if (eat("!")) {
                        if (v < 0.0 || !IsInteger(v)) { fail(Error::Domain); break; }
                        if (v > 170.0) { fail(Error::Overflow); break; }
                        double f = 1.0;
                        for (int i = 2; i <= (int)v; i++) f *= i;
                        v = f;
                    } else if (eat("%")) {
                        v /= 100.0;
                    } else {
                        break;
                    }
                }
                return v;
            }

            static double Pow(double b, double e, Error& err) {
                if (b == 0.0 && e < 0.0) { err = Error::DivByZero; return 0.0; }
                if (b < 0.0 && !IsInteger(e)) {
                    // (-8)^(1/3) = -2 のように、逆数が奇数になる指数だけは実数で答える
                    const double inv = 1.0 / e;
                    const double r = std::round(inv);
                    if (std::fabs(inv - r) < 1e-9 && std::fmod(std::fabs(r), 2.0) == 1.0) {
                        return -std::pow(-b, e);
                    }
                    err = Error::Domain;
                    return 0.0;
                }
                return std::pow(b, e);
            }

            // 累乗(右結合)。指数側には単項の符号を許す(2^-1)
            double parsePower() {
                const double base = parsePostfix();
                if (failed() || !eat("^")) return base;
                const double ex = parseUnary();
                if (failed()) return 0.0;
                Error e = Error::None;
                const double v = Pow(base, ex, e);
                if (e != Error::None) fail(e);
                return v;
            }

            double parseUnary() {
                if (failed()) return 0.0;
                if (!enter()) { depth--; return 0.0; }
                double v;
                if (eat("-"))      v = -parseUnary();
                else if (eat("+")) v = parseUnary();
                else               v = parsePower();
                depth--;
                return v;
            }

            // nCr / nPr(×÷より強い。関数電卓の一般的な優先順位)
            double parseCombination() {
                double v = parseUnary();
                while (!failed()) {
                    const bool is_c = peek("C");
                    const bool is_p = !is_c && peek("P");
                    if (!is_c && !is_p) break;
                    p++;
                    const double r = parseUnary();
                    if (failed()) break;
                    if (!IsInteger(v) || !IsInteger(r) || v < 0.0 || r < 0.0 || r > v) { fail(Error::Domain); break; }
                    double out = 1.0;
                    const int n = (int)(v > 1e6 ? 1e6 : v);
                    const int k = (int)r;
                    if (is_p) {
                        for (int i = 0; i < k && std::isfinite(out); i++) out *= (n - i);
                    } else {
                        const int kk = (k > n - k) ? n - k : k;
                        for (int i = 1; i <= kk && std::isfinite(out); i++) out = out * (n - kk + i) / i;
                        out = std::round(out);
                    }
                    v = out;
                }
                return v;
            }

            // 省略した掛け算の右側になれるもの(数値・括弧・関数・定数)が続くか
            bool startsImplicitOperand() {
                skipSpaces();
                const unsigned char c = (unsigned char)*p;
                if ((c >= '0' && c <= '9') || c == '.' || c == '(') return true;
                if (c >= 'a' && c <= 'z') return true; // sin/log/e/x…(mod は先に見る)
                if (strncmp(p, "Ans", 3) == 0) return true;
                if (strncmp(p, "π", strlen("π")) == 0) return true;
                if (strncmp(p, "√", strlen("√")) == 0) return true;
                return false;
            }

            // ×÷mod と省略した掛け算は+-より結合が強い
            double parseTerm() {
                double v = parseCombination();
                while (!failed()) {
                    if (eat("×")) {
                        const double rhs = parseCombination();
                        if (!failed()) v *= rhs;
                    } else if (eat("÷")) {
                        const double rhs = parseCombination();
                        if (failed()) break;
                        if (rhs == 0.0) { fail(Error::DivByZero); break; }
                        v /= rhs;
                    } else if (eat("mod")) {
                        const double rhs = parseCombination();
                        if (failed()) break;
                        if (rhs == 0.0) { fail(Error::DivByZero); break; }
                        v = v - rhs * std::floor(v / rhs);
                    } else if (startsImplicitOperand()) {
                        const double rhs = parseCombination();
                        if (!failed()) v *= rhs;
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

    inline Result Evaluate(const char* expr, const Context& ctx) {
        Result out;
        if (!expr || expr[0] == '\0') {
            out.error = Error::Syntax;
            return out;
        }

        detail::Parser parser{ expr, ctx };
        const double v = parser.parseExpr();

        if (!parser.failed()) {
            parser.skipSpaces();
            if (*parser.p != '\0') parser.error = Error::Syntax; // 式の途中で余りが残った
        }

        if (parser.failed()) {
            out.error = parser.error;
            return out;
        }

        if (std::isnan(v)) { out.error = Error::Domain; return out; }
        if (!std::isfinite(v)) { out.error = Error::Overflow; return out; }

        out.value = (v == 0.0) ? 0.0 : v; // -0 を "0" と出すため
        return out;
    }

    inline Result Evaluate(const char* expr) {
        const Context ctx;
        return Evaluate(expr, ctx);
    }
}
