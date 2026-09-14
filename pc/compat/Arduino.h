// PCビルド用のArduinoコア代替。
//
// 実機のArduino.hの代わりにインクルードパスの先頭から拾わせる(src/には手を入れない)。
// pico-osが実際に使っているのは millis / Serial / pinMode系 / map / constrain 程度なので、
// そこだけを用意する。
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

// ---- 時間 ----
inline unsigned long millis() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - start).count();
}
inline unsigned long micros() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return (unsigned long)duration_cast<microseconds>(steady_clock::now() - start).count();
}
// Webビルドではメインスレッドを止められない(止めた分だけタブが固まり、
// 描画もイベントも進まない)ので、待たずに戻る。
// src/ は delay() を使っていないので実害は無いが、使うときは
// 「ブラウザでは効かない」と思って組むこと
inline void delay(unsigned long ms) {
#if defined(__EMSCRIPTEN__)
    (void)ms;
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

// ---- GPIO(PCでは意味を持たないので受け流す) ----
#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2
#define LOW          0
#define HIGH         1
#define LED_BUILTIN  0

inline void pinMode(int, int){}
inline void digitalWrite(int, int){}
inline int  digitalRead(int){ return HIGH; }

// ---- Arduinoの定番マクロ(実機はマクロだが、標準ライブラリと衝突しないよう関数にする) ----
template<typename T, typename U>
constexpr auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template<typename T, typename U>
constexpr auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
template<typename T, typename L, typename H>
constexpr T constrain(T v, L lo, H hi) {
    return (v < lo) ? (T)lo : ((v > hi) ? (T)hi : v);
}
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ---- Serial(標準出力へ流す) ----
class SerialClass {
public:
    void begin(unsigned long = 0){}
    void printf(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        fflush(stdout);
    }
    void print(const char* s){ if(s) fputs(s, stdout); }
    void println(const char* s = ""){ if(s) fputs(s, stdout); fputc('\n', stdout); fflush(stdout); }
};
inline SerialClass Serial;
