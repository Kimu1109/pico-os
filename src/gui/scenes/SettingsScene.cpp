#include "gui/scenes/SettingsScene.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Display_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/WifiScanDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "net/Wifi_Profiles.hpp"
#include "task/NetworkScan.hpp"

#include "functions/DevTools_Functions.hpp"
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
    b->setFontSize(FontFn::Small);
    b->setAllowTextSpacing(false);
    b->setW(EDIT_BTN_W + Button::kFrameExtraTight);
    b->setH(EDIT_BTN_H + Button::kFrameExtraTight);

    // 全行で同じ右端に揃えるので、xは最初の1回だけ実測して覚えておく
    if(this->edit_btn_x == 0){
        const Rect content = Scene::contentRect();
        const Rect box = b->getLocalRect();
        this->edit_btn_x = content.x + content.w - MARGIN - box.w;
    }
    b->setX(this->edit_btn_x);
    b->setY(y);
    return b;
}

template <size_t N>
Label<N>* SettingsScene::makeRowLabel(int16_t x, int16_t y, int max_w, const char* text){
    auto* l = new Label<N>(x, y, text);
    l->setFontSize(FontFn::Small);
    l->setMaxWidth(max_w);
    l->setMaxHeight(Label<N>::GetLineHeight(FontFn::Small));
    return l;
}

void SettingsScene::addToTab(Tab t, Widget* w){
    WidgetFunctions::Add(w);
    const int ti = (int)t;
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
        for(int i = 0; i < this->tab_widget_count[t]; i++) this->tab_widgets[t][i]->setVisible(false);
    }
    const int cur = (int)this->tab;
    for(int i = 0; i < this->tab_widget_count[cur]; i++) this->tab_widgets[cur][i]->setVisible(true);

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
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi: 状態の1行と保存済みのネットワークの一覧
// ---------------------------------------------------------------------------

void SettingsScene::refreshWifi(){
    if(!this->wifi_status_label) return;

    const auto status  = NetworkFunctions::currentStatus;
    const bool enabled = NetworkFunctions::IsEnabled();
    if((uint8_t)status != this->shown_status || !(this->shown_ssid == NetworkFunctions::currentSSID.c_str())
        || enabled != this->shown_enabled){
        this->shown_status = (uint8_t)status;
        this->shown_ssid.assign(NetworkFunctions::currentSSID.c_str());
        this->shown_enabled = enabled;
        this->wifi_list_dirty = true; // 「接続中」の印と並び(接続できたら先頭へ繰り上がる)が変わる

        if(this->wifi_enable_checkbox && this->wifi_enable_checkbox->getIsChecked() != enabled){
            this->wifi_enable_checkbox->setIsChecked(enabled);
        }

        char buf[PICO_STR_L];
        using NS = NetworkFunctions::NetStatus;
        switch(status){
            case NS::OFF:
                snprintf(buf, sizeof(buf), "OFF");
                break;
            case NS::SUCCESS:
                snprintf(buf, sizeof(buf), "接続中: %s", this->shown_ssid.c_str());
                break;
            case NS::TRYING_CONNECT:
                snprintf(buf, sizeof(buf), "接続しています: %s", this->shown_ssid.c_str());
                break;
            default:
                snprintf(buf, sizeof(buf), "%s",
                    WifiProfiles::Count() > 0 ? "未接続(自動で再接続します)" : "未接続");
                break;
        }
        this->wifi_status_label->setText(buf);
        this->wifi_status_label->setTextColor(status == NS::SUCCESS ? PICO_DARKGREEN : PICO_BLACK);
    }

    if(this->wifi_list_dirty) this->rebuildWifiList();
}

