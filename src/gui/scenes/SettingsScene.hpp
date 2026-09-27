#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

class WifiScanDialog; // gui/widgets/dialogs/WifiScanDialog.hpp。ポインタ型でしか使わないので前方宣言で足りる
class NetworkScan;    // task/NetworkScan.hpp。同上

// 標準アプリの設定。Wi-Fi/時刻/ブラウザのホーム(network.cfg)と
// 音量(sound.cfg)と起動時セルフチェック(user.cfg)を1画面のフォームで編集する。
//
// 電卓/時計と違い「保存」ボタンは持たない。各行の操作(編集ダイアログの決定/
// チェックボックスのタップ/タイムゾーンの選択)ごとにConfig_Functions::SetValue()で
// その場で書き込む(Config_Functions::SetValue()自体はここが初めての実利用箇所)。
//
// テキスト項目の編集はMarkdownSceneの検索入力と同じ形: 押されるたびにInputDialogを
// newしてAddDialog()、閉じたらDestroyLater()で破棄する(1つを使い回さない)。
class SettingsScene : public Scene {
    private:
        // 今どの項目をInputDialogで編集中か。ダイアログのonClosedから
        // 書き込み先を振り分けるために使う
        enum class EditField : uint8_t {
            Ssid,
            Password,
            Ntp1,
            Ntp2,
            BrowserHome
        };

        Button* back_button = nullptr;

        Label<PICO_STR_L>* ssid_label           = nullptr;
        Button*            ssid_edit_button     = nullptr;
        Button*            wifi_scan_button     = nullptr;

        Label<PICO_STR_M>* password_label       = nullptr;
        Button*            password_edit_button = nullptr;

        Label<PICO_STR_M>* timezone_title       = nullptr;
        DropdownMenu*      timezone_dropdown    = nullptr;

        Label<PICO_STR_L>* ntp1_label           = nullptr;
        Button*            ntp1_edit_button     = nullptr;

        Label<PICO_STR_L>* ntp2_label           = nullptr;
        Button*            ntp2_edit_button     = nullptr;

        Label<PICO_STR_L>* home_label           = nullptr;
        Button*            home_edit_button     = nullptr;

        Label<PICO_STR_S>* volume_title         = nullptr;
        NumberSlider*      volume_slider        = nullptr;
        // 音量はドラッグ中もSoundFunctions::SetVolume()で即座に反映し、sound.cfgへの
        // 書き込みと確認音は指を離したときに1回だけ行う(ドラッグの1フレームごとにSDへ書かないため)
        int  volume_applied = -1;
        bool volume_dirty   = false;

        Label<PICO_STR_S>* brightness_title     = nullptr;
        NumberSlider*      brightness_slider    = nullptr;
        Checkbox*          auto_dim_checkbox    = nullptr;
        // 明るさも音量と同じ流儀: ドラッグ中はDisplayFunctions::SetBrightness()で即反映し、
        // display.cfgへの書き込みは指を離したときに1回だけ行う
        int  brightness_applied = -1;
        bool brightness_dirty   = false;

        Checkbox*          run_test_checkbox    = nullptr;
        Label<PICO_STR_M>* run_test_note        = nullptr;

        // ---- 周辺Wi-Fiのスキャン→選択→パスワード入力→接続 ----
        // ダイアログはSearchDialog(MarkdownScene)と同じく開くたびにnewし、
        // 閉じたらDestroyLater()する(使い回さない)
        WifiScanDialog* wifi_scan_dialog = nullptr;
        // スキャンTaskへの生ポインタ。所有権はこちらにある(task/NetworkScan.hppの
        // コメントの通り、HttpGet等と同じ「値/生ポインタとして持ち、毎フレーム
        // 自分でupdate()を呼び、終わったら自分でdeleteする」流儀)
        NetworkScan* wifi_scan_task = nullptr;
        // ダイアログでSSIDを選んだ直後、パスワード入力ダイアログを1フレーム空けて開くための保留
        // (MarkdownScene::Pending/CalendarSceneの「追加」フローと同じ理由)
        bool pending_wifi_password_dialog = false;
        FixedString<PICO_STR_M> pending_wifi_ssid;

        void startWifiScan();
        void pollWifiScan();
        void openWifiScanDialog();
        void closeWifiScanDialog();
        void openWifiPasswordDialog();
        // スキャンで選んだネットワークへの新規接続。パスワードは空でもそのまま書く
        // (commitEdit(EditField::Password)の「空欄なら既存を保持」とは違う挙動)
        void connectScannedNetwork(const char* ssid, const char* password);

        // network.cfg / user.cfgから読んだ現在値。
        // パスワードは平文を持たず「設定済みか」だけを覚える(画面に出さないため)
        FixedString<PICO_STR_M>  ssid_value;
        bool                     has_password = false;
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
        constexpr static int ROW_H      = 26; // 9行(音量・明るさを含む)を画面へ収めるため30から詰めた
        constexpr static int EDIT_BTN_W = 48;
        constexpr static int EDIT_BTN_H = 20;

        int top_row_h = 0;
        int edit_btn_x = 0; // 全ての「編集」ボタンで共通のx(右端揃え)

        // network.cfg/user.cfgを読み直して各キャッシュ値とタイムゾーン一覧を更新する
        void loadValues();
        void loadTimezoneItems(const FixedString<PICO_STR_M>& current_tz);

        // 「編集」ボタン1個を作る共通処理(位置はyだけ渡せば揃う)
        Button* makeEditButton(int16_t y);

        void refreshSsidLabel();
        void refreshPasswordLabel();
        void refreshNtp1Label();
        void refreshNtp2Label();
        void refreshHomeLabel();
        void updateVolume();
        void updateBrightness();

        // InputDialogを1つnewして開く。閉じたらcommitEdit()へ渡してから破棄する
        void openEditDialog(EditField field, const char* label_text, const char* prefill);
        void commitEdit(EditField field, const FixedString<PICO_STR_LL>& input);

    public:
        const char* getName() const override { return "Settings"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};
