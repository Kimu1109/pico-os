// 電卓アプリの式評価(CalcEval::Evaluate)のテスト。
//
// Url.hpp/SD_IOのテストと同じく純粋な文字列処理なので、SDもGUIもFixedStringすら要らない。
// ×÷√πはUTF-8のマルチバイト文字のままCalculatorKeypadから渡ってくるため、
// ここでもソースリテラルにそのまま埋め込んで確認する。
#include "util/Calc_Eval.hpp"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void eq(const char* expr, double expected, const char* label){
    const CalcEval::Result r = CalcEval::Evaluate(expr);
    const bool ok = r.ok() && std::fabs(r.value - expected) < 1e-9;
    printf("%s %-28s 式=%-20s 実測=%.10g 期待=%.10g\n",
           ok ? "[ OK ]" : "[FAIL]", label, expr, r.value, expected);
    if(!ok) failures++;
}

static void eqc(const char* expr, const CalcEval::Context& ctx, double expected, const char* label){
    const CalcEval::Result r = CalcEval::Evaluate(expr, ctx);
    const double tol = 1e-9 * (std::fabs(expected) > 1.0 ? std::fabs(expected) : 1.0);
    const bool ok = r.ok() && std::fabs(r.value - expected) <= tol;
    printf("%s %-28s 式=%-20s 実測=%.12g 期待=%.12g\n",
           ok ? "[ OK ]" : "[FAIL]", label, expr, r.value, expected);
    if(!ok) failures++;
}

static void err(const char* expr, CalcEval::Error expected, const char* label){
    const CalcEval::Result r = CalcEval::Evaluate(expr);
    const bool ok = (r.error == expected);
    printf("%s %-28s 式=%-20s\n", ok ? "[ OK ]" : "[FAIL]", label, expr);
    if(!ok) failures++;
}