void SettingsScene::rebuildWifiList(){
    this->wifi_list_dirty = false;
    if(!this->wifi_list) return;

    // 選択はSSIDで覚えて作り直した後に戻す(接続できると並びが変わるため)
    FixedString<PICO_STR_M> selected_ssid;
    const int sel = this->selectedProfile();
    if(const WifiProfiles::Profile* p = WifiProfiles::At(sel)) selected_ssid.assign(p->ssid.c_str());

    this->wifi_list->clear();
    const auto status = NetworkFunctions::currentStatus;
    int new_sel = -1;
    for(int i = 0; i < WifiProfiles::Count(); i++){
        const WifiProfiles::Profile* p = WifiProfiles::At(i);
        ScrollListTools::Item item;
        item.icon = p->password.empty() ? IconID::WifiSignal4 : IconID::Lock;
        item.text.assign(p->ssid.c_str());
        if(p->ssid == NetworkFunctions::currentSSID.c_str()){
            if(status == NetworkFunctions::NetStatus::SUCCESS){
                item.text.append(" (接続中)");
                item.color = PICO_DARKGREEN;
            }else if(status == NetworkFunctions::NetStatus::TRYING_CONNECT){
                item.text.append(" (接続しています)");
            }
        }
        this->wifi_list->add(item);
        if(!selected_ssid.empty() && p->ssid == selected_ssid.c_str()) new_sel = i;
    }
    if(new_sel >= 0) this->wifi_list->setSelectedIndex(new_sel);
    else             this->wifi_list->clearSelectedIndex();

    char title[PICO_STR_M];
    snprintf(title, sizeof(title), "保存済み %d/%d (2回タップで接続)", WifiProfiles::Count(), WifiProfiles::kMaxProfiles);
    if(this->wifi_list_title) this->wifi_list_title->setText(title);
    this->wifi_list->needsRender();
}

int SettingsScene::selectedProfile(){
    if(!this->wifi_list) return -1;
    const int i = this->wifi_list->getSelectedIndex();
    return (i >= 0 && i < WifiProfiles::Count()) ? i : -1;
}

void SettingsScene::connectSelected(){
    const int idx = this->selectedProfile();
    if(idx < 0){
        this->showMessageLater("接続するネットワークを一覧から選んでください");
        return;
    }
    NetworkFunctions::ConnectProfile(idx);
    this->refreshWifi();
}

