#pragma once

#include "widgets/Widget.hpp"
#include "widgets/ScrollList.hpp"
#include "widgets/Label.hpp"

class DropdownMenu : public Widget {

    private:
        std::vector<Widget*> children_;

        ScrollList *dropdown;
        Label *value;

        bool open_state = false;

        void relayout(){
            this->dropdown->X(this->rect.x);
            this->dropdown->Y(this->rect.y);
            this->dropdown->W(this->rect.w);
            this->dropdown->H(this->dropdown->getFittingHeight());

            this->value->X(this->rect.x);
            this->value->Y(this->rect.y);
            this->value->MaxWidth(this->rect.w);
        }

        void applyOpenState(){
            this->value->Visible(!open_state);
            this->dropdown->Visible(open_state);
        }

    public:

        using Widget::X;
        using Widget::Y;
        using Widget::W;
        using Widget::Visible;
        using Widget::onPressOut;

        DropdownMenu(int16_t x, int16_t y, int16_t w){
            this->rect = {x, y, w, 30};

            this->dropdown = new ScrollList(0, 0, 0, 30);
            this->dropdown->Visible(false);
            this->dropdown->onSelectItem([this](int index){
                if(index == -1) return;
                this->value->Text(this->dropdown->ItemAt(index));
                this->open_state = false;
                this->applyOpenState();
                this->rect.h = 30;
            });

            this->value = new Label("");
            this->value->Placeholder("タップして選択...");
            this->value->BackgroundColor(PICO_BACKGROUND);
            this->value->BorderWidth(1);
            this->value->SetBorderColor(PICO_BLACK);
            this->value->MaxHeight(30);
            this->value->DefaultHeight(30);
            this->value->onPressStart([this](){
                this->open_state = true;
                this->applyOpenState();
                this->rect.h = this->dropdown->H();
            });

            children_.push_back(this->dropdown);
            children_.push_back(this->value);

            this->relayout();
        }

        void onPressOut() override {
            Widget::onPressOut();

            if(this->dropdown->is_pressing) return;
            if(this->value->is_pressing) return;

            if(this->open_state){
                this->open_state = false;
                this->applyOpenState();
            }
        };

        void render() override {
            if(!this->needs_redraw) return;
            if(!this->visible) return;

            this->needs_redraw = false;
        };

        void X(int x) override {
            this->rect.x = x;
            this->relayout();
        }

        void Y(int y) override {
            this->rect.y = y;
            this->relayout();
        }

        void W(int w) {
            this->rect.w = w;
            this->relayout();
        }

        void Visible(bool visible) override {
            this->visible = visible;
            if(visible){
                this->applyOpenState();
            }else{
                this->value->Visible(false);
                this->dropdown->Visible(false);
            }
        }

        void Add(const String text){
            this->dropdown->Add(text);
            this->relayout();
        }

        void SelectedIndex(int index){
            this->dropdown->SelectedIndex(index);
        }
        int SelectedIndex(){ return this->dropdown->SelectedIndex(); }

        String ItemAt(int index){
            return this->dropdown->ItemAt(index);
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~DropdownMenu(){
            delete this->dropdown;
            delete this->value;
        }
};