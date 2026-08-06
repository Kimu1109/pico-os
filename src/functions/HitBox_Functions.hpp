#pragma once

#include "math.h"

namespace HitBoxFunctions {
    enum class Region {
        Inside,
        Top,
        Bottom,
        Left,
        Right
    };

    struct Rect {
        float minX, minY, maxX, maxY;
    };

    struct Point {
        float x, y;
    };

    inline Region classifyPoint(const Point& p, const Rect& r) {
        // 1. 矩形内判定
        if (p.x >= r.minX && p.x <= r.maxX &&
            p.y >= r.minY && p.y <= r.maxY) {
            return Region::Inside;
        }

        // 2. 中心を基準に正規化 (矩形を [-1,1] の正方形とみなす)
        float cx = (r.minX + r.maxX) * 0.5f;
        float cy = (r.minY + r.maxY) * 0.5f;
        float hw = (r.maxX - r.minX) * 0.5f;
        float hh = (r.maxY - r.minY) * 0.5f;

        float u = (p.x - cx) / hw;
        float v = (p.y - cy) / hh;

        // 3. 対角線との比較で上下左右を判定
        // ※ y座標が下方向に増える座標系(スクリーン系)を想定
        if (abs(v) > abs(u)) {
            return (v < 0) ? Region::Top : Region::Bottom;
        } else {
            return (u < 0) ? Region::Left : Region::Right;
        }
    }
}