void SettingsScene::confirmRemoveSelected(){
    const int idx = this->selectedProfile();
    const WifiProfiles::Profile* p = WifiProfiles::At(idx);
    if(!p){
        this->showMessageLater("削除するネットワークを一覧から選んでください");
        return;
    }

    char msg[PICO_STR_LL];
    snprintf(msg, sizeof(msg), "「%s」を削除しますか?", p->ssid.c_str());
    auto* dialog = new MsgDialog(msg, "やめる", "削除");
    dialog->setVisibleIcon(false);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);

    // 確認中に並びが変わっても(接続できて先頭へ繰り上がる等)別の組を消さないよう、SSIDで覚えておく
    FixedString<PICO_STR_M> ssid;
    ssid.assign(p->ssid.c_str());
    dialog->setOnClosed([this, dialog, ssid](bool is_ok){
        if(is_ok){
            const int i = WifiProfiles::Find(ssid.c_str());
            if(i >= 0) NetworkFunctions::RemoveProfile(i);
            if(this->wifi_list) this->wifi_list->clearSelectedIndex();
            this->wifi_list_dirty = true;
            this->shown_status = 0xFF;
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::showMessageLater(const char* text){
    this->pending_message.assign(text);
    this->pending = Pending::Message;
}

void SettingsScene::openMessage(){
    auto* dialog = new MsgDialog(this->pending_message.c_str(), "閉じる", "OK");
    dialog->setVisibleIcon(false);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([dialog](bool){ WidgetFunctions::DestroyLater(dialog); });
}

// ---------------------------------------------------------------------------
// 周辺Wi-Fiのスキャン→選択→パスワード入力→保存して接続
// ---------------------------------------------------------------------------

void SettingsScene::startWifiScan(){
    if(this->wifi_scan_task) return; //既にスキャン中なら何もしない(再スキャンの二重起動防止)
    if(this->wifi_scan_dialog){
        this->wifi_scan_dialog->clearResults();
        this->wifi_scan_dialog->setMessage("スキャン中...");
    }
    this->wifi_scan_task = NetworkFunctions::ScanAsync();
}

void SettingsScene::pollWifiScan(){
    if(!this->wifi_scan_task) return;

    // 所有権はこちらにある(task/NetworkScan.hppのコメント参照)ので、
    // 毎フレーム自分でupdate()を呼んで進める
    this->wifi_scan_task->update();

    const TaskTools::Status s = this->wifi_scan_task->getStatus();
    if(s == TaskTools::PROCESSING) return;

    if(this->wifi_scan_dialog){
        if(s == TaskTools::SUCCESS){
            this->wifi_scan_dialog->clearResults();
            const int count = this->wifi_scan_task->getCount();
            for(int i = 0; i < count; i++){
                const NetworkScan::Result& r = this->wifi_scan_task->getResult(i);
                this->wifi_scan_dialog->addResult(r.ssid.c_str(), r.rssi);
            }
            char buf[32];
            if(count == 0){
                this->wifi_scan_dialog->setMessage("見つかりませんでした");
            }else{
                snprintf(buf, sizeof(buf), "%d件見つかりました", count);
                this->wifi_scan_dialog->setMessage(buf);
            }
        }else{
            this->wifi_scan_dialog->setMessage("スキャンに失敗しました");
        }
    }

    delete this->wifi_scan_task;
    this->wifi_scan_task = nullptr;
}

void SettingsScene::openWifiScanDialog(){
    if(!NetworkFunctions::IsEnabled()){
        this->showMessageLater("Wi-FiがOFFです。ONにしてから検索してください");
        return;
    }
    // SearchDialog(MarkdownScene)と同じく開くたびにnewし、閉じたらDestroyLater()する
    if(!this->wifi_scan_dialog){
        this->wifi_scan_dialog = new WifiScanDialog();
        WidgetFunctions::AddDialog(this->wifi_scan_dialog);

        this->wifi_scan_dialog->setOnSelect([this](const char* ssid){
            const int saved = WifiProfiles::Find(ssid);
            if(saved >= 0){
                // 保存済みなら覚えているパスワードでそのまま繋ぐ(パスワードを変えたいときは「追加」から入れ直す)
                NetworkFunctions::ConnectProfile(saved);
            }else{
                // ダイアログからダイアログは1フレーム空ける(MarkdownScene::Pendingと同じ理由)
                this->pending_wifi_ssid.assign(ssid);
                this->pending = Pending::WifiPassword;
            }
            this->closeWifiScanDialog();
        });
        this->wifi_scan_dialog->setOnRescan([this](){ this->startWifiScan(); });
        this->wifi_scan_dialog->setOnClosed([this](bool){ this->closeWifiScanDialog(); });

        this->wifi_scan_dialog->setVisible(true);
    }

    this->startWifiScan();
}

void SettingsScene::closeWifiScanDialog(){
    // 進行中のスキャンTaskの所有権はこちらにある(task/NetworkScan.hpp参照)ので、
    // 完了を待たずに閉じる場合はここで自分から後始末する
    if(this->wifi_scan_task){
        delete this->wifi_scan_task;
        this->wifi_scan_task = nullptr;
    }

    if(!this->wifi_scan_dialog) return;
    this->wifi_scan_dialog->setVisible(false);
    WidgetFunctions::DestroyLater(this->wifi_scan_dialog);
    this->wifi_scan_dialog = nullptr;
}

void SettingsScene::openWifiPasswordDialog(){
    // ラベル文字列(日本語)+SSID(最大32B)を余裕を持って収める。
    // 表示側はどのみちInputDialogのLabel<PICO_STR_L>(96B)で切り詰まる
    char label[PICO_STR_LL];
    snprintf(label, sizeof(label), "「%s」のパスワード(不要なら空欄のまま決定):",
              this->pending_wifi_ssid.c_str());

    auto* dialog = new InputDialog(label, true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            this->saveAndConnect(this->pending_wifi_ssid.c_str(), dialog->getInput().c_str());
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::saveAndConnect(const char* ssid, const char* password){
    switch(WifiProfiles::Put(ssid, password)){
        case WifiProfiles::PutResult::Added:
        case WifiProfiles::PutResult::Updated:
            break;
        case WifiProfiles::PutResult::Full: {
            char msg[PICO_STR_L];
            snprintf(msg, sizeof(msg), "保存できるのは%d件までです。不要なものを削除してください",
                WifiProfiles::kMaxProfiles);
            this->showMessageLater(msg);
            return;
        }
        case WifiProfiles::PutResult::Invalid:
            this->showMessageLater("SSIDかパスワードが長すぎます(パスワードは64文字まで)");
            return;
    }

    NetworkFunctions::ConnectWiFiAsync(ssid, password);
    this->wifi_list_dirty = true;
    this->refreshWifi();
}

// ---------------------------------------------------------------------------
// 画面の組み立て
// ---------------------------------------------------------------------------

void SettingsScene::onEnter(){
    const Rect content = Scene::contentRect();

    for(int t = 0; t < (int)Tab::Count; t++) this->tab_widget_count[t] = 0;

    // ---- 上部: [戻る] + バッテリー残量(全タブ共通) ----
    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    const int16_t battery_x = (int16_t)(content.x + MARGIN + back_box.w + MARGIN);
    const int16_t battery_w = (int16_t)(content.x + content.w - MARGIN - battery_x);
    this->battery_label = makeRowLabel<PICO_STR_M>(battery_x, content.y + MARGIN, battery_w, "");
    WidgetFunctions::Add(this->battery_label);
    this->refreshBatteryLabel();

    // ---- 下部: タブ ----
    const int16_t tab_y = (int16_t)(content.y + content.h - MARGIN - TAB_H);
    this->tab_bar = new TabBar(content.x + MARGIN, tab_y, content.w - MARGIN * 2, TAB_H);
    this->tab_bar->addTab("Wi-Fi");
    this->tab_bar->addTab("本体");
    this->tab_bar->addTab("時刻");
    this->tab_bar->addTab("その他");
    this->tab_bar->setSelected((int)this->tab);
    this->tab_bar->setOnChanged([this](int index){
        this->tab = (Tab)index;
        this->applyTab();
    });
    WidgetFunctions::Add(this->tab_bar);

    const int16_t body_y   = (int16_t)(content.y + MARGIN + this->top_row_h + MARGIN);
    const int16_t body_bot = (int16_t)(tab_y - MARGIN);
    auto rowY = [&](int i) -> int16_t { return (int16_t)(body_y + i * ROW_H); };
    const int16_t left = (int16_t)(content.x + MARGIN);
    const int full_w = content.w - MARGIN * 2;

    // 起動時セルフチェックはloadValues()がチェック状態を直接流し込むので、読み込みより前に生成しておく
    this->run_test_checkbox = new Checkbox(left, rowY(1), "起動時に自己診断を実行");
    this->run_test_checkbox->setFontSize(FontFn::Small);
    this->run_test_checkbox->setOnChangeChecked([this](){
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_USER_CFG, "run-test",
            PICO_Config::ConfigValue::FromBool(this->run_test_checkbox->getIsChecked()));
    });

    this->loadValues();

    // =====================================================================
    // Wi-Fi
    // =====================================================================
    // [■ Wi-Fi]  接続中: home-ap
    this->wifi_enable_checkbox = new Checkbox(left, rowY(0), "Wi-Fi");
    this->wifi_enable_checkbox->setFontSize(FontFn::Small);
    this->wifi_enable_checkbox->setText("Wi-Fi"); // 小さいフォントで幅を測り直す(Checkboxの幅は構築時のフォントで決まるため)
    this->wifi_enable_checkbox->setIsChecked(NetworkFunctions::IsEnabled());
    this->wifi_enable_checkbox->setOnChangeChecked([this](){
        NetworkFunctions::SetEnabled(this->wifi_enable_checkbox->getIsChecked());
        this->refreshWifi();
    });
    addToTab(Tab::Wifi, this->wifi_enable_checkbox);

    const Rect cb_box = this->wifi_enable_checkbox->getLocalRect();
    const int16_t status_x = (int16_t)(cb_box.x + cb_box.w + MARGIN * 2);
    this->wifi_status_label = makeRowLabel<PICO_STR_L>(
        status_x, (int16_t)(rowY(0) + (cb_box.h - Label<PICO_STR_L>::GetLineHeight(FontFn::Small)) / 2),
        content.x + content.w - MARGIN - status_x, "");
    addToTab(Tab::Wifi, this->wifi_status_label);

    this->wifi_list_title = makeRowLabel<PICO_STR_M>(left, rowY(1), full_w, "");
    this->wifi_list_title->setTextColor(PICO_DARKGREY);
    addToTab(Tab::Wifi, this->wifi_list_title);

    // 下段のボタン: [検索][追加][接続][削除]
    constexpr int kWifiButtons = 4;
    const int btn_w = (full_w - MARGIN * (kWifiButtons - 1)) / kWifiButtons;
    Button** buttons[kWifiButtons] = {
        &this->wifi_scan_button, &this->wifi_add_button, &this->wifi_connect_button, &this->wifi_remove_button
    };
    const char* button_texts[kWifiButtons] = { "検索", "追加", "接続", "削除" };
    int16_t buttons_y = 0;
    for(int i = 0; i < kWifiButtons; i++){
        Button* b = new Button(0, 0, button_texts[i]);
        b->setFontSize(FontFn::Small);
        b->setAllowTextSpacing(false);
        b->setW(btn_w + Button::kFrameExtraTight);
        b->setH(EDIT_BTN_H + Button::kFrameExtraTight);
        const Rect box = b->getLocalRect();
        buttons_y = (int16_t)(body_bot - box.h);
        b->setX((int16_t)(left + i * (btn_w + MARGIN)));
        b->setY(buttons_y);
        *buttons[i] = b;
        addToTab(Tab::Wifi, b);
    }
    this->wifi_scan_button->setOnPressEnd([this](){ this->openWifiScanDialog(); });
    this->wifi_add_button->setOnPressEnd([this](){
        this->openEditDialog(EditField::WifiSsid, "追加するWi-FiのSSID", "");
    });
    this->wifi_connect_button->setOnPressEnd([this](){ this->connectSelected(); });
    this->wifi_remove_button->setOnPressEnd([this](){ this->confirmRemoveSelected(); });

    const int16_t list_y = (int16_t)(rowY(1) + Label<PICO_STR_M>::GetLineHeight(FontFn::Small) + MARGIN);
    this->wifi_list = new ScrollList(left, list_y, full_w, (int16_t)(buttons_y - MARGIN - list_y), WifiProfiles::kMaxProfiles);
    this->wifi_list->setFontSize(FontFn::Small);
    this->wifi_list->setEnableIcon(true);
    this->wifi_list->setOnSelectItem([this](int index, bool already_selected){
        // 1回目のタップで選択、2回目で接続(ScrollListの流儀)
        if(already_selected && index >= 0 && index < WifiProfiles::Count()){
            NetworkFunctions::ConnectProfile(index);
            this->refreshWifi();
        }
    });
    addToTab(Tab::Wifi, this->wifi_list);

    // =====================================================================
    // 本体(画面と音)
    // =====================================================================
    constexpr int16_t kTitleW = 40;
    const int16_t slider_x = (int16_t)(left + kTitleW);

    // ---- 音量(sound.cfgの volume) ----
    // 現在値はSoundFunctionsが起動時にsound.cfgから読んだもの(=今鳴っている音量)を出す
    this->volume_title = new Label<PICO_STR_S>(left, (int16_t)(rowY(0) + 2), "音量");
    this->volume_title->setFontSize(FontFn::Small);
    addToTab(Tab::Device, this->volume_title);

    this->volume_slider = new NumberSlider(slider_x, rowY(0), (int16_t)(content.x + content.w - MARGIN - slider_x));
    this->volume_slider->setMinValue(0);
    this->volume_slider->setMaxValue(100);
    this->volume_slider->setDecimalPlacesNum(0);
    this->volume_applied = SoundFunctions::GetVolume();
    this->volume_dirty   = false;
    this->volume_slider->setValue((float)this->volume_applied);
    addToTab(Tab::Device, this->volume_slider);

    // ---- 画面の明るさ(display.cfgの brightness)+ 自動調光(auto-dim) ----
    this->brightness_title = new Label<PICO_STR_S>(left, (int16_t)(rowY(1) + 2), "輝度");
    this->brightness_title->setFontSize(FontFn::Small);
    addToTab(Tab::Device, this->brightness_title);

    // 自動調光のチェックボックスは幅がテキストから自動計算されるため、先に作って実測してから
    // 明るさスライダーの幅をその手前までに詰める。setFontSize()自体は再計算しない
    // (l_rect.wは構築時のフォントのまま)ので、setText()で同じ文字列を渡し直して計算をやり直させる
    this->auto_dim_checkbox = new Checkbox(0, rowY(1), "自動調光");
    this->auto_dim_checkbox->setFontSize(FontFn::Small);
    this->auto_dim_checkbox->setText("自動調光");
    const int16_t auto_dim_w = this->auto_dim_checkbox->getLocalRect().w;
    const int16_t auto_dim_x = (int16_t)(content.x + content.w - MARGIN - auto_dim_w);
    this->auto_dim_checkbox->setX(auto_dim_x);
    this->auto_dim_checkbox->setIsChecked(DisplayFunctions::GetAutoDimEnabled());
    this->auto_dim_checkbox->setOnChangeChecked([this](){
        const bool enabled = this->auto_dim_checkbox->getIsChecked();
        DisplayFunctions::SetAutoDimEnabled(enabled);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_DISPLAY_CFG, "auto-dim",
            PICO_Config::ConfigValue::FromBool(enabled));
    });
    addToTab(Tab::Device, this->auto_dim_checkbox);

    constexpr int16_t kBrightnessGap = 4;
    const int16_t brightness_slider_w = (int16_t)(auto_dim_x - kBrightnessGap - slider_x);
    this->brightness_slider = new NumberSlider(slider_x, rowY(1), brightness_slider_w);
    this->brightness_slider->setMinValue(DisplayFunctions::kMinBrightness);
    this->brightness_slider->setMaxValue(100);
    this->brightness_slider->setDecimalPlacesNum(0);
    this->brightness_applied = DisplayFunctions::GetBrightness();
    this->brightness_dirty   = false;
    this->brightness_slider->setValue((float)this->brightness_applied);
    addToTab(Tab::Device, this->brightness_slider);

    // =====================================================================
    // 時刻(タイムゾーンのドロップダウンは一覧が下の行へ重なるので、最後にAdd()する)
    // =====================================================================
    this->ntp1_edit_button = this->makeEditButton(rowY(1));
    this->ntp1_edit_button->setOnPressEnd([this](){
        this->openEditDialog(EditField::Ntp1, "NTPサーバー1", this->ntp1_value.c_str());
    });
    addToTab(Tab::Time, this->ntp1_edit_button);

    // 編集ボタンの実測が済んだので、以降の行のラベル幅はこれで揃える
    const int label_w = this->edit_btn_x - content.x - MARGIN * 2;

    this->ntp1_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(1) + 2), label_w, "");
    addToTab(Tab::Time, this->ntp1_label);
    this->refreshNtp1Label();

    this->ntp2_edit_button = this->makeEditButton(rowY(2));
    this->ntp2_edit_button->setOnPressEnd([this](){
        this->openEditDialog(EditField::Ntp2, "NTPサーバー2", this->ntp2_value.c_str());
    });
    addToTab(Tab::Time, this->ntp2_edit_button);

    this->ntp2_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(2) + 2), label_w, "");
    addToTab(Tab::Time, this->ntp2_label);
    this->refreshNtp2Label();

    // =====================================================================
    // その他
    // =====================================================================
    this->home_edit_button = this->makeEditButton(rowY(0));
    this->home_edit_button->setOnPressEnd([this](){
        // 空欄で決定すると同梱サンプル文書に戻る(commitEdit()参照)
        this->openEditDialog(EditField::BrowserHome, "ブラウザのホームURL", this->home_value.c_str());
    });
    addToTab(Tab::Other, this->home_edit_button);

    this->home_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(0) + 2), label_w, "");
    addToTab(Tab::Other, this->home_label);
    this->refreshHomeLabel();

    // 起動時セルフチェック(本体はloadValues()より前で生成済み)
    addToTab(Tab::Other, this->run_test_checkbox);

    this->run_test_note = makeRowLabel<PICO_STR_M>(left,
        (int16_t)(rowY(1) + this->run_test_checkbox->getH() + 2), full_w, "次回の起動から反映されます");
    this->run_test_note->setTextColor(PICO_DARKGREY);
    addToTab(Tab::Other, this->run_test_note);

    // 開発者向け(/sys/debug.cfg。DevToolsFunctions)
    auto makeDevCheckbox = [&](int row, const char* text, bool checked) -> Checkbox* {
        Checkbox* cb = new Checkbox(left, rowY(row), text);
        cb->setFontSize(FontFn::Small);
        cb->setText(text); // 小さいフォントで幅を測り直す(Checkboxの幅は構築時のフォントで決まるため)
        cb->setIsChecked(checked);
        addToTab(Tab::Other, cb);
        return cb;
    };
    this->perf_overlay_checkbox = makeDevCheckbox(3, "フレーム時間を表示", DevToolsFunctions::PerfOverlay());
    this->perf_overlay_checkbox->setOnChangeChecked([this](){
        DevToolsFunctions::SetPerfOverlay(this->perf_overlay_checkbox->getIsChecked());
    });
    this->lua_debugger_checkbox = makeDevCheckbox(4, "Luaデバッガ", DevToolsFunctions::LuaDebuggerEnabled());
    this->lua_debugger_checkbox->setOnChangeChecked([this](){
        // 次に開いたLuaアプリから効く(今動いているLuaEngineには後から付けない)
        DevToolsFunctions::SetLuaDebugger(this->lua_debugger_checkbox->getIsChecked());
    });
    this->watchdog_checkbox = makeDevCheckbox(5, "固まったら再起動する", DevToolsFunctions::Watchdog());
    this->watchdog_checkbox->setOnChangeChecked([this](){
        DevToolsFunctions::SetWatchdog(this->watchdog_checkbox->getIsChecked());
    });

    // =====================================================================
    // ドロップダウン(開いた一覧が下の行へ重なるので、当たり判定・描画の両方で最前面に来るよう
    // 他より後にAdd()する。追加順=描画順、後が上に乗る)
    // =====================================================================
    constexpr int16_t kDropdownW = 110;
    const int16_t dropdown_x = (int16_t)(content.x + content.w - MARGIN - kDropdownW);

    // ---- 電池駆動中に音量を絞る(sound.cfgの battery-cap)。本体タブの5行目 ----
    // 上の2つのドロップダウンの一覧がこの行へ重なるので、それより先にAdd()する
    this->battery_cap_checkbox = new Checkbox(left, rowY(4), "電池で音量を絞る");
    this->battery_cap_checkbox->setFontSize(FontFn::Small);
    this->battery_cap_checkbox->setText("電池で音量を絞る");
    this->battery_cap_checkbox->setIsChecked(SoundFunctions::GetBatteryCap());
    this->battery_cap_checkbox->setOnChangeChecked([this](){
        const bool enabled = this->battery_cap_checkbox->getIsChecked();
        SoundFunctions::SetBatteryCap(enabled);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "battery-cap",
            PICO_Config::ConfigValue::FromBool(enabled));
    });
    addToTab(Tab::Device, this->battery_cap_checkbox);

    // ---- 音の周波数(sound.cfgの sample-rate)。本体タブの4行目 ----
    // スリープのドロップダウンの一覧がこの行へ重なるので、こちらを先にAdd()する
    this->rate_title = new Label<PICO_STR_S>(left, rowY(3) + 4, "音質");
    this->rate_title->setFontSize(FontFn::Small);
    this->rate_dropdown = new DropdownMenu(dropdown_x, rowY(3), kDropdownW);
    for(int i = 0; i < kRatePresetCount; i++) this->rate_dropdown->add(kRatePresetLabels[i]);
    this->rate_selected_index = (SoundFunctions::SampleRate() == kRatePresets[1]) ? 1 : 0;
    this->rate_dropdown->setSelectedIndex(this->rate_selected_index);
    addToTab(Tab::Device, this->rate_title);
    addToTab(Tab::Device, this->rate_dropdown);

    // ---- スリープまでの時間(display.cfgの sleep-timeout)。本体タブの3行目 ----
    // 設定値が一覧のどれとも違う(display.cfgを手で書き換えた)ときは、いちばん近い項目を選んで見せる
    this->sleep_title = new Label<PICO_STR_S>(left, rowY(2) + 4, "スリープ");
    this->sleep_title->setFontSize(FontFn::Small);

    this->sleep_dropdown = new DropdownMenu(dropdown_x, rowY(2), kDropdownW);
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
    addToTab(Tab::Device, this->sleep_title);
    addToTab(Tab::Device, this->sleep_dropdown);

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

    this->applyTab();
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

