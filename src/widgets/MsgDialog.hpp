#pragma once

#include "widgets/Widget.hpp"
#include "widgets/Label.hpp"
#include "widgets/Button.hpp"
#include "widgets/Icon.hpp"

class MsgDialog : public Widget {
    private:
        std::vector<Widget*> children_;

        Label* msg_label;
        Button* ok_button;
        Button* cancel_button;
        Icon* msg_icon;

        bool icon_visible = false;
        IconID icon_id = IconID::AppBox;

        constexpr static int DIALOG_HEIGHT = 180;
        constexpr static int DIALOG_WIDTH = 180;

        constexpr static int ICON_SIZE = 64;

        constexpr static int BASE_X = (SCREEN_WIDTH - DIALOG_WIDTH) * 0.5;
        constexpr static int BASE_Y = (SCREEN_HEIGHT - DIALOG_HEIGHT) * 0.5;
        constexpr static int MARGIN = 5;

        constexpr static int BUTTON_HEIGHT = 30;
        constexpr static int BUTTON_AREA_HEIGHT = BUTTON_HEIGHT * 2 + MARGIN * 3; 
        constexpr static int BUTTON_WIDTH = DIALOG_WIDTH - MARGIN * 2;

        void updateWidgets(){
            this->rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            msg_icon->X(BASE_X + MARGIN + (DIALOG_WIDTH - MARGIN * 2 - ICON_SIZE) * 0.5);
            msg_icon->Y(BASE_Y + MARGIN);
            msg_icon->Visible(this->icon_visible);
            msg_icon->SetIconId(this->icon_id);

            msg_label->X(BASE_X + MARGIN);
            msg_label->Y(BASE_Y + MARGIN + (this->icon_visible ? (ICON_SIZE + MARGIN) : 0));
            msg_label->MaxWidth(DIALOG_WIDTH - MARGIN * 2);
            msg_label->MaxHeight(DIALOG_HEIGHT - MARGIN * 2 - BUTTON_AREA_HEIGHT - (this->icon_visible ? ICON_SIZE : 0));

            ok_button->X(BASE_X + MARGIN - 2);
            ok_button->Y(BASE_Y + DIALOG_HEIGHT - BUTTON_AREA_HEIGHT + MARGIN);
            ok_button->W(BUTTON_WIDTH);
            ok_button->AllowTextSpacing(false);

            cancel_button->X(BASE_X + MARGIN - 2);
            cancel_button->Y(BASE_Y + DIALOG_HEIGHT - BUTTON_HEIGHT - MARGIN);
            cancel_button->W(BUTTON_WIDTH);
            cancel_button->AllowTextSpacing(false);

            this->needsRender();
        }

    public:

        MsgDialog(String msg_text, String cancel_text, String ok_text){
            this->rect = {BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT};

            msg_icon = new Icon(0, 0, this->icon_id, IconSize::Px64);

            msg_label = new Label(0, 0, msg_text);

            ok_button = new Button(0, 0, ok_text);
            ok_button->onPressStart([this](){
                this->Visible(false);
            });

            cancel_button = new Button(0, 0, cancel_text);
            cancel_button->onPressStart([this](){
                this->Visible(false);
            });

            this->updateWidgets();

            children_.push_back(msg_label);
            children_.push_back(cancel_button);
            children_.push_back(ok_button);
            children_.push_back(msg_icon);

            this->visible = false;
        };

        void render() override;

        WidgetTools::RenderMode GetRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        bool VisibleIcon() { return this->icon_visible; }
        void VisibleIcon(bool v){
            this->icon_visible = v;
            this->updateWidgets();
        }

        IconID IconId(){ return icon_id; }
        void IconId(IconID icon_id){
            this->icon_id = icon_id;
            this->updateWidgets();
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~MsgDialog(){
            delete msg_label;
            delete ok_button;
            delete cancel_button;
        }

};