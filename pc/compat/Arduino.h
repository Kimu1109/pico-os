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
// 読み取りは既定でHIGH(内部プルアップのまま何もつながっていない状態)。
// 「その先に何かがつながっている」ことを代替側で表したいときだけ read_hook を差し込む
// (compat/I2S.h がアンプの検出ピンに使う)。フックが負を返したピンは既定のHIGHになる
namespace PicoPcGpio {
    inline int (*read_hook)(int pin) = nullptr;
}
inline int  digitalRead(int pin){
    if(PicoPcGpio::read_hook){
        const int v = PicoPcGpio::read_hook(pin);
        if(v >= 0) return v;
    }
    return HIGH;
}

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

// ---- Serial(書き込みは標準出力へ、読み込みは標準入力から) ----
// 実機のUSBシリアルの代わり。読み込みは外部コントローラー(PadFunctions)が使う:
//   python3 script/pad_serial.py --stdout | ./pc/build/picoos_pc
// 標準入力は最初に available() を呼んだときから別スレッドで読み、溜めたものを read() で返す
// (read(0)はブロックするので、ループのスレッドでは読めない)。
// PICOOS_SERIAL_STDIN=off で読まない。Webは標準入力が無いので常に空
#if !defined(__EMSCRIPTEN__)
#include <cstdlib>
#include <mutex>
#include <string>
#include <unistd.h>
namespace PicoPcSerial {
    inline std::mutex mutex;
    inline std::string buffer;     // まだread()されていないバイト
    inline bool started = false;

    inline void Start() {
        std::lock_guard<std::mutex> lock(mutex);
        if (started) return;
        started = true;
        const char* env = getenv("PICOOS_SERIAL_STDIN");
        if (env && strcmp(env, "off") == 0) return;
        std::thread([] {
            char chunk[256];
            for (;;) {
                const ssize_t n = ::read(0, chunk, sizeof(chunk));
                if (n <= 0) return;     // EOF(パイプの相手が終わった)/エラー
                std::lock_guard<std::mutex> l(mutex);
                // 誰も読まないまま溜まり続けないよう頭打ちにする(実機のUSBの受信バッファ相当)
                if (buffer.size() < 4096) buffer.append(chunk, (size_t)n);
            }
        }).detach();
    }
}
#endif

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
#if defined(__EMSCRIPTEN__)
    int available(){ return 0; }
    int read(){ return -1; }
#else
    int available(){
        PicoPcSerial::Start();
        std::lock_guard<std::mutex> lock(PicoPcSerial::mutex);
        return (int)PicoPcSerial::buffer.size();
    }
    int read(){
        std::lock_guard<std::mutex> lock(PicoPcSerial::mutex);
        if (PicoPcSerial::buffer.empty()) return -1;
        const unsigned char c = (unsigned char)PicoPcSerial::buffer[0];
        PicoPcSerial::buffer.erase(0, 1);
        return c;
    }
#endif
};
inline SerialClass Serial;
