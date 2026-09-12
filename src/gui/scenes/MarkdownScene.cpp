#include "gui/scenes/MarkdownScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"

void MarkdownScene::onEnter(){
    const Rect content = Scene::contentRect();

    const int view_height = content.h - BUTTON_HEIGHT - MARGIN * 3;

    this->view = new MarkdownView(0, content.y, content.w, view_height);
    this->view->load(this->doc_path.c_str());
    WidgetFunctions::Add(this->view);

    this->back_button = new Button(MARGIN, content.y + view_height + MARGIN, "戻る");
    this->back_button->setH(BUTTON_HEIGHT);
    this->back_button->setOnPressEnd([](){
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->back_button);
}

void MarkdownScene::onExit(){
    this->view = nullptr;
    this->back_button = nullptr;
}
