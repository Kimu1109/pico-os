#include "gui/scenes/SettingsScene.hpp"
#include "services/Power_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "services/Display_Functions.hpp"
#include "services/Lock_Functions.hpp"
#include "services/DeviceKey_Functions.hpp"
#include "gui/scenes/TouchCalibScene.hpp"
#include "gui/scenes/BackgroundScene.hpp"
#include "system/Background_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "config/Hardware_Config.hpp"
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/WifiScanDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "net/Wifi_Profiles.hpp"
#include "net/NetworkScan.hpp"

#include "devtools/DevTools_Functions.hpp"
#include "services/PowerMenu_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include <cstdio>
#include <cstring>

namespace {
    // タイムゾーンのプリセット。POSIX TZ文字列そのものを項目名として使う
    // (このOSのユーザーは開発者自身が想定なので、生のTZ表記でも実害が無い)
    constexpr const char* kTimezonePresets[] = {
        "JST-9", "UTC0", "EST5EDT", "CST6CDT", "MST7MDT", "PST8PDT"
    };
    constexpr int kTimezonePresetCount = sizeof(kTimezonePresets) / sizeof(kTimezonePresets[0]);

    // スリープまでの時間(秒、0=無効)と表示名。最後の操作からの通算(自動調光の30秒より後ろの値を選ぶ)
    constexpr unsigned long kSleepPresetsSec[] = { 0, 60, 120, 300, 600, 1800 };
    constexpr const char*   kSleepPresetLabels[] = { "しない", "1分", "2分", "5分", "10分", "30分" };
    constexpr int kSleepPresetCount = sizeof(kSleepPresetsSec) / sizeof(kSleepPresetsSec[0]);

    // 音の周波数(sound.cfgの sample-rate)。44.1kHzはライン出力のDAC(PCM5102A等)向けで、2コア目の計算が倍になる
    constexpr uint32_t    kRatePresets[]      = { SoundFunctions::kSampleRateLow, SoundFunctions::kSampleRateHigh };
    constexpr const char* kRatePresetLabels[] = { "22kHz", "44.1kHz" };
    constexpr int kRatePresetCount = sizeof(kRatePresets) / sizeof(kRatePresets[0]);

    // 出力先(sound.cfgの route)
    constexpr SoundFunctions::RouteMode kRouteModes[] = {
        SoundFunctions::RouteMode::Auto, SoundFunctions::RouteMode::Speaker, SoundFunctions::RouteMode::Headphone };
    constexpr const char* kRouteModeLabels[] = { "自動", "スピーカー", "イヤホン" };
    constexpr int kRouteModeCount = sizeof(kRouteModes) / sizeof(kRouteModes[0]);
}

void SettingsScene::loadTimezoneItems(const FixedString<PICO_STR_M>& current_tz){
    this->tz_item_count     = 0;
    this->tz_selected_index = -1;

    for(int i = 0; i < kTimezonePresetCount && this->tz_item_count < kMaxTzItems; i++){
        this->tz_items[this->tz_item_count].assign(kTimezonePresets[i]);
        if(current_tz == kTimezonePresets[i]) this->tz_selected_index = this->tz_item_count;
        this->tz_item_count++;
    }

    // プリセットに無い値は、独自設定を黒く上書きしないよう末尾に追加して選ぶ
    if(this->tz_selected_index < 0 && !current_tz.empty() && this->tz_item_count < kMaxTzItems){
        this->tz_items[this->tz_item_count].assign(current_tz);
        this->tz_selected_index = this->tz_item_count;
        this->tz_item_count++;
    }

    if(this->tz_selected_index < 0) this->tz_selected_index = 0; // 空/上限溢れ時はJST-9(先頭)を既定に
}


void SettingsScene::loadValues(){
    this->ntp1_value.assign(NetworkFunctions::ntpServer1.c_str());
    this->ntp2_value.assign(NetworkFunctions::ntpServer2.c_str());
    this->home_value.clear();

    FixedString<PICO_STR_M> tz_value;
    tz_value.assign("JST-9");

    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_NETWORK_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "ntp-server-1") == 0){
                this->ntp1_value.assign(value);
            }else if(strcmp(key, "ntp-server-2") == 0){
                this->ntp2_value.assign(value);
            }else if(strcmp(key, "browser-home") == 0){
                this->home_value.assign(value);
            }else if(strcmp(key, "timezone") == 0){
                tz_value.assign(value);
            }
        }
    );

    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_USER_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "run-test") == 0){
                bool run_test = false;
                if(PICO_Config::ConfigValue::AsBool(value, run_test) && this->run_test_checkbox){
                    this->run_test_checkbox->setIsChecked(run_test);
                }
            }
        }
    );

    this->loadTimezoneItems(tz_value);
}

void SettingsScene::refreshNtp1Label(){
    if(!this->ntp1_label) return;
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "NTPサーバー1: %s", this->ntp1_value.c_str());
    this->ntp1_label->setText(buf);
}

void SettingsScene::refreshNtp2Label(){
    if(!this->ntp2_label) return;
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "NTPサーバー2: %s", this->ntp2_value.c_str());
    this->ntp2_label->setText(buf);
}

void SettingsScene::refreshHomeLabel(){
    if(!this->home_label) return;
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "ホーム: %s", this->home_value.empty() ? "(未設定)" : this->home_value.c_str());
    this->home_label->setText(buf);
}

