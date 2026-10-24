#include "gui/scenes/WifiSetupScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "services/WifiSetup_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "Arduino.h"

#include <cstdio>

void WifiSetupScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;
    const int w = content.w - MARGIN * 2;

    Button* back = new Button(content.x + MARGIN, y0, "戻る");
    back->setIcon(IconID::ArrowLeft, IconSize::Px16);
    back->setFontSize(FontFn::Small);
    back->setH(20 + Button::kFrameExtra);
    back->setOnPressEnd([](){ SceneFunctions::Pop(); }); // APを止めるのは onExit()
    WidgetFunctions::Add(back);
    const Rect back_box = back->getLocalRect();

    auto* title = new Label<PICO_STR_M>(back_box.x + back_box.w + MARGIN, y0, "スマホから設定");
    title->setFontSize(FontFn::Small);
    title->setY(y0 + (back_box.h - FontFn::GetFontSize(FontFn::Small)) / 2);
    title->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(title);

    FixedString<PICO_STR_LL> err;
    this->failed = !WifiSetupFunctions::Start(err);
    this->saved_at = 0;

    const int small_h = Label<PICO_STR_512B>::GetLineHeight(FontFn::Small);
    int y = y0 + back_box.h + MARGIN * 2;

    auto addText = [&](const char* text, int lines) -> Label<PICO_STR_512B>* {
        auto* l = new Label<PICO_STR_512B>(content.x + MARGIN, y, text);
        l->setFontSize(FontFn::Small);
        l->setMaxWidth(w);
        l->setMaxHeight(small_h * lines);
        l->setDisableAutoTextDecoration(true);
        WidgetFunctions::Add(l);
        y += small_h * lines + MARGIN;
        return l;
    };

    if(this->failed){
        char msg[PICO_STR_256B];
        snprintf(msg, sizeof(msg), "始められませんでした: %s", err.c_str());
        this->guide_label = addText(msg, 4);
        return;
    }

    addText("1. スマホのWi-Fiで次へ接続", 1);
    // APの名前とパスワードは打ち写すので大きめの字で
    const int big_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Normal);
    this->ssid_label = new Label<PICO_STR_M>(content.x + MARGIN * 3, y, WifiSetupFunctions::ApSsid());
    this->ssid_label->setFontSize(FontFn::Normal);
    this->ssid_label->setMaxWidth(w - MARGIN * 2);
    this->ssid_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->ssid_label);
    y += big_h;
    char pass[PICO_STR_S];
    snprintf(pass, sizeof(pass), "%s", WifiSetupFunctions::ApPassword());
    this->pass_label = new Label<PICO_STR_S>(content.x + MARGIN * 3, y, pass);
    this->pass_label->setFontSize(FontFn::Normal);
    this->pass_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->pass_label);
    y += big_h + MARGIN;

    addText("2. ページが自動で開きます。開かなければ次のURLを開く:", 2);
    // URLは折り返さずに1行で見せる(途中で切れると打ち写せない)
    char url[PICO_STR_M];
    snprintf(url, sizeof(url), "   %s", WifiSetupFunctions::PageUrl());
    addText(url, 1);
    addText("3. 本体を接続したいWi-Fiの名前とパスワードを入れて送る", 2);

    this->status_label = new Label<PICO_STR_LL>(content.x + MARGIN, y + MARGIN, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(w);
    this->status_label->setMaxHeight(small_h * 2);
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);
    this->shown_clients = -1;
    this->shown_state = 0xFF;
    this->refreshStatus();
}

void WifiSetupScene::refreshStatus(){
    if(!this->status_label) return;
    const auto st = WifiSetupFunctions::GetState();
    const int clients = WifiSetupFunctions::Clients();
    if((uint8_t)st == this->shown_state && clients == this->shown_clients) return;
    this->shown_state = (uint8_t)st;
    this->shown_clients = clients;
    char buf[PICO_STR_LL];
    if(st == WifiSetupFunctions::State::Saved){
        snprintf(buf, sizeof(buf), "「%s」を受け取りました。接続します", WifiSetupFunctions::SavedSsid());
        this->status_label->setTextColor(PICO_DARKGREEN);
    }else if(clients > 0){
        snprintf(buf, sizeof(buf), "待っています(接続している端末: %d台)", clients);
    }else{
        snprintf(buf, sizeof(buf), "待っています");
    }
    this->status_label->setText(buf);
}

void WifiSetupScene::onUpdate(){
    if(this->failed) return;
    PowerFunctions::KeepAwake(); // APの間は眠らない(スマホからの要求を取りこぼさない)
    WifiSetupFunctions::Update();
    this->refreshStatus();
    if(WifiSetupFunctions::GetState() == WifiSetupFunctions::State::Saved){
        if(this->saved_at == 0) this->saved_at = millis() | 1u;
        else if(millis() - this->saved_at >= kCloseAfterSavedMs) SceneFunctions::Pop();
    }
}

void WifiSetupScene::onExit(){
    WifiSetupFunctions::Stop(); // 受け取っていればそのネットワークへ、でなければ自動接続へ戻る
    this->guide_label = nullptr;
    this->ssid_label = nullptr;
    this->pass_label = nullptr;
    this->status_label = nullptr;
}
