#include "Widget_Functions.hpp"
#include "OS_Data.hpp"
#include <algorithm>

void WidgetFunctions::Add(Widget *w)
{
    w->visitAll([](Widget* widget){
        for(auto* existing : widgets){
            if(existing == widget){
                return;
            }
        }
        widgets.push_back(widget); // 追加順 = 描画順（後ろが上に乗る）
    });
}

void WidgetFunctions::AddDialog(Widget *w){
    for(auto* existing : dialog_roots){
        if(existing == w){
            return;
        }
    }
    dialog_roots.push_back(w);
}

void WidgetFunctions::BringToFront(Widget *w)
{
    auto it = std::find(widgets.begin(), widgets.end(), w);
    if(it != widgets.end()){
        widgets.erase(it);
        widgets.push_back(w);
    }
}

void WidgetFunctions::UpdateAll()
{
    if(OSData::isTouchStart){
        pressingWidget = HitTest(OSData::touchX, OSData::touchY);
        if(pressingWidget){
            pressingWidget->is_pressing = true;
            pressingWidget->causeOnPressStart();
        }
    }
    if(OSData::isTouchEnd && pressingWidget){
        pressingWidget->is_pressing = false;
        pressingWidget->causeOnPressEnd();
        pressingWidget = nullptr;
    }

    for (size_t i = 0; i < widgets.size(); i++){
        if(widgets[i]->getChildrenUpdate()){
            Add(widgets[i]);
            widgets[i]->setChildrenUpdate(false);
        }
        const Rect clipped = widgets[i]->clippedScreenRect();
        OSData::frame->setClipRect(clipped.x, clipped.y, clipped.w, clipped.h);
        widgets[i]->update(); // 下から順に描画
        OSData::frame->clearClipRect();
    }

    for (int d = (int)dialog_roots.size() - 1; d >= 0; d--) {
        if (dialog_roots[d] && dialog_roots[d]->getVisible()) {
            dialog_roots[d]->visitAll([](Widget* w) {
                const Rect clipped = w->clippedScreenRect();
                OSData::frame->setClipRect(clipped.x, clipped.y, clipped.w, clipped.h);
                w->update();
                OSData::frame->clearClipRect();
            });
        }
    }
}

// タッチは上から順に判定
Widget *WidgetFunctions::HitTest(int16_t x, int16_t y)
{
    // 1. ダイアログのタッチ判定（最初に追加されたダイアログが最優先）
    for (size_t d = 0; d < dialog_roots.size(); d++)
    {
        Widget* root = dialog_roots[d];
        if(!root || !root->getVisible()) continue;

        std::vector<Widget*> list;
        root->visitAll([&list](Widget* w){
            list.push_back(w);
        });

        // ダイアログ内部は子（末尾）から親（先頭）の順で判定
        for (int i = (int)list.size() - 1; i >= 0; i--)
        {
            if(list[i]->getVisible() && list[i]->hitTest(x, y)){
                return list[i];
            }
        }
    }

    // 2. 通常ウィジェットの判定（末尾から逆順）
    for (int i = (int)widgets.size() - 1; i >= 0; i--)
    {
        if (widgets[i]->getVisible() && widgets[i]->hitTest(x, y))
            return widgets[i];
    }
    return nullptr;
}