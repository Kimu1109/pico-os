#pragma once

#include "widgets/Widget.hpp"

namespace WidgetFunctions
{
    inline const int MAX_WIDGETS = 48;
    inline const int MAX_DIALOG_ROOTS = 8;
    inline Widget *widgets[MAX_WIDGETS];
    inline Widget *dialog_roots[MAX_DIALOG_ROOTS];
    inline int count = 0;
    inline int count_dialog = 0;

    inline Widget *pressingWidget = nullptr;

    void add(Widget *w);
    void addDialog(Widget *w);

    void bringToFront(Widget *w);

    void updateAll();

    // タッチは上から順に判定
    Widget *hitTest(int16_t x, int16_t y);
};