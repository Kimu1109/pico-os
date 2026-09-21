#pragma once

#include <algorithm>
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"

class ScrollContainer : public Widget, public IBorderColor {
    private:
        std::vector<Widget*> children_;

        int sx;
        int sy;

        int s_scroll_x;
        int s_scroll_y;

        int scroll_x = 0;
        int scroll_y = 0;

        bool horizontal_scroll = false;
        bool vertical_scroll = true;

        bool is_scrolling = false;

        int max_scroll_x = 0;
        int max_scroll_y = 0;

        void updateContentBounds();

        constexpr static int SCROLL_L = 15;

    public:

        ScrollContainer(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::ScrollContainer; }

        void causeOnPressStart() override;
        void causeOnPressMove() override;

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void add(Widget* w){
            w->setParent(this);
            children_.push_back(w);
            this->needs_children_update = true;
            this->updateContentBounds();
        }

        // LayoutContainer/GridContainerと同じ理由でoverrideが必要:
        // 基底のremoveChild()は何もしないため、これが無いままWidgetFunctions::Destroy()等で
        // 子を個別に破棄すると、children_に残った破棄済みポインタをこのデストラクタが
        // もう一度deleteして二重解放になる(Lua側からpico.destroy()で子だけ消す経路で
        // 実際に踏みうる)。
        void removeChild(Widget* child) override {
            auto it = std::find(children_.begin(), children_.end(), child);
            if(it == children_.end()) return;
            children_.erase(it);
            // LayoutContainer::removeChild()と同じ理由(コメント参照)
            child->setParent(nullptr);
            this->updateContentBounds();
        }

        // 横スクロール専用のナビバー等、縦横どちらのスクロールバーを
        // 表示するかを構築時に切り替えたい用途向け。
        // 呼び出し後、既に追加済みの子がある場合に備えてcontentBoundsを再計算する。
        void setScrollAxes(bool horizontal, bool vertical){
            this->horizontal_scroll = horizontal;
            this->vertical_scroll = vertical;
            this->updateContentBounds();
        }

        // 子(Label等)のテキストを差し替えて大きさが変わった場合に呼ぶ。
        // updateContentBounds()は追加時とドラッグ開始時にしか呼ばれないため、
        // 中身を書き換えるだけの呼び出し元はこれを呼ばないとスクロール範囲が
        // 古いままになる(スクロールし過ぎて空白しか見えない/逆に短くなった
        // のにスクロールできない、のどちらも起こり得る)。既存のスクロール位置は
        // 新しい範囲へ収まるよう詰めるだけで、0へは戻さない
        // (戻したい場合はscrollToTop()と組み合わせて呼ぶこと)。
        void refreshContentBounds(){
            this->updateContentBounds();
            const int new_scroll_x = constrain(this->scroll_x, 0, this->max_scroll_x);
            const int new_scroll_y = constrain(this->scroll_y, 0, this->max_scroll_y);
            if(new_scroll_x != this->scroll_x || new_scroll_y != this->scroll_y){
                this->scroll_x = new_scroll_x;
                this->scroll_y = new_scroll_y;
            }
            this->needsRender();
            for(Widget* child : children_) child->needsRender();
        }

        // 表示中の中身が別物に変わった(=前回のスクロール位置に意味が無い)
        // ときに、先頭へ戻す。
        void scrollToTop(){
            this->scroll_x = 0;
            this->scroll_y = 0;
            this->needsRender();
            for(Widget* child : children_) child->needsRender();
        }

        Rect getScreenClipRect() const override {
            Rect dst = this->getScreenRect();
            if(horizontal_scroll){
                dst.h -= SCROLL_L;
            }
            if(vertical_scroll){
                dst.w -= SCROLL_L;
            }
            // 親がいる場合は親のクリップ範囲と交差させる
            if(parent){
                return parent->getScreenClipRect().intersection(dst);
            }
            return dst;
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }

        int getScrollOffsetX() const override { return scroll_x; }
        int getScrollOffsetY() const override { return scroll_y; }

        ~ScrollContainer(){
            for(Widget* child : children_){
                delete child;
            }
        }
};