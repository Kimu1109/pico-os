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

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
