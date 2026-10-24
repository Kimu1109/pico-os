#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "hal/Sound_Functions.hpp"
#include "net/Wifi_Profiles.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

class WifiScanDialog; // gui/widgets/dialogs/WifiScanDialog.hpp。ポインタ型でしか使わないので前方宣言で足りる
class NetworkScan;    // net/NetworkScan.hpp。同上

// 標準アプリの設定。画面下のタブで大まかな項目ごとに分けてある:
//   Wi-Fi   … ON/OFF・接続状態・保存済みのネットワークの一覧(接続/削除)・周辺の検索・手入力での追加
//   画面    … 明るさと自動調光・スリープ・ダークテーマ(display.cfg)・画面ロック(lock.cfg)・タッチ位置の補正(touch.cfg)
//   音      … 音量・音質(出力の周波数)・電池駆動中に音量を絞る(sound.cfg)
//   時刻    … タイムゾーン・NTPサーバー(network.cfg)・時刻の手動設定(Wi-Fiが無いとき用)
//   他      … ランチャで隠したアプリ・ブラウザのホーム(network.cfg)・バックグラウンドの一覧(BackgroundScene)・起動時の自己診断(user.cfg)・開発者向け(debug.cfg)
// 上の段の右端の「電源」はスリープ/再起動のメニュー(PowerMenuFunctions。ステータスバーの長押しと同じもの)
//
// ウィジェットは全タブぶんをonEnter()で作り、表示中のタブのものだけを見せる
// (ClocksSceneのapplyVisibility()と同じ考え方。タブごとの持ち物はtab_widgets[]に登録する)。
//
// 電卓/時計と違い「保存」ボタンは持たない。各操作(編集ダイアログの決定/チェックボックスの
// タップ/ドロップダウンの選択)ごとにその場で書き込む。保存済みのWi-Fiネットワークは
// net/Wifi_Profiles(/sys/wifi.cfg)が持ち、接続と自動再接続はNetworkFunctionsの仕事。
//
// テキスト項目の編集はMarkdownSceneの検索入力と同じ形: 押されるたびにInputDialogを
// newしてAddDialog()、閉じたらDestroyLater()で破棄する(1つを使い回さない)。
class SettingsScene : public Scene {
    private:
        enum class Tab : uint8_t { Wifi = 0, Display, Sound, Time, Other, Count };

        // 今どの項目をInputDialogで編集中か。ダイアログのonClosedから書き込み先を振り分ける
        enum class EditField : uint8_t {
            Ntp1,
            Ntp2,
            BrowserHome,
            WifiSsid,     // 「追加」: SSIDの手入力(決定後、1フレーム空けてパスワードを聞く)
            Hostname,     // 本体の名前(mDNSの <名前>.local)
        };

        // ダイアログを閉じた直後に次のダイアログを開くための保留(1フレーム空ける。
        // MarkdownScene::Pendingと同じ理由: TRANSLUCENTの下は同じフレームでは描き直されない)
        // IpAddress→IpGateway→IpDns は「IP」ボタンの3つの入力(固定IP・ゲートウェイ・DNS)
        enum class Pending : uint8_t { None, WifiPassword, Message, ClockTime, IpGateway, IpDns };

        Tab tab = Tab::Wifi; // onExit()を跨いで残す(上へ別のシーンを積んで戻ると同じタブ)

        // ---- タブごとのウィジェット(表示の切り替え用。所有はWidgetFunctions) ----
        static constexpr int kMaxTabWidgets = 16;
        Widget* tab_widgets[(int)Tab::Count][kMaxTabWidgets] = {};
        int     tab_widget_count[(int)Tab::Count] = {};
        // WidgetFunctions::Add()してタブへ登録する
        void addToTab(Tab t, Widget* w);
        void applyTab();

        Button*   back_button = nullptr;
        Button*   power_button = nullptr;
        TabBar*   tab_bar     = nullptr;

        // 戻るボタンと同じ行の右側に表示するバッテリー残量(全タブ共通)
        Label<PICO_STR_M>* battery_label = nullptr;
        void refreshBatteryLabel();

        // ================= Wi-Fi =================
        Checkbox*          wifi_enable_checkbox = nullptr;
        Label<PICO_STR_L>* wifi_status_label    = nullptr;
        Label<PICO_STR_M>* wifi_list_title      = nullptr;
        ScrollList*        wifi_list            = nullptr;
        Button*            wifi_scan_button     = nullptr;
        Button*            wifi_add_button      = nullptr;
        Button*            wifi_connect_button  = nullptr;
        Button*            wifi_remove_button   = nullptr;
        Button*            wifi_ip_button       = nullptr;
        // 本体の名前(<名前>.local)と今のIPアドレスの1行と、名前の[編集]
        Label<PICO_STR_L>* wifi_info_label      = nullptr;
        Button*            wifi_name_button     = nullptr;
        void refreshWifiInfo();

        // 状態の1行と一覧は、接続状態/接続先/一覧の中身が変わったときだけ作り直す
        uint8_t                 shown_status = 0xFF;
        FixedString<PICO_STR_M> shown_ssid;
        bool                    wifi_list_dirty = true;
        bool                    shown_enabled = true;
        void refreshWifi();
        void rebuildWifiList();
        int  selectedProfile();

