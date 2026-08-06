#pragma once

#include "widgets/Widget.hpp"

namespace WidgetFunctions
{
    inline Widget *hitTest(int16_t x, int16_t y);

    inline const int MAX_WIDGETS = 48;
    inline Widget *widgets[MAX_WIDGETS];
    inline int count = 0;

    inline Widget *pressingWidget = nullptr;

    inline void add(Widget *w)
    {
        w->visitAll([](Widget* widget){
            widgets[count++] = widget; // 追加順 = 描画順（後ろが上に乗る）
        });
    }

    inline void bringToFront(Widget *w)
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

    inline void updateAll()
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
    inline Widget *hitTest(int16_t x, int16_t y)
    {
        for (int i = count - 1; i >= 0; i--)
        {
            if (widgets[i]->Visible() && widgets[i]->hitTest(x, y))
                return widgets[i];
        }
        return nullptr;
    }
};