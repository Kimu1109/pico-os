#include "gui/scenes/InputTestScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"

void InputTestScene::onEnter(){
    const Rect content = Scene::contentRect();

    int y = content.y + MARGIN;

    this->caption = new Label<PICO_STR_M>(MARGIN, y, "キーボードは常駐");
    WidgetFunctions::Add(this->caption);
    y += this->caption->getH() + ROW_GAP;

    this->textbox = new Textbox<PICO_STR_LL>(
        this->saved_text.c_str(),
        MARGIN, y,
        content.w - MARGIN * 2, TEXTBOX_HEIGHT,
        false
    );
    WidgetFunctions::Add(this->textbox);
    y += TEXTBOX_HEIGHT + ROW_GAP;

    this->number = new NumberInput(MARGIN, y, content.w - MARGIN * 2);
    WidgetFunctions::Add(this->number);
    y += this->number->getH() + ROW_GAP;

    this->back_button = new Button(MARGIN, y, "戻る");
    this->back_button->setH(BUTTON_HEIGHT);
    this->back_button->setOnPressEnd([](){
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->back_button);
}

void InputTestScene::onExit(){
    //ウィジェットが破棄される前に、次回復元したい状態だけ吸い出しておく
    if(this->textbox) this->saved_text = *this->textbox->getText();

    this->caption = nullptr;
    this->textbox = nullptr;
    this->number = nullptr;
    this->back_button = nullptr;
}