        void connectSelected();
        void confirmRemoveSelected();

        // ---- 選んだ保存済みネットワークのIPの設定(固定IP→ゲートウェイ→DNSの順に聞く) ----
        FixedString<PICO_STR_M> pending_ip_ssid;   // 聞いている間に並びが変わってもよいようSSIDで覚える
        WifiProfiles::IpConfig pending_ip;         // 入力の途中の値
        void openIpAddressDialog();
        void openIpGatewayDialog();
        void openIpDnsDialog();
        void commitIpConfig();

        // ---- 周辺Wi-Fiのスキャン→選択→(未保存なら)パスワード入力→保存して接続 ----
        // ダイアログはSearchDialog(MarkdownScene)と同じく開くたびにnewし、
        // 閉じたらDestroyLater()する(使い回さない)
        WifiScanDialog* wifi_scan_dialog = nullptr;
        // スキャンTaskへの生ポインタ。所有権はこちらにある(net/NetworkScan.hppのコメントの通り、
        // HttpGet等と同じ「生ポインタとして持ち、毎フレーム自分でupdate()を呼び、
        // 終わったら自分でdeleteする」流儀)
        NetworkScan* wifi_scan_task = nullptr;

        Pending pending = Pending::None;
        FixedString<PICO_STR_M> pending_wifi_ssid;
        FixedString<PICO_STR_256B> pending_message;

        void startWifiScan();
        void pollWifiScan();
        void openWifiScanDialog();
        void closeWifiScanDialog();
        void openWifiPasswordDialog();
        // 保存済みの一覧へ入れて(同じSSIDならパスワードを書き換えて)接続する
        void saveAndConnect(const char* ssid, const char* password);
        // 次のフレームでMsgDialogを出す(ダイアログを閉じた直後に呼ばれることがあるため)
        void showMessageLater(const char* text);
        void openMessage();

        // ================= 本体(画面と音) =================
        Label<PICO_STR_S>* volume_title         = nullptr;
        NumberSlider*      volume_slider        = nullptr;
        // 音量はドラッグ中もSoundFunctions::SetVolume()で即座に反映し、sound.cfgへの
        // 書き込みと確認音は指を離したときに1回だけ行う(ドラッグの1フレームごとにSDへ書かないため)
        // 音量は鳴らす先(スピーカー/イヤホン)ごとに別。スライダーは今の鳴らす先の値を出し、
        // 鳴らす先が変わったら(イヤホンの抜き差し)触っていない間に出し直す
        int  volume_applied = -1;
        bool volume_dirty   = false;
        SoundFunctions::Route volume_route = SoundFunctions::Route::Speaker;  // スライダーが今出している鳴らす先

        Label<PICO_STR_S>* brightness_title     = nullptr;
        NumberSlider*      brightness_slider    = nullptr;
        Checkbox*          auto_dim_checkbox    = nullptr;
        // 明るさも音量と同じ流儀: ドラッグ中はDisplayFunctions::SetBrightness()で即反映し、
        // display.cfgへの書き込みは指を離したときに1回だけ行う
        int  brightness_applied = -1;
        bool brightness_dirty   = false;

        // スリープまでの時間(display.cfgの sleep-timeout)。項目の並びはSettingsScene.cppのkSleepPresetsSec
        Label<PICO_STR_S>* sleep_title          = nullptr;
        DropdownMenu*      sleep_dropdown       = nullptr;
        int                sleep_selected_index = -1; // onUpdate()での変化検出用

        // 音の周波数(sound.cfgの sample-rate)と、電池駆動中の音量の頭打ち(battery-cap)
        Label<PICO_STR_S>* rate_title           = nullptr;
        DropdownMenu*      rate_dropdown        = nullptr;
        int                rate_selected_index  = -1;
        Checkbox*          battery_cap_checkbox = nullptr;
        // 出力先(sound.cfgの route)と、今どちらから鳴るかの表示。項目の並びはSettingsScene.cppのkRouteModes
        Label<PICO_STR_S>* route_title          = nullptr;
        DropdownMenu*      route_dropdown       = nullptr;
        int                route_selected_index = -1;
        Label<PICO_STR_L>* route_status_label   = nullptr;
        uint8_t            route_status_shown   = 0xFF;    // 表示中の状態(refreshRouteStatus()が変化を見る)
        Checkbox*          dark_checkbox = nullptr;
        Label<PICO_STR_L>* lock_label    = nullptr;
        Button*            lock_button   = nullptr;
        // 端末の鍵(OTP。DeviceKeyFunctions)。作る/暗号化し直す
        Label<PICO_STR_L>* devkey_label  = nullptr;
        Button*            devkey_button = nullptr;
        void refreshDeviceKeyLabel();
        void openDeviceKeyDialog();

        // ================= 時刻 =================
        Label<PICO_STR_M>* timezone_title       = nullptr;
        DropdownMenu*      timezone_dropdown    = nullptr;

        Label<PICO_STR_L>* ntp1_label           = nullptr;
        Button*            ntp1_edit_button     = nullptr;