void SettingsScene::refreshBatteryLabel(){
    if(!this->battery_label) return;
    char buf[PICO_STR_M];
    if(!BatteryFunctions::HasSample()){
        buf[0] = '\0';
    }else if(BatteryFunctions::IsExternallyPowered()){
        snprintf(buf, sizeof(buf), "電池 USB給電中");
    }else{
        snprintf(buf, sizeof(buf), "電池 %d%% (%.2fV)",
            BatteryFunctions::GetPercent(), BatteryFunctions::GetVoltage());
    }
    this->battery_label->setText(buf);
}

Button* SettingsScene::makeEditButton(int16_t y){
    Button* b = new Button("編集");
    b->setIcon(IconID::Pencil, IconSize::Px16);
    b->setFontSize(FontFn::Small);
    b->setAllowTextSpacing(false);
    b->setW(EDIT_BTN_W + Button::kFrameExtraTight);
    b->setH(EDIT_BTN_H + Button::kFrameExtraTight);

    // 全行で同じ右端に揃えるので、xは最初の1回だけ実測して覚えておく
    if(this->edit_btn_x == 0){
        const Rect box = b->getLocalRect();
        this->edit_btn_x = this->row_right - box.w;
    }
    b->setX(this->edit_btn_x);
    b->setY(y);
    return b;
}


void SettingsScene::addToTab(Tab t, Widget* w){
    const int ti = (int)t;
    //表示だけのラベルは当たり判定を素通りさせ、押したまま動かすと容器ごとスクロールするようにする
    if(w->getWidgetType() == WidgetType::Label) w->setHitTransparent(true);
    this->tab_scroll[ti]->add(w); //所有は容器へ移る(onEnter()の最後に容器ごとAdd()する)
    if(this->tab_widget_count[ti] < kMaxTabWidgets){
        this->tab_widgets[ti][this->tab_widget_count[ti]++] = w;
    }else{
        LOG_SYS_WARN("Settings: タブのウィジェットが多すぎます(%d)", ti);
    }
}

void SettingsScene::applyTab(){
    // 先に隠してから見せる(隠す側のdirtyと見せる側のdirtyが同じフレームにまとまる)
    for(int t = 0; t < (int)Tab::Count; t++){
        if(t == (int)this->tab) continue;
        this->tab_scroll[t]->setVisible(false); //子もまとめて隠れる
    }
    const int cur = (int)this->tab;
    this->tab_scroll[cur]->setVisible(true);
    //容器のsetVisible()は子孫の表示を一律に立てるので、開閉の状態を持つ部品(ドロップダウンの一覧等)は
    //自分のsetVisible()で整え直す
    for(int i = 0; i < this->tab_widget_count[cur]; i++) this->tab_widgets[cur][i]->setVisible(true);
    this->tab_scroll[cur]->refreshContentBounds();
    this->shown_open_dropdown = nullptr;

    if(this->tab == Tab::Wifi){
        // 隠れている間は更新していないので、見せる側を今の状態で埋め直す
        this->shown_status = 0xFF;
        this->wifi_list_dirty = true;
        this->refreshWifi();
    }
}

