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

        bool isSingleLine = true;

        Button* submit_button;
        Button* cancel_button;

        Textbox* input;

        Label* label;

        void updatePlaces() {
            this->label->setX(BASE_X + MARGIN);
            this->label->setY(BASE_Y + MARGIN);
            this->label->setMaxWidth(DIALOG_WIDTH - MARGIN * 2);

            this->submit_button->setX(BASE_X + MARGIN - 2);
            this->submit_button->setY(BASE_Y + DIALOG_HEIGHT - BUTTON_AREA_HEIGHT + MARGIN);
            this->submit_button->setW(BUTTON_WIDTH);
            this->submit_button->setAllowTextSpacing(false);

            this->cancel_button->setX(BASE_X + MARGIN - 2);
            this->cancel_button->setY(BASE_Y + DIALOG_HEIGHT - BUTTON_HEIGHT - MARGIN);
            this->cancel_button->setW(BUTTON_WIDTH);
            this->cancel_button->setAllowTextSpacing(false);

            this->input->setX(BASE_X + MARGIN);
            this->input->setY(this->label->getY() + this->label->getH() + MARGIN);
            this->input->setMaxWidth(DIALOG_WIDTH - MARGIN * 2);
            this->input->setIsSingleLine(isSingleLine);
            if(this->isSingleLine){
                this->input->setMaxHeight(30);
            }else{
                this->input->setMaxHeight(this->submit_button->getY() - this->input->getY() - MARGIN);
                this->input->setDefaultHeight(this->input->getMaxHeight());
            }
        }

    public:

        InputDialog(String label_content, bool isSingleLine){
            this->isSingleLine = isSingleLine;

            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->label = new Label(label_content);
            this->label->setParent(this);

            this->input = new Textbox("", 0, 0, 0, 0, true);
            this->input->setPlaceholder("ここに入力...");
            this->input->setParent(this);

            this->submit_button = new Button("決定");
            this->submit_button->setOnPressStart([this](){
                this->setVisible(false);
            });
            this->submit_button->setParent(this);

            this->cancel_button = new Button("キャンセル");
            this->cancel_button->setOnPressStart([this](){
                this->setVisible(false);
            });
            this->cancel_button->setParent(this);

            this->updatePlaces();

            children_.push_back(this->label);
            children_.push_back(this->input);
            children_.push_back(this->submit_button);
            children_.push_back(this->cancel_button);

            this->visible = false;
        }

        bool getIsSingleLine() {
            return isSingleLine;
        }
        void setIsSingleLine(bool isSingleLine){
            this->isSingleLine = isSingleLine;
            this->updatePlaces();
            this->needsRender();
        }

        void render() override;

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

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