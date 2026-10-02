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
// - ラベルが既定の大きさに収まらなければ、①小さい文字(16px)→②ダイアログを大きく→③スクロール
//   の順で収める(DialogLayout::FitDialog。MsgDialogと同じ)。
// - ボタンの文字は変えられ、空("")ならそのボタンは出さずに詰める。
//   両方とも空だと閉じる手段が無くなるので、そのときだけ「決定」を出す。
class InputDialog : public Widget {

    private:
        std::vector<Widget*> children_;

        // 既定の大きさ。ラベルが収まらなければ DialogLayout::FitDialog が広げる
        constexpr static int DIALOG_HEIGHT = 200;
        constexpr static int DIALOG_WIDTH = 180;

        constexpr static int MARGIN = DialogLayout::kMargin;

        // 今のダイアログの枠(画面座標)。render()もこれを描く
        Rect dlg = DialogLayout::Place(DIALOG_WIDTH, DIALOG_HEIGHT);

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
            //ラベル以外が使う高さ: 上の余白 + 入力欄(と上の余白) + ボタン(無ければ下の余白)
            const int input_min_h = this->isSingleLine ? SINGLE_INPUT_H : MIN_MULTI_INPUT_H;
            int overhead = MARGIN + MARGIN + input_min_h;
            overhead += this->buttonCount() ? DialogLayout::ButtonAreaHeight(this->buttonCount()) : MARGIN;
            const DialogLayout::Fit fit = DialogLayout::FitDialog(label_box, label, DIALOG_WIDTH, DIALOG_HEIGHT, overhead);
            this->dlg = fit.dialog;

            const int top = dlg.y + MARGIN;
            const int bottom = dlg.y + dlg.h;
            int content_bottom = bottom - DialogLayout::ButtonAreaHeight(this->buttonCount());
            if(this->buttonCount() == 0) content_bottom -= MARGIN;

            label_box->setX(dlg.x + MARGIN);
            label_box->setY(top);

            Button* buttons[] = { submit_button, cancel_button };
            DialogLayout::StackButtons(buttons, 2, dlg.x + MARGIN, dlg.w - MARGIN * 2, bottom);

            this->input->setX(dlg.x + MARGIN);
            this->input->setY(top + fit.text_h + MARGIN);
            this->input->setMaxWidth(dlg.w - MARGIN * 2);
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