void SettingsScene::openEditDialog(EditField field, const char* label_text, const char* prefill){
    // MarkdownSceneの検索入力と同じ形: 押されるたびにnewし、閉じたらDestroyLater()で破棄する
    // (設定項目ごとに専用ダイアログを持つより省メモリで、既存の流儀にも合う)
    auto* dialog = new InputDialog(label_text, true);
    dialog->setAsciiInput(true); //英数字の欄(キーボードを英字で開く)
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(prefill ? prefill : "");
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, field](bool is_submit){
        if(is_submit){
            this->commitEdit(field, dialog->getInput());
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::commitEdit(EditField field, const FixedString<PICO_STR_LL>& input){
    switch(field){
        case EditField::Ntp1: {
            if(input.empty()) break;
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "ntp-server-1", input.c_str());
            this->ntp1_value.assign(input.c_str());
            NetworkFunctions::ntpServer1.assign(input.c_str());
            this->refreshNtp1Label();
            break;
        }
        case EditField::Ntp2: {
            if(input.empty()) break;
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "ntp-server-2", input.c_str());
            this->ntp2_value.assign(input.c_str());
            NetworkFunctions::ntpServer2.assign(input.c_str());
            this->refreshNtp2Label();
            break;
        }
        case EditField::BrowserHome: {
            // 空欄も許可する(空にすると同梱サンプル文書に戻る。MarkdownScene::readHome()参照)
            PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "browser-home", input.c_str());
            this->home_value.assign(input.c_str());
            this->refreshHomeLabel();
            break;
        }
        case EditField::WifiSsid: {
            if(input.empty()) break;
            if(input.length() > WifiProfiles::kMaxSsidBytes){
                this->showMessageLater("SSIDが長すぎます(32バイトまで)");
                break;
            }
            // ダイアログからダイアログは1フレーム空ける
            this->pending_wifi_ssid.assign(input.c_str());
            this->pending = Pending::WifiPassword;
            break;
        }
        case EditField::Hostname: {
            if(input.empty()) break;
            if(!NetworkFunctions::SetHostname(input.c_str())){
                this->showMessageLater("名前は英数字とハイフンの63文字までです(先頭と末尾にハイフンは使えません)");
                break;
            }
            this->refreshWifiInfo();
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// 画面の組み立て
// ---------------------------------------------------------------------------

int SettingsScene::labelW(const Rect& content) const {
    return this->edit_btn_x - content.x - MARGIN * 2;
}

void SettingsScene::onEnter(){
    const Rect content = Scene::contentRect();

    for(int t = 0; t < (int)Tab::Count; t++) this->tab_widget_count[t] = 0;

    // ---- 上部: [戻る] + バッテリー残量(全タブ共通) ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    //右端の「電源」(スリープ/再起動。ステータスバーの長押しと同じメニュー)
    this->power_button = new Button(0, content.y + MARGIN, "電源");
    this->power_button->setIcon(IconID::Power, IconSize::Px16);
    this->power_button->setFontSize(FontFn::Small);
    this->power_button->setH(20 + Button::kFrameExtra);
    this->power_button->setX((int16_t)(content.x + content.w - MARGIN - this->power_button->getLocalRect().w));
    this->power_button->setOnPressEnd([](){ PowerMenuFunctions::Open(); });
    WidgetFunctions::Add(this->power_button);

    const int16_t battery_x = (int16_t)(content.x + MARGIN + back_box.w + MARGIN);
    const int16_t battery_w = (int16_t)(this->power_button->getLocalRect().x - MARGIN - battery_x);
    this->battery_label = makeRowLabel<PICO_STR_M>(battery_x, content.y + MARGIN, battery_w, "");
    WidgetFunctions::Add(this->battery_label);
    this->refreshBatteryLabel();

    // ---- 下部: タブ ----
    const int16_t tab_y = (int16_t)(content.y + content.h - MARGIN - TAB_H);
    this->tab_bar = new TabBar(content.x + MARGIN, tab_y, content.w - MARGIN * 2, TAB_H);
    this->tab_bar->addTab("Wi-Fi");
    this->tab_bar->addTab("画面");
    this->tab_bar->addTab("音");
    this->tab_bar->addTab("時刻");
    this->tab_bar->addTab("他");
    this->tab_bar->setSelected((int)this->tab);
    this->tab_bar->setOnChanged([this](int index){
        this->tab = (Tab)index;
        this->applyTab();
    });
    WidgetFunctions::Add(this->tab_bar);

    // ---- 本文: タブごとの縦スクロールの容器(上の段とタブの間) ----
    const int16_t body_top = (int16_t)(content.y + MARGIN + this->top_row_h + MARGIN);
    const int16_t body_h   = (int16_t)(tab_y - MARGIN - body_top);
    for(int t = 0; t < (int)Tab::Count; t++){
        auto* sc = new ScrollContainer(content.x, body_top, content.w, body_h);
        sc->setScrollBarWidth(SCROLL_BAR_W);
        sc->setBorderColor(PICO_BACKGROUND); //枠は描かない(背景と同じ色)
        this->tab_scroll[t] = sc;
    }

    // 本文の行の置き方(各タブの組み立てが使う。座標は容器の中)
    const int16_t view_w = (int16_t)(content.w - SCROLL_BAR_W);
    Layout lay;
    lay.content  = { 0, 0, view_w, body_h };
    lay.body_y   = MARGIN;
    lay.body_bot = body_h;
    lay.left     = MARGIN;
    lay.full_w   = view_w - MARGIN * 2;
    this->row_right = view_w - MARGIN;
    const int16_t left = lay.left;

    // 起動時セルフチェックはloadValues()がチェック状態を直接流し込むので、読み込みより前に生成しておく
    this->run_test_checkbox = new Checkbox(left, 0, "起動時に自己診断を実行");
    this->run_test_checkbox->setFontSize(FontFn::Small);
    this->run_test_checkbox->setOnChangeChecked([this](){
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_USER_CFG, "run-test",
            PICO_Config::ConfigValue::FromBool(this->run_test_checkbox->getIsChecked()));
    });

    this->loadValues();

    // 追加順=描画順(後が上)なので、この順を変えないこと(ドロップダウンは開いた一覧が下の行へ
    // 重なるので最後の buildLateRows())
    this->buildWifiTab(lay);
    this->buildLevelRows(lay);
    this->buildTimeRows(lay);
    this->buildOtherTab(lay);
    this->buildLateRows(lay);

    //中身を入れ終えてから容器ごと登録する(子は容器へ足した順=描画順のまま並ぶ)
    for(int t = 0; t < (int)Tab::Count; t++) WidgetFunctions::Add(this->tab_scroll[t]);

    this->applyTab();
}

void SettingsScene::revealOpenDropdown(){
    ScrollContainer* sc = this->tab_scroll[(int)this->tab];
    if(!sc) return;
    DropdownMenu* const dropdowns[] = {
        this->rate_dropdown, this->route_dropdown, this->sleep_dropdown,
        this->text_size_dropdown, this->timezone_dropdown,
    };
    DropdownMenu* open = nullptr;
    for(DropdownMenu* d : dropdowns){
        if(d && d->getVisible() && d->isOpen()) open = d;
    }
    if(open == this->shown_open_dropdown) return;
    this->shown_open_dropdown = open;
    //開くと一覧の大きさへ伸び、閉じると戻るので、スクロールの範囲を測り直す
    sc->refreshContentBounds();
    if(open) sc->revealRect(open->getScreenRect());
}


void SettingsScene::refreshLockLabel(){
    if(this->lock_label) this->lock_label->setText(LockFunctions::HasPin() ? "画面ロック: PIN" : "画面ロック: なし");
}

void SettingsScene::openLockDialog(){
    auto* d = new PickerDialog(PickerDialog::Mode::Choice, "画面ロック", "", "キャンセル");
    const bool has = LockFunctions::HasPin();
    if(has){
        d->addChoice("PINを変える");
        d->addChoice("PINを外す");
        d->addChoice("今すぐロック");
    }else{
        d->addChoice("PINを決める");
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClosed([d, has](bool ok){
        const int sel = ok ? d->getSelectedIndex() : -1;
        WidgetFunctions::DestroyLater(d);
        //ロック画面は全画面を覆うオーバーレイなので、ダイアログを閉じた同じフレームで出してよい
        if(sel == 0) LockFunctions::BeginChange();
        else if(has && sel == 1) LockFunctions::BeginRemove();
        else if(has && sel == 2) LockFunctions::LockNow();
    });
}

void SettingsScene::refreshDeviceKeyLabel(){
    if(!this->devkey_label) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "端末の鍵: %s", DeviceKeyFunctions::StateText());
    this->devkey_label->setText(buf);
    if(this->devkey_button){
        const DeviceKeyFunctions::State st = DeviceKeyFunctions::GetState();
        this->devkey_button->setText(st == DeviceKeyFunctions::State::None ? "作成" :
                                     st == DeviceKeyFunctions::State::Ready ? "更新" : "詳細");
    }
}

void SettingsScene::openDeviceKeyDialog(){
    using DeviceKeyFunctions::State;
    const State st = DeviceKeyFunctions::GetState();
    if(st == State::Broken){
        this->showMessageLater("OTPの端末の鍵が壊れていて、作り直す空きもありません。固定鍵のまま暗号化します");
        return;
    }
    const bool ready = (st == State::Ready);
    auto* dialog = ready
        ? new MsgDialog("SDの暗号化された設定のうち、まだ端末の鍵で暗号化していないものを暗号化し直します", "やめる", "実行")
        : new MsgDialog("端末の鍵を作り、本体のOTPへ書き込みます。\n"
                        "・OTPは消せません(取り消せません)\n"
                        "・Wi-Fi・SSHの鍵・トークン・カレンダーのURL・PINを、この本体の鍵で暗号化し直します\n"
                        "・基板が壊れると、それらは二度と読めません。元の値はPCに控えてください\n"
                        "・守れるのはSDだけを盗まれた場合です(本体ごとは守れません)", "やめる", "作る");
    dialog->setVisibleIcon(false);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, ready](bool ok){
        WidgetFunctions::DestroyLater(dialog);
        if(!ok) return;
        char msg[PICO_STR_256B];
        if(ready){
            const DeviceKeyFunctions::MigrateResult r = DeviceKeyFunctions::MigrateSecrets();
            snprintf(msg, sizeof(msg), "%d項目を暗号化し直しました%s%s", r.upgraded,
                     r.failed ? "(失敗あり。ログを見てください)" : "",
                     r.pin_pending ? "。PINは次に解除したとき" : "");
        }else{
            DeviceKeyFunctions::Provision(msg, sizeof(msg));
        }
        this->refreshDeviceKeyLabel();
        this->showMessageLater(msg);   //ダイアログからダイアログは1フレーム空ける
    });
}

