#include "apps/store/StoreScene.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "system/Config_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "storage/SD_Path.hpp"

#include <cstdio>
#include <cstring>
#include <new>

// 一番下のボタンの行の高さ
static constexpr int kActionH = 24;
// 選んだアプリの欄の行数(名前と版 / 権限 / 説明2行)
static constexpr int kDetailLines = 4;
// 進み具合のバーの高さ
static constexpr int kProgressH = 6;

void StoreScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;
    const int line_h = FontFn::GetFontSize(FontFn::Small) + 2;

    // ---- 上部の行: [戻る] アプリストア [設定] [更新] ----
    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);
    const int row_h = this->back_button->getLocalRect().h;

    this->refresh_button = new Button(0, y0, "更新");
    this->refresh_button->setFontSize(FontFn::Small);
    this->refresh_button->setH(20 + Button::kFrameExtra);
    this->refresh_button->setW(this->refresh_button->getLocalRect().w);
    this->refresh_button->setX(content.x + content.w - MARGIN - this->refresh_button->getLocalRect().w);
    this->refresh_button->setOnPressEnd([this](){
        if(!this->store) return;
        if(this->store->busy()){
            this->store->cancel();
            this->notice.clear();
            this->refreshStatus();
            this->refreshButtons();
            return;
        }
        this->startRefresh();
    });
    WidgetFunctions::Add(this->refresh_button);

    this->settings_button = new Button(0, y0, "設定");
    this->settings_button->setIcon(IconID::Settings, IconSize::Px16);
    this->settings_button->setFontSize(FontFn::Small);
    this->settings_button->setH(20 + Button::kFrameExtra);
    this->settings_button->setX(this->refresh_button->getLocalRect().x - MARGIN - this->settings_button->getLocalRect().w);
    this->settings_button->setOnPressEnd([this](){ this->openSettings(); });
    WidgetFunctions::Add(this->settings_button);

    const int title_x = this->back_button->getLocalRect().x + this->back_button->getLocalRect().w + MARGIN * 2;
    const int title_w = this->settings_button->getLocalRect().x - MARGIN * 2 - title_x;
    this->title_label = new Label<PICO_STR_M>(title_x, y0, "アプリストア");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setMaxWidth(title_w);
    this->title_label->setMaxHeight(line_h);
    this->title_label->setY(y0 + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(this->title_label);

    // ---- 一番下: [入れる] [開く] [削除] ----
    const int action_y = content.y + content.h - MARGIN - kActionH;
    const int third_w = (content.w - MARGIN * 4) / 3;
    auto action = [&](int col, const char* text) -> Button* {
        Button* b = new Button(content.x + MARGIN + (third_w + MARGIN) * col, action_y, text);
        b->setFontSize(FontFn::Small);
        b->setH(kActionH + Button::kFrameExtra);
        b->setW(third_w + Button::kFrameExtra);
        WidgetFunctions::Add(b);
        return b;
    };
    this->install_button = action(0, "入れる");
    this->install_button->setOnPressEnd([this](){ this->confirmInstall(); });
    this->open_button = action(1, "開く");
    this->open_button->setOnPressEnd([this](){
        const int i = this->selectedIndex();
        if(i < 0 || !this->store) return;
        //押した指を離したフレームのうちに別の画面へ移ると、ボタンの押し込みの絵が戻る前に消えるので次のフレームで
        this->pending_launch = true;
    });
    this->remove_button = action(2, "削除");
    this->remove_button->setOnPressEnd([this](){ this->chooseRollbackOrRemove(); });

    // ---- 状態の1行・進み具合・選んだアプリの欄 ----
    const int status_y = action_y - MARGIN - line_h;
    this->status_label = new Label<PICO_STR_LL>(content.x + MARGIN, status_y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setMaxHeight(line_h);
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    const int progress_y = status_y - 2 - kProgressH;
    this->progress = new ProgressBar(content.x + MARGIN, progress_y, content.w - MARGIN * 2, kProgressH);
    this->progress->setColor(PICO_BLUE);
    this->progress->setVisible(false);
    WidgetFunctions::Add(this->progress);

    const int detail_y = progress_y - MARGIN - line_h * kDetailLines;
    this->detail_label = new Label<PICO_STR_512B>(content.x + MARGIN, detail_y, "");
    this->detail_label->setFontSize(FontFn::Small);
    this->detail_label->setMaxWidth(content.w - MARGIN * 2);
    this->detail_label->setMaxHeight(line_h * kDetailLines);
    this->detail_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->detail_label);

    // ---- 一覧 ----
    const int list_y = y0 + row_h + MARGIN;
    this->app_list = new ScrollList(content.x + MARGIN, list_y, content.w - MARGIN * 2,
                                    detail_y - MARGIN - list_y, AppRepo::kMaxEntries + 1);
    this->app_list->setFontSize(FontFn::Text); //一覧の文字は文字の大きさの設定に従う
    this->app_list->setEnableIcon(true);
    this->app_list->setOnSelectItem([this](int row, bool already){
        const int index = (row >= 0 && row < this->row_count) ? this->rows[row] : -1;
        if(index == kRowUpdateAll){
            this->confirmUpdateAll();
            return;
        }
        const AppRepo::Entry* e = this->store ? this->store->entry(index) : nullptr;
        if(!e) return;
        this->selected_id.assign(e->id.c_str());
        this->notice.clear();
        this->refreshDetail();
        this->refreshStatus();
        this->refreshButtons();
        //同じ項目の2回目のタップ: 入っていれば開く、入っていなければ入れる
        if(already){
            const AppStore::Installed st = this->store->installed(index);
            if(st == AppStore::Installed::No || st == AppStore::Installed::Update || st == AppStore::Installed::Foreign){
                this->confirmInstall();
            }else if(st != AppStore::Installed::Conflict){
                this->pending_launch = true; //Same/Newer/KeyChanged
            }
        }
    });
    WidgetFunctions::Add(this->app_list);

    this->store = new (std::nothrow) AppStore();
    if(!this->store){
        ErrorFunctions::ShowFatal("メモリが足りないため、アプリストアを開けません");
    }else{
        this->store->loadConfig();
        this->store->loadCachedCatalog();
    }
    this->seen_revision = this->store ? this->store->revision() - 1 : 0;
    this->seen_state = AppStore::State::Idle;
    this->frames_since_enter = 0;
    this->pending_launch = false;
    //Wi-Fiに繋がっていれば開いたときに1回取りに行く(1回描いてから。TLSの接続で止まる前に画面を見せる)
    this->auto_refresh = this->store != nullptr && NetworkFunctions::IsConnected();
    if(this->store && !this->auto_refresh && this->store->count() == 0){
        this->setNotice("Wi-Fiに繋いで [更新] を押してください");
    }

    this->refreshList();
    this->refreshDetail();
    this->refreshStatus();
    this->refreshButtons();
}

