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
        virtual void onPressStart();
        virtual void onPressStart(std::function<void()> callback);

        //タッチ終了
        virtual void onPressEnd();
        virtual void onPressEnd(std::function<void()> callback);

        //タッチ中
        virtual void onPressMove();
        virtual void onPressMove(std::function<void()> callback);

        //他ウィジェットをタッチ開始
        virtual void onPressOut();
        virtual void onPressOut(std::function<void()> callback);

        //描画
        virtual void render() = 0;
        virtual void renderForce() {
            this->needs_redraw = true;
            this->render();
        }

        //表示
        virtual bool Visible();
        virtual void Visible(bool visible);

        //ローカル座標
        virtual Rect getLocalRect() const { return l_rect; }
        virtual Rect getScreenRect() const {
            const Rect local_rect = this->getLocalRect();
            return { (int16_t)getScreenX(), (int16_t)getScreenY(), local_rect.w, local_rect.h };
        }
        virtual Rect getScreenPrevRect() const { return prev_l_rect; }

        //描画モード
        virtual WidgetTools::RenderMode GetRenderMode() const { return WidgetTools::CLEAR; }

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
            const Rect rect = this->getScreenRect();
            return px >= rect.x && px < rect.x + rect.w &&
                py >= rect.y && py < rect.y + rect.h;
        }
        virtual bool isInsideViewport(int px, int py) {
            const Rect sr = this->getScreenRect();
            bool inside = (px >= sr.x && px < sr.x + sr.w && py >= sr.y && py < sr.y + sr.h);
            if (!inside) return false;
            return parent ? parent->isInsideViewport(px, py) : true;
        }

        //描画を要求
        virtual void needsRender();

        virtual int X(){ return this->l_rect.x; }
        virtual void X(int x){
            this->l_rect.x = x;
            this->needsRender();
        }

        virtual int Y() { return this->l_rect.y; }
        virtual void Y(int y){
            this->l_rect.y = y;
            this->needsRender();
        }

        virtual int W() { return this->l_rect.w; }
        virtual int H() { return this->l_rect.h; }

        virtual int8_t BackgroundColor() { return this->background_color; }
        virtual void BackgroundColor(int8_t palette_color) {
            this->background_color = palette_color;
            this->needsRender();
        }

        virtual void SetParent(Widget* parent){
            this->parent = parent;
        }
        virtual Widget* GetParent(){
            return this->parent;
        }
};