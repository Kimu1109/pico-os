#include "gui/scenes/HomeScene.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "gui/scenes/SettingsScene.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "home/Home_Layout.hpp"
#include "home/Home_Widget.hpp"

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

    //ウィジェットに、SDから読み直す等の重い仕事をさせておく(置いてある種類ごとに1回)
    for(int k = 0; k < HomeLayout::kKindCount; k++){
        if(!HomeLayout::HasKind((HomeLayout::Kind)k)) continue;
        if(HomeWidget* w = HomeLayout::Impl((HomeLayout::Kind)k)) w->onHomeEnter();
    }

    //ページ送りの分は常に空けておく(1ページの行数は空けても変わらない)。
    //ウィジェットを足してページが増えることがあるので、ページ送りは作っておき、1ページなら隠す
    this->grid = new AppGrid(0, content.y, content.w, content.h - PAGER_H);
    this->grid->setOnLaunch([](int app_index){
        AppFunctions::Launch(app_index);
    });
    WidgetFunctions::Add(this->grid);

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
    this->shown_pages = this->grid->pageCount();
    const bool pager = this->shown_pages > 1;
    this->prev_button->setVisible(pager);
    this->next_button->setVisible(pager);
    this->page_label->setVisible(pager);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", this->grid->getPage() + 1, this->shown_pages);
    this->page_label->setText(buf);
}

void HomeScene::onUpdate(){
    if(!this->grid) return;
    for(int k = 0; k < HomeLayout::kKindCount; k++){
        if(!HomeLayout::HasKind((HomeLayout::Kind)k)) continue;
        if(HomeWidget* w = HomeLayout::Impl((HomeLayout::Kind)k)) w->tick();
    }
    if(this->page_label && (this->grid->getPage() != this->shown_page || this->grid->pageCount() != this->shown_pages)){
        this->updatePageLabel();
    }

    if(this->has_pending_menu){
        this->has_pending_menu = false;
        this->openItemMenu(this->pending_menu);
    }else if(this->pending_add >= 0 && !WidgetRegistry::Resolve(this->app_menu)){
        const int pos = this->pending_add;
        this->pending_add = -1;
        this->openAddMenu(pos);
    }
    AppGrid::LongPress lp;
    if(this->grid->takeLongPress(millis(), kLongPressMs, lp)){
        this->pending_menu = lp;
        this->has_pending_menu = true;
    }

    //コントローラーのX/Yは、選んでいる項目のメニュー(Aは起動、十字キーは選ぶ=フォーカスの仕事)
    if(PadFunctions::Pressed(PadFunctions::X | PadFunctions::Y)) this->openFocusedMenu();
}

void HomeScene::openFocusedMenu(){
    AppGrid::LongPress t;
    if(this->grid && this->grid->focusedItem(t.type, t.index)) t.on_item = true;
    this->openItemMenu(t);
}

bool HomeScene::onKey(const KeyInputFunctions::Event& ev){
    //m: 選んでいる項目のメニュー
    if(this->grid && ev.isPlainChar() && (ev.cp == 'm' || ev.cp == 'M')){
        this->openFocusedMenu();
        return true;
    }
    return false;
}

bool HomeScene::canHide(int app_index){
    const AppEntry* e = AppFunctions::Get(app_index);
    if(!e || e->create == &AppFunctions::MakeScene<SettingsScene>) return false;
    return AppFunctions::VisibleCount() > 1;
}