void StoreScene::onExit(){
    if(this->store){
        this->store->cancel();
        this->store->close();
        delete this->store;
        this->store = nullptr;
    }
    this->back_button = this->refresh_button = this->settings_button = nullptr;
    this->install_button = this->open_button = this->remove_button = nullptr;
    this->title_label = nullptr;
    this->app_list = nullptr;
    this->detail_label = nullptr;
    this->progress = nullptr;
    this->status_label = nullptr;
}

int StoreScene::selectedIndex() const {
    if(!this->store || this->selected_id.empty()) return -1;
    for(int i = 0; i < this->store->count(); i++){
        if(this->store->entry(i)->id == this->selected_id.c_str()) return i;
    }
    return -1;
}

void StoreScene::refreshList(){
    if(!this->app_list || !this->store) return;
    this->app_list->clear();
    this->row_count = 0;
    this->blocked_warning.clear();
    const int batch = this->store->busy() ? 0 : this->store->batchCount();
    if(batch > 0){
        ScrollListTools::Item item;
        item.text.appendFormat("すべて更新 (%d件)", batch);
        item.icon = IconID::Refresh;
        item.color = PICO_BLUE;
        this->rows[this->row_count++] = kRowUpdateAll;
        this->app_list->add(item);
    }
    //危険の印が付いて入っているものを先に、それから入れられるもの・入っているもの
    for(int pass = 0; pass < 2; pass++){
        for(int i = 0; i < this->store->count(); i++){
            const bool blocked = this->store->blockedInstalled(i);
            if(blocked != (pass == 0)) continue;
            const AppStore::Installed st = this->store->installed(i);
            const bool present = st == AppStore::Installed::Same || st == AppStore::Installed::Update
                              || st == AppStore::Installed::Newer || st == AppStore::Installed::KeyChanged;
            //本体が古くて入れられないものも並べる(本体を更新すれば入れられると分かるように)
            const bool old_os = this->store->needsNewerOs(i) && this->store->entry(i)->status == AppRepo::Status::Normal;
            if(!this->store->installable(i) && !present && !old_os) continue;
            const AppRepo::Entry* e = this->store->entry(i);
            ScrollListTools::Item item;
            item.text.assign(e->name.c_str());
            switch(st){
                case AppStore::Installed::No:
                    item.icon = IconID::AppStore;
                    break;
                case AppStore::Installed::Same:
                case AppStore::Installed::Newer:
                    item.icon = IconID::CheckboxOn;
                    item.color = PICO_DARKGREEN;
                    break;
                case AppStore::Installed::Update:
                    item.icon = IconID::AppStore;
                    item.color = PICO_BLUE;
                    if(this->store->held(i)) item.text.append(" (見送り中)");
                    else if(this->store->installable(i)) item.text.append(" (更新)");
                    break;
                case AppStore::Installed::Foreign:
                    item.icon = IconID::AppBox;
                    break;
                case AppStore::Installed::Conflict:
                case AppStore::Installed::KeyChanged:
                    item.icon = IconID::AppBox;
                    item.color = PICO_RED;
                    break;
            }
            if(blocked){
                item.color = PICO_RED;
                item.text.append(" (危険)");
                if(this->blocked_warning.empty()){
                    //状態の行は1行なので短く(名前が長ければ後ろが切れる)
                    this->blocked_warning.assign(e->name.c_str());
                    this->blocked_warning.append("を削除してください(危険)");
                }
            }else if(old_os){
                item.text.append(" (本体が古い)");
            }else if(!this->store->installable(i)){
                item.text.append(" (取り下げ)");
            }
            this->rows[this->row_count++] = (int8_t)i;
            this->app_list->add(item);
        }
    }
    const int sel = this->selectedIndex();
    int sel_row = -1;
    for(int r = 0; r < this->row_count; r++) if(sel >= 0 && this->rows[r] == sel) sel_row = r;
    if(sel_row >= 0) this->app_list->setSelectedIndex(sel_row);
    else this->selected_id.clear();

    FixedString<PICO_STR_M> t("アプリストア");
    const int apps = this->row_count - (batch > 0 ? 1 : 0);
    if(apps > 0) t.appendFormat(" (%d)", apps);
    if(this->title_label) this->title_label->setText(t);
}