void SettingsScene::updateSampleRate(){
    if(!this->rate_dropdown) return;
    const int idx = this->rate_dropdown->getSelectedIndex();
    if(idx < 0 || idx >= kRatePresetCount || idx == this->rate_selected_index) return;
    this->rate_selected_index = idx;
    //切り替えると鳴っている音は止まる。確認音で新しい周波数の音を聞かせる
    SoundFunctions::SetSampleRate(kRatePresets[idx]);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)kRatePresets[idx]);
    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "sample-rate", buf);
    SoundFunctions::Beep(880, 120);
}

void SettingsScene::updateRoute(){
    if(!this->route_dropdown) return;
    const int idx = this->route_dropdown->getSelectedIndex();
    if(idx >= 0 && idx < kRouteModeCount && idx != this->route_selected_index){
        this->route_selected_index = idx;
        SoundFunctions::SetRouteMode(kRouteModes[idx]);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "route", SoundFunctions::RouteModeName(kRouteModes[idx]));
        //新しい出力先の音量で確認音(つながっていなければ鳴らないだけ)
        SoundFunctions::Beep(880, 120);
    }
    this->refreshRouteStatus();
}

// 今どちらから鳴るか(イヤホンの抜き差しで変わる)。変わったときだけ書き直す
void SettingsScene::refreshRouteStatus(){
    if(!this->route_status_label) return;
    const SoundFunctions::Route route = SoundFunctions::GetRoute();
    const bool connected = SoundFunctions::IsConnected();
    const uint8_t shown = (uint8_t)(((uint8_t)route << 1) | (connected ? 1 : 0));
    if(shown == this->route_status_shown) return;
    this->route_status_shown = shown;
    const char* name = (route == SoundFunctions::Route::Headphone) ? "イヤホン" : "スピーカー";
    char buf[PICO_STR_256B];
    snprintf(buf, sizeof(buf), connected ? "今は%sから鳴ります" : "%sがつながっていません", name);
    this->route_status_label->setText(buf);
}