void HomeScene::openItemMenu(const AppGrid::LongPress& target){
    if(!this->grid || WidgetRegistry::Resolve(this->app_menu)) return;
    using HomeLayout::ItemType;

    //空いた所: そのまま追加のメニューへ(並びの最後へ足す)
    if(!target.on_item){
        HomeLayout::SeqEntry seq[HomeLayout::kMaxItems];
        this->openAddMenu(HomeLayout::Sequence(seq, HomeLayout::kMaxItems));
        return;
    }

    enum class Action : uint8_t { Front, Prev, Next, Hide, Remove, Size1x1, Size2x1, Size2x2, Add };
    constexpr int kMaxActions = 9;
    Action actions[kMaxActions];
    int count = 0;

    const bool is_widget = (target.type == ItemType::Widget);
    const char* title = "";
    if(is_widget){
        if(target.index < 0 || target.index >= HomeLayout::placed_count) return;
        title = HomeLayout::Info(HomeLayout::placed[target.index].kind).name;
    }else{
        const AppEntry* entry = AppFunctions::Get(target.index);
        if(!entry) return;
        title = entry->name.c_str();
    }

    HomeLayout::SeqEntry seq[HomeLayout::kMaxItems];
    const int n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    const int pos = HomeLayout::SequencePosition(target.type, target.index);

    auto* d = new PickerDialog(PickerDialog::Mode::Choice, title, "", "キャンセル");
    if(!d) return;
    if(pos > 1)     { d->addChoice("先頭へ移動"); actions[count++] = Action::Front; }
    if(pos > 0)     { d->addChoice("前へ移動");   actions[count++] = Action::Prev; }
    if(pos < n - 1) { d->addChoice("後ろへ移動"); actions[count++] = Action::Next; }
    if(is_widget){
        const HomeLayout::Placed& p = HomeLayout::placed[target.index];
        const uint8_t sizes = HomeLayout::Info(p.kind).sizes;
        const uint8_t cur = HomeLayout::SizeBitOf(p.w, p.h);
        if((sizes & HomeLayout::S1x1) && cur != HomeLayout::S1x1){ d->addChoice("大きさ: 小");   actions[count++] = Action::Size1x1; }
        if((sizes & HomeLayout::S2x1) && cur != HomeLayout::S2x1){ d->addChoice("大きさ: 横長"); actions[count++] = Action::Size2x1; }
        if((sizes & HomeLayout::S2x2) && cur != HomeLayout::S2x2){ d->addChoice("大きさ: 大");   actions[count++] = Action::Size2x2; }
        d->addChoice("削除"); actions[count++] = Action::Remove;
    }else if(canHide(target.index)){
        d->addChoice("ランチャから隠す"); actions[count++] = Action::Hide;
    }
    if(HomeLayout::placed_count < HomeLayout::kMaxWidgets){
        d->addChoice("ウィジェット追加"); actions[count++] = Action::Add;
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    this->app_menu = d->getId();

    const AppGrid::LongPress t = target;
    d->setOnClosed([this, d, t, pos, actions, count](bool is_ok){
        const int sel = d->getSelectedIndex();
        WidgetFunctions::DestroyLater(d);
        this->app_menu = WidgetIdTools::Invalid();
        if(!is_ok || sel < 0 || sel >= count || !this->grid) return;
        const int rows = this->grid->rowsPerPage();
        int idx = t.index;
        bool follow = true;
        switch(actions[sel]){
            case Action::Front:
                HomeLayout::MoveToFront(t.type, t.index);
                if(t.type == HomeLayout::ItemType::Widget) idx = this->findWidgetAfterMove(0);
                break;
            case Action::Prev:    HomeLayout::Move(t.type, t.index, -1, rows, &idx); break;
            case Action::Next:    HomeLayout::Move(t.type, t.index, +1, rows, &idx); break;
            case Action::Hide:    HomeLayout::HideApp(t.index); follow = false; break;
            case Action::Remove:  HomeLayout::Remove(t.index); follow = false; break;
            case Action::Size1x1: HomeLayout::Resize(t.index, 1, 1); break;
            case Action::Size2x1: HomeLayout::Resize(t.index, 2, 1); break;
            case Action::Size2x2: HomeLayout::Resize(t.index, 2, 2); break;
            case Action::Add:
                //ダイアログを閉じた次のフレームで追加のメニューを開く。押した項目の前へ足す
                this->pending_add = pos < 0 ? 0 : pos;
                follow = false;
                break;
        }
        if(follow && idx >= 0) this->grid->showItem(t.type, idx);
        else                   this->grid->layoutChanged();
        this->updatePageLabel();
    });
}

int HomeScene::findWidgetAfterMove(int seq_pos){
    HomeLayout::SeqEntry seq[HomeLayout::kMaxItems];
    const int n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    if(seq_pos < 0 || seq_pos >= n || seq[seq_pos].type != HomeLayout::ItemType::Widget) return -1;
    return seq[seq_pos].index;
}

void HomeScene::openAddMenu(int seq_pos){
    if(!this->grid || WidgetRegistry::Resolve(this->app_menu)) return;
    if(HomeLayout::placed_count >= HomeLayout::kMaxWidgets) return;

    struct Choice { HomeLayout::Kind kind; uint8_t w, h; };
    constexpr int kMaxChoices = HomeLayout::kKindCount * 3;
    Choice choices[kMaxChoices];
    int count = 0;

    auto* d = new PickerDialog(PickerDialog::Mode::Choice, "ウィジェット", "", "キャンセル");
    if(!d) return;
    static const uint8_t kSizes[3][2] = { {1, 1}, {2, 1}, {2, 2} };
    for(int k = 0; k < HomeLayout::kKindCount; k++){
        const HomeLayout::KindInfo& info = HomeLayout::Info((HomeLayout::Kind)k);
        for(const auto& sz : kSizes){
            if(!(info.sizes & HomeLayout::SizeBitOf(sz[0], sz[1]))) continue;
            char label[48];
            snprintf(label, sizeof(label), "%s(%s)", info.name, HomeLayout::SizeName(sz[0], sz[1]));
            d->addChoice(label);
            choices[count++] = Choice{ (HomeLayout::Kind)k, sz[0], sz[1] };
        }
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    this->app_menu = d->getId();

    d->setOnClosed([this, d, choices, count, seq_pos](bool is_ok){
        const int sel = d->getSelectedIndex();
        WidgetFunctions::DestroyLater(d);
        this->app_menu = WidgetIdTools::Invalid();
        if(!is_ok || sel < 0 || sel >= count || !this->grid) return;
        const int idx = HomeLayout::Add(choices[sel].kind, choices[sel].w, choices[sel].h, seq_pos);
        if(idx < 0) return;
        if(HomeWidget* w = HomeLayout::Impl(choices[sel].kind)) w->onHomeEnter();
        this->grid->showItem(HomeLayout::ItemType::Widget, idx);
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
    this->has_pending_menu = false;
    this->pending_add = -1;
}
