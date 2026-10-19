#pragma once
// Iso_World を分けた .cpp(Iso_World / Iso_Gen / Iso_Files / Iso_Render / Iso_Entity / Iso_Td)だけが使う小さな道具
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "iso/Iso_World.hpp"

namespace Iso {
namespace Detail {

constexpr char kMagic[4] = {'B', 'L', 'K', '2'};
constexpr int kHead = 17;

// 床のある割り算(Lua の //)。b > 0
inline int fdiv(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

inline int imax(int a, int b) { return a > b ? a : b; }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int iround(float v) { return (int)floorf(v + 0.5f); }
constexpr float kEps = 0.01f;   // 人や物の箱が面で接しているのを「離れている」とみなす余裕(小数の誤差)

// 絵(32x31)の中の点 (lx, ly) がどの面か
inline Face FaceAt(int lx, int ly) {
    if (lx < 0 || lx > 31 || ly < 0 || ly > 30) return Face::None;
    if (ly <= 14) {
        const int half = (ly <= 7) ? 2 * (ly + 1) : 2 * (15 - ly);
        if (lx >= 16 - half && lx < 16 + half) return Face::Top;
    }
    if (lx < 16) {
        const int i = lx / 2;
        if (ly >= 8 + i && ly <= 23 + i) return Face::Left;
    } else {
        const int i = (lx - 16) / 2;
        if (ly >= 15 - i && ly <= 30 - i) return Face::Right;
    }
    return Face::None;
}

// SD へ丸ごと書く / offset から最大 len バイト読む(読めたバイト数、開けなければ -1)。Iso_World.cpp
bool WriteFile(const char* path, const void* data, size_t len);
int ReadFile(const char* path, uint32_t offset, void* out, size_t len);

inline uint16_t R16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

}  // namespace Detail
}  // namespace Iso