void SettingsScene::updateSleep(){
    if(!this->sleep_dropdown) return;
    const int idx = this->sleep_dropdown->getSelectedIndex();
    if(idx < 0 || idx >= kSleepPresetCount || idx == this->sleep_selected_index) return;
    this->sleep_selected_index = idx;

    const unsigned long sec = kSleepPresetsSec[idx];
    PowerFunctions::SetSleepTimeoutMs(sec * 1000UL);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", sec);
    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG, "sleep-timeout", buf);
}

void SettingsScene::updateTextSize(){
    if(!this->text_size_dropdown) return;
    const int idx = this->text_size_dropdown->getSelectedIndex();
    if(idx < 0 || idx > 1 || idx == this->text_size_selected_index) return;
    this->text_size_selected_index = idx;

    // 開いている画面は組み直さない(この設定画面の部品は従わないので見た目は変わらない)。
    // 読む所はシーンに入り直したときに新しい大きさで組む
    const bool large = (idx == 1);
    DisplayFunctions::SetLargeText(large);
    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG, "text-size", large ? "large" : "normal");
}

void SettingsScene::updateVolume(){
    if(!this->volume_slider) return;

    //鳴らす先が変わったら(イヤホンの抜き差し・出力先の切り替え)、その鳴らす先の音量を出し直す。
    //動かしている途中なら、指を離して保存するまで待つ
    if(!this->volume_dirty && SoundFunctions::GetRoute() != this->volume_route){
        this->volume_route = SoundFunctions::GetRoute();
        this->volume_applied = SoundFunctions::GetVolume();
        this->volume_slider->setValue((float)this->volume_applied);
    }

    const int v = (int)(this->volume_slider->getValue() + 0.5f);
    if(v != this->volume_applied){
        this->volume_applied = v;
        SoundFunctions::SetVolumeFor(this->volume_route, v);
        this->volume_dirty = true;
    }

    // 指を離したら(スライダーの外で離した場合も含む)保存して、その音量で確認音を1回鳴らす
    if(this->volume_dirty && !OSData::isTouched){
        this->volume_dirty = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", this->volume_applied);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, SoundFunctions::VolumeConfigKey(this->volume_route), buf);
        SoundFunctions::Beep(880, 120);
    }
}

void SettingsScene::updateBrightness(){
    if(!this->brightness_slider) return;

    const int v = (int)(this->brightness_slider->getValue() + 0.5f);
    if(v != this->brightness_applied){
        this->brightness_applied = v;
        DisplayFunctions::SetBrightness(v);
        this->brightness_dirty = true;
    }

    // 指を離したら(スライダーの外で離した場合も含む)display.cfgへ書く
    if(this->brightness_dirty && !OSData::isTouched){
        this->brightness_dirty = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", this->brightness_applied);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG, "brightness", buf);
    }
}

void SettingsScene::updateTimezone(){
    if(!this->timezone_dropdown) return;

    const int idx = this->timezone_dropdown->getSelectedIndex();
    if(idx < 0 || idx == this->tz_selected_index) return;
    if(idx >= this->tz_item_count) return;

    this->tz_selected_index = idx;

    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "timezone", this->tz_items[idx].c_str());
    TimeFunctions::ApplyTimezone(this->tz_items[idx].c_str());
}

void SettingsScene::onUpdate(){
    if(this->wifi_scan_task) PowerFunctions::KeepAwake(); // スキャン中はスリープさせない
    this->refreshLockLabel();   //ロック画面でPINを決めた/外した後に反映

    const Pending p = this->pending;
    this->pending = Pending::None;
    if(p == Pending::WifiPassword) this->openWifiPasswordDialog();
    else if(p == Pending::Message) this->openMessage();
    else if(p == Pending::ClockTime) this->openClockTimeDialog();
    else if(p == Pending::IpGateway) this->openIpGatewayDialog();
    else if(p == Pending::IpDns) this->openIpDnsDialog();
    else if(p == Pending::CaptiveInput) this->openCaptiveInputDialog();

    this->pollWifiScan();
    this->pollCaptiveLogin();
    if(this->tab == Tab::Wifi) this->refreshWifi();

    this->updateVolume();
    this->updateBrightness();
    this->updateSleep();
    this->updateTextSize();
    this->updateSampleRate();
    this->updateRoute();
    this->updateTimezone();
    this->revealOpenDropdown();
    this->refreshBatteryLabel();
    if(this->tab == Tab::Time) this->refreshClockLabel(false);
}

