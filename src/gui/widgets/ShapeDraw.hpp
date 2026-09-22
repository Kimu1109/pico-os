#pragma once

#include <LovyanGFX.hpp>
#include <cmath>

// RectShape/LineShape/TriangleShapeで共有する線描画ヘルパー。
//
// LovyanGFXのdrawWideLine()はアンチエイリアスのため4bitパレットに無い中間色を
// 要求するので、OSData::frame(4bitパレットのフレームバッファ)へは使えない
// (CLAUDE.md「AnalogClockの針」参照)。太さが要る場合は、AnalogClock::drawHand()と
// 同じく進行方向に垂直な単位ベクトルへオフセットした2枚のfillTriangleで
// 塗りつぶし四角形を作る(アンチエイリアス無しで単色のまま太らせられる)。
namespace ShapeDraw {
    inline void DrawThickLine(LGFX_Sprite* frame, int x0, int y0, int x1, int y1,
                               int thickness, int8_t color) {
        if (thickness <= 1) {
            frame->drawLine(x0, y0, x1, y1, color);
            return;
        }

        const float dx = (float)(x1 - x0);
        const float dy = (float)(y1 - y0);
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-3f) {
            // 始点=終点(長さゼロ)は方向が定まらないので太らせられない。1px線で妥協する
            frame->drawLine(x0, y0, x1, y1, color);
            return;
        }

        // 進行方向に垂直な単位ベクトル
        const float nx = -dy / len;
        const float ny = dx / len;
        const float half = thickness / 2.0f;

        const int ox = (int)lroundf(nx * half);
        const int oy = (int)lroundf(ny * half);

        frame->fillTriangle(x0 + ox, y0 + oy, x1 + ox, y1 + oy, x1 - ox, y1 - oy, color);
        frame->fillTriangle(x0 + ox, y0 + oy, x1 - ox, y1 - oy, x0 - ox, y0 - oy, color);
    }
}
