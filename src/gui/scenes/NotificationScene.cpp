#include "gui/scenes/NotificationScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "functions/Notification_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <ctime>

namespace {
    using namespace NotificationFunctions;

    static_assert(kMaxRules <= 16 && kMaxHistory <= 16, "NotificationScene::kMaxRowsを広げること");

    void FormatTime(int64_t epoch, char* out, size_t cap){
        if(epoch <= 0){ snprintf(out, cap, "--:--"); return; }
        const time_t t = (time_t)epoch;
        struct tm tm_ = {};
        localtime_r(&t, &tm_);
        snprintf(out, cap, "%d/%d %02d:%02d", tm_.tm_mon + 1, tm_.tm_mday, tm_.tm_hour, tm_.tm_min);
    }

    // 予約の「いつ」を短い日本語で
    void DescribeWhen(const Rule& r, unsigned long now_ms, char* out, size_t cap){
        const When& w = r.when;
        switch(w.trigger){
            case Trigger::Delay: {
                const long left = (long)(r.due_ms - now_ms);
                const long sec = left > 0 ? (left + 999) / 1000 : 0;
                if(sec >= 3600) snprintf(out, cap, "あと%ld時間%ld分", sec / 3600, (sec % 3600) / 60);
                else if(sec >= 60) snprintf(out, cap, "あと%ld分%ld秒", sec / 60, sec % 60);
                else snprintf(out, cap, "あと%ld秒", sec);
                break;
            }
            case Trigger::At: {
                char t[24];
                FormatTime(w.at_epoch, t, sizeof(t));
                snprintf(out, cap, "%s", t);
                break;
            }
            case Trigger::Daily: snprintf(out, cap, "毎日%02u:%02u", w.hour, w.minute); break;
            case Trigger::Every: {
                const unsigned long sec = w.every_ms / 1000;
                if(sec >= 3600 && sec % 3600 == 0) snprintf(out, cap, "%lu時間ごと", sec / 3600);
                else if(sec >= 60 && sec % 60 == 0) snprintf(out, cap, "%lu分ごと", sec / 60);
                else snprintf(out, cap, "%lu秒ごと", sec);
                break;
            }
            case Trigger::BatteryLow: snprintf(out, cap, "電池%u%%未満", w.below); break;
            case Trigger::WifiConnected: snprintf(out, cap, "Wi-Fi接続時"); break;
            case Trigger::WifiDisconnected: snprintf(out, cap, "Wi-Fi切断時"); break;
            case Trigger::Now: snprintf(out, cap, "-"); break;
        }
    }
}

void NotificationScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    // ---- 上の行 ----
    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back = this->back_button->getLocalRect();
    const int row_h = back.h;

    auto* title_label = new Label<PICO_STR_M>(back.x + back.w + MARGIN * 2, y0, "通知");
    title_label->setFontSize(FontFn::Small);
    title_label->setY(y0 + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(title_label);

    this->clear_button = new Button(0, y0, "全消去");
    this->clear_button->setFontSize(FontFn::Small);
    this->clear_button->setH(20 + Button::kFrameExtra);
    this->clear_button->setX(content.x + content.w - MARGIN - this->clear_button->getLocalRect().w);
    this->clear_button->setOnPressEnd([this](){
        ClearHistory();
        this->selected_key = 0;
    });
    WidgetFunctions::Add(this->clear_button);

    this->read_button = new Button(0, y0, "既読");
    this->read_button->setFontSize(FontFn::Small);
    this->read_button->setH(20 + Button::kFrameExtra);
    this->read_button->setX(this->clear_button->getLocalRect().x - MARGIN - this->read_button->getLocalRect().w);
    this->read_button->setOnPressEnd([](){ MarkAllRead(); });
    WidgetFunctions::Add(this->read_button);

    // ---- タブ ----
    const int tab_y = y0 + row_h + MARGIN;
    constexpr int kTabH = 22;
    this->tab = new TabBar(content.x + MARGIN, tab_y, content.w - MARGIN * 2, kTabH);
    this->tab->setFontSize(FontFn::Small);
    this->tab->addTab("履歴");
    this->tab->addTab("予約");
    this->tab->setSelected((int)this->current_tab);
    this->tab->setOnChanged([this](int index){
        this->current_tab = (Tab)index;
        this->selected_key = 0;
        this->rebuild();
    });
    WidgetFunctions::Add(this->tab);

    // ---- 下の行: [表示] [音] … [開く] ----
    const int bottom_y = content.y + content.h - MARGIN - row_h - 4;
    this->mode_button = new Button(content.x + MARGIN, bottom_y, "控えめ");
    this->mode_button->setFontSize(FontFn::Small);
    this->mode_button->setH(20 + Button::kFrameExtra);
    this->mode_button->setW(this->mode_button->getLocalRect().w + Button::kFrameExtra);
    this->mode_button->setOnPressEnd([this](){
        SetMode(GetMode() == Mode::On ? Mode::Quiet : Mode::On);
        this->refreshSettingButtons();
    });
    WidgetFunctions::Add(this->mode_button);

    const Rect mode_box = this->mode_button->getLocalRect();
    this->sound_button = new Button(mode_box.x + mode_box.w + MARGIN, bottom_y, "音なし");
    this->sound_button->setFontSize(FontFn::Small);
    this->sound_button->setH(20 + Button::kFrameExtra);
    this->sound_button->setW(this->sound_button->getLocalRect().w + Button::kFrameExtra);
    this->sound_button->setOnPressEnd([this](){
        SetSoundEnabled(!GetSoundEnabled());
        this->refreshSettingButtons();
    });
    WidgetFunctions::Add(this->sound_button);

    this->action_button = new Button(0, bottom_y, "取り消す");
    this->action_button->setFontSize(FontFn::Small);
    this->action_button->setH(20 + Button::kFrameExtra);
    const int action_w = this->action_button->getLocalRect().w;
    this->action_button->setW(action_w); //全体の幅のまま固定する(右端が余白からはみ出さない)
    this->action_button->setX(content.x + content.w - MARGIN - action_w);
    this->action_button->setOnPressEnd([this](){ this->doAction(); });
    WidgetFunctions::Add(this->action_button);

    // ---- 詳しく(3行) ----
    const int line_h = FontFn::GetFontSize(FontFn::Small);
    const int detail_h = line_h * 3 + 4;
    const int detail_y = bottom_y - MARGIN - detail_h;
    this->detail = new Label<PICO_STR_256B>(content.x + MARGIN, detail_y, "");
    this->detail->setFontSize(FontFn::Small);
    this->detail->setMaxWidth(content.w - MARGIN * 2);
    //アプリが付けたタイトルの _ や * をマークアップとして消さない
    this->detail->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->detail);

    // ---- 一覧 ----
    const int list_y = tab_y + kTabH + MARGIN;
    this->list = new ScrollList(content.x + MARGIN, list_y,
                                content.w - MARGIN * 2, detail_y - MARGIN - list_y, kMaxRows);
    this->list->setFontSize(FontFn::Small);
    this->list->setEnableIcon(true);
    this->list->setOnSelectItem([this](int index, bool already_selected){
        this->selected_key = this->keyAt(index);
        if(already_selected) this->doAction();
        else this->refreshDetail();
    });
    WidgetFunctions::Add(this->list);

    this->refreshSettingButtons();
    this->rebuild();
}

void NotificationScene::onExit(){
    //ここで見たので既読にする(ステータスバーの印を消す)
    MarkAllRead();

    this->back_button = nullptr;
    this->read_button = nullptr;
    this->clear_button = nullptr;
    this->mode_button = nullptr;
    this->sound_button = nullptr;
    this->action_button = nullptr;
    this->tab = nullptr;
    this->list = nullptr;
    this->detail = nullptr;
}

void NotificationScene::onUpdate(){
    if(Revision() != this->last_revision){
        this->rebuild();
        return;
    }
    //「あと○秒」を動かす(予約のタブで何か選んでいるときだけ、1秒ごと)
    if(this->current_tab == Tab::Rules && this->selected_key != 0 && millis() - this->last_detail_ms >= 1000){
        this->last_detail_ms = millis();
        this->refreshDetail();
    }
}

