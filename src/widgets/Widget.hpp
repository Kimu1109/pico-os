#pragma once

#include "OS_Data.hpp"

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

        bool needs_redraw;

        std::function<void()> on_press_start = nullptr;
        std::function<void()> on_press_move = nullptr;
        std::function<void()> on_press_end = nullptr;
    
    public:
        bool is_pressing = false;

        void update(){
            if(OSData::isTouchMove && this->is_pressing){
                this->onPressMove();
            }
            if(OSData::isTouchEnd && this->is_pressing){
                this->onPressEnd();
                is_pressing = false;
            }

            render();
        };

        virtual bool hitTest(int px, int py) {
            return px >= this->x && px < this->x + this->w &&
                py >= this->y && py < this->y + this->h;
        }

        virtual void onPressStart() {
            if (on_press_start) on_press_start();
        }
        void onPressStart(std::function<void()> callback) {
            this->on_press_start = callback;
        }
        virtual void onPressEnd(){
            if (on_press_end) on_press_end();
        }
        void onPressEnd(std::function<void()> callback) {
            this->on_press_end = callback;
        }
        virtual void onPressMove(){
            if (on_press_move) on_press_move();
        }
        void onPressMove(std::function<void()> callback) {
            this->on_press_move = callback;
        }

        virtual void render() = 0;
};