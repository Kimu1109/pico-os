#include "gui/scenes/HomeScene.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Widget_Functions.hpp"

#include <cstdio>

void HomeScene::onEnter(){
    const Rect content = Scene::contentRect();

    if(AppFunctions::Count() == 0){
        this->empty_label = new Label<PICO_STR_L>(MARGIN, content.y + MARGIN,
            "アプリが登録されていません。\nApp_List.cpp のSetup()へ追加してください。");
        this->empty_label->setMaxWidth(content.w - MARGIN * 2);
        WidgetFunctions::Add(this->empty_label);
        return;
    }

    //先にページ送りが要るかを知りたいが、それは1ページに何個入るか次第なので、
    //いったん全面サイズでグリッドを作って判定し、必要なら高さを詰める
    this->grid = new AppGrid(0, content.y, content.w, content.h);
    const bool needs_pager = this->grid->pageCount() > 1;
    if(needs_pager){
        this->grid->setH(content.h - PAGER_H);
    }
    this->grid->setOnLaunch([](int app_index){
        AppFunctions::Launch(app_index);
    });
    WidgetFunctions::Add(this->grid);

    if(!needs_pager) return;

    const int pager_y = content.y + content.h - PAGER_H + 2;

    this->prev_button = new Button(MARGIN, pager_y, "<");
    this->prev_button->setW(PAGER_BUTTON_W);
    this->prev_button->setH(PAGER_H - 4);
    this->prev_button->setOnPressEnd([this](){
        if(this->grid->prevPage()) this->updatePageLabel();
    });
    WidgetFunctions::Add(this->prev_button);

    this->next_button = new Button(content.w - MARGIN - PAGER_BUTTON_W, pager_y, ">");
    this->next_button->setW(PAGER_BUTTON_W);
    this->next_button->setH(PAGER_H - 4);
    this->next_button->setOnPressEnd([this](){
        if(this->grid->nextPage()) this->updatePageLabel();
    });
    WidgetFunctions::Add(this->next_button);

    this->page_label = new Label<PICO_STR_S>(0, pager_y + 4, "");
    this->page_label->setFontSize(FontFn::Small);
    this->page_label->setMaxWidth(content.w);
    this->page_label->setTextAlign(TextAlign::Center);
    // 中央揃えのためmaxWidthを画面幅いっぱいに取っているので、当たり判定も
    // そのままだと横一杯(=左右の</>ボタンの真上)まで広がってしまい、
    // ボタンの中央付近を叩いたタップをこのラベルが横取りしてしまう
    // (--shotでボタンが反応しないのを確認して気付いた)。表示専用なので
    // タップを素通りさせる
    this->page_label->setHitTransparent(true);
    WidgetFunctions::Add(this->page_label);

    this->updatePageLabel();
}

void HomeScene::updatePageLabel(){
    if(!this->page_label || !this->grid) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", this->grid->getPage() + 1, this->grid->pageCount());
    this->page_label->setText(buf);
}

void HomeScene::onExit(){
    //ウィジェット本体の破棄はSceneFunctionsが行うので、ここではポインタを捨てるだけ
    this->grid = nullptr;
    this->prev_button = nullptr;
    this->next_button = nullptr;
    this->page_label = nullptr;
    this->empty_label = nullptr;
}
