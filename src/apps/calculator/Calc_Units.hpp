// src/apps/calculator/Calc_Units.hpp
//
// 電卓の「変換」ページの単位の表と換算。Calc_Evalと同じく純粋な計算だけ(ヒープ非使用)。
//
// 単位はどれも「基準の単位へ直す1次式」 基準 = 値 × factor + offset で表す。
// offsetが要るのは温度だけ(基準はケルビン)。他の種類はoffset=0で、factorが基準の単位いくつぶんか。
// 表はconstなので実機ではフラッシュに置かれる(RAMを食わない)。
//
// 通貨はレートがネットワークから来るのでここには無い(Calc_Currency)。
// 尺貫法・畳・坪・合/升のような日本の単位も入れてある(畳は不動産の表示規約の1.62m²)。
#pragma once

#include <cstdint>
#include "system/I18n_Functions.hpp"

namespace CalcUnits {

    struct Unit {
        const char* symbol; // ボタンに出す短い名前
        const char* name;   // 選ぶ一覧に出す名前
        double factor;
        double offset;
    };

    struct Category {
        const char* name;
        const Unit* units;
        uint8_t count;
        uint8_t default_from;
        uint8_t default_to;
    };

    namespace detail {
        constexpr double kPi = 3.14159265358979323846;

        constexpr Unit kLength[] = {
            { "m",   TR_NOOP("メートル"),     1.0,        0 },
            { "km",  TR_NOOP("キロメートル"), 1000.0,     0 },
            { "cm",  TR_NOOP("センチメートル"), 0.01,     0 },
            { "mm",  TR_NOOP("ミリメートル"), 0.001,      0 },
            { "in",  TR_NOOP("インチ"),       0.0254,     0 },
            { "ft",  TR_NOOP("フィート"),     0.3048,     0 },
            { "yd",  TR_NOOP("ヤード"),       0.9144,     0 },
            { "mi",  TR_NOOP("マイル"),       1609.344,   0 },
            { "海里", TR_NOOP("海里"),        1852.0,     0 },
            { "尺",  TR_NOOP("尺"),           10.0 / 33.0, 0 },
            { "寸",  TR_NOOP("寸"),           1.0 / 33.0,  0 },
            { "里",  TR_NOOP("里"),           12960.0 / 3.3, 0 },
        };
        constexpr Unit kMass[] = {
            { "kg",  TR_NOOP("キログラム"), 1.0,            0 },
            { "g",   TR_NOOP("グラム"),     0.001,          0 },
            { "mg",  TR_NOOP("ミリグラム"), 0.000001,       0 },
            { "t",   TR_NOOP("トン"),       1000.0,         0 },
            { "lb",  TR_NOOP("ポンド"),     0.45359237,     0 },
            { "oz",  TR_NOOP("オンス"),     0.028349523125, 0 },
            { "ct",  TR_NOOP("カラット"),   0.0002,         0 },
            { "貫",  TR_NOOP("貫"),         3.75,           0 },
            { "匁",  TR_NOOP("匁"),         0.00375,        0 },
        };
        constexpr Unit kArea[] = {
            { "m²",  TR_NOOP("平方メートル"), 1.0,          0 },
            { "km²", TR_NOOP("平方キロメートル"), 1000000.0, 0 },
            { "cm²", TR_NOOP("平方センチメートル"), 0.0001, 0 },
            { "a",   TR_NOOP("アール"),       100.0,        0 },
            { "ha",  TR_NOOP("ヘクタール"),   10000.0,      0 },
            { "ft²", TR_NOOP("平方フィート"), 0.09290304,   0 },
            { "ac",  TR_NOOP("エーカー"),     4046.8564224, 0 },
            { "坪",  TR_NOOP("坪"),           400.0 / 121.0, 0 },
            { "畳",  TR_NOOP("畳"),           1.62,         0 },
        };
        constexpr Unit kVolume[] = {
            { "L",    TR_NOOP("リットル"),         1.0,                0 },
            { "mL",   TR_NOOP("ミリリットル"),     0.001,              0 },
            { "m³",   TR_NOOP("立方メートル"),     1000.0,             0 },
            { "gal",  TR_NOOP("ガロン(米)"),       3.785411784,        0 },
            { "qt",   TR_NOOP("クォート(米)"),     0.946352946,        0 },
            { "cup",  TR_NOOP("カップ(米)"),       0.2365882365,       0 },
            { "floz", TR_NOOP("液量オンス(米)"),   0.0295735295625,    0 },
            { "大さじ", TR_NOOP("大さじ"),         0.015,              0 },
            { "小さじ", TR_NOOP("小さじ"),         0.005,              0 },
            { "合",   TR_NOOP("合"),               2.401 / 13.31,      0 },
            { "升",   TR_NOOP("升"),               24.01 / 13.31,      0 },
        };
        // 基準はケルビン。°C = K - 273.15、°F = °C × 9/5 + 32(℉は日本語フォントに無いので°C/°Fと書く)
        constexpr Unit kTemperature[] = {
            { "°C", TR_NOOP("摂氏"),       1.0,       273.15 },
            { "°F", TR_NOOP("華氏"),       5.0 / 9.0, 273.15 - 32.0 * 5.0 / 9.0 },
            { "K",  TR_NOOP("ケルビン"),   1.0,       0 },
        };
        constexpr Unit kSpeed[] = {
            { "km/h", TR_NOOP("キロメートル毎時"), 1.0 / 3.6,      0 },
            { "m/s",  TR_NOOP("メートル毎秒"),     1.0,            0 },
            { "mph",  TR_NOOP("マイル毎時"),       0.44704,        0 },
            { "kt",   TR_NOOP("ノット"),           1852.0 / 3600.0, 0 },
            { "ft/s", TR_NOOP("フィート毎秒"),     0.3048,         0 },
            { TR_NOOP("マッハ"), TR_NOOP("マッハ(15°C)"),    340.0,          0 },
        };
        constexpr Unit kTime[] = {
            { "秒",  TR_NOOP("秒"),       1.0,        0 },
            { "ms",  TR_NOOP("ミリ秒"),   0.001,      0 },
            { "分",  TR_NOOP("分"),       60.0,       0 },
            { "時間", TR_NOOP("時間"),    3600.0,     0 },
            { "日",  TR_NOOP("日"),       86400.0,    0 },
            { "週",  TR_NOOP("週"),       604800.0,   0 },
            { TR_NOOP("年"),  TR_NOOP("年(365日)"), 31536000.0, 0 },
        };
        // 基準はバイト。KB等は1000倍、KiB等は1024倍
        constexpr Unit kData[] = {
            { "B",   TR_NOOP("バイト"),     1.0,                 0 },
            { "bit", TR_NOOP("ビット"),     0.125,               0 },
            { "KB",  TR_NOOP("キロバイト"), 1e3,                 0 },
            { "MB",  TR_NOOP("メガバイト"), 1e6,                 0 },
            { "GB",  TR_NOOP("ギガバイト"), 1e9,                 0 },
            { "TB",  TR_NOOP("テラバイト"), 1e12,                0 },
            { "KiB", TR_NOOP("キビバイト"), 1024.0,              0 },
            { "MiB", TR_NOOP("メビバイト"), 1048576.0,           0 },
            { "GiB", TR_NOOP("ギビバイト"), 1073741824.0,        0 },
        };
        constexpr Unit kPressure[] = {
            { "hPa",  TR_NOOP("ヘクトパスカル"), 100.0,              0 },
            { "Pa",   TR_NOOP("パスカル"),       1.0,                0 },
            { "kPa",  TR_NOOP("キロパスカル"),   1000.0,             0 },
            { "MPa",  TR_NOOP("メガパスカル"),   1e6,                0 },
            { "atm",  TR_NOOP("気圧"),           101325.0,           0 },
            { "bar",  TR_NOOP("バール"),         100000.0,           0 },
            { "mmHg", TR_NOOP("水銀柱ミリメートル"), 133.322387415,  0 },
            { "psi",  TR_NOOP("重量ポンド毎平方インチ"), 6894.757293168, 0 },
        };
        constexpr Unit kEnergy[] = {
            { "kcal", TR_NOOP("キロカロリー"),   4184.0,    0 },
            { "kJ",   TR_NOOP("キロジュール"),   1000.0,    0 },
            { "J",    TR_NOOP("ジュール"),       1.0,       0 },
            { "cal",  TR_NOOP("カロリー"),       4.184,     0 },
            { "Wh",   TR_NOOP("ワット時"),       3600.0,    0 },
            { "kWh",  TR_NOOP("キロワット時"),   3.6e6,     0 },
            { "eV",   TR_NOOP("電子ボルト"),     1.602176634e-19, 0 },
        };
        constexpr Unit kAngle[] = {
            { "°",    TR_NOOP("度"),           1.0,             0 },
            { "rad",  TR_NOOP("ラジアン"),     180.0 / kPi,     0 },
            { "grad", TR_NOOP("グラード"),     0.9,             0 },
            { "'",    TR_NOOP("分(角度)"),     1.0 / 60.0,      0 },
            { "\"",   TR_NOOP("秒(角度)"),     1.0 / 3600.0,    0 },
            { "回転", TR_NOOP("回転"),         360.0,           0 },
        };

