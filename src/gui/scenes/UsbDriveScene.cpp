#include "gui/scenes/UsbDriveScene.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "services/Power_Functions.hpp"

#include <Arduino.h>
#include <cstdio>

void UsbDriveScene::onEnter(){
    const Rect content = Scene::contentRect();
    int y = content.y + MARGIN;

    auto* title = new Label<PICO_STR_L>(content.x + MARGIN, y, "USBドライブ");
    title->setFontSize(FontFn::Normal);
    WidgetFunctions::Add(title);
    y += 32;

    this->state_label = new Label<PICO_STR_L>(content.x + MARGIN, y, "準備しています…");
    this->state_label->setFontSize(FontFn::Small);
    this->state_label->setMaxWidth(content.w - MARGIN * 2);
    this->state_label->setMaxHeight(36);
    WidgetFunctions::Add(this->state_label);
    y += 40;

    this->count_label = new Label<PICO_STR_L>(content.x + MARGIN, y, "");
    this->count_label->setFontSize(FontFn::Small);
    this->count_label->setMaxWidth(content.w - MARGIN * 2);
    this->count_label->setMaxHeight(36);
    this->count_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->count_label);
    y += 40;

    this->diag_label = new Label<PICO_STR_LL>(content.x + MARGIN, y, "");
    this->diag_label->setFontSize(FontFn::Small);
    this->diag_label->setMaxWidth(content.w - MARGIN * 2);
    this->diag_label->setMaxHeight(36);
    this->diag_label->setTextColor(PICO_NAVY);
    this->diag_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->diag_label);
    y += 40;

    this->help_label = new Label<PICO_STR_LL>(content.x + MARGIN, y,
        "PCで「取り出し」をしてから終了してください。その間pico-osはSDを使えません");
    this->help_label->setFontSize(FontFn::Small);
    this->help_label->setMaxWidth(content.w - MARGIN * 2);
    this->help_label->setMaxHeight(60);
    this->help_label->setTextColor(PICO_DARKGREY);
    WidgetFunctions::Add(this->help_label);

    this->exit_button = new Button(content.x + MARGIN, content.y + content.h - 48 - MARGIN, "終了");
    this->exit_button->setW(content.w - MARGIN * 2);
    this->exit_button->setH(48);
    this->exit_button->setOnPressEnd([this](){ this->requestExit(); });
    WidgetFunctions::Add(this->exit_button);

    this->start_pending = !UsbDriveFunctions::IsActive();
    this->start_failed = false;
    this->confirming = false;
    this->shown_read = this->shown_written = 0xFFFFFFFFu;
    this->refresh(true);
}

void UsbDriveScene::onExit(){
    //画面を離れるときは必ずSDを取り戻す(電源メニューの再起動も onExit() を通る)
    UsbDriveFunctions::Stop();
    this->state_label = nullptr;
    this->help_label = nullptr;
    this->count_label = nullptr;
    this->diag_label = nullptr;
    this->exit_button = nullptr;
}

void UsbDriveScene::onUpdate(){
    if(UsbDriveFunctions::IsActive()) PowerFunctions::KeepAwake();

    if(this->start_pending){
        //開いたフレームは描くだけにして、次のフレームで始める
        this->start_pending = false;
        const UsbDriveFunctions::StartError e = UsbDriveFunctions::Start();
        if(e != UsbDriveFunctions::StartError::None){
            this->start_failed = true;
            ErrorFunctions::ShowFatal(UsbDriveFunctions::StartErrorToStr(e));
        }
        this->refresh(true);
        return;
    }

    const uint32_t now = millis();
    if(now - this->last_refresh_ms >= kRefreshMs){
        this->last_refresh_ms = now;
        this->refresh(false);
    }
}

void UsbDriveScene::refresh(bool force){
    if(!this->state_label) return;
    using UsbDriveFunctions::State;
    const State st = UsbDriveFunctions::GetState();
    if(force || st != this->shown_state){
        this->shown_state = st;
        const char* text = "";
        int8_t color = PICO_FORECOLOR;
        switch(st){
            case State::Off:
                text = this->start_failed ? "始められませんでした" : "準備しています…";
                color = this->start_failed ? PICO_RED : PICO_FORECOLOR;
                break;
            case State::Waiting: text = "PCとUSBでつないでください"; break;
            case State::Mounted: text = "PCにつながっています"; color = PICO_DARKGREEN; break;
            case State::Ejected: text = "PCで取り出されました。終了してよいです"; color = PICO_BLUE; break;
        }
        this->state_label->setText(text);
        this->state_label->setTextColor(color);
        this->exit_button->setText(st == State::Off ? "戻る" : "終了");
    }

    const uint32_t r = UsbDriveFunctions::SectorsRead();
    const uint32_t w = UsbDriveFunctions::SectorsWritten();
    if(force || r != this->shown_read || w != this->shown_written){
        this->shown_read = r;
        this->shown_written = w;
        char buf[96];
        if(UsbDriveFunctions::IsActive()){
            const uint32_t mib = UsbDriveFunctions::CapacityMiB();
            char cap[16];
            if(mib >= 1024) snprintf(cap, sizeof(cap), "%lu.%luGB", (unsigned long)(mib / 1024), (unsigned long)(mib % 1024 * 10 / 1024));
            else snprintf(cap, sizeof(cap), "%luMB", (unsigned long)mib);
            snprintf(buf, sizeof(buf), "容量 %s\n読み %luKB / 書き %luKB",
                cap, (unsigned long)(r / 2), (unsigned long)(w / 2));
        }else{
            buf[0] = '\0';
        }
        this->count_label->setText(buf);
    }

    //診断: USBの列挙 → マスストレージの口 → SCSIのコマンド、のどこまで進んだか
    if(UsbDriveFunctions::IsActive()){
        const UsbDriveFunctions::Diag d = UsbDriveFunctions::GetDiag();
        const char* link = !d.usb_connected ? "USB:線なし"
                         : !d.usb_mounted   ? "USB:列挙待ち"
                         : !d.msc_open      ? "USB:列挙済/口まだ"
                         :                    "USB:口OK";
        char buf[128];
        snprintf(buf, sizeof(buf), "%s ep%02X/%02X\nINQ%lu TUR%lu CAP%lu 他%lu(%02X) 誤%lu",
            link, d.ep_in, d.ep_out,
            (unsigned long)d.inquiry, (unsigned long)d.ready, (unsigned long)d.capacity,
            (unsigned long)d.other, d.last_other, (unsigned long)d.errors);
        this->diag_label->setText(buf);
    }else{
        this->diag_label->setText("");
    }
}

void UsbDriveScene::requestExit(){
    if(this->confirming) return;
    if(UsbDriveFunctions::GetState() != UsbDriveFunctions::State::Mounted){
        this->finish();
        return;
    }
    //PCがまだ使っている。書きかけのまま取り戻すとPC側で壊れたファイルになりうる
    this->confirming = true;
    auto* dialog = new MsgDialog("PCで「取り出し」をしてから終了してください。\nこのまま終了しますか?", "キャンセル", "終了");
    if(!dialog){ this->confirming = false; return; }
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_ok){
        this->confirming = false;
        WidgetFunctions::DestroyLater(dialog);
        if(is_ok) this->finish();
    });
}

void UsbDriveScene::finish(){
    UsbDriveFunctions::Stop();
    SceneFunctions::Pop();
}
