#pragma once

#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "widgets/Button.hpp"

class MsgDialog : public Widget {
    private:
        std::vector<Widget*> children_;

        Label* msg_label;
        Button* ok_button;
        Button* cancel_button;

        constexpr static int DIALOG_HEIGHT = 180;
        constexpr static int DIALOG_WIDTH = 180;

        constexpr static int BASE_X = (SCREEN_WIDTH - DIALOG_WIDTH) * 0.5;
        constexpr static int BASE_Y = (SCREEN_HEIGHT - DIALOG_HEIGHT) * 0.5;
        constexpr static int MARGIN = 5;

        constexpr static int BUTTON_HEIGHT = 30;
        constexpr static int BUTTON_AREA_HEIGHT = BUTTON_HEIGHT * 2 + MARGIN * 3; 
        constexpr static int BUTTON_WIDTH = DIALOG_WIDTH - MARGIN * 2;

    public:

        MsgDialog(String msg_text, String cancel_text, String ok_text){
            this->rect = {BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT};

            msg_label = new Label(BASE_X + MARGIN, BASE_Y + MARGIN, msg_text);
            msg_label->MaxWidth(DIALOG_WIDTH - MARGIN * 2);
            msg_label->MaxHeight(DIALOG_HEIGHT - MARGIN * 2 - BUTTON_AREA_HEIGHT);

            ok_button = new Button(BASE_X + MARGIN - 2, BASE_Y + DIALOG_HEIGHT - BUTTON_AREA_HEIGHT + MARGIN, ok_text);
            ok_button->W(BUTTON_WIDTH);
            ok_button->AllowTextSpacing(false);

            cancel_button = new Button(BASE_X + MARGIN - 2, BASE_Y + DIALOG_HEIGHT - BUTTON_HEIGHT - MARGIN, cancel_text);
            cancel_button->W(BUTTON_WIDTH);
            cancel_button->AllowTextSpacing(false);

            children_.push_back(msg_label);
            children_.push_back(cancel_button);
            children_.push_back(ok_button);

            this->visible = false;
        };

        void render() override;
        bool isOpaque() const override { return true; }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~MsgDialog(){
            delete msg_label;
            delete ok_button;
            delete cancel_button;
        }

};