        Label<PICO_STR_L>* ntp2_label           = nullptr;
        Button*            ntp2_edit_button     = nullptr;

        // 時刻の手動設定。日付(PickerDialogのDate)→1フレーム空けて時刻(Time)の順に聞き、TimeFunctions::SetLocalDateTime()
        Label<PICO_STR_L>* clock_label          = nullptr;
        Button*            clock_edit_button    = nullptr;
        Label<PICO_STR_L>* clock_note           = nullptr;
        int                clock_shown_min      = -1;  // 表示している分(変わったら書き直す)
        int clock_year = 2026, clock_month = 1, clock_day = 1; // 日付のダイアログで選んだもの
        void refreshClockLabel(bool force);
        void openClockDateDialog();
        void openClockTimeDialog();

        // ================= その他 =================
        Label<PICO_STR_L>* home_label           = nullptr;
        Button*            home_edit_button     = nullptr;

        Checkbox*          run_test_checkbox    = nullptr;

        // ランチャで隠したアプリ(AppFunctions::SetHidden)。一覧から選んで表示し直す/並びを元に戻す
        Label<PICO_STR_L>* launcher_label       = nullptr;
        Button*            launcher_button      = nullptr;
        void refreshLauncherLabel();
        void openLauncherDialog();
        void openLockDialog();
        void refreshLockLabel();
        // 開発者向け(/sys/debug.cfg)
        Checkbox*          perf_overlay_checkbox = nullptr;
        Checkbox*          lua_debugger_checkbox = nullptr;
        Checkbox*          watchdog_checkbox    = nullptr;

        // network.cfg / user.cfgから読んだ現在値
        FixedString<PICO_STR_M>  ntp1_value;
        FixedString<PICO_STR_M>  ntp2_value;
        FixedString<PICO_STR_LL> home_value;

        // タイムゾーンのプリセット。設定ファイルの値がどれとも一致しない場合は
        // 末尾へその値自体を追加して選択する(独自設定を黒く上書きしないため)
        static constexpr int kMaxTzItems = 6;
        FixedString<PICO_STR_S> tz_items[kMaxTzItems];
        int tz_item_count     = 0;
        int tz_selected_index = -1; // onUpdate()での変化検出用

        constexpr static int MARGIN     = 3;
        constexpr static int ROW_H      = 34;
        constexpr static int EDIT_BTN_W = 48;
        constexpr static int EDIT_BTN_H = 20;
        constexpr static int TAB_H      = 28;

        int top_row_h = 0;
        int edit_btn_x = 0; // 全ての「編集」ボタンで共通のx(右端揃え)

        // network.cfg/user.cfgを読み直して各キャッシュ値とタイムゾーン一覧を更新する
        void loadValues();
        void loadTimezoneItems(const FixedString<PICO_STR_M>& current_tz);

        // 「編集」ボタン1個を作る共通処理(位置はyだけ渡せば揃う)
        Button* makeEditButton(int16_t y);
        // 小さい文字の1行ラベル
        template <size_t N>
        Label<N>* makeRowLabel(int16_t x, int16_t y, int max_w, const char* text);

        // 本文の行の置き方。onEnter() が決め、各タブの組み立て(build*)が使う
        struct Layout {
            Rect content;
            int16_t body_y = 0, body_bot = 0, left = 0;
            int full_w = 0;
            int16_t rowY(int i) const { return (int16_t)(body_y + i * ROW_H); }
        };
        // 編集ボタンの左までのラベルの幅(最初の makeEditButton() の後に決まる)
        int labelW(const Rect& content) const;
        // 追加順=描画順なので、onEnter() はこの順に呼ぶ
        void buildWifiTab(const Layout& lay);    // SettingsScene_Wifi.cpp
        void buildLevelRows(const Layout& lay);  // 音量・明るさ・自動調光
        void buildTimeRows(const Layout& lay);   // SettingsScene_Time.cpp(NTP・時刻・タッチ補正)
        void buildOtherTab(const Layout& lay);   // SettingsScene_Other.cpp
        void buildLateRows(const Layout& lay);   // ダークテーマ・ロック・端末の鍵・電池・ドロップダウン

        void refreshNtp1Label();
        void refreshNtp2Label();
        void refreshHomeLabel();
        void updateVolume();
        void updateBrightness();
        void updateSleep();
        void updateSampleRate();
        void updateRoute();
        void refreshRouteStatus();
        void updateTimezone();

        // InputDialogを1つnewして開く。閉じたらcommitEdit()へ渡してから破棄する
        void openEditDialog(EditField field, const char* label_text, const char* prefill);
        void commitEdit(EditField field, const FixedString<PICO_STR_LL>& input);

    public:
        const char* getName() const override { return "Settings"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};

template <size_t N>
inline Label<N>* SettingsScene::makeRowLabel(int16_t x, int16_t y, int max_w, const char* text){
    auto* l = new Label<N>(x, y, text);
    l->setFontSize(FontFn::Small);
    l->setMaxWidth(max_w);
    l->setMaxHeight(Label<N>::GetLineHeight(FontFn::Small));
    return l;
}
