#include "gui/scenes/ClocksScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Time_Functions.hpp"

static const char* const WDAY_JP[7] = { "日", "月", "火", "水", "木", "金", "土" };

Rect ClocksScene::bodyRect() const {
    const Rect content = Scene::contentRect();

    const int16_t top = content.y + MARGIN + this->top_row_h + MARGIN;

    return {
        content.x,
        top,
        content.w,
        (int16_t)(content.y + content.h - BOTTOM_RESERVED - top)
    };
}

void ClocksScene::applyMode(){
    const bool is_digital = (this->mode == ClockMode::Digital);

    if(this->date_label)   this->date_label->setVisible(is_digital);
    if(this->time_label)   this->time_label->setVisible(is_digital);
    if(this->analog_clock) this->analog_clock->setVisible(!is_digital);

    //隠れている間は時刻を流し込んでいないので、表示へ戻した側を今の時刻で埋め直す
    this->before_sec  = -1;
    this->before_mday = -1;
    this->refresh_time();
}

void ClocksScene::refresh_time(){
    const struct tm& t = TimeFunctions::timeinfo;

    //見えていない側は更新しない。needsRender()は表示状態を見ないので、
    //隠れたウィジェットを更新すると無駄なdirty矩形が毎秒積まれる
    if(this->mode == ClockMode::Analog){
        if(this->analog_clock) this->analog_clock->setTime(t.tm_hour, t.tm_min, t.tm_sec);
        return;
    }

    if(t.tm_sec != this->before_sec){
        char buf[PICO_STR_M];
        this->before_sec = t.tm_sec;
        strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        this->time_label->setText(buf);
    }

    if(t.tm_mday != this->before_mday){
        char buf[PICO_STR_M];
        this->before_mday = t.tm_mday;
        const int wday = (t.tm_wday >= 0 && t.tm_wday < 7) ? t.tm_wday : 0;
        snprintf(buf, sizeof(buf), "%d月%d日(%s)",
                 t.tm_mon + 1, t.tm_mday, WDAY_JP[wday]);
        this->date_label->setText(buf);
    }
}

void ClocksScene::onEnter(){
    const Rect content = Scene::contentRect();

    // ---- 上部の行: [戻る][デジタル|アナログ] ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([](){
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->back_button);

    //Buttonの箱は文字の余白と立体ぶんが足された大きさになるので、実測して高さを揃える
    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    const int tab_x = back_box.x + back_box.w + MARGIN;

    this->mode_tab = new TabBar(
        tab_x, content.y + MARGIN,
        content.x + content.w - MARGIN - tab_x, this->top_row_h
    );
    this->mode_tab->addTab("デジタル");
    this->mode_tab->addTab("アナログ");
    this->mode_tab->setSelected((int)this->mode);
    this->mode_tab->setOnChanged([this](int index){
        this->mode = (ClockMode)index;
        this->applyMode();
    });
    WidgetFunctions::Add(this->mode_tab);

    const Rect body = this->bodyRect();

    // ---- デジタル表示 ----
    const int date_time_height = 24 + MARGIN + 48;

    this->date_label = new Label<PICO_STR_M>(
        body.x, body.y + (body.h - date_time_height) / 2,
        "1月1日(月)"
    );
    this->date_label->setMaxWidth(body.w);
    this->date_label->setFontSize(FontFn::Normal);
    this->date_label->setTextAlign(TextAlign::Center);
    WidgetFunctions::Add(this->date_label);

    this->time_label = new Label<PICO_STR_M>(
        body.x, this->date_label->getScreenY() + this->date_label->getH() + MARGIN,
        "00:00:00"
    );
    this->time_label->setMaxWidth(body.w);
    this->time_label->setFontSize(FontFn::Bigger);
    this->time_label->setTextAlign(TextAlign::Center);
    WidgetFunctions::Add(this->time_label);

    // ---- アナログ表示 ----
    //本体領域の短辺いっぱいの正方形を中央に置く
    const int diameter = ((body.w < body.h) ? body.w : body.h) - MARGIN * 2;

    this->analog_clock = new AnalogClock(
        body.x + (body.w - diameter) / 2,
        body.y + (body.h - diameter) / 2,
        diameter
    );
    WidgetFunctions::Add(this->analog_clock);

    //どちらか一方だけを表示する。ウィジェットは両方作っておき、
    //切り替えは可視/不可視だけで済ませる(切り替えのたびにnew/deleteしない)
    this->applyMode();
}

void ClocksScene::onUpdate(){
    this->refresh_time();
}

void ClocksScene::onExit(){
    this->back_button = nullptr;
    this->mode_tab    = nullptr;

    this->date_label = nullptr;
    this->time_label = nullptr;

    this->analog_clock = nullptr;
}
