#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Icon.hpp"

class MsgDialog : public Widget {
    private:
        std::vector<Widget*> children_;

        Label* msg_label;
        Button* ok_button;
        Button* cancel_button;
        Icon* msg_icon;

        bool icon_visible = false;
        IconID icon_id = IconID::AppBox;

        std::function<void(bool is_ok)> on_closed = nullptr;

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
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            msg_icon->setX(BASE_X + MARGIN + (DIALOG_WIDTH - MARGIN * 2 - ICON_SIZE) * 0.5);
            msg_icon->setY(BASE_Y + MARGIN);
            msg_icon->setVisible(this->icon_visible);
            msg_icon->setIconId(this->icon_id);

            msg_label->setX(BASE_X + MARGIN);
            msg_label->setY(BASE_Y + MARGIN + (this->icon_visible ? (ICON_SIZE + MARGIN) : 0));
            msg_label->setMaxWidth(DIALOG_WIDTH - MARGIN * 2);
            msg_label->setMaxHeight(DIALOG_HEIGHT - MARGIN * 2 - BUTTON_AREA_HEIGHT - (this->icon_visible ? ICON_SIZE : 0));

            ok_button->setX(BASE_X + MARGIN - 2);
            ok_button->setY(BASE_Y + DIALOG_HEIGHT - BUTTON_AREA_HEIGHT + MARGIN);
            ok_button->setW(BUTTON_WIDTH);
            ok_button->setAllowTextSpacing(false);

            cancel_button->setX(BASE_X + MARGIN - 2);
            cancel_button->setY(BASE_Y + DIALOG_HEIGHT - BUTTON_HEIGHT - MARGIN);
            cancel_button->setW(BUTTON_WIDTH);
            cancel_button->setAllowTextSpacing(false);

            this->needsRender();
        }

    public:

        MsgDialog(String msg_text, String cancel_text, String ok_text){
            this->l_rect = {0, 0, DIALOG_WIDTH, DIALOG_HEIGHT};

            msg_icon = new Icon(0, 0, this->icon_id, IconSize::Px64);
            msg_icon->setParent(this);

            msg_label = new Label(0, 0, msg_text);
            msg_label->setParent(this);

            ok_button = new Button(0, 0, ok_text);
            ok_button->setOnPressStart([this](){
                this->causeOnClosed(true);
                this->setVisible(false);
            });
            ok_button->setParent(this);

            cancel_button = new Button(0, 0, cancel_text);
            cancel_button->setOnPressStart([this](){
                this->causeOnClosed(false);
                this->setVisible(false);
            });
            cancel_button->setParent(this);

            this->updateWidgets();

            children_.push_back(msg_label);
            children_.push_back(cancel_button);
            children_.push_back(ok_button);
            children_.push_back(msg_icon);

            this->visible = false;
        };

        void render() override;

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        bool getVisibleIcon() { return this->icon_visible; }
        void setVisibleIcon(bool v){
            this->icon_visible = v;
            this->updateWidgets();
        }

        IconID getIconId(){ return icon_id; }
        void setIconId(IconID icon_id){
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
            delete msg_icon;
        }

        void setOnClosed(std::function<void(bool is_ok)> callback){
            this->on_closed = callback;
        }
        void clearOnClosed(){
            this->on_closed = nullptr;
        }
        void causeOnClosed(bool is_ok){
            if(this->on_closed) this->on_closed(is_ok);
        }
};