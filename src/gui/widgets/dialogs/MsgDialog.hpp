#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Icon.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/dialogs/DialogLayout.hpp"

// メッセージ + 任意のアイコン + ボタン(最大2つ)のダイアログ。
//
// - 本文が既定の大きさ(180x180)に収まらなければ、①小さい文字(16px)→②ダイアログを大きく→
//   ③スクロール の順で収める
//   (DialogLayout::FitDialog)。本文は512バイトまで。
// - ボタンの文字が空("")ならそのボタンは出さず、本文の枠をその分広げる。
//   両方とも空だと閉じる手段が無くなるので、そのときだけ「OK」を出す。
class MsgDialog : public Widget {
    private:
        std::vector<Widget*> children_;

        ScrollContainer* msg_box;
        Label<PICO_STR_512B>* msg_label; // 所有権は msg_box にある
        Button* ok_button = nullptr;     // 文字が空なら作らない
        Button* cancel_button = nullptr; // 同上
        Icon* msg_icon;

        bool icon_visible = false;
        IconID icon_id = IconID::AppBox;

        std::function<void(bool is_ok)> on_closed = nullptr;

        // 既定の大きさ。本文が収まらなければ DialogLayout::FitDialog が広げる
        constexpr static int DIALOG_HEIGHT = 180;
        constexpr static int DIALOG_WIDTH = 180;

        constexpr static int ICON_SIZE = 64;

        constexpr static int MARGIN = DialogLayout::kMargin;

        // 今のダイアログの枠(画面座標)。render()もこれを描く
        Rect dlg = DialogLayout::Place(DIALOG_WIDTH, DIALOG_HEIGHT);

        int buttonCount() const { return (ok_button ? 1 : 0) + (cancel_button ? 1 : 0); }

        void updateWidgets(){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            //本文以外が使う高さ: 上の余白 + アイコン + ボタン(無ければ下の余白)
            int overhead = MARGIN + (this->icon_visible ? (ICON_SIZE + MARGIN) : 0);
            overhead += this->buttonCount() ? DialogLayout::ButtonAreaHeight(this->buttonCount()) : MARGIN;
            const DialogLayout::Fit fit = DialogLayout::FitDialog(msg_box, msg_label, DIALOG_WIDTH, DIALOG_HEIGHT, overhead);
            this->dlg = fit.dialog;

            msg_icon->setX(dlg.x + (dlg.w - ICON_SIZE) / 2);
            msg_icon->setY(dlg.y + MARGIN);
            msg_icon->setIconId(this->icon_id);
            msg_icon->setVisible(this->visible && this->icon_visible);

            msg_box->setX(dlg.x + MARGIN);
            msg_box->setY(dlg.y + MARGIN + (this->icon_visible ? (ICON_SIZE + MARGIN) : 0));

            Button* buttons[] = { ok_button, cancel_button };
            DialogLayout::StackButtons(buttons, 2, dlg.x + MARGIN, dlg.w - MARGIN * 2, dlg.y + dlg.h);

            this->needsRender();
        }

        Button* makeButton(const char* text, bool is_ok){
            Button* b = new Button(0, 0, text);
            b->setOnPressStart([this, is_ok](){
                this->causeOnClosed(is_ok);
                this->setVisible(false);
            });
            b->setParent(this);
            return b;
        }

    public:

        MsgDialog(const char* msg_text, const char* cancel_text, const char* ok_text){
            this->l_rect = {0, 0, DIALOG_WIDTH, DIALOG_HEIGHT};

            msg_icon = new Icon(0, 0, this->icon_id, IconSize::Px64);
            msg_icon->setParent(this);

            msg_box = new ScrollContainer(0, 0, DIALOG_WIDTH - MARGIN * 2, 0);
            msg_box->setParent(this);
            msg_label = new Label<PICO_STR_512B>(0, 0, msg_text ? msg_text : "");
            msg_box->add(msg_label); //所有権は msg_box へ移る

            const bool has_ok = DialogLayout::HasText(ok_text);
            const bool has_cancel = DialogLayout::HasText(cancel_text);
            if(has_ok) ok_button = makeButton(ok_text, true);
            if(has_cancel) cancel_button = makeButton(cancel_text, false);
            //どちらも空だと閉じられなくなるので、OKだけは出す
            if(!has_ok && !has_cancel) ok_button = makeButton("OK", true);

            this->updateWidgets();

            children_.push_back(msg_box);
            if(cancel_button) children_.push_back(cancel_button);
            if(ok_button) children_.push_back(ok_button);
            children_.push_back(msg_icon);

            this->visible = false;
        };

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::MsgDialog; }

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
            delete msg_box; //msg_labelも一緒に消える
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

        void setVisible(bool visible) override {
            Widget::setVisible(visible);
            if(visible && !icon_visible){
                msg_icon->setVisible(false);
            }
        };
};