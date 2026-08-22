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
            this->dropdown->W(this->l_rect.w);
            this->dropdown->H(this->dropdown->getFittingHeight());

            this->value->MaxWidth(this->l_rect.w);
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
            this->l_rect = {x, y, w, 30};

            this->dropdown = new ScrollList(0, 0, 0, 30);
            this->dropdown->Visible(false);
            this->dropdown->onSelectItem([this](int index){
                if(index == -1) return;
                this->value->Text(this->dropdown->ItemAt(index));
                this->open_state = false;
                this->applyOpenState();
                this->l_rect.h = 30;
            });
            this->dropdown->SetParent(this);

            this->value = new Label("");
            this->value->X(0);
            this->value->Y(0);
            this->value->Placeholder("タップして選択...");
            this->value->BackgroundColor(PICO_BACKGROUND);
            this->value->BorderWidth(1);
            this->value->SetBorderColor(PICO_BLACK);
            this->value->MaxHeight(30);
            this->value->DefaultHeight(30);
            this->value->onPressStart([this](){
                this->open_state = true;
                this->applyOpenState();
                this->l_rect.h = this->dropdown->H();
            });
            this->value->SetParent(this);

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
                this->l_rect.h = 30;
            }
        };

        void render() override {
            if(!this->needs_redraw) return;
            if(!this->visible) return;

            this->needs_redraw = false;
        };

        void X(int x) override {
            this->l_rect.x = x;
            this->dropdown->needsRender();
            this->value->needsRender();
        }

        void Y(int y) override {
            this->l_rect.y = y;
            this->dropdown->needsRender();
            this->value->needsRender();
        }

        void W(int w) {
            this->l_rect.w = w;
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