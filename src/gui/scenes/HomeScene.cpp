#include "gui/scenes/HomeScene.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "gui/scenes/SettingsScene.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"

#include <Arduino.h>
#include <cstdio>

void HomeScene::onEnter(){
    const Rect content = Scene::contentRect();

    if(AppFunctions::VisibleCount() == 0){
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
    this->prev_button->setW(PAGER_BUTTON_W + Button::kFrameExtra);
    this->prev_button->setH(PAGER_H - 4 + Button::kFrameExtra);
    this->prev_button->setOnPressEnd([this](){
        if(this->grid->prevPage()) this->updatePageLabel();
    });
    WidgetFunctions::Add(this->prev_button);

    this->next_button = new Button(content.w - MARGIN - PAGER_BUTTON_W, pager_y, ">");
    this->next_button->setW(PAGER_BUTTON_W + Button::kFrameExtra);
    this->next_button->setH(PAGER_H - 4 + Button::kFrameExtra);
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

    this->shown_page = this->grid->getPage();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", this->grid->getPage() + 1, this->grid->pageCount());
    this->page_label->setText(buf);
}

void HomeScene::onUpdate(){
    if(this->grid && this->page_label && this->grid->getPage() != this->shown_page) this->updatePageLabel();
    if(!this->grid) return;

    if(this->pending_menu >= 0){
        this->openAppMenu(this->pending_menu);
        this->pending_menu = -1;
    }
    const int long_pressed = this->grid->takeLongPress(millis(), kLongPressMs);
    if(long_pressed >= 0) this->pending_menu = long_pressed;

    //コントローラーのX/Yは、選んでいるタイルのメニュー(Aは起動、十字キーは選ぶ=フォーカスの仕事)
    if(PadFunctions::Pressed(PadFunctions::X | PadFunctions::Y)){
        const int index = this->grid->focusedApp();
        if(index >= 0) this->openAppMenu(index);
    }
}

bool HomeScene::onKey(const KeyInputFunctions::Event& ev){
    //m: 選んでいるタイルのメニュー
    if(this->grid && ev.isPlainChar() && (ev.cp == 'm' || ev.cp == 'M')){
        const int index = this->grid->focusedApp();
        if(index >= 0) this->openAppMenu(index);
        return true;
    }
    return false;
}

bool HomeScene::canHide(int app_index){
    const AppEntry* e = AppFunctions::Get(app_index);
    if(!e || e->create == &AppFunctions::MakeScene<SettingsScene>) return false;
    return AppFunctions::VisibleCount() > 1;
}

void HomeScene::openAppMenu(int app_index){
    const AppEntry* entry = AppFunctions::Get(app_index);
    if(!entry || WidgetRegistry::Resolve(this->app_menu)) return;

    enum class Action : uint8_t { Front, Prev, Next, Hide };
    constexpr int kMaxActions = 4;
    Action actions[kMaxActions];
    int count = 0;

    const int pos = AppFunctions::VisiblePosition(app_index);
    const int visible = AppFunctions::VisibleCount();
    auto* d = new PickerDialog(PickerDialog::Mode::Choice, entry->name.c_str(), "", "キャンセル");
    if(!d) return;
    if(pos > 1)           { d->addChoice("先頭へ移動"); actions[count++] = Action::Front; }
    if(pos > 0)           { d->addChoice("前へ移動");   actions[count++] = Action::Prev; }
    if(pos < visible - 1) { d->addChoice("後ろへ移動"); actions[count++] = Action::Next; }
    if(canHide(app_index)){ d->addChoice("ランチャから隠す"); actions[count++] = Action::Hide; }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    this->app_menu = d->getId();

    d->setOnClosed([this, d, app_index, actions, count](bool is_ok){
        const int sel = d->getSelectedIndex();
        WidgetFunctions::DestroyLater(d);
        this->app_menu = WidgetIdTools::Invalid();
        if(!is_ok || sel < 0 || sel >= count || !this->grid) return;
        switch(actions[sel]){
            case Action::Front: AppFunctions::MoveAppToFront(app_index); break;
            case Action::Prev:  AppFunctions::MoveApp(app_index, -1); break;
            case Action::Next:  AppFunctions::MoveApp(app_index, +1); break;
            case Action::Hide:  AppFunctions::SetHidden(app_index, true); break;
        }
        if(actions[sel] == Action::Hide) this->grid->layoutChanged();
        else                             this->grid->showApp(app_index);
        this->updatePageLabel();
    });
}

void HomeScene::onExit(){
    //ウィジェット本体の破棄はSceneFunctionsが行うので、ここではポインタを捨てるだけ
    this->grid = nullptr;
    this->prev_button = nullptr;
    this->next_button = nullptr;
    this->page_label = nullptr;
    this->empty_label = nullptr;
    this->app_menu = WidgetIdTools::Invalid();
    this->pending_menu = -1;
}