void StoreScene::refreshDetail(){
    if(!this->detail_label) return;
    const int i = this->selectedIndex();
    const AppRepo::Entry* e = this->store ? this->store->entry(i) : nullptr;
    if(!e){
        this->detail_label->setText(this->row_count > 0 ? "アプリを選んでください" : "");
        return;
    }
    FixedString<PICO_STR_512B> text;
    text.appendFormat("%s  %s  %luKB", e->name.c_str(), e->version.c_str(),
                      (unsigned long)((e->bytes + 1023) / 1024));
    switch(this->store->installed(i)){
        case AppStore::Installed::Same:     text.append(" (入っています)"); break;
        case AppStore::Installed::Update:   text.appendFormat(" (%s が入っています)", this->store->installedVersion(i)); break;
        case AppStore::Installed::Newer:    text.appendFormat(" (新しい%sが入っています)", this->store->installedVersion(i)); break;
        case AppStore::Installed::Foreign:  text.append(" (手で入れた同名のアプリがあります)"); break;
        case AppStore::Installed::Conflict: text.append(" (同名の別のアプリがあります)"); break;
        case AppStore::Installed::KeyChanged: text.append(" (作者の鍵が変わりました)"); break;
        default: break;
    }
    if(e->status == AppRepo::Status::Blocked) text.append(" 危険と判断されました");
    else if(e->status == AppRepo::Status::Yanked) text.append(" 取り下げられました");
    if(this->store->needsNewerOs(i)) text.appendFormat(" 本体#%lu以降が要ります", (unsigned long)e->min_os);
    if(this->store->held(i)) text.append(" この版は見送り中");
    FixedString<PICO_STR_S> prev;
    if(this->store->previousVersion(i, prev)) text.appendFormat(" 前の版%sに戻せます", prev.c_str());
    FixedString<PICO_STR_L> perms;
    AppRepo::PermsText(e->perms, perms);
    text.appendFormat("\n作者: %s  権限: %s\n%s", e->author.c_str(), perms.c_str(), e->description.c_str());
    this->detail_label->setText(text);
}

