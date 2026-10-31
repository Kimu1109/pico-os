#include "gui/scenes/BackgroundScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "system/Background_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "Arduino.h"
#include "system/I18n_Functions.hpp"

#include <cstring>

namespace {
    constexpr uint32_t kRefreshMs = 500;

    // 一覧の1行(名前 + 状態の一言)
    void RowText(BackgroundService* s, FixedString<PICO_PATH_LEN>& out){
        out.assign(s->name());
        if(s->toggleable()) out.append(s->enabled() ? TR("  [入]") : TR("  [切]"));
        else if(s->active()) out.append(TR("  [動作中]"));
    }
}

void BackgroundScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, y0, TR("戻る"));
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);
    const Rect back = this->back_button->getLocalRect();

    this->title_label = new Label<PICO_STR_M>(back.x + back.w + MARGIN, y0, TR("バックグラウンド"));
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setY(y0 + (back.h - FontFn::GetFontSize(FontFn::Small)) / 2);
    this->title_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->title_label);

    // 下の段: ボタン3つを等分に
    constexpr int kActionH = 24;
    const int action_y = content.y + content.h - MARGIN - (kActionH + Button::kFrameExtra);
    const int third = (content.w - MARGIN * 4) / 3;
    auto makeAction = [&](int i, const char* text) -> Button* {
        Button* b = new Button(content.x + MARGIN + i * (third + MARGIN), action_y, text);
        b->setFontSize(FontFn::Small);
        b->setH(kActionH + Button::kFrameExtra);
        b->setW(third);
        WidgetFunctions::Add(b);
        return b;
    };
    this->toggle_button = makeAction(0, TR("停止"));
    this->toggle_button->setOnPressEnd([this](){ this->onToggle(); });
    this->run_button = makeAction(1, TR("今すぐ"));
    this->run_button->setOnPressEnd([this](){ this->onRunNow(); });
    this->open_button = makeAction(2, TR("開く"));
    this->open_button->setOnPressEnd([this](){ this->onOpen(); });

    const int line_h = Label<PICO_STR_512B>::GetLineHeight(FontFn::Small);
    const int detail_y = action_y - MARGIN - line_h * kDetailLines;
    this->detail_label = new Label<PICO_STR_512B>(content.x + MARGIN, detail_y, "");
    this->detail_label->setFontSize(FontFn::Small);
    this->detail_label->setMaxWidth(content.w - MARGIN * 2);
    this->detail_label->setMaxHeight(line_h * kDetailLines);
    this->detail_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->detail_label);

    const int list_y = y0 + back.h + MARGIN;
    this->list = new ScrollList(content.x + MARGIN, list_y, content.w - MARGIN * 2,
                                detail_y - MARGIN - list_y, BackgroundFunctions::kMaxServices);
    this->list->setFontSize(FontFn::Small);
    this->list->setEnableIcon(true);
    this->list->setOnSelectItem([this](int, bool){ this->refreshTexts(true); });
    WidgetFunctions::Add(this->list);

    this->last_selected = -2;
    this->rebuildList();
    if(BackgroundFunctions::Count() > 0) this->list->setSelectedIndex(0);
    this->refreshTexts(true);
}

void BackgroundScene::onExit(){
    this->back_button = nullptr;
    this->toggle_button = nullptr;
    this->run_button = nullptr;
    this->open_button = nullptr;
    this->title_label = nullptr;
    this->detail_label = nullptr;
    this->list = nullptr;
}

void BackgroundScene::onUpdate(){
    if(BackgroundFunctions::Revision() != this->seen_revision){
        this->rebuildList();
        this->refreshTexts(true);
        return;
    }
    const bool sel_changed = this->selected() != this->last_selected;
    if(sel_changed || millis() - this->last_refresh_ms >= kRefreshMs) this->refreshTexts(sel_changed);
}