        template <typename T, int N>
        constexpr uint8_t CountOf(const T (&)[N]){ return (uint8_t)N; }
    }

    constexpr Category kCategories[] = {
        { TR_NOOP("長さ"),       detail::kLength,      detail::CountOf(detail::kLength),      1, 7 },
        { TR_NOOP("重さ"),       detail::kMass,        detail::CountOf(detail::kMass),        0, 4 },
        { TR_NOOP("面積"),       detail::kArea,        detail::CountOf(detail::kArea),        0, 7 },
        { TR_NOOP("体積"),       detail::kVolume,      detail::CountOf(detail::kVolume),      0, 3 },
        { TR_NOOP("温度"),       detail::kTemperature, detail::CountOf(detail::kTemperature), 0, 1 },
        { TR_NOOP("速さ"),       detail::kSpeed,       detail::CountOf(detail::kSpeed),       0, 1 },
        { TR_NOOP("時間"),       detail::kTime,        detail::CountOf(detail::kTime),        3, 2 },
        { TR_NOOP("データ量"),   detail::kData,        detail::CountOf(detail::kData),        4, 8 },
        { TR_NOOP("圧力"),       detail::kPressure,    detail::CountOf(detail::kPressure),    0, 4 },
        { TR_NOOP("エネルギー"), detail::kEnergy,      detail::CountOf(detail::kEnergy),      0, 1 },
        { TR_NOOP("角度"),       detail::kAngle,       detail::CountOf(detail::kAngle),       0, 1 },
    };
    constexpr int kCategoryCount = (int)(sizeof(kCategories) / sizeof(kCategories[0]));

    // cat の from 単位の値 v を to 単位へ。範囲外の番号ならfalse
    inline bool Convert(int cat, int from, int to, double v, double& out){
        if (cat < 0 || cat >= kCategoryCount) return false;
        const Category& c = kCategories[cat];
        if (from < 0 || from >= c.count || to < 0 || to >= c.count) return false;
        const Unit& a = c.units[from];
        const Unit& b = c.units[to];
        if (from == to) { out = v; return true; } // 同じ単位は桁を落とさずにそのまま
        const double base = v * a.factor + a.offset;
        out = (base - b.offset) / b.factor;
        return true;
    }
}