void StoreScene::setNotice(const char* text, int8_t color){
    this->notice.assign(text);
    this->notice_color = color;
    this->refreshStatus();
}

void StoreScene::refreshStatus(){
    if(!this->status_label || !this->store) return;
    const AppStore::State st = this->store->state();
    if(st == AppStore::State::Busy){
        this->status_label->setTextColor(PICO_DARKGREY);
        this->status_label->setText(this->store->message());
    }else if(!this->notice.empty()){
        this->status_label->setTextColor(this->notice_color);
        this->status_label->setText(this->notice);
    }else if(st == AppStore::State::Failed){
        this->status_label->setTextColor(PICO_RED);
        this->status_label->setText(this->store->message());
    }else if(!this->blocked_warning.empty()){
        this->status_label->setTextColor(PICO_RED);
        this->status_label->setText(this->blocked_warning);
    }else if(st == AppStore::State::Done){
        this->status_label->setTextColor(PICO_DARKGREEN);
        this->status_label->setText(this->store->message());
    }else{
        this->status_label->setTextColor(PICO_DARKGREY);
        this->status_label->setText("");
    }
}

void StoreScene::refreshButtons(){
    if(!this->store || !this->install_button) return;
    const bool busy = this->store->busy();
    this->refresh_button->setText(busy ? "中止" : "更新");
    const int i = this->selectedIndex();
    const AppStore::Installed st = this->store->installed(i);
    const char* label = "入れる";
    if(st == AppStore::Installed::Update) label = "更新";
    else if(st == AppStore::Installed::Same) label = "入れ直す";
    else if(st == AppStore::Installed::Foreign) label = "上書き";
    this->install_button->setText(label);
    const bool has = i >= 0;
    const bool present = has && st != AppStore::Installed::No && st != AppStore::Installed::Conflict;
    const bool can_install = st != AppStore::Installed::Conflict && st != AppStore::Installed::KeyChanged
                          && st != AppStore::Installed::Newer && this->store->installable(i);
    this->install_button->setEnabled(has && !busy && can_install);
    this->open_button->setEnabled(present && !busy);
    FixedString<PICO_STR_S> prev;
    this->remove_button->setText(present && this->store->previousVersion(i, prev) ? "戻す/削除" : "削除");
    this->remove_button->setEnabled(present && !busy);
    this->settings_button->setEnabled(!busy);
}

void StoreScene::startRefresh(){
    if(!this->store) return;
    this->notice.clear();
    if(!NetworkFunctions::IsConnected()){
        this->setNotice("Wi-Fiに繋がっていません", PICO_RED);
        return;
    }
    this->store->beginRefresh();
    this->refreshStatus();
    this->refreshButtons();
}

