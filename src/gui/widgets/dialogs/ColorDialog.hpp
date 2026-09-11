#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"

class ColorDialog : public Widget {
    private:
        std::vector<Widget*> children_;

        std::function<void(bool is_ok)> on_close = nullptr;

        constexpr static int DIALOG_W = 200;

        constexpr static int BASE_X = 20;
        constexpr static int BASE_Y = 85;

        constexpr static int TITLE_H = 30;

        constexpr static int COLOR_H = 30;
        constexpr static int COLOR_W = DIALOG_W / 4;

        constexpr static int BUTTON_Y = BASE_Y + COLOR_H * 4 + TITLE_H + 5;
        constexpr static int BUTTON_H = 26;

        constexpr static int DIALOG_H = COLOR_H * 4 + BUTTON_H + TITLE_H + 5;

        constexpr static int BUTTON_MARGIN = 2;
        constexpr static int BUTTON_W = DIALOG_W / 2 - BUTTON_MARGIN * 2 - 3;

        int selected_color = -1;
        int getIndexToColor(int x, int y){
            return x + y * 4;
        }
  
        Button* button_no;
        Button* button_ok;

        Label<PICO_STR_S>* title;

    public:
        ColorDialog(){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->title = new Label<PICO_STR_S>(BASE_X, BASE_Y, "色を選択");

            this->button_ok = new Button(BASE_X + DIALOG_W / 2 + BUTTON_MARGIN, BUTTON_Y - 3, "OK");
            this->button_ok->setAllowTextSpacing(false);
            this->button_ok->setFontSize(FontFn::Small);
            this->button_ok->setW(BUTTON_W);
            this->button_ok->setH(BUTTON_H - 2);
            this->button_ok->setOnPressStart([this](){
                if(this->on_close) this->on_close(true);
                this->setVisible(false);
            });

            this->button_no = new Button(BASE_X + BUTTON_MARGIN, BUTTON_Y - 3, "キャンセル");
            this->button_no->setAllowTextSpacing(false);
            this->button_no->setFontSize(FontFn::Small);
            this->button_no->setW(BUTTON_W);
            this->button_no->setH(BUTTON_H - 2);
            this->button_no->setOnPressStart([this](){
                if(this->on_close) this->on_close(false);
                this->setVisible(false);
            });

            children_.push_back(this->title);
            children_.push_back(this->button_ok);
            children_.push_back(this->button_no);

            this->setVisible(false);
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        int getSelectedColor(){
            return this->selected_color;
        }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::ColorDialog; }

        void setOnClose(std::function<void(bool is_ok)> callback){
            this->on_close = callback;
        }

        ~ColorDialog(){
            delete this->title;
            delete this->button_ok;
            delete this->button_no;
        }
};