void SettingsScene::onExit(){
    // 離す前に画面を抜けた(Popされた)場合も、変えた音量は保存しておく
    if(this->volume_dirty){
        this->volume_dirty = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", this->volume_applied);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "volume", buf);
    }
    // 同じく明るさも離す前に画面を抜けたら保存しておく
    if(this->brightness_dirty){
        this->brightness_dirty = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", this->brightness_applied);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG, "brightness", buf);
    }

    // スキャンTaskの所有権はこちらにある(net/NetworkScan.hpp参照)ので、
    // 完了を待たずシーンごと抜けた場合はここで自分から後始末する
    if(this->wifi_scan_task){
        delete this->wifi_scan_task;
        this->wifi_scan_task = nullptr;
    }
    this->endCaptiveLogin();
    // ダイアログ層はシーン終了時にフレームワーク側がまとめて片付ける(CalendarScene::detail_dialogと同じ)
    this->wifi_scan_dialog = nullptr;
    this->pending = Pending::None;

    for(int t = 0; t < (int)Tab::Count; t++){
        for(int i = 0; i < kMaxTabWidgets; i++) this->tab_widgets[t][i] = nullptr;
        this->tab_widget_count[t] = 0;
        this->tab_scroll[t] = nullptr; //容器と中身はシーンの片付けが消す
    }

    this->back_button   = nullptr;
    this->power_button  = nullptr;
    this->tab_bar       = nullptr;
    this->battery_label = nullptr;

    this->wifi_enable_checkbox = nullptr;
    this->wifi_status_label    = nullptr;
    this->wifi_list_title      = nullptr;
    this->wifi_list            = nullptr;
    this->wifi_scan_button     = nullptr;
    this->wifi_add_button      = nullptr;
    this->wifi_connect_button  = nullptr;
    this->wifi_remove_button   = nullptr;
    this->wifi_ip_button       = nullptr;
    this->wifi_info_label      = nullptr;
    this->wifi_name_button     = nullptr;
    this->wifi_login_button    = nullptr;
    this->shown_internet       = 0xFF;
    this->shown_status         = 0xFF;
    this->wifi_list_dirty      = true;

    this->volume_title         = nullptr;
    this->volume_slider        = nullptr;
    this->brightness_title     = nullptr;
    this->brightness_slider    = nullptr;
    this->auto_dim_checkbox    = nullptr;
    this->sleep_title          = nullptr;
    this->sleep_dropdown       = nullptr;
    this->text_size_dropdown   = nullptr;
    this->rate_title           = nullptr;
    this->rate_dropdown        = nullptr;
    this->battery_cap_checkbox = nullptr;
    this->route_title          = nullptr;
    this->route_dropdown       = nullptr;
    this->route_status_label   = nullptr;
    this->route_status_shown   = 0xFF;
    this->dark_checkbox = nullptr;
    this->lock_label = nullptr;
    this->lock_button = nullptr;

    this->timezone_title       = nullptr;
    this->timezone_dropdown    = nullptr;
    this->ntp1_label           = nullptr;
    this->ntp1_edit_button     = nullptr;
    this->ntp2_label           = nullptr;
    this->ntp2_edit_button     = nullptr;
    this->clock_label          = nullptr;
    this->clock_edit_button    = nullptr;
    this->clock_note           = nullptr;
    this->clock_shown_min      = -1;
    this->launcher_label       = nullptr;
    this->launcher_button      = nullptr;

    this->home_label           = nullptr;
    this->home_edit_button     = nullptr;
    this->run_test_checkbox    = nullptr;
    this->perf_overlay_checkbox = nullptr;
    this->lua_debugger_checkbox = nullptr;
    this->watchdog_checkbox    = nullptr;

    this->edit_btn_x = 0;
    this->shown_open_dropdown = nullptr;
}

// ---------------------------------------------------------------------------
// 画面・音のスライダーと、最後に足す行(ドロップダウン)
// ---------------------------------------------------------------------------

void SettingsScene::buildLevelRows(const Layout& lay){
    const Rect content = lay.content;
    const int16_t left = lay.left;
    auto rowY = [&lay](int i) -> int16_t { return lay.rowY(i); };
    // =====================================================================
    // 画面・音のスライダー(各タブの1行目。行の幅いっぱいに置く)
    // =====================================================================
    constexpr int16_t kTitleW = 40;
    const int16_t slider_x = (int16_t)(left + kTitleW);
    const int16_t slider_w = (int16_t)(content.x + content.w - MARGIN - slider_x);

    if constexpr (!HardwareConfig::kSound){
        // 音声出力を載せない版(config/Hardware_Config.hpp)。音タブはその旨だけ出す
        auto* none = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(0) + 2), lay.full_w, "音声出力なしのファームです");
        none->setTextColor(PICO_DARKGREY);
        addToTab(Tab::Sound, none);
    }else{
        // ---- 音量(sound.cfgの volume)。音タブの1行目 ----
        // 現在値はSoundFunctionsが起動時にsound.cfgから読んだもの(=今鳴っている音量)を出す
        this->volume_title = new Label<PICO_STR_S>(left, (int16_t)(rowY(0) + 2), "音量");
        this->volume_title->setFontSize(FontFn::Small);
        addToTab(Tab::Sound, this->volume_title);

        this->volume_slider = new NumberSlider(slider_x, rowY(0), slider_w);
        this->volume_slider->setMinValue(0);
        this->volume_slider->setMaxValue(100);
        this->volume_slider->setDecimalPlacesNum(0);
        this->volume_route   = SoundFunctions::GetRoute();
        this->volume_applied = SoundFunctions::GetVolume();
        this->volume_dirty   = false;
        this->volume_slider->setValue((float)this->volume_applied);
        addToTab(Tab::Sound, this->volume_slider);
    }

    // ---- 画面の明るさ(display.cfgの brightness)。画面タブの1行目 ----
    this->brightness_title = new Label<PICO_STR_S>(left, (int16_t)(rowY(0) + 2), "輝度");
    this->brightness_title->setFontSize(FontFn::Small);
    addToTab(Tab::Display, this->brightness_title);

    this->brightness_slider = new NumberSlider(slider_x, rowY(0), slider_w);
    this->brightness_slider->setMinValue(DisplayFunctions::kMinBrightness);
    this->brightness_slider->setMaxValue(100);
    this->brightness_slider->setDecimalPlacesNum(0);
    this->brightness_applied = DisplayFunctions::GetBrightness();
    this->brightness_dirty   = false;
    this->brightness_slider->setValue((float)this->brightness_applied);
    addToTab(Tab::Display, this->brightness_slider);

    // ---- 自動調光(display.cfgの auto-dim)。画面タブの2行目 ----
    // setFontSize()は幅を測り直さないので、setText()で同じ文字列を渡し直す(他のチェックボックスと同じ)
    this->auto_dim_checkbox = new Checkbox(left, rowY(1), "しばらく触らないと暗くする");
    this->auto_dim_checkbox->setFontSize(FontFn::Small);
    this->auto_dim_checkbox->setText("しばらく触らないと暗くする");
    this->auto_dim_checkbox->setIsChecked(DisplayFunctions::GetAutoDimEnabled());
    this->auto_dim_checkbox->setOnChangeChecked([this](){
        const bool enabled = this->auto_dim_checkbox->getIsChecked();
        DisplayFunctions::SetAutoDimEnabled(enabled);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG, "auto-dim",
            PICO_Config::ConfigValue::FromBool(enabled));
    });
    addToTab(Tab::Display, this->auto_dim_checkbox);
}

