#include "gui/widgets/dialogs/EventDetailDialog.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

EventDetailDialog::EventDetailDialog(){
    this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

    this->title = new Label<PICO_STR_L>(BASE_X + MARGIN, BASE_Y + MARGIN, "");
    this->title->setFontSize(FontFn::Small);
    this->title->setMaxWidth(INNER_W);
    this->title->setMaxHeight(TITLE_H);
    //予定の題名は自由記述なので、** や ~ をマークアップとして解釈させない
    this->title->setDisableAutoTextDecoration(true);
    this->title->setParent(this);

    this->body_scroll = new ScrollContainer(BASE_X + MARGIN, BODY_Y, INNER_W, BODY_H);
    this->body_scroll->setParent(this);
    this->body = new Label<PICO_STR_1KiB>(BODY_PADDING, BODY_PADDING, "");
    this->body->setFontSize(FontFn::Small);
    this->body->setDisableAutoTextDecoration(true);
    this->body->setMaxWidth(INNER_W - BODY_SCROLLBAR_W - BODY_PADDING * 2);
    this->body_scroll->add(this->body); //所有権は body_scroll へ移る

    this->close_button = new Button(BASE_X + DIALOG_WIDTH - MARGIN - CLOSE_W - BUTTON_EDGE, BUTTON_Y, "閉じる");
    this->close_button->setFontSize(FontFn::Small);
    this->close_button->setAllowTextSpacing(false);
    this->close_button->setW(CLOSE_W);
    this->close_button->setH(BUTTON_H);
    this->close_button->setParent(this);
    this->close_button->setOnPressStart([this](){
        if(this->on_closed) this->on_closed(false);
        this->setVisible(false);
    });

    children_.push_back(this->title);
    children_.push_back(this->body_scroll);
    children_.push_back(this->close_button);

    this->visible = false;
}

EventDetailDialog::~EventDetailDialog(){
    delete this->title;
    delete this->body_scroll; //bodyも一緒に消える
    delete this->close_button;
}

void EventDetailDialog::setContent(const char* title_text, const char* body_text){
    this->title->setText(title_text ? title_text : "");
    this->body->setText(body_text ? body_text : "");
    //本文の長さが変わったのでスクロールできる範囲を測り直し、先頭から見せる
    this->body_scroll->refreshContentBounds();
    this->body_scroll->scrollToTop();
    this->needsRender();
}

void EventDetailDialog::render(){
    if(!needs_redraw) return;
    if(!visible) return;

    markdirty(this->getScreenRect());
    PICO_GFX::DrawDialogBackground();

    OSData::frame->fillRect(BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT, this->background_color);
    OSData::frame->drawRect(BASE_X, BASE_Y, DIALOG_WIDTH, DIALOG_HEIGHT, PICO_BLACK);

    needs_redraw = false;
}