int main(){
    printf("---- 四則演算 ----\n");
    eq("1+2", 3.0, "足し算");
    eq("5-8", -3.0, "引き算(負の結果)");
    eq("3×4", 12.0, "掛け算");
    eq("7÷2", 3.5, "割り算");
    eq("2+3×4", 14.0, "乗除算が加減算より優先される");
    eq("(2+3)×4", 20.0, "括弧で優先順位を上書きできる");
    eq("10÷2÷5", 1.0, "同じ優先度は左から計算される");
    eq("-5+3", -2.0, "先頭の単項マイナス");
    eq("3×-2", -6.0, "演算子の直後の単項マイナス");
    eq("2×(3+4×(5-2))", 30.0, "括弧の入れ子");

    printf("\n---- √ / π ----\n");
    eq("√(9)", 3.0, "括弧付きの平方根");
    eq("√9", 3.0, "括弧なしの平方根");
    eq("√(2+7)", 3.0, "平方根の中で式を評価できる");
    eq("√√16", 2.0, "平方根の入れ子");
    eq("-√4", -2.0, "平方根の前の単項マイナス");
    eq("π", 3.14159265358979323846, "円周率の値");
    eq("2×π", 6.28318530717958647692, "円周率を使った式");

    printf("\n---- エラー ----\n");
    err("1÷0", CalcEval::Error::DivByZero, "0で割る");
    err("√(-1)", CalcEval::Error::Domain, "負の数の平方根");
    err("1+", CalcEval::Error::Syntax, "式の途中で終わる");
    err("()", CalcEval::Error::Syntax, "空の括弧");
    err("(1+2", CalcEval::Error::Syntax, "閉じていない括弧");
    err("1+2)", CalcEval::Error::Syntax, "開いていない括弧");
    err("1+×2", CalcEval::Error::Syntax, "演算子が連続する");
    err("", CalcEval::Error::Syntax, "空文字列");

    printf("\n---- 関数電卓: 三角関数 ----\n");
    CalcEval::Context deg;                       // 既定は度
    CalcEval::Context rad; rad.angle = CalcEval::AngleMode::Rad;
    CalcEval::Context grad; grad.angle = CalcEval::AngleMode::Grad;
    eq("sin(30)", 0.5, "度: sin30°");
    eq("cos(60)", 0.5, "度: cos60°");
    eq("tan(45)", 1.0, "度: tan45°");
    {
        const CalcEval::Result r = CalcEval::Evaluate("sin(180)");
        const bool ok = r.ok() && r.value == 0.0;
        printf("%s sin(180°)はちょうど0(1.2e-16にならない)\n", ok ? "[ OK ]" : "[FAIL]");
        if(!ok) failures++;
        const CalcEval::Result r2 = CalcEval::Evaluate("cos(90)");
        const bool ok2 = r2.ok() && r2.value == 0.0;
        printf("%s cos(90°)はちょうど0\n", ok2 ? "[ OK ]" : "[FAIL]");
        if(!ok2) failures++;
        const CalcEval::Result r3 = CalcEval::Evaluate("sin(π)", rad);
        const bool ok3 = r3.ok() && r3.value == 0.0;
        printf("%s ラジアン: sin(π)はちょうど0\n", ok3 ? "[ OK ]" : "[FAIL]");
        if(!ok3) failures++;
    }
    eq("sin(-90)", -1.0, "度: sin(-90°)");
    eq("sin30", 0.5, "括弧なしの引数");
    eqc("sin(π÷6)", rad, 0.5, "ラジアン: sin(π/6)");
    eqc("cos(100)", grad, 0.0, "グラード: cos(100g)");
    eq("asin(1)", 90.0, "度: asin(1)=90");
    eqc("asin(1)", rad, 3.14159265358979323846 / 2, "ラジアン: asin(1)=π/2");
    eq("acos(0.5)", 60.0, "度: acos(0.5)=60");
    eq("atan(1)", 45.0, "度: atan(1)=45");
    eq("sinh(0)", 0.0, "sinh(0)");
    eq("cosh(0)", 1.0, "cosh(0)");
    eq("tanh(0)", 0.0, "tanh(0)");
    eq("asinh(0)", 0.0, "asinh(0)");
    eq("acosh(1)", 0.0, "acosh(1)");
    eq("atanh(0.5)", 0.5493061443340548, "atanh(0.5)");
    err("tan(90)", CalcEval::Error::Domain, "tan(90°)は定義されない");
    err("asin(2)", CalcEval::Error::Domain, "asin(2)は定義域の外");
    err("acosh(0)", CalcEval::Error::Domain, "acosh(0)は定義域の外");
    err("atanh(1)", CalcEval::Error::Domain, "atanh(1)は定義域の外");

    printf("\n---- 関数電卓: 指数・対数 ----\n");
    eq("2^10", 1024.0, "累乗");
    eq("2^3^2", 512.0, "累乗は右結合");
    eq("-2^2", -4.0, "単項マイナスより累乗が強い");
    eq("2^-1", 0.5, "指数に符号");
    eq("(-8)^(1÷3)", -2.0, "負の数の奇数乗根");
    eq("4^0.5", 2.0, "小数の指数");
    eq("log(1000)", 3.0, "常用対数");
    eq("log(2,8)", 3.0, "底を指定した対数");
    eq("ln(e)", 1.0, "自然対数とe");
    eq("e^(2)", 7.38905609893065, "eの累乗");
    eq("10^(3)", 1000.0, "10の累乗");
    eq("cbrt(-27)", -3.0, "立方根");
    eq("root(4,16)", 2.0, "n乗根");
    eq("root(3,-8)", -2.0, "負の数の奇数乗根(root)");
    eq("2^(-1)", 0.5, "逆数(x^-1)");
    err("log(0)", CalcEval::Error::Domain, "log(0)");
    err("ln(-1)", CalcEval::Error::Domain, "ln(-1)");
    err("log(1,5)", CalcEval::Error::Domain, "底が1の対数");
    err("(-8)^0.5", CalcEval::Error::Domain, "負の数の平方根を累乗で");
    err("0^-1", CalcEval::Error::DivByZero, "0の負の累乗");
    err("root(2,-4)", CalcEval::Error::Domain, "負の数の偶数乗根");
    err("log(1,2,3)", CalcEval::Error::Syntax, "引数が多すぎる");
    err("root(2)", CalcEval::Error::Syntax, "引数が足りない");

    printf("\n---- 関数電卓: 階乗・順列・組合せ・その他 ----\n");
    eq("5!", 120.0, "階乗");
    eq("0!", 1.0, "0の階乗");
    eq("3!^2", 36.0, "階乗の後に累乗");
    eq("5C2", 10.0, "組合せ nCr");
    eq("5P2", 20.0, "順列 nPr");
    eq("10C0", 1.0, "nC0");
    eq("2×5C2", 20.0, "nCrは×より強い");
    eq("50%", 0.5, "パーセント");
    eq("200×10%", 20.0, "パーセントを使った式");
    eq("abs(-3.5)", 3.5, "絶対値");
    eq("7mod3", 1.0, "剰余");
    eq("-7mod3", 2.0, "負の数の剰余は除数の符号に揃う");
    eq("1.5E3", 1500.0, "EXP(×10^n)");
    eq("2E-3", 0.002, "EXPの負の指数");
    err("2E", CalcEval::Error::Syntax, "EXPの指数が無い");
    err("(-1)!", CalcEval::Error::Domain, "負の数の階乗");
    err("2.5!", CalcEval::Error::Domain, "小数の階乗");
    err("171!", CalcEval::Error::Overflow, "大きすぎる階乗");
    err("2C5", CalcEval::Error::Domain, "r>nの組合せ");
    err("5mod0", CalcEval::Error::DivByZero, "0での剰余");
    err("10^(400)", CalcEval::Error::Overflow, "桁あふれ");
    err("foo", CalcEval::Error::Syntax, "知らない名前");
    err("sin", CalcEval::Error::Syntax, "引数の無い関数");

    printf("\n---- 省略した掛け算 / Ans / x ----\n");
    eq("2π", 6.28318530717958647692, "2π");
    eq("3(4+1)", 15.0, "数と括弧");
    eq("(1+1)(2+1)", 6.0, "括弧と括弧");
    eq("2sin(30)", 1.0, "数と関数");
    eq("2√9", 6.0, "数と√");
    eq("6÷2(1+2)", 9.0, "省略した掛け算は×と同じ強さで左から");
    CalcEval::Context ans; ans.ans = 42.0;
    eqc("Ans+1", ans, 43.0, "Ans");
    eqc("2Ans", ans, 84.0, "数とAns");
    CalcEval::Context gx; gx.has_x = true; gx.x = 3.0;
    eqc("x^2+2x+1", gx, 16.0, "xの式");
    eqc("2x", gx, 6.0, "数とx");
    eqc("xe", gx, 3.0 * 2.71828182845904523536, "xとe");
    err("x+1", CalcEval::Error::NoVariable, "グラフ以外ではxを使えない");
    err("((((((((((((((((((((((((((1))))))))))))))))))))))))))", CalcEval::Error::TooComplex, "入れ子が深すぎる");
    err("------------------------------1", CalcEval::Error::TooComplex, "単項の入れ子が深すぎる");

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