void SettingsScene::buildLateRows(const Layout& lay){
    const Rect content = lay.content;
    const int16_t left = lay.left;
    const int full_w = lay.full_w;
    auto rowY = [&lay](int i) -> int16_t { return lay.rowY(i); };
    const int label_w = this->labelW(content);
    // =====================================================================
    // ドロップダウン(開いた一覧が下の行へ重なるので、当たり判定・描画の両方で最前面に来るよう
    // 他より後にAdd()する。追加順=描画順、後が上に乗る)
    // =====================================================================
    constexpr int16_t kDropdownW = 110;
    const int16_t dropdown_x = (int16_t)(content.x + content.w - MARGIN - kDropdownW);

    // ---- ダークテーマ(display.cfgの theme)。画面タブの3行目(明るさの仲間なので近くに) ----
    this->dark_checkbox = new Checkbox(left, rowY(2), "ダークテーマ");
    this->dark_checkbox->setFontSize(FontFn::Small);
    this->dark_checkbox->setText("ダークテーマ");
    this->dark_checkbox->setIsChecked(DisplayFunctions::IsDarkTheme());
    this->dark_checkbox->setOnChangeChecked([this](){
        const bool dark = this->dark_checkbox->getIsChecked();
        DisplayFunctions::SetDarkTheme(dark);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG, "theme", dark ? "dark" : "light");
    });
    addToTab(Tab::Display, this->dark_checkbox);

    // ---- 画面ロック(PIN)。画面タブの5行目 ----
    this->lock_button = this->makeEditButton(rowY(4));
    this->lock_button->setOnPressEnd([this](){ this->openLockDialog(); });
    addToTab(Tab::Display, this->lock_button);
    this->lock_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(4) + 2), label_w, "");
    addToTab(Tab::Display, this->lock_label);
    this->refreshLockLabel();

    // ---- 端末の鍵(OTP)。画面タブの7行目(画面ロックと同じく本体の守り) ----
    this->devkey_button = this->makeEditButton(rowY(6));
    this->devkey_button->setOnPressEnd([this](){ this->openDeviceKeyDialog(); });
    addToTab(Tab::Display, this->devkey_button);
    this->devkey_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(6) + 2), label_w, "");
    addToTab(Tab::Display, this->devkey_label);
    this->refreshDeviceKeyLabel();

    // 音タブの残り(音声出力を載せない版は出さない。config/Hardware_Config.hpp)
    if constexpr (HardwareConfig::kSound){
        // ---- 電池駆動中に音量を絞る(sound.cfgの battery-cap)。音タブの3行目(+下に説明) ----
        if constexpr (HardwareConfig::kBattery){
            // 音質のドロップダウンの一覧がこの行へ重なるので、それより先にAdd()する。
            // 電池を載せない版は常にUSB給電なので出さない
            this->battery_cap_checkbox = new Checkbox(left, rowY(2), "電池で音量を絞る");
            this->battery_cap_checkbox->setFontSize(FontFn::Small);
            this->battery_cap_checkbox->setText("電池で音量を絞る");
            this->battery_cap_checkbox->setIsChecked(SoundFunctions::GetBatteryCap());
            this->battery_cap_checkbox->setOnChangeChecked([this](){
                const bool enabled = this->battery_cap_checkbox->getIsChecked();
                SoundFunctions::SetBatteryCap(enabled);
                PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "battery-cap",
                    PICO_Config::ConfigValue::FromBool(enabled));
            });
            addToTab(Tab::Sound, this->battery_cap_checkbox);
            auto* cap_note = makeRowLabel<PICO_STR_L>(left,
                (int16_t)(rowY(2) + this->battery_cap_checkbox->getH() + 2), full_w, "電池では最大20%(スピーカー)");
            cap_note->setTextColor(PICO_DARKGREY);
            addToTab(Tab::Sound, cap_note);
        }

        // ---- 出力先(sound.cfgの route)。音タブの4行目(+下に今どちらから鳴るか) ----
        // 一覧は下へ開くので、上の行の音質のドロップダウンより先にAdd()する(音質の一覧がこの行へ重なる)。
        // 「スピーカー」が入るよう他より広くする(開いて下が切れるときはrevealOpenDropdown()がスクロールする)
        constexpr int16_t kRouteDropdownW = 140;
        //電池の行は下に説明が付くので、出力先はその分(半行)下げる
        const int16_t route_y = (int16_t)(rowY(3) + (HardwareConfig::kBattery ? ROW_H / 2 : 0));
        this->route_title = new Label<PICO_STR_S>(left, route_y + 4, "出力先");
        this->route_title->setFontSize(FontFn::Small);
        this->route_dropdown = new DropdownMenu((int16_t)(content.x + content.w - MARGIN - kRouteDropdownW), route_y, kRouteDropdownW);
        for(int i = 0; i < kRouteModeCount; i++) this->route_dropdown->add(kRouteModeLabels[i]);
        this->route_selected_index = 0;
        for(int i = 0; i < kRouteModeCount; i++) if(kRouteModes[i] == SoundFunctions::GetRouteMode()) this->route_selected_index = i;
        this->route_dropdown->setSelectedIndex(this->route_selected_index);
        this->route_status_label = makeRowLabel<PICO_STR_L>(left,
            (int16_t)(route_y + this->route_dropdown->getH() + 2), full_w, "");
        this->route_status_label->setTextColor(PICO_DARKGREY);
        this->refreshRouteStatus();
        addToTab(Tab::Sound, this->route_status_label);
        addToTab(Tab::Sound, this->route_title);
        addToTab(Tab::Sound, this->route_dropdown);

        // ---- 音の周波数(sound.cfgの sample-rate)。音タブの2行目 ----
        this->rate_title = new Label<PICO_STR_S>(left, rowY(1) + 4, "音質");
        this->rate_title->setFontSize(FontFn::Small);
        this->rate_dropdown = new DropdownMenu(dropdown_x, rowY(1), kDropdownW);
        for(int i = 0; i < kRatePresetCount; i++) this->rate_dropdown->add(kRatePresetLabels[i]);
        this->rate_selected_index = (SoundFunctions::ConfiguredSampleRate() == kRatePresets[1]) ? 1 : 0;
        this->rate_dropdown->setSelectedIndex(this->rate_selected_index);
        addToTab(Tab::Sound, this->rate_title);
        addToTab(Tab::Sound, this->rate_dropdown);
    }

    // ---- スリープまでの時間(display.cfgの sleep-timeout)。画面タブの4行目 ----
    // 一覧が下の画面ロック・タッチ補正の行へ重なるので、それらより後にAdd()している
    // 設定値が一覧のどれとも違う(display.cfgを手で書き換えた)ときは、いちばん近い項目を選んで見せる
    this->sleep_title = new Label<PICO_STR_S>(left, rowY(3) + 4, "スリープ");
    this->sleep_title->setFontSize(FontFn::Small);

    this->sleep_dropdown = new DropdownMenu(dropdown_x, rowY(3), kDropdownW);
    for(int i = 0; i < kSleepPresetCount; i++){
        this->sleep_dropdown->add(kSleepPresetLabels[i]);
    }
    const unsigned long cur_sec = PowerFunctions::GetSleepTimeoutMs() / 1000UL;
    int best = 0;
    unsigned long best_diff = (unsigned long)-1;
    for(int i = 0; i < kSleepPresetCount; i++){
        const unsigned long v = kSleepPresetsSec[i];
        const unsigned long d = (v > cur_sec) ? (v - cur_sec) : (cur_sec - v);
        if(d < best_diff){ best_diff = d; best = i; }
    }
    // 無効(0)は「一番近い」ではなく厳密に見る(0秒に近いのが1分になってしまうため)
    if(cur_sec == 0) best = 0;
    this->sleep_selected_index = best;
    this->sleep_dropdown->setSelectedIndex(best);
    addToTab(Tab::Display, this->sleep_title);
    addToTab(Tab::Display, this->sleep_dropdown);

    // ---- 本文の文字の大きさ(display.cfgの text-size)。画面タブの3行目、ダークテーマの右 ----
    // 一覧が下のスリープの行へ重なるので、スリープより後にAdd()する
    this->text_size_dropdown = new DropdownMenu(dropdown_x, rowY(2), kDropdownW);
    this->text_size_dropdown->add("標準の字");
    this->text_size_dropdown->add("大きい字");
    this->text_size_selected_index = DisplayFunctions::IsLargeText() ? 1 : 0;
    this->text_size_dropdown->setSelectedIndex(this->text_size_selected_index);
    addToTab(Tab::Display, this->text_size_dropdown);

    // ---- タイムゾーン。時刻の1行目 ----
    this->timezone_title = new Label<PICO_STR_M>(left, rowY(0) + 4, "タイムゾーン");
    this->timezone_title->setFontSize(FontFn::Small);

    this->timezone_dropdown = new DropdownMenu(dropdown_x, rowY(0), kDropdownW);
    for(int i = 0; i < this->tz_item_count; i++){
        this->timezone_dropdown->add(this->tz_items[i].c_str());
    }
    this->timezone_dropdown->setSelectedIndex(this->tz_selected_index);
    addToTab(Tab::Time, this->timezone_title);
    addToTab(Tab::Time, this->timezone_dropdown);
}
