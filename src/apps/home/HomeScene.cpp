#include "apps/home/HomeScene.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "gui/scenes/SettingsScene.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "apps/home/Home_Layout.hpp"
#include "apps/home/Home_Widget.hpp"

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

    //フォルダを開いている間だけ出す。押すとトップへ戻る
    this->back_button = new Button(MARGIN, pager_y, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setW(PAGER_BUTTON_W + Button::kFrameExtra);
    this->back_button->setH(PAGER_H - 4 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([this](){
        this->grid->closeFolder();
        this->updatePageLabel();
    });
    WidgetFunctions::Add(this->back_button);

    this->page_label = new Label<PICO_STR_L>(0, pager_y + 4, "");
    this->page_label->setFontSize(FontFn::Small);
    this->page_label->setMaxWidth(content.w);
    //フォルダの名前が長いときに2行目へはみ出さないよう、1行の高さで切る
    this->page_label->setMaxHeight(Label<PICO_STR_L>::GetLineHeight(FontFn::Small));
    this->page_label->setTextAlign(TextAlign::Center);
    // 中央揃えのためmaxWidthを画面幅いっぱいに取っているので、当たり判定も
    // そのままだと横一杯(=左右の</>ボタンの真上)まで広がってしまい、
    // ボタンの中央付近を叩いたタップをこのラベルが横取りしてしまう
    // (--shotでボタンが反応しないのを確認して気付いた)。表示専用なので
    // タップを素通りさせる
    this->page_label->setHitTransparent(true);
    WidgetFunctions::Add(this->page_label);

    //アプリを開く前に開いていたフォルダ・ページへ戻す(フォルダが無くなっていればトップ)
    if(this->reopen_folder >= 0) this->grid->openFolder(this->reopen_folder);
    this->grid->setPage(this->reopen_page);
    this->updatePageLabel();
}

void HomeScene::updatePageLabel(){
    if(!this->page_label || !this->grid) return;

    const Rect content = Scene::contentRect();
    this->shown_page = this->grid->getPage();
    this->shown_pages = this->grid->pageCount();
    this->shown_folder = this->grid->openedFolder();
    const bool pager = this->shown_pages > 1;
    const bool in_folder = this->shown_folder >= 0;
    this->prev_button->setVisible(pager);
    this->next_button->setVisible(pager);
    this->back_button->setVisible(in_folder);

    char buf[96];
    if(in_folder){
        //[戻る] n/m 名前 [<][>](名前が長くてもページは見えるように先に)。ページ送りは右へ寄せる
        const int prev_x = content.w - MARGIN - PAGER_BUTTON_W * 2 - 4;
        this->prev_button->setX(prev_x);
        const int label_x = MARGIN + PAGER_BUTTON_W + Button::kFrameExtra + 6;
        const int label_right = pager ? prev_x - 4 : content.w - MARGIN;
        this->page_label->setX(label_x);
        this->page_label->setMaxWidth(label_right - label_x);
        this->page_label->setTextAlign(TextAlign::Left);
        if(pager) snprintf(buf, sizeof(buf), "%d/%d %s", this->shown_page + 1, this->shown_pages, AppFunctions::FolderName(this->shown_folder));
        else      snprintf(buf, sizeof(buf), "%s", AppFunctions::FolderName(this->shown_folder));
        this->page_label->setVisible(true);
    }else{
        this->prev_button->setX(MARGIN);
        this->page_label->setX(0);
        this->page_label->setMaxWidth(content.w);
        this->page_label->setTextAlign(TextAlign::Center);
        snprintf(buf, sizeof(buf), "%d / %d", this->shown_page + 1, this->shown_pages);
        this->page_label->setVisible(pager);
    }
    this->page_label->setText(buf);
}

void HomeScene::onUpdate(){
    if(!this->grid) return;
    for(int k = 0; k < HomeLayout::kKindCount; k++){
        if(!HomeLayout::HasKind((HomeLayout::Kind)k)) continue;
        if(HomeWidget* w = HomeLayout::Impl((HomeLayout::Kind)k)) w->tick();
    }
    if(this->page_label && (this->grid->getPage() != this->shown_page || this->grid->pageCount() != this->shown_pages
                            || this->grid->openedFolder() != this->shown_folder)){
        this->updatePageLabel();
    }

    if(this->has_pending_menu){
        this->has_pending_menu = false;
        this->openItemMenu(this->pending_menu);
    }else if(this->pending != Pending::None && !WidgetRegistry::Resolve(this->app_menu)){
        const Pending kind = this->pending;
        const int value = this->pending_value;
        this->setPending(Pending::None, -1);
        switch(kind){
            case Pending::Add:        this->openAddMenu(value); break;
            case Pending::FolderPick: this->openFolderPicker(value); break;
            case Pending::NewFolder:  this->openFolderName(value, -1); break;
            case Pending::Rename:     this->openFolderName(-1, value); break;
            case Pending::None:       break;
        }
    }
    AppGrid::LongPress lp;
    if(this->grid->takeLongPress(millis(), kLongPressMs, lp)){
        this->pending_menu = lp;
        this->has_pending_menu = true;
    }

    //コントローラーのX/Yは、選んでいる項目のメニュー(Aは起動、十字キーは選ぶ=フォーカスの仕事)
    if(PadFunctions::Pressed(PadFunctions::X | PadFunctions::Y)) this->openFocusedMenu();
    //Bはフォルダを閉じる(フォーカスの枠が出ていないときも。出ていればグリッドへも届くが、もう閉じている)
    if(this->grid->openedFolder() >= 0 && PadFunctions::Pressed(PadFunctions::B)
       && !WidgetRegistry::Resolve(this->app_menu)){
        this->grid->closeFolder();
        this->updatePageLabel();
    }
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
    //Esc/BackSpace: フォルダを閉じる
    using KeyInputFunctions::Key;
    if(this->grid && this->grid->openedFolder() >= 0 && (ev.key == Key::Escape || ev.key == Key::Backspace)
       && !WidgetRegistry::Resolve(this->app_menu)){
        this->grid->closeFolder();
        this->updatePageLabel();
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

    const int open_folder = this->grid->openedFolder();
    const bool in_folder = open_folder >= 0;

    //空いた所: そのまま追加のメニューへ(並びの最後へ足す)。フォルダの中にはウィジェットを置かない
    if(!target.on_item){
        if(in_folder) return;
        HomeLayout::SeqEntry seq[HomeLayout::kMaxItems];
        this->openAddMenu(HomeLayout::Sequence(seq, HomeLayout::kMaxItems));
        return;
    }

    enum class Action : uint8_t { Front, Prev, Next, Hide, Remove, Size1x1, Size2x1, Size2x2, Add,
                                  ToFolder, OutOfFolder, Rename, Dissolve };
    constexpr int kMaxActions = 10;
    Action actions[kMaxActions];
    int count = 0;

    const bool is_widget = (target.type == ItemType::Widget);
    const bool is_folder = (target.type == ItemType::Folder);
    const char* title = "";
    if(is_widget){
        if(target.index < 0 || target.index >= HomeLayout::placed_count) return;
        title = HomeLayout::Info(HomeLayout::placed[target.index].kind).name;
    }else if(is_folder){
        if(!AppFunctions::FolderExists(target.index)) return;
        title = AppFunctions::FolderName(target.index);
    }else{
        const AppEntry* entry = AppFunctions::Get(target.index);
        if(!entry) return;
        title = entry->name.c_str();
    }

    //並びの中の位置と長さ(フォルダの中ではフォルダの中の見えているアプリ)
    int pos, n;
    if(in_folder){
        pos = AppFunctions::FolderAppPosition(target.index);
        n = AppFunctions::FolderAppCount(open_folder);
    }else{
        HomeLayout::SeqEntry seq[HomeLayout::kMaxItems];
        n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
        pos = HomeLayout::SequencePosition(target.type, target.index);
    }

    auto* d = new PickerDialog(PickerDialog::Mode::Choice, title, "", "キャンセル");
    if(!d) return;
    if(pos > 1)     { d->addChoice("先頭へ移動"); actions[count++] = Action::Front; }
    if(pos > 0)     { d->addChoice("前へ移動");   actions[count++] = Action::Prev; }
    if(pos >= 0 && pos < n - 1) { d->addChoice("後ろへ移動"); actions[count++] = Action::Next; }
    if(is_widget){
        const HomeLayout::Placed& p = HomeLayout::placed[target.index];
        const uint8_t sizes = HomeLayout::Info(p.kind).sizes;
        const uint8_t cur = HomeLayout::SizeBitOf(p.w, p.h);
        if((sizes & HomeLayout::S1x1) && cur != HomeLayout::S1x1){ d->addChoice("大きさ: 小");   actions[count++] = Action::Size1x1; }
        if((sizes & HomeLayout::S2x1) && cur != HomeLayout::S2x1){ d->addChoice("大きさ: 横長"); actions[count++] = Action::Size2x1; }
        if((sizes & HomeLayout::S2x2) && cur != HomeLayout::S2x2){ d->addChoice("大きさ: 大");   actions[count++] = Action::Size2x2; }
        d->addChoice("削除"); actions[count++] = Action::Remove;
    }else if(is_folder){
        d->addChoice("名前を変える");   actions[count++] = Action::Rename;
        d->addChoice("フォルダを解除"); actions[count++] = Action::Dissolve;
    }else{
        //別のフォルダへ移せるのは、他にフォルダがあるか、新しく作れるとき
        const int others = AppFunctions::FolderCount() - (in_folder ? 1 : 0);
        if(others > 0 || AppFunctions::FolderCount() < AppFunctions::kMaxFolders){
            d->addChoice(in_folder ? "別のフォルダへ" : "フォルダへ入れる"); actions[count++] = Action::ToFolder;
        }
        if(in_folder){ d->addChoice("フォルダから出す"); actions[count++] = Action::OutOfFolder; }
        if(canHide(target.index)){ d->addChoice("ランチャから隠す"); actions[count++] = Action::Hide; }
    }
    if(!in_folder && HomeLayout::placed_count < HomeLayout::kMaxWidgets){
        d->addChoice("ウィジェット追加"); actions[count++] = Action::Add;
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    this->app_menu = d->getId();

    const AppGrid::LongPress t = target;
    d->setOnClosed([this, d, t, pos, actions, count, in_folder](bool is_ok){
        const int sel = d->getSelectedIndex();
        WidgetFunctions::DestroyLater(d);
        this->app_menu = WidgetIdTools::Invalid();
        if(!is_ok || sel < 0 || sel >= count || !this->grid) return;
        const int rows = this->grid->rowsPerPage();
        int idx = t.index;
        bool follow = true;
        switch(actions[sel]){
            case Action::Front:
                if(in_folder){ AppFunctions::MoveAppToFront(t.index); break; }
                HomeLayout::MoveToFront(t.type, t.index);
                if(t.type == HomeLayout::ItemType::Widget) idx = this->findWidgetAfterMove(0);
                break;
            case Action::Prev:
                if(in_folder) AppFunctions::MoveApp(t.index, -1);
                else          HomeLayout::Move(t.type, t.index, -1, rows, &idx);
                break;
            case Action::Next:
                if(in_folder) AppFunctions::MoveApp(t.index, +1);
                else          HomeLayout::Move(t.type, t.index, +1, rows, &idx);
                break;
            case Action::Hide:    HomeLayout::HideApp(t.index); follow = false; break;
            case Action::Remove:  HomeLayout::Remove(t.index); follow = false; break;
            case Action::Size1x1: HomeLayout::Resize(t.index, 1, 1); break;
            case Action::Size2x1: HomeLayout::Resize(t.index, 2, 1); break;
            case Action::Size2x2: HomeLayout::Resize(t.index, 2, 2); break;
            case Action::Add:
                //ダイアログを閉じた次のフレームで追加のメニューを開く。押した項目の前へ足す
                this->setPending(Pending::Add, pos < 0 ? 0 : pos);
                follow = false;
                break;
            case Action::ToFolder:
                this->setPending(Pending::FolderPick, t.index);
                follow = false;
                break;
            case Action::OutOfFolder:
                //出したアプリはフォルダのすぐ後ろ(トップ)。フォルダは開いたまま
                HomeLayout::RemoveFromFolder(t.index);
                follow = false;
                break;
            case Action::Rename:
                this->setPending(Pending::Rename, t.index);
                follow = false;
                break;
            case Action::Dissolve:
                HomeLayout::DissolveFolder(t.index);
                follow = false;
                break;
        }
        if(follow && idx >= 0) this->grid->showItem(t.type, idx);
        else                   this->grid->layoutChanged();
        this->updatePageLabel();
    });
}

void HomeScene::openFolderPicker(int app_index){
    if(!this->grid || WidgetRegistry::Resolve(this->app_menu) || !AppFunctions::Get(app_index)) return;
    const int current = AppFunctions::FolderOf(app_index);

    //選んだ行 → フォルダの番号(-1 = 新しいフォルダ)
    const bool can_create = AppFunctions::FolderCount() < AppFunctions::kMaxFolders;
    if(!can_create && AppFunctions::FolderCount() - (current >= 0 ? 1 : 0) <= 0) return;
    int folders[AppFunctions::kMaxFolders + 1];
    int count = 0;
    auto* d = new PickerDialog(PickerDialog::Mode::Choice, "どのフォルダへ", "", "キャンセル");
    if(!d) return;
    if(can_create){
        d->addChoice("新しいフォルダ");
        folders[count++] = -1;
    }
    for(int f = 0; f < AppFunctions::kMaxFolders; f++){
        if(!AppFunctions::FolderExists(f) || f == current) continue;
        d->addChoice(AppFunctions::FolderName(f));
        folders[count++] = f;
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    this->app_menu = d->getId();

    d->setOnClosed([this, d, folders, count, app_index](bool is_ok){
        const int sel = d->getSelectedIndex();
        WidgetFunctions::DestroyLater(d);
        this->app_menu = WidgetIdTools::Invalid();
        if(!is_ok || sel < 0 || sel >= count || !this->grid) return;
        if(folders[sel] < 0){
            this->setPending(Pending::NewFolder, app_index);
            return;
        }
        HomeLayout::MoveToFolder(app_index, folders[sel]);
        //トップから入れたときは入れた先のフォルダを見せる。フォルダの中からなら今のフォルダのまま
        if(this->grid->openedFolder() < 0) this->grid->showItem(HomeLayout::ItemType::Folder, folders[sel]);
        else                               this->grid->layoutChanged();
        this->updatePageLabel();
    });
}

void HomeScene::openFolderName(int app_index, int folder){
    if(!this->grid || WidgetRegistry::Resolve(this->app_menu)) return;
    const bool rename = folder >= 0;
    if(rename ? !AppFunctions::FolderExists(folder) : !AppFunctions::Get(app_index)) return;

    auto* d = new InputDialog("フォルダ名:", true);
    if(!d) return;
    if(rename) d->setInput(AppFunctions::FolderName(folder));
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->openKeyboard();
    this->app_menu = d->getId();

    d->setOnClosed([this, d, rename, folder, app_index](bool is_ok){
        const auto name = d->getInput();
        WidgetFunctions::DestroyLater(d);
        this->app_menu = WidgetIdTools::Invalid();
        if(!is_ok || !this->grid) return;
        if(rename){
            AppFunctions::RenameFolder(folder, name.c_str());
            this->grid->layoutChanged();
        }else{
            const int f = HomeLayout::CreateFolder(app_index, name.c_str());
            if(f >= 0 && this->grid->openedFolder() < 0) this->grid->showItem(HomeLayout::ItemType::Folder, f);
            else                                         this->grid->layoutChanged();
        }
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
    //戻ってきたときに同じフォルダ・ページを開く
    if(this->grid){
        this->reopen_folder = this->grid->openedFolder();
        this->reopen_page = this->grid->getPage();
    }
    //ウィジェット本体の破棄はSceneFunctionsが行うので、ここではポインタを捨てるだけ
    this->grid = nullptr;
    this->prev_button = nullptr;
    this->next_button = nullptr;
    this->back_button = nullptr;
    this->page_label = nullptr;
    this->empty_label = nullptr;
    this->app_menu = WidgetIdTools::Invalid();
    this->has_pending_menu = false;
    this->setPending(Pending::None, -1);
    this->shown_folder = -2;
}
