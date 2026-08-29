#pragma once

#include "gui/widgets/Widget.hpp"

#include <vector>

namespace WidgetFunctions
{
    inline std::vector<Widget*> widgets;
    inline std::vector<Widget*> dialog_roots;

    inline Widget *pressingWidget = nullptr;

    void Add(Widget *w);
    void AddDialog(Widget *w);

    void BringToFront(Widget *w);

    void UpdateAll();

    // タッチは上から順に判定
    Widget *HitTest(int16_t x, int16_t y);
};