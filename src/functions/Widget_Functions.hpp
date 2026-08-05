#pragma once

#include "widgets/Widget.hpp"

namespace WidgetFunctions
{
    Widget *hitTest(int16_t x, int16_t y);

    const int MAX_WIDGETS = 48;
    Widget *widgets[MAX_WIDGETS];
    int count = 0;

    Widget *pressingWidget = nullptr;

    void add(Widget *w)
    {
        widgets[count++] = w; // 追加順 = 描画順（後ろが上に乗る）
    }

    void bringToFront(Widget *w)
    {
        int idx = -1;
        for (int i = 0; i < count; i++)
            if (widgets[i] == w)
            {
                idx = i;
                break;
            }
        if (idx < 0)
            return;
        for (int i = idx; i < count - 1; i++)
            widgets[i] = widgets[i + 1];
        widgets[count - 1] = w;
    }

    void updateAll()
    {
        if(OSData::isTouchStart){
            pressingWidget = hitTest(OSData::touchX, OSData::touchY);
            if(pressingWidget){
                pressingWidget->is_pressing = true;
                pressingWidget->onPressStart();
            }
        }
        if(OSData::isTouchEnd && pressingWidget){
            pressingWidget->is_pressing = false;
            pressingWidget->onPressEnd();
            pressingWidget = nullptr;
        }

        for (int i = 0; i < count; i++)
            widgets[i]->update(); // 下から順に描画
    }

    // タッチは上（配列末尾）から順に判定
    Widget *hitTest(int16_t x, int16_t y)
    {
        for (int i = count - 1; i >= 0; i--)
        {
            if (widgets[i]->hitTest(x, y))
                return widgets[i];
        }
        return nullptr;
    }
};