int BackgroundScene::selected() const {
    if(!this->list) return -1;
    const int i = this->list->getSelectedIndex();
    return (i >= 0 && i < BackgroundFunctions::Count()) ? i : -1;
}

void BackgroundScene::rebuildList(){
    this->seen_revision = BackgroundFunctions::Revision();
    const int keep = this->list->getSelectedIndex();
    this->list->clear();
    for(int i = 0; i < BackgroundFunctions::Count(); i++){
        BackgroundService* s = BackgroundFunctions::At(i);
        ScrollListTools::Item item;
        item.icon = s->icon();
        RowText(s, item.text);
        item.color = s->active() ? (int8_t)PICO_BLUE : (int8_t)-1;
        this->list->add(item);
    }
    if(keep >= 0 && keep < BackgroundFunctions::Count()) this->list->setSelectedIndex(keep);
    this->list->needsRender();
}

void BackgroundScene::refreshTexts(bool force){
    this->last_refresh_ms = millis();
    this->last_selected = this->selected();

    //一覧の行(状態の一言と色)は変わったものだけ書き直す
    bool list_changed = false;
    for(int i = 0; i < BackgroundFunctions::Count(); i++){
        ScrollListTools::Item* it = this->list->itemAt(i);
        if(!it) break;
        BackgroundService* s = BackgroundFunctions::At(i);
        FixedString<PICO_PATH_LEN> text;
        RowText(s, text);
        const int8_t color = s->active() ? (int8_t)PICO_BLUE : (int8_t)-1;
        if(!(it->text == text.c_str()) || it->color != color){
            it->text = text;
            it->color = color;
            list_changed = true;
        }
    }
    if(list_changed || force) this->list->needsRender();

    const int i = this->selected();
    if(i < 0){
        this->detail_label->setTextColor(PICO_DARKGREY);
        this->detail_label->setText(BackgroundFunctions::Count() == 0
            ? TR("バックグラウンドで動くものはありません")
            : TR("一覧から選んでください"));
    }else{
        BackgroundService* s = BackgroundFunctions::At(i);
        FixedString<PICO_STR_LL> st;
        s->status(st);
        FixedString<PICO_STR_512B> text;
        text.appendFormat("%s\n%s", s->name(), st.c_str());
        this->detail_label->setTextColor(PICO_BLACK);
        this->detail_label->setText(text.c_str());
    }
    this->refreshButtons();
}

void BackgroundScene::refreshButtons(){
    const int i = this->selected();
    BackgroundService* s = i >= 0 ? BackgroundFunctions::At(i) : nullptr;
    if(!s){
        this->toggle_button->setVisible(false);
        this->run_button->setVisible(false);
        this->open_button->setVisible(false);
        return;
    }
    if(s->toggleable()){
        this->toggle_button->setText(s->enabled() ? TR("切る") : TR("入れる"));
        this->toggle_button->setVisible(true);
    }else{
        this->toggle_button->setText(TR("停止"));
        this->toggle_button->setVisible(s->active());
    }
    this->run_button->setVisible(s->toggleable() && s->enabled());
    const char* app = s->appName();
    this->open_button->setVisible(app && *app);
}

void BackgroundScene::onToggle(){
    const int i = this->selected();
    BackgroundService* s = i >= 0 ? BackgroundFunctions::At(i) : nullptr;
    if(!s) return;
    if(s->toggleable()) BackgroundFunctions::SetEnabled(s, !s->enabled());
    else s->stop();
    this->refreshTexts(true);
}

void BackgroundScene::onRunNow(){
    const int i = this->selected();
    BackgroundService* s = i >= 0 ? BackgroundFunctions::At(i) : nullptr;
    if(s) s->runNow();
    this->refreshTexts(true);
}

void BackgroundScene::onOpen(){
    const int i = this->selected();
    BackgroundService* s = i >= 0 ? BackgroundFunctions::At(i) : nullptr;
    if(!s || !s->appName()) return;
    AppFunctions::LaunchByName(s->appName());
}
