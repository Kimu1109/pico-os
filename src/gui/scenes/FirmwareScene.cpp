#include "gui/scenes/FirmwareScene.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "firmware/Fw_Image.hpp"
#include "firmware/Fw_Version.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <ctime>

void FirmwareScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int w = content.w - MARGIN * 2;
    const int16_t x = (int16_t)(content.x + MARGIN);
    int y = content.y + MARGIN;

    auto* title = new Label<PICO_STR_L>(x, y, kAppName);
    title->setFontSize(FontFn::Normal);
    WidgetFunctions::Add(title);
    y += 32;

    char text[96];
    snprintf(text, sizeof(text), "今の版 %s\n%s", FwVersion::Text(), FwVersion::Date()[0] ? FwVersion::Date() : "");
    this->version_label = new Label<PICO_STR_L>(x, y, text);
    this->version_label->setFontSize(FontFn::Small);
    this->version_label->setMaxWidth(w);
    this->version_label->setMaxHeight(36);
    this->version_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->version_label);
    y += 38;

    const bool has_key = FwImage::HasBuiltinKey();
    snprintf(text, sizeof(text), "署名の鍵: %s%s", has_key ? "登録済み" : "未登録",
        FirmwareFunctions::AllowUnsigned() ? "\n署名なしも確認して受け付ける" : "");
    this->key_label = new Label<PICO_STR_LL>(x, y, text);
    this->key_label->setFontSize(FontFn::Small);
    this->key_label->setMaxWidth(w);
    this->key_label->setMaxHeight(36);
    this->key_label->setTextColor(PICO_DARKGREY);
    this->key_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->key_label);
    y += 40;

    this->status_label = new Label<PICO_STR_LL>(x, y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(w);
    this->status_label->setMaxHeight(54);
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);
    y += 56;

    this->progress = new ProgressBar(x, (int16_t)y, (int16_t)w, 12);
    this->progress->setColor(PICO_BLUE);
    WidgetFunctions::Add(this->progress);
    y += 16;

    this->detail_label = new Label<PICO_STR_L>(x, y, "");
    this->detail_label->setFontSize(FontFn::Small);
    this->detail_label->setMaxWidth(w);
    this->detail_label->setMaxHeight(20);
    this->detail_label->setTextColor(PICO_DARKGREY);
    this->detail_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->detail_label);

    // 下の段: [SDから選ぶ][受け取る] / [戻る]
    constexpr int kButtonH = 40;
    const int16_t row2 = (int16_t)(content.y + content.h - MARGIN - kButtonH);
    const int16_t row1 = (int16_t)(row2 - MARGIN - kButtonH);
    const int half = (w - MARGIN) / 2;

    this->sd_button = new Button(x, row1, "SDから選ぶ");
    this->sd_button->setFontSize(FontFn::Small);
    this->sd_button->setW(half);
    this->sd_button->setH(kButtonH);
    this->sd_button->setOnPressEnd([this](){ this->openPicker(); });
    WidgetFunctions::Add(this->sd_button);

    this->listen_button = new Button((int16_t)(x + half + MARGIN), row1, "受け取る");
    this->listen_button->setFontSize(FontFn::Small);
    this->listen_button->setW(w - half - MARGIN);
    this->listen_button->setH(kButtonH);
    this->listen_button->setOnPressEnd([this](){ this->toggleListen(); });
    WidgetFunctions::Add(this->listen_button);

    this->back_button = new Button(x, row2, "戻る");
    this->back_button->setW(w);
    this->back_button->setH(kButtonH);
    this->back_button->setOnPressEnd([this](){ this->goBack(); });
    WidgetFunctions::Add(this->back_button);

    this->confirm_open = false;
    this->shown_state = -1;
    this->shown_done = 0xFFFFFFFFu;
    // 開いたフレームは描くだけにして、次のフレームで始める(ファイルアプリから渡されたとき)
    this->start_pending = !this->initial_path.empty();
    this->refresh(true);
}

