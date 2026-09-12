#include "gui/scenes/HomeScene.hpp"
#include "gui/scenes/MarkdownScene.hpp"
#include "gui/scenes/InputTestScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"

void HomeScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->title = new Label<PICO_STR_M>(MARGIN, content.y + MARGIN, "pico-os");
    this->title->setFontSize(FontFn::Big);
    WidgetFunctions::Add(this->title);

    const int button_base_y = content.y + MARGIN + 40;

    this->markdown_button = new Button(MARGIN, button_base_y, "Markdownを開く");
    this->markdown_button->setW(BUTTON_WIDTH);
    this->markdown_button->setH(BUTTON_HEIGHT);
    this->markdown_button->setOnPressEnd([](){
        //Push: このシーンを退避して進む(MarkdownScene側の「戻る」でPop()できる)
        SceneFunctions::Push(new MarkdownScene());
    });
    WidgetFunctions::Add(this->markdown_button);

    this->input_button = new Button(MARGIN, button_base_y + BUTTON_HEIGHT + BUTTON_GAP + 10, "入力テスト");
    this->input_button->setW(BUTTON_WIDTH);
    this->input_button->setH(BUTTON_HEIGHT);
    this->input_button->setOnPressEnd([](){
        SceneFunctions::Push(new InputTestScene());
    });
    WidgetFunctions::Add(this->input_button);
}

void HomeScene::onExit(){
    //ウィジェット本体の破棄はSceneFunctionsが行うので、ここではポインタを捨てるだけ
    this->title = nullptr;
    this->markdown_button = nullptr;
    this->input_button = nullptr;
}
