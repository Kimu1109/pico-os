#pragma once

#include <vector>
#include <functional>
#include "model/Rect.hpp"

class Widget {
    protected:
        Rect rect;
        Rect prev_rect;

        bool needs_redraw = true;
        bool visible = true;

        std::function<void()> on_press_start = nullptr;
        std::function<void()> on_press_move = nullptr;
        std::function<void()> on_press_end = nullptr;
    
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

        virtual void onPressStart();
        virtual void onPressStart(std::function<void()> callback);

        virtual void onPressEnd();
        virtual void onPressEnd(std::function<void()> callback);

        virtual void onPressMove();
        void onPressMove(std::function<void()> callback);

        virtual void render() = 0;
        virtual void renderForce() {
            this->needs_redraw = true;
            this->render();
        }

        virtual bool Visible();
        virtual void Visible(bool visible);

        virtual Rect getRect() const { return rect; }
        virtual bool isOpaque() const { return true; }

        virtual bool hitTest(int px, int py) {
            const Rect rect = this->getRect();
            return px >= rect.x && px < rect.x + rect.w &&
                py >= rect.y && py < rect.y + rect.h;
        }
        virtual void needsRender();

        virtual int X(){ return this->rect.x; }
        virtual void X(int x){
            this->rect.x = x;
            this->needsRender();
        }

        virtual int Y() { return this->rect.y; }
        virtual void Y(int y){
            this->rect.y = y;
            this->needsRender();
        }

        virtual int W() { return this->rect.w; }
        virtual int H() { return this->rect.h; }
};