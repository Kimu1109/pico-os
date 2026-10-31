#include "apps/firmware/FirmwareScene.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "apps/firmware/Fw_Image.hpp"
#include "apps/firmware/Fw_Version.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <ctime>
#include "system/I18n_Functions.hpp"

void FirmwareScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int w = content.w - MARGIN * 2;
    const int16_t x = (int16_t)(content.x + MARGIN);
    const int y0 = content.y + MARGIN;

    auto* title = new Label<PICO_STR_L>(x, y0, I18nFunctions::Tr(kAppName));
    title->setFontSize(FontFn::Normal);
    WidgetFunctions::Add(title);

    // 下の段: [Webから][SDから][受け取る] / [戻る](操作はいつも見えるよう、スクロールの外で下に固定)
    constexpr int kButtonH = 40;
    const int16_t row2 = (int16_t)(content.y + content.h - MARGIN - kButtonH);
    const int16_t row1 = (int16_t)(row2 - MARGIN - kButtonH);

    // ---- 題名とボタンの間: 版・鍵・状態・進み具合。座標は容器の中(左上が(0,0)) ----
    constexpr int kScrollBarW = 6;   // 本体のドラッグでも動くので細く
    const int info_y = y0 + 30;
    this->info_scroll = new ScrollContainer(x, (int16_t)info_y, (int16_t)w, (int16_t)(row1 - MARGIN - info_y));
    this->info_scroll->setScrollBarWidth(kScrollBarW);
    this->info_scroll->setBorderColor(PICO_BACKGROUND); //枠は描かない
    const int iw = w - kScrollBarW - 2;
    const int line_h = Label<PICO_STR_L>::GetLineHeight(FontFn::Small);
    constexpr int kGap = 8;   // 項目の間
    int y = 0;
    //表示だけの部品なので当たり判定を素通りさせ、上でのドラッグでも容器ごとスクロールする
    auto addInfo = [this](Widget* wd){
        wd->setHitTransparent(true);
        this->info_scroll->add(wd);
    };

    char text[96];
    snprintf(text, sizeof(text), TR("今の版 %s\n%s"), FwVersion::Text(), FwVersion::Date()[0] ? FwVersion::Date() : "");
    this->version_label = new Label<PICO_STR_L>(0, y, text);
    this->version_label->setFontSize(FontFn::Small);
    this->version_label->setMaxWidth(iw);
    this->version_label->setMaxHeight(line_h * 2);
    this->version_label->setDisableAutoTextDecoration(true);
    addInfo(this->version_label);
    y += line_h * 2 + kGap;

    const bool has_key = FwImage::HasBuiltinKey();
    snprintf(text, sizeof(text), TR("署名の鍵: %s%s"), has_key ? TR("登録済み") : TR("未登録"),
        FirmwareFunctions::AllowUnsigned() ? TR("\n署名なしも確認して受け付ける") : "");
    this->key_label = new Label<PICO_STR_LL>(0, y, text);
    this->key_label->setFontSize(FontFn::Small);
    this->key_label->setMaxWidth(iw);
    this->key_label->setMaxHeight(line_h * 2);
    this->key_label->setTextColor(PICO_DARKGREY);
    this->key_label->setDisableAutoTextDecoration(true);
    addInfo(this->key_label);
    y += line_h * (FirmwareFunctions::AllowUnsigned() ? 2 : 1) + kGap;

    //状態は長い説明が入るので4行ぶん取る
    this->status_label = new Label<PICO_STR_LL>(0, y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(iw);
    this->status_label->setMaxHeight(line_h * 4);
    this->status_label->setDisableAutoTextDecoration(true);
    addInfo(this->status_label);
    y += line_h * 4 + kGap;

    this->progress = new ProgressBar(0, (int16_t)y, (int16_t)iw, 12);
    this->progress->setColor(PICO_BLUE);
    addInfo(this->progress);
    y += 12 + 4;

    this->detail_label = new Label<PICO_STR_L>(0, y, "");
    this->detail_label->setFontSize(FontFn::Small);
    this->detail_label->setMaxWidth(iw);
    this->detail_label->setMaxHeight(line_h);
    this->detail_label->setDefaultHeight(line_h); //空のうちもスクロールの範囲に入れておく
    this->detail_label->setTextColor(PICO_DARKGREY);
    this->detail_label->setDisableAutoTextDecoration(true);
    addInfo(this->detail_label);
    WidgetFunctions::Add(this->info_scroll); //子もまとめて登録される

    const int third = (w - MARGIN * 2) / 3;

    this->web_button = new Button(x, row1, TR("Webから"));
    this->web_button->setFontSize(FontFn::Small);
    this->web_button->setW(third);
    this->web_button->setH(kButtonH);
    this->web_button->setOnPressEnd([this](){ this->startWeb(); });
    WidgetFunctions::Add(this->web_button);

    this->sd_button = new Button((int16_t)(x + third + MARGIN), row1, TR("SDから"));
    this->sd_button->setFontSize(FontFn::Small);
    this->sd_button->setW(third);
    this->sd_button->setH(kButtonH);
    this->sd_button->setOnPressEnd([this](){ this->openPicker(); });
    WidgetFunctions::Add(this->sd_button);

    this->listen_button = new Button((int16_t)(x + (third + MARGIN) * 2), row1, TR("受け取る"));
    this->listen_button->setFontSize(FontFn::Small);
    this->listen_button->setW(w - (third + MARGIN) * 2);
    this->listen_button->setH(kButtonH);
    this->listen_button->setOnPressEnd([this](){ this->toggleListen(); });
    WidgetFunctions::Add(this->listen_button);

    this->back_button = new Button(x, row2, TR("戻る"));
    this->back_button->setW(w);
    this->back_button->setH(kButtonH);
    this->back_button->setOnPressEnd([this](){ this->goBack(); });
    WidgetFunctions::Add(this->back_button);

    this->confirm_open = false;
    this->web_pick_pending = false;
    this->picker_open = false;
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
    // 一覧と接続(TLS)は再起動待ちでも要らない
    FirmwareFunctions::CloseWeb();
    this->info_scroll = nullptr;
    this->version_label = nullptr;
    this->key_label = nullptr;
    this->status_label = nullptr;
    this->detail_label = nullptr;
    this->progress = nullptr;
    this->web_button = nullptr;
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

    FixedString<PICO_STR_LL> web_err;
    if(FirmwareFunctions::TakeWebError(web_err)){
        this->refresh(true);
        this->showError(web_err.c_str());
        return;
    }
    if(this->web_pick_pending){
        FwDownload* web = FirmwareFunctions::Web();
        if(!web || web->state() != FwDownload::State::Index){
            this->web_pick_pending = false;
            this->refresh(true);
            // 先に下の画面を書き換えてから、次のフレームで出す(半透明のダイアログの下は描き直されないため)
            if(web && web->state() == FwDownload::State::Listed && web->count() > 0) this->picker_open = true;
            return;
        }
    }
    if(this->picker_open && !WidgetRegistry::Resolve(this->confirm_dialog)){
        this->openWebPicker();
        return;
    }

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

void FirmwareScene::startWeb(){
    if(FirmwareFunctions::Busy() || FirmwareFunctions::WebBusy() || FirmwareFunctions::RebootPending()) return;
    FwDownload* web = FirmwareFunctions::Web();
    // 一覧を取ってあれば選び直すだけ(取り直したいときは画面を開き直す)
    if(web && web->count() > 0 && (web->state() == FwDownload::State::Listed || web->state() == FwDownload::State::Failed)){
        this->picker_open = true;
        return;
    }
    FirmwareFunctions::Dismiss();
    FixedString<PICO_STR_LL> err;
    if(!FirmwareFunctions::StartWebIndex(err)){
        this->showError(err.c_str());
    }else{
        this->web_pick_pending = true;
    }
    this->refresh(true);
}

void FirmwareScene::openWebPicker(){
    this->picker_open = false;
    FwDownload* web = FirmwareFunctions::Web();
    if(!web || web->count() <= 0) return;
    auto* d = new PickerDialog(PickerDialog::Mode::Choice, TR("書き込む版を選ぶ"), "", TR("やめる"));
    if(!d) return;
    const uint32_t current = FwVersion::Serial();
    for(int i = 0; i < web->count(); i++){
        const FwIndex::Entry* e = web->entry(i);
        // "#523 夜間 10/28"。幅が狭いので、印(今の版・古い・署名なし)があれば日付の代わりに出す
        // (日付は選んだ後の確認のダイアログにも出る)
        const char* date = e->date.c_str();
        char tail[16] = "";
        if(!e->signed_flag) snprintf(tail, sizeof(tail), TR("無署名"));
        else if(current > 0 && e->serial == current && strcmp(e->git.c_str(), FwVersion::Git()) == 0) snprintf(tail, sizeof(tail), TR("(今)"));
        else if(current > 0 && e->serial < current) snprintf(tail, sizeof(tail), TR("(古)"));
        else if(e->date.length() >= 10) snprintf(tail, sizeof(tail), "%.2s/%.2s", date + 5, date + 8);
        char text[96];
        snprintf(text, sizeof(text), "#%lu %s %s", (unsigned long)e->serial, FwIndex::ChannelText(e->channel), tail);
        d->addChoice(text);
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    this->confirm_dialog = d->getId();
    d->setOnClosed([this, d](bool is_ok){
        const int index = is_ok ? d->getSelectedIndex() : -1;
        this->confirm_dialog = WidgetIdTools::Invalid();
        WidgetFunctions::DestroyLater(d);
        if(index >= 0){
            FixedString<PICO_STR_LL> err;
            if(!FirmwareFunctions::StartWebFile(index, err)) this->showError(err.c_str());
        }
        this->refresh(true);
    });
}

void FirmwareScene::openPicker(){
    if(FirmwareFunctions::Busy() || FirmwareFunctions::WebBusy() || FirmwareFunctions::RebootPending()) return;
    if(!OSData::SD_usable){ this->showError(TR("SDカードが使えません")); return; }
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
    if(FirmwareFunctions::RebootPending() || FirmwareFunctions::WebBusy()) return;
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
    int n = snprintf(text, sizeof(text), TR("新しい版: #%lu (%s)\n%s%s%luKB\n今の版: %s\n書き込みますか?"),
        (unsigned long)h.serial, h.git, date, date[0] ? " / " : "",
        (unsigned long)(h.image_bytes / 1024), FwVersion::Text());
    if(n > 0 && (size_t)n < sizeof(text)){
        // 署名を確かめられないときは注意書きの方に出る
        if(job->confirmText()[0]) snprintf(text + n, sizeof(text) - (size_t)n, TR("\n\n注意: %s"), job->confirmText());
        else snprintf(text + n, sizeof(text) - (size_t)n, "\n(%s)", FwImage::SigText(job->signature()));
    }

    auto* dialog = new MsgDialog(text, TR("やめる"), TR("書き込む"));
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
    if(FirmwareFunctions::WebBusy()){
        FirmwareFunctions::CancelWeb();
        this->web_pick_pending = false;
        this->refresh(true);
        return;
    }
    SceneFunctions::Pop();
}

void FirmwareScene::refresh(bool force){
    if(!this->status_label) return;
    FwUpdate* job = FirmwareFunctions::Job();
    FwDownload* web = job ? nullptr : FirmwareFunctions::Web();
    const bool listening = FirmwareFunctions::Listening();
    // 更新の状態と取得の状態を1つの数にまとめる(変わったときだけ書き直す)
    const int state = job ? (int)job->state() : (web ? 16 + (int)web->state() : -1);
    const uint32_t done = job ? job->progressDone() : (web ? web->progressDone() : 0);

    if(force || state != this->shown_state || listening != this->shown_listening){
        this->shown_state = state;
        this->shown_listening = listening;

        char text[PICO_STR_LL];
        int8_t color = PICO_FORECOLOR;
        if(FirmwareFunctions::RebootPending()){
            snprintf(text, sizeof(text), "%s", job ? job->message() : TR("再起動します"));
            color = PICO_DARKGREEN;
        }else if(job && !job->finished()){
            snprintf(text, sizeof(text), "%s", job->message());
        }else if(job && job->state() == FwUpdate::State::Failed){
            snprintf(text, sizeof(text), "%s", job->message());
            color = PICO_RED;
        }else if(web && web->state() != FwDownload::State::Idle){
            snprintf(text, sizeof(text), "%s", web->message());
            if(web->state() == FwDownload::State::Failed) color = PICO_RED;
            else if(web->state() == FwDownload::State::Listed) color = web->count() > 0 ? PICO_BLUE : PICO_DARKGREY;
        }else if(listening){
            char addr[40];
            FirmwareFunctions::ListenAddress(addr, sizeof(addr));
            snprintf(text, sizeof(text), TR("PCからの更新を待っています\n%s"), addr);
            color = PICO_BLUE;
        }else{
            snprintf(text, sizeof(text), TR("Webの版の一覧から選ぶか、更新ファイル(.pfw)をSDから選ぶか、PCから送ってください"));
            color = PICO_DARKGREY;
        }
        this->status_label->setText(text);
        this->status_label->setTextColor(color);

        const bool busy = FirmwareFunctions::Busy() || FirmwareFunctions::RebootPending();
        const bool web_busy = FirmwareFunctions::WebBusy();
        this->web_button->setEnabled(!busy && !web_busy && !listening);
        this->sd_button->setEnabled(!busy && !web_busy && !listening);
        this->listen_button->setEnabled((!busy && !web_busy) || listening);
        this->listen_button->setText(listening ? TR("待ち受けをやめる") : TR("受け取る"));
        this->back_button->setEnabled(!FirmwareFunctions::RebootPending());
        this->back_button->setText(busy || web_busy ? TR("やめる") : TR("戻る"));
        force = true;
    }

    if(force || done != this->shown_done){
        this->shown_done = done;
        const uint32_t total = job ? job->progressTotal() : (web ? web->progressTotal() : 0);
        const bool show = total > 0 && (job ?
            (job->state() == FwUpdate::State::Payload || job->state() == FwUpdate::State::Verify ||
             job->state() == FwUpdate::State::Done) :
            (web && web->state() == FwDownload::State::File));
        this->progress->setMaxValue(show ? (float)total : 1.0f);
        this->progress->setValue(show ? (float)(job && job->state() == FwUpdate::State::Done ? total : done) : 0.0f);
        char text[64] = "";
        if(show) snprintf(text, sizeof(text), "%lu / %luKB", (unsigned long)(done / 1024), (unsigned long)(total / 1024));
        this->detail_label->setText(text);
    }
}