void StoreScene::confirmInstall(){
    const int i = this->selectedIndex();
    if(i < 0 || !this->store || this->store->busy()) return;
    const AppRepo::Entry* e = this->store->entry(i);
    const AppStore::Installed st = this->store->installed(i);
    if(st == AppStore::Installed::Conflict){
        this->setNotice("同じ名前の別のアプリが入っているので入れられません", PICO_RED);
        return;
    }
    if(st == AppStore::Installed::KeyChanged){
        this->setNotice("作者の鍵が変わりました。削除してから入れ直してください", PICO_RED);
        return;
    }
    if(st == AppStore::Installed::Newer){
        this->setNotice("入っている版より古い版は入れられません", PICO_RED);
        return;
    }
    if(this->store->needsNewerOs(i)){
        FixedString<PICO_STR_LL> m;
        m.appendFormat("本体の更新が要ります(#%lu以降)", (unsigned long)e->min_os);
        this->setNotice(m.c_str(), PICO_RED);
        return;
    }
    if(!this->store->installable(i)){
        this->setNotice("取り下げられたアプリなので入れられません", PICO_RED);
        return;
    }
    if(!NetworkFunctions::IsConnected()){
        this->setNotice("Wi-Fiに繋がっていません", PICO_RED);
        return;
    }
    FixedString<PICO_STR_L> perms;
    AppRepo::PermsText(e->perms, perms);
    FixedString<PICO_STR_512B> text;
    text.appendFormat("%s %s を入れますか?\n作者: %s\n使う権限: %s", e->name.c_str(), e->version.c_str(),
                      e->author.c_str(), perms.c_str());
    if(st == AppStore::Installed::Update && this->store->permsGrow(i)){
        text.append("\n※この更新で使う権限が増えます");
    }
    if(st == AppStore::Installed::Foreign){
        text.appendFormat("\n\n/lua/apps/%s に同じ名前のアプリがあります。上書きします(アプリが書いたファイルは残ります)", e->dir.c_str());
    }
    this->confirm_id.assign(e->id.c_str());
    auto* dialog = new MsgDialog(text.c_str(), "やめる",
                                 st == AppStore::Installed::Update ? "更新" : "入れる");
    dialog->setIconId(IconID::AppStore);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool ok){
        if(ok && this->store && this->selected_id == this->confirm_id.c_str()){
            const int idx = this->selectedIndex();
            const bool replace = this->store->installed(idx) == AppStore::Installed::Foreign;
            this->notice.clear();
            if(this->store->busy()) this->setNotice("一覧の取得が終わってから入れてください", PICO_RED);
            else this->store->beginInstall(idx, replace);
            this->refreshStatus();
            this->refreshButtons();
        }
        this->confirm_id.clear();
        WidgetFunctions::DestroyLater(dialog);
    });
}

void StoreScene::confirmRemove(){
    const int i = this->selectedIndex();
    if(i < 0 || !this->store || this->store->busy()) return;
    const AppRepo::Entry* e = this->store->entry(i);
    FixedString<PICO_STR_512B> text;
    text.appendFormat("%s を削除しますか?\n/lua/apps/%s を丸ごと消します(セーブデータ等も消えます)",
                      e->name.c_str(), e->dir.c_str());
    this->confirm_id.assign(e->id.c_str());
    auto* dialog = new MsgDialog(text.c_str(), "やめる", "削除");
    dialog->setIconId(IconID::Trash);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool ok){
        if(ok && this->store && this->selected_id == this->confirm_id.c_str()){
            this->notice.clear();
            this->store->remove(this->selectedIndex());
        }
        this->confirm_id.clear();
        WidgetFunctions::DestroyLater(dialog);
    });
}

void StoreScene::confirmUpdateAll(){
    if(!this->store || this->store->busy()) return;
    const int n = this->store->batchCount();
    if(n <= 0) return;
    if(!NetworkFunctions::IsConnected()){
        this->setNotice("Wi-Fiに繋がっていません", PICO_RED);
        return;
    }
    //名前を並べる(長ければ切れる。ダイアログの中で折り返す)
    FixedString<PICO_STR_512B> text;
    text.appendFormat("%d件のアプリを更新しますか?\n", n);
    int listed = 0;
    for(int i = 0; i < this->store->count(); i++){
        if(!this->store->batchUpdatable(i)) continue;
        const AppRepo::Entry* e = this->store->entry(i);
        if(listed++ > 0) text.append("、");
        text.appendFormat("%s %s", e->name.c_str(), e->version.c_str());
    }
    text.append("\n(権限が増える更新は1つずつ入れてください)");
    auto* dialog = new MsgDialog(text.c_str(), "やめる", "更新");
    dialog->setIconId(IconID::Refresh);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool ok){
        if(ok && this->store){
            this->notice.clear();
            if(!this->store->beginUpdateAll()) this->setNotice("今は更新できません", PICO_RED);
            this->refreshList();
            this->refreshStatus();
            this->refreshButtons();
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void StoreScene::chooseRollbackOrRemove(){
    const int i = this->selectedIndex();
    if(i < 0 || !this->store || this->store->busy()) return;
    FixedString<PICO_STR_S> prev;
    if(!this->store->previousVersion(i, prev)){
        this->confirmRemove();
        return;
    }
    const AppRepo::Entry* e = this->store->entry(i);
    FixedString<PICO_STR_L> title;
    title.appendFormat("%s をどうしますか?", e->name.c_str());
    FixedString<PICO_STR_M> back;
    back.appendFormat("%s に戻す", prev.c_str()); //一覧の幅に収まるよう短く(題と欄で前の版だと分かる)
    this->confirm_id.assign(e->id.c_str());
    auto* dialog = new PickerDialog(PickerDialog::Mode::Choice, title.c_str(), "", "やめる");
    dialog->addChoice(back.c_str());
    dialog->addChoice("削除する");
    dialog->layout();
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool ok){
        const int choice = dialog->getSelectedIndex();
        const bool same = this->store && this->selected_id == this->confirm_id.c_str();
        this->confirm_id.clear();
        WidgetFunctions::DestroyLater(dialog);
        if(!ok || !same) return;
        if(choice == 0) this->confirmRollback();
        else if(choice == 1) this->confirmRemove();
    });
}

