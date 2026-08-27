#pragma once

#include "gui/widgets/Widget.hpp"

namespace WidgetFunctions
{
    inline const int MAX_WIDGETS = 48;
    inline const int MAX_DIALOG_ROOTS = 8;
    inline Widget *widgets[MAX_WIDGETS];
    inline Widget *dialog_roots[MAX_DIALOG_ROOTS];
    inline int count = 0;
    inline int count_dialog = 0;

    inline Widget *pressingWidget = nullptr;

    void Add(Widget *w);
    void AddDialog(Widget *w);

    void BringToFront(Widget *w);

    void UpdateAll();

    // タッチは上から順に判定
    Widget *HitTest(int16_t x, int16_t y);
};