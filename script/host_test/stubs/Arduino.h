// ホストテスト(script/host_test/run.sh)専用のダミーヘッダ。実機ビルドでは使われない。
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstddef>
static inline unsigned long millis(){ return 0; }
#define LED_BUILTIN 0

// Arduinoコアが提供している min/max/constrain。実機側のコードがそのまま使っているので用意する。
// 実機と違いマクロではなく関数テンプレートにしてある。マクロにすると
// std::numeric_limits<T>::min() のような標準ライブラリ側の名前まで置換してしまい、
// LovyanGFXや<limits>を巻き込んでコンパイルが通らなくなるため
template<typename T, typename U>
constexpr auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template<typename T, typename U>
constexpr auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
template<typename T, typename L, typename H>
constexpr T constrain(T v, L lo, H hi) {
    return (v < lo) ? (T)lo : ((v > hi) ? (T)hi : v);
}