void FirmwareScene::onExit(){
    // 準備ができて再起動を待っている(電源メニューの再起動の onExit() もここを通る)ならそのまま
    if(!FirmwareFunctions::RebootPending()){
        FirmwareFunctions::StopListening();
        FirmwareFunctions::Cancel();
        FirmwareFunctions::Dismiss();
    }
    this->version_label = nullptr;
    this->key_label = nullptr;
    this->status_label = nullptr;
    this->detail_label = nullptr;
    this->progress = nullptr;
    this->sd_button = nullptr;
    this->listen_button = nullptr;
    this->back_button = nullptr;
}

void FirmwareScene::onUpdate(){
    if(this->start_pending){
        this->start_pending = false;
        FixedString<PICO_PATH_LEN> path = this->initial_path;
        this->initial_path.clear(); // 戻ってきたときにもう一度始めない
        this->startFromSd(path.c_str());
        return;
    }
    FirmwareFunctions::Update();

    FwUpdate* job = FirmwareFunctions::Job();
    // 先に下の画面を書き換える(半透明のダイアログを開いた後だと、その下は描き直されないため)
    this->refresh(false);
    const bool confirming = job && job->state() == FwUpdate::State::Confirm;
    if(confirming && !this->confirm_open) this->openConfirm();
    // 確認を待つ間に相手が切れた等で確認が要らなくなったら、ダイアログを閉じる
    if(!confirming && this->confirm_open){
        if(Widget* d = WidgetRegistry::Resolve(this->confirm_dialog)) WidgetFunctions::DestroyLater(d);
        this->confirm_open = false;
        this->refresh(true);
    }
}

bool FirmwareScene::onKey(const KeyInputFunctions::Event& ev){
    if(ev.key == KeyInputFunctions::Key::Escape){
        this->goBack();
        return true;
    }
    return false;
}

void FirmwareScene::showError(const char* text){
    ErrorFunctions::ShowFatal(text);
}

void FirmwareScene::startFromSd(const char* path){
    FixedString<PICO_STR_LL> err;
    if(!FirmwareFunctions::StartFromSd(path, err)) this->showError(err.c_str());
    this->refresh(true);
}

void FirmwareScene::openPicker(){
    if(FirmwareFunctions::Busy() || FirmwareFunctions::RebootPending()) return;
    if(!OSData::SD_usable){ this->showError("SDカードが使えません"); return; }
    const char* dir = OSData::SD.exists(PICO_Path::DIR::FIRMWARE) ? PICO_Path::DIR::FIRMWARE : "/";
    auto* d = new FileSelectDialog(dir);
    if(!d) return;
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClose([this, d](bool is_ok){
        FixedString<PICO_PATH_LEN> path;
        const char* sel = d->getSelectedPath();
        if(is_ok && sel) path.assign(sel);
        WidgetFunctions::DestroyLater(d);
        if(!path.empty()){
            // ダイアログを閉じたフレームに次のダイアログ(確認)を出すと跡が残るので、次のフレームで始める
            this->initial_path = path;
            this->start_pending = true;
        }
    });
}

void FirmwareScene::toggleListen(){
    if(FirmwareFunctions::RebootPending()) return;
    if(FirmwareFunctions::Listening()){
        FirmwareFunctions::StopListening();
    }else{
        if(FirmwareFunctions::Busy()) return;
        FirmwareFunctions::Dismiss();
        FixedString<PICO_STR_LL> err;
        if(!FirmwareFunctions::StartListening(err)) this->showError(err.c_str());
    }
    this->refresh(true);
}

