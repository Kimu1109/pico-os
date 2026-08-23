#include "Widget_Functions.hpp"
#include "OS_Data.hpp"

void WidgetFunctions::add(Widget *w)
{
    w->visitAll([](Widget* widget){
        bool validates = true;
        for(int i = 0; i < count; i++){
            if(widgets[i] && widgets[i] == widget){
                validates = false;
                break;
            }
        }
        if(validates){
            widgets[count++] = widget; // 追加順 = 描画順（後ろが上に乗る）
        }
    });
}

void WidgetFunctions::addDialog(Widget *w){
    for(int i = 0; i < count_dialog; i++){
        if(dialog_roots[i] && dialog_roots[i] == w){
            return;
        }
    }
    if(count_dialog < MAX_DIALOG_ROOTS){
        dialog_roots[count_dialog++] = w;
    }
}

void WidgetFunctions::bringToFront(Widget *w)
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

void WidgetFunctions::updateAll()
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

    for (int i = 0; i < count; i++){
        if(widgets[i]->childrenUpdate()){
            add(widgets[i]);
            widgets[i]->childrenUpdate(false);
        }
        const Rect clipped = widgets[i]->clippedScreenRect();
        OSData::frame->setClipRect(clipped.x, clipped.y, clipped.w, clipped.h);
        widgets[i]->update(); // 下から順に描画
        OSData::frame->clearClipRect();
    }

    for (int d = count_dialog - 1; d >= 0; d--) {
        if (dialog_roots[d] && dialog_roots[d]->Visible()) {
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
Widget *WidgetFunctions::hitTest(int16_t x, int16_t y)
{
    // 1. ダイアログのタッチ判定（最初に追加されたダイアログが最優先）
    for (int d = 0; d < count_dialog; d++)
    {
        Widget* root = dialog_roots[d];
        if(!root || !root->Visible()) continue;

        std::vector<Widget*> list;
        root->visitAll([&list](Widget* w){
            list.push_back(w);
        });

        // ダイアログ内部は子（末尾）から親（先頭）の順で判定
        for (int i = (int)list.size() - 1; i >= 0; i--)
        {
            if(list[i]->Visible() && list[i]->hitTest(x, y)){
                return list[i];
            }
        }
    }

    // 2. 通常ウィジェットの判定（末尾から逆順）
    for (int i = count - 1; i >= 0; i--)
    {
        if (widgets[i]->Visible() && widgets[i]->hitTest(x, y))
            return widgets[i];
    }
    return nullptr;
}