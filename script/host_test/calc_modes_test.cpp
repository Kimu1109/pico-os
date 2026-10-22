// 電卓の「変換」「進数」ページの中身(画面抜き)を検証するテスト。
//
//   1. CalcProg(Calc_Prog.hpp): 基数ごとの数の読み方・ビット演算の優先順位・語長での折り返し・
//      2の補数・シフト/ローテート・0除算・使えない数字・各基数での書き出し
//   2. CalcUnits(Calc_Units.hpp): 代表的な換算(長さ・温度・面積・データ量)と、表の値の健全さ
//   3. CurrencyRates(Calc_Currency): Frankfurter / open.er-api の応答の読み取り・並べ替え・換算・
//      壊れた応答で手元のレートを壊さないこと・SDへの控えの往復(url の指定を残すこと)
#include "apps/calculator/Calc_Prog.hpp"
#include "apps/calculator/Calc_Units.hpp"
#include "apps/calculator/Calc_Currency.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eqInt(long long actual, long long expected, const char* label){
    const bool ok = actual == expected;
    printf("%s %-44s 実測=%lld 期待=%lld\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}
static void eqStr(const char* actual, const char* expected, const char* label){
    const bool ok = strcmp(actual, expected) == 0;
    printf("%s %-44s 実測=%s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}
static void near(double actual, double expected, double tol, const char* label){
    const bool ok = std::fabs(actual - expected) <= tol;
    printf("%s %-44s 実測=%.12g 期待=%.12g\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

// ---- 進数 ----
static long long prog(const char* expr, int base = 16, int bits = 64, int64_t ans = 0){
    const CalcProg::Result r = CalcProg::Evaluate(expr, base, bits, ans);
    return r.ok() ? (long long)r.value : -999999;
}
static CalcProg::Error progErr(const char* expr, int base = 16, int bits = 64){
    return CalcProg::Evaluate(expr, base, bits, 0).error;
}
static std::string fmt(int64_t v, int base, int bits){
    char buf[72];
    CalcProg::Format(v, base, bits, buf, sizeof(buf));
    return buf;
}

static void testProg(){
    // 数の読み方
    eqInt(prog("FF"), 255, "16進: FF");
    eqInt(prog("777", 8), 511, "8進: 777");
    eqInt(prog("1010", 2), 10, "2進: 1010");
    eqInt(prog("255", 10), 255, "10進: 255");
    check(progErr("2", 2) == CalcProg::Error::BadDigit, "2進で\"2\"は使えない数字");
    check(progErr("A", 10) == CalcProg::Error::BadDigit, "10進で\"A\"は使えない数字");
    check(progErr("10000000000000000") == CalcProg::Error::Overflow, "64bitを超える数はあふれ");
    check(progErr("") == CalcProg::Error::Syntax, "空の式は誤り");
    check(progErr("FF AND") == CalcProg::Error::Syntax, "途中の式は誤り");

    // ビット演算と優先順位(C言語と同じ: シフト > AND > XOR > OR、+-はシフトより強い)
    eqInt(prog("F0 OR 0F"), 0xFF, "OR");
    eqInt(prog("FF AND 0F"), 0x0F, "AND");
    eqInt(prog("FF XOR 0F"), 0xF0, "XOR");
    eqInt(prog("FF AND 0F << 4"), 0xF0, "<<はANDより強い");
    eqInt(prog("1 OR 2 AND 3"), 3, "ANDはORより強い(1 OR (2 AND 3))");
    eqInt(prog("1 << 2 + 1"), 8, "+はシフトより強い(1 << 3)");
    eqInt(prog("(1 << 2) + 1"), 5, "括弧");
    eqInt(prog("2×3+4", 10), 10, "×は+より強い");
    eqInt(prog("7÷2", 10), 3, "÷は整数の割り算");
    eqInt(prog("-7÷2", 10), -3, "負の数の割り算は0へ向けて切り捨て");
    eqInt(prog("7 mod 3", 10), 1, "mod");
    eqInt(prog("-7 mod 3", 10), -1, "modの符号は割られる数に合わせる");
    check(progErr("1÷0", 10) == CalcProg::Error::DivByZero, "0除算");
    check(progErr("1 mod 0", 10) == CalcProg::Error::DivByZero, "0で余り");
    eqInt(prog("FF NAND 0F", 16, 8), (int8_t)0xF0, "NAND(8bit)");
    eqInt(prog("F0 NOR 0F", 16, 8), 0, "NOR(8bit)");
    eqInt(prog("Ans + 1", 10, 64, 41), 42, "Ans");
    eqInt(prog("Ans AND F", 16, 64, 0xAB), 0xB, "Ans は16進の\"A\"に食われない");

    // 語長と2の補数
    eqInt(prog("NOT 0", 10, 8), -1, "NOT 0 は -1");
    eqInt(prog("NOT 0", 16, 64), -1, "NOT 0 は -1(64bit)");
    eqInt(prog("FF", 16, 8), -1, "8bitの FF は -1");
    eqInt(prog("7F + 1", 16, 8), -128, "8bitの 7F+1 は -128(折り返す)");
    eqInt(prog("100", 16, 8), 0, "8bitに入らない桁は落ちる");
    eqInt(prog("-1", 10, 16), -1, "16bitの -1");
    eqInt(prog("-9223372036854775807 - 1", 10, 64), INT64_MIN, "64bitの最小値");
    eqInt(prog("-9223372036854775807 - 1 ÷ -1", 10, 64), INT64_MIN + 2, "÷は-より強い(-…807 - (1÷-1))");
    eqInt(prog("(-9223372036854775807 - 1) ÷ -1", 10, 64), INT64_MIN, "最小値÷-1 は折り返して最小値(未定義動作にしない)");
    eqInt(prog("7FFFFFFFFFFFFFFF × 2", 16, 64), -2, "64bitの掛け算のあふれは折り返す");

    // シフトとローテート
    eqInt(prog("1 << 7", 10, 8), -128, "8bitの 1<<7 は符号ビット");
    eqInt(prog("1 << 8", 10, 8), 0, "語長以上のシフトは0");
    eqInt(prog("-16 >> 2", 10, 32), -4, ">>は算術シフト(符号を保つ)");
    eqInt(prog("-1 >> 100", 10, 32), -1, "負の数を大きく右シフトすると-1");
    eqInt(prog("81 RoL 1", 16, 8), 3, "RoL(8bit): 1000 0001 → 0000 0011");
    eqInt(prog("81 RoR 1", 16, 8), (int8_t)0xC0, "RoR(8bit): 1000 0001 → 1100 0000");
    eqInt(prog("1 RoR 1", 16, 64), INT64_MIN, "RoR(64bit)");
    eqInt(prog("1 RoL 9", 16, 8), 2, "語長を超えるローテートは回り込む");

    // 書き出し
    eqStr(fmt(-1, 16, 8).c_str(), "FF", "Format: 8bitの-1は16進でFF");
    eqStr(fmt(-1, 10, 8).c_str(), "-1", "Format: 10進は符号付き");
    eqStr(fmt(-1, 16, 64).c_str(), "FFFFFFFFFFFFFFFF", "Format: 64bitの-1");
    eqStr(fmt(INT64_MIN, 10, 64).c_str(), "-9223372036854775808", "Format: 64bitの最小値");
    eqStr(fmt(255, 2, 16).c_str(), "11111111", "Format: 2進(先頭の0は付けない)");
    eqStr(fmt(8, 8, 32).c_str(), "10", "Format: 8進");
    eqStr(fmt(0, 2, 8).c_str(), "0", "Format: 0");
    // 書き出したものを同じ基数で読み戻すと同じ値
    for(int bits : { 8, 16, 32, 64 }){
        for(int base : { 2, 8, 10, 16 }){
            const int64_t vals[] = { 0, 1, -1, 100, -100, CalcProg::Wrap((uint64_t)INT64_MIN, bits) };
            bool ok = true;
            for(int64_t v : vals){
                const int64_t w = CalcProg::Wrap((uint64_t)v, bits);
                if(prog(fmt(w, base, bits).c_str(), base, bits) != w) ok = false;
            }
            char label[64];
            snprintf(label, sizeof(label), "Format→Evaluate の往復(%d進・%dbit)", base, bits);
            check(ok, label);
        }
    }
}

// ---- 単位 ----
static int catIndex(const char* name){
    for(int i = 0; i < CalcUnits::kCategoryCount; i++) if(strcmp(CalcUnits::kCategories[i].name, name) == 0) return i;
    return -1;
}
static int unitIndex(int cat, const char* sym){
    const CalcUnits::Category& c = CalcUnits::kCategories[cat];
    for(int i = 0; i < c.count; i++) if(strcmp(c.units[i].symbol, sym) == 0) return i;
    return -1;
}
static double conv(const char* cat, const char* from, const char* to, double v){
    const int c = catIndex(cat);
    double out = NAN;
    if(c < 0 || !CalcUnits::Convert(c, unitIndex(c, from), unitIndex(c, to), v, out)) return NAN;
    return out;
}

static void testUnits(){
    near(conv("長さ", "mi", "km", 1), 1.609344, 1e-12, "1マイル = 1.609344km");
    near(conv("長さ", "in", "cm", 1), 2.54, 1e-12, "1インチ = 2.54cm");
    near(conv("長さ", "尺", "cm", 1), 30.303030303, 1e-8, "1尺 = 10/33m");
    near(conv("重さ", "lb", "kg", 1), 0.45359237, 1e-12, "1ポンド");
    near(conv("温度", "°C", "°F", 100), 212, 1e-9, "100°C = 212°F");
    near(conv("温度", "°F", "°C", 32), 0, 1e-9, "32°F = 0°C");
    near(conv("温度", "°C", "K", -273.15), 0, 1e-9, "-273.15°C = 0K");
    near(conv("温度", "°F", "K", -40), 233.15, 1e-9, "-40°F = 233.15K");
    near(conv("面積", "坪", "m²", 1), 3.305785124, 1e-8, "1坪");
    near(conv("体積", "升", "合", 1), 10, 1e-9, "1升 = 10合");
    near(conv("速さ", "km/h", "m/s", 36), 10, 1e-9, "36km/h = 10m/s");
    near(conv("時間", "日", "時間", 1), 24, 1e-12, "1日 = 24時間");
    near(conv("データ量", "GiB", "MB", 1), 1073.741824, 1e-9, "1GiB = 1073.741824MB");
    near(conv("データ量", "B", "bit", 1), 8, 1e-12, "1B = 8bit");
    near(conv("圧力", "atm", "hPa", 1), 1013.25, 1e-9, "1気圧 = 1013.25hPa");
    near(conv("エネルギー", "kcal", "kJ", 1), 4.184, 1e-12, "1kcal = 4.184kJ");
    near(conv("角度", "回転", "rad", 1), 2 * 3.14159265358979323846, 1e-12, "1回転 = 2πrad");
    near(conv("長さ", "km", "km", 0.1), 0.1, 0, "同じ単位はそのまま");

    // 表の健全さ: 係数は正、既定の組み合わせは範囲内、記号は重ならない
    bool ok = true;
    for(int c = 0; c < CalcUnits::kCategoryCount; c++){
        const CalcUnits::Category& cat = CalcUnits::kCategories[c];
        if(cat.default_from >= cat.count || cat.default_to >= cat.count || cat.default_from == cat.default_to) ok = false;
        for(int i = 0; i < cat.count; i++){
            if(!(cat.units[i].factor > 0)) ok = false;
            for(int j = i + 1; j < cat.count; j++) if(strcmp(cat.units[i].symbol, cat.units[j].symbol) == 0) ok = false;
        }
    }
    check(ok, "単位の表: 係数が正・既定の組み合わせが範囲内・記号が重ならない");
    double dummy = 0;
    check(!CalcUnits::Convert(CalcUnits::kCategoryCount, 0, 1, 1, dummy), "範囲外の種類はfalse");
    check(!CalcUnits::Convert(0, 0, 99, 1, dummy), "範囲外の単位はfalse");
}

// ---- 通貨 ----
static const char* kFrankfurter =
    "{\"amount\":1.0,\"base\":\"EUR\",\"date\":\"2026-10-21\",\"rates\":{\"AUD\":1.6,\"GBP\":0.84,"
    "\"JPY\":177.3,\"USD\":1.12,\"ZAR\":19.9,\"KRW\":1500}}";
static const char* kErApi =
    "{\"result\":\"success\",\"time_last_update_utc\":\"Wed, 21 Oct 2026 00:02:32 +0000\","
    "\"base_code\":\"JPY\",\"rates\":{\"JPY\":1,\"USD\":0.0066,\"EUR\":0.0056,\"XYZ1\":3,\"usd\":9}}";

static void testCurrency(){
    CurrencyRates r;
    check(!r.hasRates(), "最初はレートが無い");
    check(r.parse(kFrankfurter, strlen(kFrankfurter)), "Frankfurterの応答を読める");
    eqInt(r.count(), 7, "基準通貨(EUR)を足して7通貨");
    eqStr(r.base(), "EUR", "基準通貨");
    eqStr(r.date(), "2026-10-21", "日付");
    eqStr(r.codeAt(0), "JPY", "よく使う通貨が先頭(JPY)");
    eqStr(r.codeAt(1), "USD", "2番目はUSD");
    eqStr(r.codeAt(2), "EUR", "3番目はEUR");
    eqStr(r.codeAt(3), "GBP", "4番目はGBP");
    eqStr(r.codeAt(6), "ZAR", "残りはアルファベット順");
    double out = 0;
    check(r.convert(r.find("USD"), r.find("JPY"), 1, out), "USD→JPYを換算できる");
    near(out, 177.3 / 1.12, 1e-9, "1USD = 177.3/1.12 JPY");
    check(r.convert(r.find("EUR"), r.find("USD"), 10, out), "EUR→USD");
    near(out, 11.2, 1e-9, "10EUR = 11.2USD");
    check(!r.convert(-1, 0, 1, out), "無い通貨はfalse");
    check(CurrencyRates::NameOf("JPY") && strcmp(CurrencyRates::NameOf("JPY"), "日本円") == 0, "日本語の名前");
    check(CurrencyRates::NameOf("XXX") == nullptr, "知らない通貨の名前はnullptr");

    // 壊れた応答では手元のレートを壊さない
    const uint32_t rev = r.revision();
    const char* broken = "{\"base\":\"EUR\",\"rates\":{\"USD\":1.1";
    check(!r.parse(broken, strlen(broken)), "途中で切れた応答は読めない");
    const char* html = "<html>error</html>";
    check(!r.parse(html, strlen(html)), "JSONでない応答は読めない");
    const char* norates = "{\"base\":\"EUR\",\"rates\":{}}";
    check(!r.parse(norates, strlen(norates)), "レートが空の応答は読めない");
    eqInt(r.count(), 7, "失敗しても前のレートが残る");
    check(r.revision() == rev, "失敗では revision が増えない");

    // open.er-api の形(base_code / time_last_update_utc)。3文字の大文字でないキーは捨てる
    check(r.parse(kErApi, strlen(kErApi)), "open.er-apiの応答を読める");
    eqInt(r.count(), 3, "JPY/USD/EUR の3通貨(XYZ1/usdは捨てる)");
    eqStr(r.base(), "JPY", "base_code");
    eqStr(r.date(), "Wed, 21 Oct 2026", "time_last_update_utc の日付の部分");

    // 入りきらない数の通貨(open.er-apiは約160)でも、よく使う通貨は残る
    {
        std::string big = "{\"base_code\":\"USD\",\"rates\":{";
        for(int i = 0; i < 26 * 4; i++){
            char code[8];
            snprintf(code, sizeof(code), "A%c%c", 'A' + i / 26, 'A' + i % 26); //AAA, AAB, … (JPYより前)
            big += std::string("\"") + code + "\":1.5,";
        }
        big += "\"JPY\":150,\"USD\":1}}";
        CurrencyRates m;
        check(m.parse(big.c_str(), big.size()), "100通貨以上の応答を読める");
        eqInt(m.count(), CurrencyRates::kMaxRates, "上限まで入る");
        eqStr(m.codeAt(0), "JPY", "後ろにあったJPYも入る(よく使う通貨は追い出されない)");
        eqStr(m.codeAt(1), "USD", "基準のUSDも入る");
    }

    // SDへの控え: 書いて読み戻す。url の指定は書き直しても残る
    HostSd::files.clear();
    const char* path = PICO_Path::FILE::CFG::SYS_CURRENCY_CFG;
    HostSd::files[path] = "url = https://example.com/rates\nJPY = 1\n";
    CurrencyRates a;
    check(!a.load(), "基準通貨の無い控えは読まない");
    eqStr(a.url(), "https://example.com/rates", "url の指定を読む");
    check(a.parse(kFrankfurter, strlen(kFrankfurter)), "(準備)レートを読む");
    check(a.save(), "SDへ書ける");
    const std::string saved = HostSd::files[path];
    check(saved.find("url=https://example.com/rates") != std::string::npos, "書き直しても url の指定が残る");
    check(saved.find("USD=1.12") != std::string::npos, "レートが書かれる");

    CurrencyRates b;
    check(b.load(), "控えを読み戻せる");
    eqInt(b.count(), 7, "読み戻した通貨の数");
    eqStr(b.date(), "2026-10-21", "読み戻した日付");
    eqStr(b.codeAt(0), "JPY", "読み戻しても並びは同じ");
    check(b.convert(b.find("USD"), b.find("JPY"), 1, out), "読み戻したレートで換算できる");
    near(out, 177.3 / 1.12, 1e-6, "読み戻したレートの値");

    CurrencyRates c;
    eqStr(c.url(), CurrencyRates::kDefaultUrl, "url の指定が無ければ既定の取得元");
    HostSd::files.clear();
    check(!c.load(), "控えが無ければfalse");
}

int main(){
    testProg();
    testUnits();
    testCurrency();
    printf("\n%s (%d件の失敗)\n", failures == 0 ? "すべて成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
