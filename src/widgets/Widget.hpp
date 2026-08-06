#pragma once

#include <vector>
#include <functional>

class Widget {
    protected:
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        int prev_x = 0;
        int prev_y = 0;
        int prev_w = 0;
        int prev_h = 0;

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

        virtual bool hitTest(int px, int py) {
            return px >= this->x && px < this->x + this->w &&
                py >= this->y && py < this->y + this->h;
        }

        virtual void onPressStart();
        virtual void onPressStart(std::function<void()> callback);

        virtual void onPressEnd();
        virtual void onPressEnd(std::function<void()> callback);

        virtual void onPressMove();
        void onPressMove(std::function<void()> callback);

        virtual void render() = 0;

        virtual bool Visible();
        virtual void Visible(bool visible);
};