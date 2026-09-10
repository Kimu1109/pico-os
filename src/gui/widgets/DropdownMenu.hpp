#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/Label.hpp"

class DropdownMenu : public Widget {

    private:
        std::vector<Widget*> children_;
    
        ScrollList *dropdown;
        Label<PICO_STR_L> *value;

        bool open_state = false;

        //移動検出用の前回の絶対座標(グローバル座標)矩形
        Rect prev_screen_rect{0, 0, 0, 0};

        void relayout(){
            this->dropdown->setW(this->l_rect.w);
            this->dropdown->setH(this->dropdown->getFittingHeight());

            this->value->setMaxWidth(this->l_rect.w);
        }

        void applyOpenState(){
            this->value->setVisible(!open_state);
            this->dropdown->setVisible(open_state);
        }

    public:

        DropdownMenu(int16_t x, int16_t y, int16_t w){
            this->l_rect = {x, y, w, 30};

            this->dropdown = new ScrollList(0, 0, 0, 30);
            this->dropdown->setVisible(false);
            this->dropdown->setOnSelectItem([this](int index, bool already_selected){
                if(index == -1) return;
                ScrollListTools::Item* valueItem = this->dropdown->itemAt(index);
                if(valueItem){
                    this->value->setText(valueItem->text);
                    this->open_state = false;
                    this->applyOpenState();
                    this->l_rect.h = 30;
                }
            });
            this->dropdown->setParent(this);

            this->value = new Label<PICO_STR_L>("");
            this->value->setX(0);
            this->value->setY(0);
            this->value->setPlaceholder("タップして選択...");
            this->value->setBackgroundColor(PICO_BACKGROUND);
            this->value->setBorderWidth(1);
            this->value->setBorderColor(PICO_BLACK);
            this->value->setMaxHeight(30);
            this->value->setDefaultHeight(30);
            this->value->setOnPressStart([this](){
                this->open_state = true;
                this->applyOpenState();
                this->l_rect.h = this->dropdown->getH();
            });
            this->value->setParent(this);

            children_.push_back(this->dropdown);
            children_.push_back(this->value);

            this->relayout();
        }

        void causeOnPressOut() override {
            Widget::causeOnPressOut();

            if(this->dropdown->is_pressing) return;
            if(this->value->is_pressing) return;

            if(this->open_state){
                this->open_state = false;
                this->applyOpenState();
                this->l_rect.h = 30;
            }
        };

        void render() override {
            if(!this->needs_redraw) return;
            if(!this->visible) return;

            //グローバル座標で前回位置と比較し、移動していれば旧位置を再描画対象にする
            const Rect g_rect = this->getScreenRect();
            if(this->prev_screen_rect != g_rect)
                markdirty(this->prev_screen_rect);
            this->prev_screen_rect = g_rect;

            this->needs_redraw = false;
        };

        void setX(int x) override {
            this->l_rect.x = x;
            this->needsRender();
            this->dropdown->needsRender();
            this->value->needsRender();
        }

        void setY(int y) override {
            this->l_rect.y = y;
            this->needsRender();
            this->dropdown->needsRender();
            this->value->needsRender();
        }

        void setW(int w) {
            this->l_rect.w = w;
            this->relayout();
        }

        void setVisible(bool visible) override {
            this->visible = visible;
            if(visible){
                this->applyOpenState();
            }else{
                this->value->setVisible(false);
                this->dropdown->setVisible(false);
            }
        }

        void add(const char* text){
            auto item = ScrollListTools::Item();
            item.icon = IconID::AppBox;
            item.text.assign(text);

            this->dropdown->add(item);
            this->relayout();
        }

        void setSelectedIndex(int index){
            this->dropdown->setSelectedIndex(index);
        }
        void clearSelectedIndex(){
            this->dropdown->clearSelectedIndex();
        }
        int getSelectedIndex(){ return this->dropdown->getSelectedIndex(); }

        ScrollListTools::Item* itemAt(int index){
            return this->dropdown->itemAt(index);
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~DropdownMenu(){
            delete this->dropdown;
            delete this->value;
        }
};