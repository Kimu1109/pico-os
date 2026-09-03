#pragma once

#include "gui/widgets/Widget.hpp"

#include <vector>

namespace WidgetFunctions
{
    inline std::vector<Widget*> widgets;
    inline std::vector<Widget*> dialog_roots;
    inline std::vector<Widget*> pending_deletes;

    inline Widget *pressingWidget = nullptr;

    void Setup();

    void Add(Widget *w);
    void AddDialog(Widget *w);

    void Remove(Widget *w);
    void RemoveDialog(Widget *w);
    void RemoveAny(Widget *w);

    void Destroy(Widget *w);
    void DestroyLater(Widget *w);

    void ProcessPendingDeletes();

    void BringToFront(Widget *w);

    void UpdateAll();

    // タッチは上から順に判定
    Widget *HitTest(int16_t x, int16_t y);
};