void StoreScene::confirmRollback(){
    const int i = this->selectedIndex();
    FixedString<PICO_STR_S> prev;
    if(i < 0 || !this->store || this->store->busy() || !this->store->previousVersion(i, prev)) return;
    const AppRepo::Entry* e = this->store->entry(i);
    FixedString<PICO_STR_512B> text;
    text.appendFormat("%s を %s から %s へ戻しますか?\n"
                      "アプリが書いたファイル(セーブ等)はそのまま残ります。%s は、[更新] を押すまで更新の知らせに出しません",
                      e->name.c_str(), this->store->installedVersion(i), prev.c_str(), this->store->installedVersion(i));
    this->confirm_id.assign(e->id.c_str());
    auto* dialog = new MsgDialog(text.c_str(), "やめる", "戻す");
    dialog->setIconId(IconID::Refresh);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool ok){
        if(ok && this->store && this->selected_id == this->confirm_id.c_str()){
            this->notice.clear();
            this->store->rollback(this->selectedIndex());
        }
        this->confirm_id.clear();
        WidgetFunctions::DestroyLater(dialog);
    });
}

void StoreScene::openSettings(){
    if(!this->store || this->store->busy()) return;
    auto* dialog = new InputDialog("配布元のURL:", true);
    dialog->setAsciiInput(true); //英数字の欄(キーボードを英字で開く)
    dialog->setInput(this->store->baseUrl());
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool ok){
        if(ok && this->store){
            const FixedString<PICO_STR_LL> url = dialog->getInput();
            if(this->store->setBaseUrl(url.c_str())){
                PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_STORE_CFG, "url", this->store->baseUrl());
                //別の取得元の一覧は使わない(loadCachedCatalog()がURLを見て断る)
                AppStore* fresh = new (std::nothrow) AppStore();
                if(fresh){
                    this->store->close();
                    delete this->store;
                    this->store = fresh;
                    this->store->loadConfig();
                    this->store->loadCachedCatalog();
                    this->selected_id.clear();
                    this->seen_revision = this->store->revision() - 1;
                }
                this->setNotice("配布元を変えました。[更新] で取得します");
            }else{
                this->setNotice("URLを読めません(http:// か https:// で始めてください)", PICO_RED);
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void StoreScene::onUpdate(){
    if(!this->store) return;
    this->frames_since_enter++;
    if(this->auto_refresh && this->frames_since_enter >= 2){
        this->auto_refresh = false;
        this->startRefresh();
    }
    if(this->store->busy()){
        PowerFunctions::KeepAwake();
        this->store->update();
    }

    if(this->pending_launch){
        this->pending_launch = false;
        const int i = this->selectedIndex();
        if(i >= 0 && !this->store->launch(i)){
            this->setNotice("ランチャに登録されていません(再起動すると並びます)", PICO_RED);
        }
        return;
    }

    if(this->store->revision() != this->seen_revision){
        this->seen_revision = this->store->revision();
        this->refreshList();
        this->refreshDetail();
    }
    const AppStore::State st = this->store->state();
    if(st != this->seen_state){
        this->seen_state = st;
        this->refreshButtons();
    }
    if(this->progress){
        const bool show = this->store->busy() && this->store->progressTotal() > 0;
        if(show){
            this->progress->setMaxValue((float)this->store->progressTotal());
            this->progress->setValue((float)this->store->progressDone());
        }
        if(this->progress->getVisible() != show) this->progress->setVisible(show);
    }
    this->refreshStatus();
}
