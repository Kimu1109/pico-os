#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/dialogs/DialogLayout.hpp"
#include "functions/Keyboard_Functions.hpp"

// ラベル + テキスト入力 + ボタン(最大2つ)のダイアログ。
//
// - ラベルが枠に収まらなければ小さい文字(16px)にし、それでも収まらなければスクロールさせる
//   (DialogLayout::FitText。MsgDialogと同じ)。
// - ボタンの文字は変えられ、空("")ならそのボタンは出さずに詰める。
//   両方とも空だと閉じる手段が無くなるので、そのときだけ「決定」を出す。
class InputDialog : public Widget {

    private:
        std::vector<Widget*> children_;

        constexpr static int DIALOG_HEIGHT = 200;
        constexpr static int DIALOG_WIDTH = 180;

        constexpr static int BASE_X = (SCREEN_WIDTH - DIALOG_WIDTH) * 0.5;
        constexpr static int BASE_Y = (SCREEN_HEIGHT - DIALOG_HEIGHT) * 0.5;
        constexpr static int MARGIN = DialogLayout::kMargin;

        constexpr static int BUTTON_WIDTH = DIALOG_WIDTH - MARGIN * 2;

        // 入力欄の高さ(1行)と、複数行のときに最低限残す高さ
        constexpr static int SINGLE_INPUT_H = 30;
        constexpr static int MIN_MULTI_INPUT_H = 54;

        bool isSingleLine = true;

        Button* submit_button = nullptr; // 文字が空なら作らない
        Button* cancel_button = nullptr; // 同上

        Textbox<PICO_STR_LL>* input;

        ScrollContainer* label_box;
        Label<PICO_STR_512B>* label; // 所有権は label_box にある

        std::function<void(bool is_submit)> on_closed = nullptr;

        int buttonCount() const { return (submit_button ? 1 : 0) + (cancel_button ? 1 : 0); }

        void updatePlaces() {
            const int bottom = BASE_Y + DIALOG_HEIGHT;
            int content_bottom = bottom - DialogLayout::ButtonAreaHeight(this->buttonCount());
            if(this->buttonCount() == 0) content_bottom -= MARGIN;

            const int top = BASE_Y + MARGIN;
            const int input_min_h = this->isSingleLine ? SINGLE_INPUT_H : MIN_MULTI_INPUT_H;
            const int label_max_h = content_bottom - top - MARGIN - input_min_h;
            const int label_h = DialogLayout::FitText(label_box, label, BASE_X + MARGIN, top,
                                                      DIALOG_WIDTH - MARGIN * 2, label_max_h);

            Button* buttons[] = { submit_button, cancel_button };
            DialogLayout::StackButtons(buttons, 2, BASE_X + MARGIN, BUTTON_WIDTH, bottom);

            this->input->setX(BASE_X + MARGIN);
            this->input->setY(top + label_h + MARGIN);
            this->input->setMaxWidth(DIALOG_WIDTH - MARGIN * 2);
            this->input->setIsSingleLine(isSingleLine);
            if(this->isSingleLine){
                this->input->setMaxHeight(SINGLE_INPUT_H);
            }else{
                this->input->setMaxHeight(content_bottom - this->input->getY());
                this->input->setDefaultHeight(this->input->getMaxHeight());
            }
        }

        Button* makeButton(const char* text, bool is_submit){
            Button* b = new Button(text);
            b->setOnPressStart([this, is_submit](){
                //入力対象がこの後消えるので、出しっぱなしのキーボードを閉じる
                KeyboardFunctions::HideAll();
                this->causeOnClosed(is_submit);
                this->setVisible(false);
            });
            b->setParent(this);
            return b;
        }

    public:

        InputDialog(const char* label_content, bool isSingleLine,
                    const char* submit_text = "決定", const char* cancel_text = "キャンセル"){
            this->isSingleLine = isSingleLine;

            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->label_box = new ScrollContainer(0, 0, DIALOG_WIDTH - MARGIN * 2, 0);
            this->label_box->setParent(this);
            this->label = new Label<PICO_STR_512B>(label_content ? label_content : "");
            this->label_box->add(this->label); //所有権は label_box へ移る

            this->input = new Textbox<PICO_STR_LL>("", 0, 0, 0, 0, true);
            this->input->setPlaceholder("ここに入力...");
            this->input->setParent(this);

            const bool has_submit = DialogLayout::HasText(submit_text);
            const bool has_cancel = DialogLayout::HasText(cancel_text);
            if(has_submit) this->submit_button = makeButton(submit_text, true);
            if(has_cancel) this->cancel_button = makeButton(cancel_text, false);
            //どちらも空だと閉じられなくなるので、決定だけは出す
            if(!has_submit && !has_cancel) this->submit_button = makeButton("決定", true);

            this->updatePlaces();

            children_.push_back(this->label_box);
            children_.push_back(this->input);
            if(this->submit_button) children_.push_back(this->submit_button);
            if(this->cancel_button) children_.push_back(this->cancel_button);

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

        void setOnClosed(std::function<void(bool is_submit)> callback){
            this->on_closed = callback;
        }
        void clearOnClosed(){
            this->on_closed = nullptr;
        }
        void causeOnClosed(bool is_submit){
            if(this->on_closed) this->on_closed(is_submit);
        }

        FixedString<PICO_STR_LL> getInput(){
            return *this->input->getText();
        }
        void setInput(const char* input){
            this->input->setText(input);
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::InputDialog; }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~InputDialog(){
            delete this->label_box; //labelも一緒に消える
            delete this->submit_button;
            delete this->cancel_button;
            delete this->input;
        }
};