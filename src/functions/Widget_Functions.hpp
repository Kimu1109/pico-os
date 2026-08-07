#pragma once

#include "widgets/Widget.hpp"

namespace WidgetFunctions
{
    inline Widget *hitTest(int16_t x, int16_t y);

    inline const int MAX_WIDGETS = 48;
    inline Widget *widgets[MAX_WIDGETS];
    inline int count = 0;

    inline Widget *pressingWidget = nullptr;

    void add(Widget *w);
    inline void bringToFront(Widget *w);

    void updateAll();

    // タッチは上（配列末尾）から順に判定
    inline Widget *hitTest(int16_t x, int16_t y);
};