// 設定アプリ(SettingsScene)の「Wi-Fi」タブ(保存済みのネットワーク・スキャン・接続)。画面の組み立ての全体は SettingsScene.cpp の onEnter()
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
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/WifiScanDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "net/Wifi_Profiles.hpp"
#include "net/NetworkScan.hpp"
#include "util/Net_Addr.hpp"

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
        this->refreshWifiInfo();
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
        if(p->ip.static_ip) item.text.append(" [固定IP]");
        else if(!p->ip.isDefault()) item.text.append(" [DNS]");
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

void SettingsScene::refreshWifiInfo(){
    if(!this->wifi_info_label) return;
    char ip[16];
    NetworkFunctions::CurrentAddress(ip, sizeof(ip));
    char buf[PICO_STR_L];
    // 2行: 本体の名前(mDNS)と今のIPアドレス(1行だと固定IPの長いアドレスが入りきらない)
    if(ip[0]) snprintf(buf, sizeof(buf), "%s.local\nIP %s", NetworkFunctions::hostname.c_str(), ip);
    else      snprintf(buf, sizeof(buf), "%s.local\nIP -", NetworkFunctions::hostname.c_str());
    this->wifi_info_label->setText(buf);
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

// ---------------------------------------------------------------------------
// 保存済みネットワークのIPの設定: 固定IP(空欄=DHCP)→ゲートウェイ→DNS の順に聞く。
// 1つ聞くたびにダイアログを閉じ、1フレーム空けて次を開く(Pending)
// ---------------------------------------------------------------------------

void SettingsScene::openIpAddressDialog(){
    const int idx = this->selectedProfile();
    const WifiProfiles::Profile* p = WifiProfiles::At(idx);
    if(!p){
        this->showMessageLater("IPを設定するネットワークを一覧から選んでください");
        return;
    }
    this->pending_ip_ssid.assign(p->ssid.c_str());
    this->pending_ip = p->ip;

    char prefill[24] = "";
    if(p->ip.static_ip){
        char a[16];
        NetAddr::Format(p->ip.ip, a, sizeof(a));
        snprintf(prefill, sizeof(prefill), "%s/%d", a, NetAddr::MaskToPrefix(p->ip.mask));
    }
    char label[PICO_STR_LL];
    snprintf(label, sizeof(label), "「%s」の固定IP(例 192.168.1.50/24。空欄でDHCP):", p->ssid.c_str());
    auto* dialog = new InputDialog(label, true);
    dialog->setAsciiInput(true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(prefill);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            const FixedString<PICO_STR_LL> input = dialog->getInput(); // 値で返るので受けておく
            const char* text = input.c_str();
            uint32_t ip = 0, mask = 0;
            bool blank = true;
            for(const char* c = text; *c; c++) if(*c != ' ') blank = false;
            if(blank){
                // DHCP。DNSだけ変えたいこともあるのでDNSは聞く
                this->pending_ip.static_ip = false;
                this->pending = Pending::IpDns;
            }else if(NetAddr::ParseCidr(text, ip, mask) && NetAddr::MaskToPrefix(mask) >= 8
                     && NetAddr::IsValidHostInSubnet(ip, mask)){
                // 前のゲートウェイが新しいサブネットに無ければ、そのサブネットの .1 を候補にする
                if(!this->pending_ip.static_ip || !NetAddr::SameSubnet(this->pending_ip.gateway, ip, mask)){
                    this->pending_ip.gateway = (ip & mask) | 1u;
                }
                this->pending_ip.static_ip = true;
                this->pending_ip.ip = ip;
                this->pending_ip.mask = mask;
                this->pending = Pending::IpGateway;
            }else{
                this->showMessageLater("IPアドレスは「192.168.1.50/24」の形で入れてください");
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::openIpGatewayDialog(){
    char prefill[16] = "";
    if(this->pending_ip.gateway) NetAddr::Format(this->pending_ip.gateway, prefill, sizeof(prefill));
    auto* dialog = new InputDialog("ゲートウェイ(ルーター)のIP(空欄で無し):", true);
    dialog->setAsciiInput(true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(prefill);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            int n = 0;
            uint32_t gw[1] = {0};
            if(NetAddr::ParseList(dialog->getInput().c_str(), gw, 1, n)
               && (n == 0 || (NetAddr::SameSubnet(gw[0], this->pending_ip.ip, this->pending_ip.mask)
                              && gw[0] != this->pending_ip.ip && NetAddr::IsUsableHost(gw[0])))){
                this->pending_ip.gateway = (n == 0) ? 0 : gw[0];
                this->pending = Pending::IpDns;
            }else{
                this->showMessageLater("ゲートウェイはIPアドレスと同じサブネットのアドレスにしてください");
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::openIpDnsDialog(){
    char prefill[40] = "";
    char a[16], b[16];
    if(this->pending_ip.dns[0]){
        NetAddr::Format(this->pending_ip.dns[0], a, sizeof(a));
        if(this->pending_ip.dns[1]){
            NetAddr::Format(this->pending_ip.dns[1], b, sizeof(b));
            snprintf(prefill, sizeof(prefill), "%s,%s", a, b);
        }else{
            snprintf(prefill, sizeof(prefill), "%s", a);
        }
    }
    auto* dialog = new InputDialog(this->pending_ip.static_ip
        ? "DNS(カンマ区切りで2つまで。空欄でゲートウェイ):"
        : "DNS(カンマ区切りで2つまで。空欄で自動):", true);
    dialog->setAsciiInput(true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(prefill);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            int n = 0;
            uint32_t list[WifiProfiles::kMaxDns] = {};
            bool ok = NetAddr::ParseList(dialog->getInput().c_str(), list, WifiProfiles::kMaxDns, n);
            for(int i = 0; ok && i < n; i++) ok = NetAddr::IsUsableHost(list[i]);
            if(ok){
                for(int i = 0; i < WifiProfiles::kMaxDns; i++) this->pending_ip.dns[i] = list[i];
                this->commitIpConfig();
            }else{
                this->showMessageLater("DNSは「1.1.1.1,8.8.8.8」のように2つまで入れてください");
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::commitIpConfig(){
    const int idx = WifiProfiles::Find(this->pending_ip_ssid.c_str());
    if(idx < 0){
        this->showMessageLater("ネットワークが削除されたので保存しませんでした");
        return;
    }
    if(!WifiProfiles::SetIpConfig(idx, this->pending_ip)){
        this->showMessageLater("IPの設定を保存できませんでした");
        return;
    }
    NetworkFunctions::ReapplyIpConfig(this->pending_ip_ssid.c_str()); // 繋いでいるなら繋ぎ直して反映
    this->wifi_list_dirty = true;
    this->showMessageLater(this->pending_ip.isDefault() ? "DHCPに戻しました" : "IPの設定を保存しました");
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

    // 所有権はこちらにある(net/NetworkScan.hppのコメント参照)ので、
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
    // 進行中のスキャンTaskの所有権はこちらにある(net/NetworkScan.hpp参照)ので、
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
    dialog->setAsciiInput(true); //英数字の欄(キーボードを英字で開く)
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
// 組み立て
// ---------------------------------------------------------------------------

void SettingsScene::buildWifiTab(const Layout& lay){
    const Rect content = lay.content;
    const int16_t left = lay.left;
    const int full_w = lay.full_w;
    const int16_t body_bot = lay.body_bot;
    auto rowY = [&lay](int i) -> int16_t { return lay.rowY(i); };
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

    // 下段のボタン: [検索][追加][接続][IP][削除]
    constexpr int kWifiButtons = 5;
    const int btn_w = (full_w - MARGIN * (kWifiButtons - 1)) / kWifiButtons;
    Button** buttons[kWifiButtons] = {
        &this->wifi_scan_button, &this->wifi_add_button, &this->wifi_connect_button, &this->wifi_ip_button,
        &this->wifi_remove_button
    };
    const char* button_texts[kWifiButtons] = { "検索", "追加", "接続", "IP", "削除" };
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
    this->wifi_ip_button->setOnPressEnd([this](){ this->openIpAddressDialog(); });

    // ボタンの上の1行: 「picoos.local  192.168.1.50」と、名前の[名前]ボタン
    this->wifi_name_button = new Button(0, 0, "名前");
    this->wifi_name_button->setFontSize(FontFn::Small);
    this->wifi_name_button->setAllowTextSpacing(false);
    this->wifi_name_button->setW(EDIT_BTN_W + Button::kFrameExtraTight);
    this->wifi_name_button->setH(EDIT_BTN_H + Button::kFrameExtraTight);
    const Rect name_box = this->wifi_name_button->getLocalRect();
    const int info_line_h = Label<PICO_STR_L>::GetLineHeight(FontFn::Small);
    const int16_t info_y = (int16_t)(buttons_y - MARGIN - info_line_h * 2);
    this->wifi_name_button->setX((int16_t)(left + full_w - name_box.w));
    this->wifi_name_button->setY((int16_t)(info_y + (info_line_h * 2 - name_box.h) / 2));
    this->wifi_name_button->setOnPressEnd([this](){
        this->openEditDialog(EditField::Hostname, "本体の名前(<名前>.local で見つかる。英数字とハイフン)",
                             NetworkFunctions::hostname.c_str());
    });
    addToTab(Tab::Wifi, this->wifi_name_button);
    this->wifi_info_label = makeRowLabel<PICO_STR_L>(left, info_y, full_w - name_box.w - MARGIN, "");
    this->wifi_info_label->setMaxHeight(info_line_h * 2);
    addToTab(Tab::Wifi, this->wifi_info_label);
    this->refreshWifiInfo();

    const int16_t list_y = (int16_t)(rowY(1) + Label<PICO_STR_M>::GetLineHeight(FontFn::Small) + MARGIN);
    this->wifi_list = new ScrollList(left, list_y, full_w, (int16_t)(info_y - MARGIN - list_y), WifiProfiles::kMaxProfiles);
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
}
