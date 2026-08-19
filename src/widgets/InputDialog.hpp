#pragma once

#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "widgets/Button.hpp"
#include "widgets/Textbox.hpp"

class InputDialog : public Widget {

    private:
        std::vector<Widget*> children_;

        constexpr static int DIALOG_HEIGHT = 200;
        constexpr static int DIALOG_WIDTH = 180;

        constexpr static int BASE_X = (SCREEN_WIDTH - DIALOG_WIDTH) * 0.5;
        constexpr static int BASE_Y = (SCREEN_HEIGHT - DIALOG_HEIGHT) * 0.5;
        constexpr static int MARGIN = 5;

        constexpr static int BUTTON_HEIGHT = 30;
        constexpr static int BUTTON_AREA_HEIGHT = BUTTON_HEIGHT * 2 + MARGIN * 3; 
        constexpr static int BUTTON_WIDTH = DIALOG_WIDTH - MARGIN * 2;

        Button* submit_button;
        Button* cancel_button;

        Textbox* input;

        Label* label;

    public:

        InputDialog(String label_content){
            this->rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->label = new Label(label_content);
            this->label->X(BASE_X + MARGIN);
            this->label->Y(BASE_Y + MARGIN);
            this->label->MaxWidth(DIALOG_WIDTH - MARGIN * 2);

            this->input = new Textbox(
                "",
                BASE_X + MARGIN, this->label->Y() + this->label->H() + MARGIN,
                DIALOG_WIDTH - MARGIN * 2, 30,
                true
            );

            this->submit_button = new Button("決定");
            this->submit_button->X(BASE_X + MARGIN - 2);
            this->submit_button->Y(BASE_Y + DIALOG_HEIGHT - BUTTON_AREA_HEIGHT + MARGIN);
            this->submit_button->W(BUTTON_WIDTH);
            this->submit_button->AllowTextSpacing(false);

            this->cancel_button = new Button("キャンセル");
            this->cancel_button->X(BASE_X + MARGIN - 2);
            this->cancel_button->Y(BASE_Y + DIALOG_HEIGHT - BUTTON_HEIGHT - MARGIN);
            this->cancel_button->W(BUTTON_WIDTH);
            this->cancel_button->AllowTextSpacing(false);

            children_.push_back(this->label);
            children_.push_back(this->input);
            children_.push_back(this->submit_button);
            children_.push_back(this->cancel_button);

            this->visible = false;
        }

        void render() override;

        WidgetTools::RenderMode GetRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~InputDialog(){
            delete this->label;
            delete this->submit_button;
            delete this->cancel_button;
            delete this->input;
        }
};