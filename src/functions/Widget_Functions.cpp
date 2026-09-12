#include "Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"
#include <algorithm>

void WidgetFunctions::Setup(){
    widgets.reserve(48);
    dialog_roots.reserve(9);
    overlays.reserve(4);
    pending_deletes.reserve(8);

    LOG_SYS_OK("Widget Setup has succeeded!");
}

void WidgetFunctions::Add(Widget *w)
{
    if (!w) return;
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
    if (!w) return;
    auto it = std::find(dialog_roots.begin(), dialog_roots.end(), w);
    if (it != dialog_roots.end()) {
        dialog_roots.erase(it);
    }
    dialog_roots.push_back(w);
}

void WidgetFunctions::AddOverlay(Widget *w){
    if (!w) return;
    auto it = std::find(overlays.begin(), overlays.end(), w);
    if (it != overlays.end()) {
        overlays.erase(it);
    }
    overlays.push_back(w);
}

void WidgetFunctions::Remove(Widget *w)
{
    if (!w) return;

    PICO_GFX::MarkDirty(w->getScreenRect());

    w->visitAll([](Widget* widget){
        if (pressingWidget == widget) {
            pressingWidget = nullptr;
        }
        auto it = std::find(widgets.begin(), widgets.end(), widget);
        if (it != widgets.end()) {
            widgets.erase(it);
        }
    });

    if (w->getParent()) {
        w->getParent()->removeChild(w);
    }
}

void WidgetFunctions::RemoveDialog(Widget *w)
{
    if (!w) return;

    PICO_GFX::MarkDirty(w->getScreenRect());

    w->visitAll([](Widget* widget){
        if (pressingWidget == widget) {
            pressingWidget = nullptr;
        }
    });

    auto it = std::find(dialog_roots.begin(), dialog_roots.end(), w);
    if (it != dialog_roots.end()) {
        dialog_roots.erase(it);
    }
}

void WidgetFunctions::RemoveOverlay(Widget *w)
{
    if (!w) return;

    PICO_GFX::MarkDirty(w->getScreenRect());

    w->visitAll([](Widget* widget){
        if (pressingWidget == widget) {
            pressingWidget = nullptr;
        }
    });

    auto it = std::find(overlays.begin(), overlays.end(), w);
    if (it != overlays.end()) {
        overlays.erase(it);
    }
}

void WidgetFunctions::RemoveAny(Widget *w)
{
    if (!w) return;

    PICO_GFX::MarkDirty(w->getScreenRect());

    auto o_it = std::find(overlays.begin(), overlays.end(), w);
    if (o_it != overlays.end()) {
        overlays.erase(o_it);
    }

    auto d_it = std::find(dialog_roots.begin(), dialog_roots.end(), w);
    if (d_it != dialog_roots.end()) {
        dialog_roots.erase(d_it);
    }

    w->visitAll([](Widget* widget){
        if (pressingWidget == widget) {
            pressingWidget = nullptr;
        }
        auto it = std::find(widgets.begin(), widgets.end(), widget);
        if (it != widgets.end()) {
            widgets.erase(it);
        }
    });

    if (w->getParent()) {
        w->getParent()->removeChild(w);
    }
}

void WidgetFunctions::Destroy(Widget *w)
{
    if (!w) return;
    RemoveAny(w);
    delete w;
}

void WidgetFunctions::DestroyLater(Widget *w)
{
    if (!w) return;

    w->setVisible(false);

    w->visitAll([](Widget* widget){
        if (pressingWidget == widget) {
            pressingWidget = nullptr;
        }
    });

    for (auto* p : pending_deletes) {
        if (p == w) return;
    }
    pending_deletes.push_back(w);
}

void WidgetFunctions::ProcessPendingDeletes()
{
    if (pending_deletes.empty()) return;

    std::vector<Widget*> to_delete = std::move(pending_deletes);
    pending_deletes.clear();

    for (auto* w : to_delete) {
        RemoveAny(w);
        delete w;
    }
}

void WidgetFunctions::ClearSceneWidgets()
{
    // 先に予約済みのdeleteを消化しておく(二重管理を防ぐ)
    ProcessPendingDeletes();

    // ダイアログはシーン内ウィジェットのthisをコールバックに捕捉しているため、
    // 通常レイヤより先に破棄する
    while (!dialog_roots.empty()) {
        Destroy(dialog_roots.back());
    }

    // widgetsにはルートと子孫が混在している。子孫は親のデストラクタが解放するので、
    // 親を持たないルートのみを破棄する。
    // Destroy() -> RemoveAny() が子孫をwidgetsから取り除くのでループ中にvectorが縮む。
    // そのため毎回先頭から探し直す(遷移時のみの処理なのでコストは問題にならない)
    bool found_root = true;
    while (found_root) {
        found_root = false;
        for (Widget* w : widgets) {
            if (w && !w->getParent()) {
                Destroy(w);
                found_root = true;
                break;
            }
        }
    }

    // ここに残るのは「ルートがwidgetsに登録されていない子ウィジェット」だけで、
    // 本来は発生しない。残っていた場合は解放責任が持てないので、
    // ダングリング参照を避けるためリストからのみ外して警告を出す
    if (!widgets.empty()) {
        LOG_SYS_WARN("シーン破棄後に親不明のウィジェットが%d件残りました", (int)widgets.size());
        widgets.clear();
    }

    pressingWidget = nullptr;
}