uint32_t NotificationScene::keyAt(int index) const {
    if(index < 0 || index >= this->key_count) return 0;
    return this->keys[index];
}

void NotificationScene::rebuild(){
    this->last_revision = Revision();
    if(!this->list) return;

    this->list->clear();
    this->key_count = 0;
    int select_index = -1;

    if(this->current_tab == Tab::History){
        const int n = HistoryCount();
        for(int i = 0; i < n && this->key_count < kMaxRows; i++){
            const Entry* e = HistoryAt(i);
            if(!e) continue;
            ScrollListTools::Item item;
            item.icon = IconID::Bell;
            char t[24];
            FormatTime(e->epoch, t, sizeof(t));
            //時刻は「月/日」を省いて短く(詳しくは下の欄)
            const char* hm = strchr(t, ' ');
            item.text.appendFormat("%s %s", hm ? hm + 1 : t, e->content.title.c_str());
            if(!e->read) item.color = PICO_BLUE;
            this->list->add(item);
            if(e->seq == this->selected_key) select_index = this->key_count;
            this->keys[this->key_count++] = e->seq;
        }
    }else{
        const int n = RuleCount();
        const unsigned long now = millis();
        for(int i = 0; i < n && this->key_count < kMaxRows; i++){
            const Rule* r = RuleAt(i);
            if(!r) continue;
            ScrollListTools::Item item;
            item.icon = IconID::Clock;
            char when[32];
            DescribeWhen(*r, now, when, sizeof(when));
            item.text.appendFormat("%s %s", when, r->content.title.c_str());
            this->list->add(item);
            if(r->id == this->selected_key) select_index = this->key_count;
            this->keys[this->key_count++] = r->id;
        }
    }

    if(select_index >= 0) this->list->setSelectedIndex(select_index);
    else this->selected_key = 0;
    this->list->needsRender();

    this->action_button->setText(this->current_tab == Tab::History ? "開く" : "取り消す");
    this->refreshDetail();
}

void NotificationScene::refreshDetail(){
    if(!this->detail) return;
    FixedString<PICO_STR_256B> text;

    if(this->selected_key == 0){
        if(this->key_count == 0){
            text.assign(this->current_tab == Tab::History ? "通知はありません" : "予約はありません");
        }else{
            text.assign(this->current_tab == Tab::History ? "2回タップで開く" : "2回タップで取り消す");
        }
    }else if(this->current_tab == Tab::History){
        const Entry* e = FindEntry(this->selected_key);
        if(e){
            char t[24];
            FormatTime(e->epoch, t, sizeof(t));
            text.appendFormat("%s  %s\n%s\n%s", t,
                e->content.app.empty() ? "システム" : e->content.app.c_str(),
                e->content.title.c_str(), e->content.body.c_str());
        }
    }else{
        const Rule* r = FindRule((uint16_t)this->selected_key);
        if(r){
            char when[32];
            DescribeWhen(*r, millis(), when, sizeof(when));
            text.appendFormat("%s  %s\n%s\n%s", when,
                r->content.app.empty() ? "システム" : r->content.app.c_str(),
                r->content.title.c_str(), r->content.body.c_str());
        }
    }
    this->detail->setText(text.c_str());
}

void NotificationScene::refreshSettingButtons(){
    //箱の幅は作ったときの文字(長いほう)で固定してあるので、並びはずれない
    if(this->mode_button) this->mode_button->setText(GetMode() == Mode::On ? "通常" : "控えめ");
    if(this->sound_button) this->sound_button->setText(GetSoundEnabled() ? "音あり" : "音なし");
}

void NotificationScene::doAction(){
    if(this->selected_key == 0) return;
    if(this->current_tab == Tab::History){
        const uint32_t seq = this->selected_key;
        //送ったアプリを開く(Push()なので戻るとここへ帰ってくる)。開けなければ既読にするだけ
        Open(seq);
    }else{
        CancelById((uint16_t)this->selected_key);
        this->selected_key = 0;
    }
}
