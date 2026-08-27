#pragma once

#include <vector>
#include <functional>
#include "model/Rect.hpp"
#include "consts.hpp"

namespace WidgetTools {
    enum RenderMode {
        OPAQUE,        // 完全に不透明
        CLEAR,         // 完全に透明、背景を再描画
        TRANSLUCENT    // 部分的に透明、背景は再描画しない
    };
};

class Widget {
    protected:
        Rect l_rect; //ローカル座標
        Rect prev_l_rect; //過去ローカル座標

        Widget* parent = nullptr; //親ウィジェット(基点座標)

        bool needs_redraw = true;
        bool visible = true;
        bool needs_children_update = false;

        //親ウィジェットが描画の反映のすべてを保証する場合に便利です
        bool disable_markdirty = false;

        int8_t background_color = PICO_BACKGROUND;

        std::function<void()> on_press_start = nullptr;
        std::function<void()> on_press_move = nullptr;
        std::function<void()> on_press_end = nullptr;
        std::function<void()> on_press_out = nullptr;
    
    public:
        bool is_pressing = false;

        virtual const std::vector<Widget*>& getChildren() const {
            static const std::vector<Widget*> empty;
            return empty;
        }

        template<typename F>
        void visitAll(F&& visitor) {
            visitor(this);
            for (Widget* child : getChildren()) {
                child->visitAll(visitor);
            }
        }

        void update();

        //タッチ開始
        virtual void causeOnPressStart();
        virtual void clearOnPressStart();
        virtual void setOnPressStart(std::function<void()> callback);

        //タッチ終了
        virtual void causeOnPressEnd();
        virtual void clearOnPressEnd();
        virtual void setOnPressEnd(std::function<void()> callback);

        //タッチ中
        virtual void causeOnPressMove();
        virtual void clearOnPressMove();
        virtual void setOnPressMove(std::function<void()> callback);

        //他ウィジェットをタッチ開始
        virtual void causeOnPressOut();
        virtual void clearOnPressOut();
        virtual void setOnPressOut(std::function<void()> callback);

        //描画
        virtual void render() = 0;
        virtual void renderForce() {
            this->needs_redraw = true;
            this->render();
        }

        //表示
        virtual bool getVisible();
        virtual void setVisible(bool visible);

        //相対的なrect
        virtual Rect getLocalRect() const { return l_rect; }
        //絶対的なrect
        virtual Rect getScreenRect() const {
            const Rect local_rect = this->getLocalRect();
            return { (int16_t)getScreenX(), (int16_t)getScreenY(), local_rect.w, local_rect.h };
        }
        //絶対的なprev_rect
        virtual Rect getScreenPrevRect() const { return prev_l_rect; }
        //小ウィジェットに対して描画可能範囲を与えるための関数(絶対座標)
        virtual Rect getScreenClipRect() const {
            const Rect my_rect = getScreenRect();
            if (parent) {
                return parent->getScreenClipRect().intersection(my_rect);
            }
            return my_rect;
        }

        //描画モード
        virtual WidgetTools::RenderMode getRenderMode() const { return WidgetTools::CLEAR; }

        //絶対座標(スクリーン基点)
        virtual int getScreenX() const {
            return (parent ? parent->getScreenX() - parent->getScrollOffsetX() : 0) + getLocalRect().x;
        }
        virtual int getScreenY() const {
            return (parent ? parent->getScreenY() - parent->getScrollOffsetY() : 0) + getLocalRect().y;
        }

        //スクロール
        virtual int getScrollOffsetX() const { return 0; }
        virtual int getScrollOffsetY() const { return 0; }

        virtual bool hitTest(int px, int py) {
            if (parent && !parent->isInsideViewport(px, py)) return false;
            const Rect rect = this->clippedScreenRect();
            return px >= rect.x && px < rect.x + rect.w &&
                py >= rect.y && py < rect.y + rect.h;
        }
        virtual bool isInsideViewport(int px, int py) {
            const Rect sr = this->clippedScreenRect();
            bool inside = (px >= sr.x && px < sr.x + sr.w && py >= sr.y && py < sr.y + sr.h);
            if (!inside) return false;
            return parent ? parent->isInsideViewport(px, py) : true;
        }

        //描画を要求
        virtual void needsRender();
        virtual void markdirty(Rect rect);

        //子ウィジェットの更新を要求
        virtual void setChildrenUpdate(bool needs_update) {
            this->needs_children_update = needs_update;
        }
        virtual bool getChildrenUpdate() const {
            return this->needs_children_update;
        }

        virtual int getX(){ return this->l_rect.x; }
        virtual void setX(int x){
            this->l_rect.x = x;
            this->needsRender();
        }

        virtual int getY() { return this->l_rect.y; }
        virtual void setY(int y){
            this->l_rect.y = y;
            this->needsRender();
        }

        virtual int getW() { return this->l_rect.w; }
        virtual int getH() { return this->l_rect.h; }

        virtual int8_t getBackgroundColor() { return this->background_color; }
        virtual void setBackgroundColor(int8_t palette_color) {
            this->background_color = palette_color;
            this->needsRender();
        }

        virtual void setParent(Widget* parent){
            this->parent = parent;
        }
        virtual Widget* getParent(){
            return this->parent;
        }

        virtual bool getDisableMarkdirty(){ return this->disable_markdirty; }
        //markdirtyをしても実際にはマークしないかのフラグ
        //親ウィジェットが描画の反映のすべてを保証する場合に便利です
        //それ以外の場合は予想外の描画バグが連発するので使用は厳禁
        virtual void setDisableMarkdirty(bool value){
            this->disable_markdirty = value;
        }

        Rect clippedScreenRect() const {
            const Rect child_rect = this->getScreenRect();
            if (!this->parent) return child_rect;
    
            return this->parent->getScreenClipRect().intersection(child_rect);
        }
};