void FirmwareScene::openConfirm(){
    FwUpdate* job = FirmwareFunctions::Job();
    if(!job) return;
    const FwImage::Header& h = job->header();

    char date[24] = "";
    if(h.build_time){
        const time_t t = (time_t)h.build_time;
        struct tm tmv;
        gmtime_r(&t, &tmv);
        snprintf(date, sizeof(date), "%04d-%02d-%02d %02d:%02d",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    }
    char text[480];
    int n = snprintf(text, sizeof(text), "新しい版: #%lu (%s)\n%s%s%luKB\n今の版: %s\n書き込みますか?",
        (unsigned long)h.serial, h.git, date, date[0] ? " / " : "",
        (unsigned long)(h.image_bytes / 1024), FwVersion::Text());
    if(n > 0 && (size_t)n < sizeof(text)){
        // 署名を確かめられないときは注意書きの方に出る
        if(job->confirmText()[0]) snprintf(text + n, sizeof(text) - (size_t)n, "\n\n注意: %s", job->confirmText());
        else snprintf(text + n, sizeof(text) - (size_t)n, "\n(%s)", FwImage::SigText(job->signature()));
    }

    auto* dialog = new MsgDialog(text, "やめる", "書き込む");
    if(!dialog){ FirmwareFunctions::Confirm(false); return; }
    this->confirm_open = true;
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    this->confirm_dialog = dialog->getId();
    dialog->setOnClosed([this, dialog](bool is_ok){
        this->confirm_open = false;
        this->confirm_dialog = WidgetIdTools::Invalid();
        WidgetFunctions::DestroyLater(dialog);
        FirmwareFunctions::Confirm(is_ok);
        this->refresh(true);
    });
}

void FirmwareScene::goBack(){
    if(FirmwareFunctions::RebootPending()) return;
    if(FirmwareFunctions::Busy()){
        // 途中ならやめるだけ(置き場を片付ける)。もう一度押すと戻る
        FirmwareFunctions::Cancel();
        this->refresh(true);
        return;
    }
    SceneFunctions::Pop();
}

void FirmwareScene::refresh(bool force){
    if(!this->status_label) return;
    FwUpdate* job = FirmwareFunctions::Job();
    const bool listening = FirmwareFunctions::Listening();
    const int state = job ? (int)job->state() : -1;
    const uint32_t done = job ? job->progressDone() : 0;

    if(force || state != this->shown_state || listening != this->shown_listening){
        this->shown_state = state;
        this->shown_listening = listening;

        char text[PICO_STR_LL];
        int8_t color = PICO_FORECOLOR;
        if(FirmwareFunctions::RebootPending()){
            snprintf(text, sizeof(text), "%s", job ? job->message() : "再起動します");
            color = PICO_DARKGREEN;
        }else if(job && !job->finished()){
            snprintf(text, sizeof(text), "%s", job->message());
        }else if(job && job->state() == FwUpdate::State::Failed){
            snprintf(text, sizeof(text), "%s", job->message());
            color = PICO_RED;
        }else if(listening){
            char addr[40];
            FirmwareFunctions::ListenAddress(addr, sizeof(addr));
            snprintf(text, sizeof(text), "PCからの更新を待っています\n%s", addr);
            color = PICO_BLUE;
        }else{
            snprintf(text, sizeof(text), "更新ファイル(.pfw)をSDから選ぶか、PCから送ってください");
            color = PICO_DARKGREY;
        }
        this->status_label->setText(text);
        this->status_label->setTextColor(color);

        const bool busy = FirmwareFunctions::Busy() || FirmwareFunctions::RebootPending();
        this->sd_button->setEnabled(!busy && !listening);
        this->listen_button->setEnabled(!busy || listening);
        this->listen_button->setText(listening ? "待ち受けをやめる" : "受け取る");
        this->back_button->setEnabled(!FirmwareFunctions::RebootPending());
        this->back_button->setText(busy ? "やめる" : "戻る");
        force = true;
    }

    if(force || done != this->shown_done){
        this->shown_done = done;
        const uint32_t total = job ? job->progressTotal() : 0;
        const bool show = job && total > 0 &&
            (job->state() == FwUpdate::State::Payload || job->state() == FwUpdate::State::Verify ||
             job->state() == FwUpdate::State::Done);
        this->progress->setMaxValue(show ? (float)total : 1.0f);
        this->progress->setValue(show ? (float)(job->state() == FwUpdate::State::Done ? total : done) : 0.0f);
        char text[64] = "";
        if(show) snprintf(text, sizeof(text), "%lu / %luKB", (unsigned long)(done / 1024), (unsigned long)(total / 1024));
        this->detail_label->setText(text);
    }
}