void SettingsScene::updateVolume(){
    if(!this->volume_slider) return;

    const int v = (int)(this->volume_slider->getValue() + 0.5f);
    if(v != this->volume_applied){
        this->volume_applied = v;
        SoundFunctions::SetVolume(v);
        this->volume_dirty = true;
    }

    // 指を離したら(スライダーの外で離した場合も含む)保存して、その音量で確認音を1回鳴らす
    if(this->volume_dirty && !OSData::isTouched){
        this->volume_dirty = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", this->volume_applied);
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_SOUND_CFG, "volume", buf);
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

    const Pending p = this->pending;
    this->pending = Pending::None;
    if(p == Pending::WifiPassword) this->openWifiPasswordDialog();
    else if(p == Pending::Message) this->openMessage();

    this->pollWifiScan();
    if(this->tab == Tab::Wifi) this->refreshWifi();

    this->updateVolume();
    this->updateBrightness();
    this->updateSleep();
    this->updateSampleRate();
    this->updateTimezone();
    this->refreshBatteryLabel();
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

    // スキャンTaskの所有権はこちらにある(task/NetworkScan.hpp参照)ので、
    // 完了を待たずシーンごと抜けた場合はここで自分から後始末する
    if(this->wifi_scan_task){
        delete this->wifi_scan_task;
        this->wifi_scan_task = nullptr;
    }
    // ダイアログ層はシーン終了時にフレームワーク側がまとめて片付ける(CalendarScene::detail_dialogと同じ)
    this->wifi_scan_dialog = nullptr;
    this->pending = Pending::None;

    for(int t = 0; t < (int)Tab::Count; t++){
        for(int i = 0; i < kMaxTabWidgets; i++) this->tab_widgets[t][i] = nullptr;
        this->tab_widget_count[t] = 0;
    }

    this->back_button   = nullptr;
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
    this->shown_status         = 0xFF;
    this->wifi_list_dirty      = true;

    this->volume_title         = nullptr;
    this->volume_slider        = nullptr;
    this->brightness_title     = nullptr;
    this->brightness_slider    = nullptr;
    this->auto_dim_checkbox    = nullptr;
    this->sleep_title          = nullptr;
    this->sleep_dropdown       = nullptr;
    this->rate_title           = nullptr;
    this->rate_dropdown        = nullptr;
    this->battery_cap_checkbox = nullptr;

    this->timezone_title       = nullptr;
    this->timezone_dropdown    = nullptr;
    this->ntp1_label           = nullptr;
    this->ntp1_edit_button     = nullptr;
    this->ntp2_label           = nullptr;
    this->ntp2_edit_button     = nullptr;

    this->home_label           = nullptr;
    this->home_edit_button     = nullptr;
    this->run_test_checkbox    = nullptr;
    this->run_test_note        = nullptr;
    this->perf_overlay_checkbox = nullptr;
    this->lua_debugger_checkbox = nullptr;
    this->watchdog_checkbox    = nullptr;

    this->edit_btn_x = 0;
}
