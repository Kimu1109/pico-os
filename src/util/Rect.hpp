#pragma once

#include "Arduino.h"
#include "consts.hpp"

struct Rect {
    int16_t x, y, w, h;

    bool intersects(const Rect& o) const {
        return !(x + w <= o.x || o.x + o.w <= x ||
                 y + h <= o.y || o.y + o.h <= y);
    }
    bool contains(const Rect& o) const {
        return x <= o.x && y <= o.y &&
               (x + w) >= (o.x + o.w) && (y + h) >= (o.y + o.h);
    }
    Rect intersection(const Rect& o) const {
        int16_t nx = std::max(x, o.x);
        int16_t ny = std::max(y, o.y);
        int16_t nx2 = std::min(x + w, o.x + o.w);
        int16_t ny2 = std::min(y + h, o.y + o.h);
        return { nx, ny, (int16_t)std::max(0, nx2 - nx), (int16_t)std::max(0, ny2 - ny) };
    }
    void copy(const Rect& o) {
        x = o.x;
        y = o.y;

        w = o.w;
        h = o.h;
    }
    
    bool operator==(const Rect& other) const {
        return x == other.x && y == other.y && w == other.w && h == other.h;
    }
    bool operator!=(const Rect& other) const {
        return !(*this == other);
    }
};