void WidgetFunctions::BringToFront(Widget *w)
{
    auto it = std::find(widgets.begin(), widgets.end(), w);
    if(it != widgets.end()){
        widgets.erase(it);
        widgets.push_back(w);
    }
    auto d_it = std::find(dialog_roots.begin(), dialog_roots.end(), w);
    if(d_it != dialog_roots.end()){
        dialog_roots.erase(d_it);
        dialog_roots.push_back(w);
    }
    auto o_it = std::find(overlays.begin(), overlays.end(), w);
    if(o_it != overlays.end()){
        overlays.erase(o_it);
        overlays.push_back(w);
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

    // 1. 通常ウィジェット描画（下層）
    for (size_t i = 0; i < widgets.size(); i++){
        if (!widgets[i]) continue;
        if(widgets[i]->getChildrenUpdate()){
            Add(widgets[i]);
            widgets[i]->setChildrenUpdate(false);
        }
        if(!widgets[i]->getVisible()) continue;
        const Rect clipped = widgets[i]->clippedScreenRect();
        OSData::frame->setClipRect(clipped.x, clipped.y, clipped.w, clipped.h);
        widgets[i]->update(); // 下から順に描画
        OSData::frame->clearClipRect();
    }

    // 2. ダイアログ描画（中層: 0 から順に描画し、後から開いたダイアログが上に重なる）
    for (size_t d = 0; d < dialog_roots.size(); d++) {
        if (dialog_roots[d] && dialog_roots[d]->getVisible()) {
            dialog_roots[d]->visitAll([](Widget* w) {
                if (!w || !w->getVisible()) return;
                const Rect clipped = w->clippedScreenRect();
                OSData::frame->setClipRect(clipped.x, clipped.y, clipped.w, clipped.h);
                w->update();
                OSData::frame->clearClipRect();
            });
        }
    }

    // 3. 最前面オーバーレイ描画（上層: キーボードやステータスバー）
    for (size_t o = 0; o < overlays.size(); o++) {
        if (overlays[o] && overlays[o]->getVisible()) {
            overlays[o]->visitAll([](Widget* w) {
                if (!w || !w->getVisible()) return;
                const Rect clipped = w->clippedScreenRect();
                OSData::frame->setClipRect(clipped.x, clipped.y, clipped.w, clipped.h);
                w->update();
                OSData::frame->clearClipRect();
            });
        }
    }

    ProcessPendingDeletes();
}

// タッチは上から順に判定
Widget *WidgetFunctions::HitTest(int16_t x, int16_t y)
{
    // 1. 最前面オーバーレイのタッチ判定（最優先）
    for (int o = (int)overlays.size() - 1; o >= 0; o--)
    {
        Widget* root = overlays[o];
        if(!root || !root->getVisible()) continue;

        std::vector<Widget*> list;
        root->visitAll([&list](Widget* w){
            if (w) list.push_back(w);
        });

        // 子（末尾）から親（先頭）の順で判定
        for (int i = (int)list.size() - 1; i >= 0; i--)
        {
            if(list[i] && list[i]->getVisible() && list[i]->hitTest(x, y)){
                return list[i];
            }
        }
    }

    // 2. ダイアログのタッチ判定（末尾＝新しく開いたダイアログから判定）
    for (int d = (int)dialog_roots.size() - 1; d >= 0; d--)
    {
        Widget* root = dialog_roots[d];
        if(!root || !root->getVisible()) continue;

        std::vector<Widget*> list;
        root->visitAll([&list](Widget* w){
            if (w) list.push_back(w);
        });

        // ダイアログ内部は子（末尾）から親（先頭）の順で判定
        for (int i = (int)list.size() - 1; i >= 0; i--)
        {
            if(list[i] && list[i]->getVisible() && list[i]->hitTest(x, y)){
                return list[i];
            }
        }
    }

    // 3. 通常ウィジェットの判定（末尾から逆順）
    for (int i = (int)widgets.size() - 1; i >= 0; i--)
    {
        if (widgets[i] && widgets[i]->getVisible() && widgets[i]->hitTest(x, y))
            return widgets[i];
    }
    return nullptr;
}