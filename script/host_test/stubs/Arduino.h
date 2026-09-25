// ホストテスト(script/host_test/run.sh)専用のダミーヘッダ。実機ビルドでは使われない。
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstddef>
#include <type_traits>
static inline unsigned long millis(){ return 0; }
static inline unsigned long micros(){ return 0; }
#define LED_BUILTIN 0

// Arduinoコアが提供している min/max/constrain。実機側のコードがそのまま使っているので用意する。
// 実機と違いマクロではなく関数テンプレートにしてある。マクロにすると
// std::numeric_limits<T>::min() のような標準ライブラリ側の名前まで置換してしまい、
// LovyanGFXや<limits>を巻き込んでコンパイルが通らなくなるため
//
// 戻り値型はdecltype(a<b?a:b)ではなくstd::common_type_tにしてある。T==Uのとき、
// 三項演算子は両辺とも同じ型の左辺値(a/b自身)なので、decltypeで受けると参照型に
// 推論され、関数を抜けた時点で仮引数を指す参照を返してしまう
// (NumberSlider::updateTextW()のmax(int,int)呼び出しでASanのstack-use-after-returnとして発覚)。
// common_type_tは常に値型になるためこの問題が起きない
template<typename T, typename U>
constexpr auto min(T a, U b) -> std::common_type_t<T, U> { return (a < b) ? a : b; }
template<typename T, typename U>
constexpr auto max(T a, U b) -> std::common_type_t<T, U> { return (a > b) ? a : b; }
template<typename T, typename L, typename H>
constexpr T constrain(T v, L lo, H hi) {
    return (v < lo) ? (T)lo : ((v > hi) ? (T)hi : v);
}

// ---- GPIO ----
// 読み取りは既定でHIGH(内部プルアップのまま何もつながっていない状態)。
// テストが「その先に何かがつながっている」状態を作りたいときだけ read_hook を差し込む
// (pc/compat/Arduino.h と同じ形。sound_testがアンプの検出ピンに使う)。
// 負を返したピンは既定のHIGHになる
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2
#define LOW          0
#define HIGH         1
namespace HostGpio {
    inline int (*read_hook)(int pin) = nullptr;
    inline int last_written[64] = {};    //digitalWrite()の最後の値(ピンごと)
}
static inline void pinMode(int, int){}
static inline void digitalWrite(int pin, int v){ if(pin >= 0 && pin < 64) HostGpio::last_written[pin] = v; }
static inline int  digitalRead(int pin){
    if(HostGpio::read_hook){
        const int v = HostGpio::read_hook(pin);
        if(v >= 0) return v;
    }
    return HIGH;
}
