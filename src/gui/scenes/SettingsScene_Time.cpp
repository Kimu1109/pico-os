// 設定アプリ(SettingsScene)の「時刻」タブ(NTP・時刻の手動設定)と画面タブのタッチ補正の行。画面の組み立ての全体は SettingsScene.cpp の onEnter()
#include "gui/scenes/SettingsScene.hpp"
#include "services/Power_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "hal/Rtc_Functions.hpp"
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
// 時刻の手動設定(Wi-Fi・NTPが無いとき用)
// ---------------------------------------------------------------------------

void SettingsScene::refreshClockLabel(bool force){
    if(!this->clock_label) return;
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if(!force && t.tm_min == this->clock_shown_min) return;
    this->clock_shown_min = t.tm_min;

    static const char* const kWeek[] = { "日", "月", "火", "水", "木", "金", "土" };
    const bool valid = t.tm_year + 1900 >= TimeFunctions::kValidYear;
    char buf[PICO_STR_L];
    if(valid){
        snprintf(buf, sizeof(buf), "%04d/%02d/%02d(%s) %02d:%02d",
            t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, kWeek[t.tm_wday % 7], t.tm_hour, t.tm_min);
    }else{
        snprintf(buf, sizeof(buf), "時刻が合っていません");
    }
    this->clock_label->setText(buf);
    this->clock_label->setTextColor(valid ? PICO_BLACK : PICO_RED);
    if(this->clock_note){
        //時刻の控え(hal/Rtc_Functions.hpp)がどこまで効くか
        const char* note;
        if(!valid)                              note = RtcFunctions::HasDs3231() ? "合わせるとDS3231へ控えます"
                                                                                 : "「変更」で手動で合わせられます";
        else if(RtcFunctions::Ds3231Valid())    note = "DS3231で電源を切っても保持";
        else if(RtcFunctions::HasAon())         note = "再起動では消えません";
        else                                    note = "NTP同期で上書きされます";
        this->clock_note->setText(note);
    }
}

void SettingsScene::openClockDateDialog(){
    //時計が合っていなければ(1970年)今年の1月1日から選ばせる
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
    if(y < TimeFunctions::kValidYear){ y = 2026; m = 1; d = 1; }

    auto* dialog = new PickerDialog(PickerDialog::Mode::Date, "日付", "次へ", "キャンセル");
    if(!dialog) return;
    dialog->setDate(y, m, d);
    dialog->layout();
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_ok){
        if(is_ok){
            this->clock_year  = dialog->getYear();
            this->clock_month = dialog->getMonth();
            this->clock_day   = dialog->getDay();
            //ダイアログからダイアログは1フレーム空ける
            this->pending = Pending::ClockTime;
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void SettingsScene::openClockTimeDialog(){
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    const bool valid = t.tm_year + 1900 >= TimeFunctions::kValidYear;

    char title[PICO_STR_M];
    snprintf(title, sizeof(title), "%d/%d の時刻", this->clock_month, this->clock_day);
    auto* dialog = new PickerDialog(PickerDialog::Mode::Time, title, "設定", "キャンセル");
    if(!dialog) return;
    dialog->setTime(valid ? t.tm_hour : 12, valid ? t.tm_min : 0, 0);
    dialog->layout();
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_ok){
        if(is_ok){
            const uint32_t sec = dialog->getTimeSeconds();
            const bool ok = TimeFunctions::SetLocalDateTime(this->clock_year, this->clock_month, this->clock_day,
                (int)(sec / 3600) % 24, (int)(sec / 60) % 60, (int)(sec % 60));
            if(ok){
                LOG_SYS_MSG("時刻を手動で合わせました: %04d/%02d/%02d %02u:%02u:%02u",
                    this->clock_year, this->clock_month, this->clock_day,
                    (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60), (unsigned)(sec % 60));
                //RTCへすぐ控える(RtcFunctions::Update() も飛びを見て書くが、下の表示を先に合わせるため)
                RtcFunctions::SaveNow(time(nullptr));
            }else{
                this->showMessageLater("時刻を合わせられませんでした");
            }
            this->refreshClockLabel(true);
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

// ---------------------------------------------------------------------------
// 組み立て
// ---------------------------------------------------------------------------

void SettingsScene::buildTimeRows(const Layout& lay){
    const Rect content = lay.content;
    const int16_t left = lay.left;
    const int full_w = lay.full_w;
    auto rowY = [&lay](int i) -> int16_t { return lay.rowY(i); };
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

    // ---- 時刻の手動設定。時刻の4行目(+下に注意書き) ----
    this->clock_edit_button = this->makeEditButton(rowY(3));
    this->clock_edit_button->setText("変更");
    this->clock_edit_button->setOnPressEnd([this](){ this->openClockDateDialog(); });
    addToTab(Tab::Time, this->clock_edit_button);

    this->clock_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(3) + 2), label_w, "");
    addToTab(Tab::Time, this->clock_label);
    this->clock_note = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(4) + 2), full_w, "");
    this->clock_note->setTextColor(PICO_DARKGREY);
    addToTab(Tab::Time, this->clock_note);
    this->refreshClockLabel(true);


    // ---- タッチ位置の補正。画面タブの6行目 ----
    // 押すと補正の画面(TouchCalibScene)へ移る。戻ると設定アプリはこのタブのまま開き直る
    Button* calib_button = this->makeEditButton(rowY(5));
    calib_button->setText("補正");
    calib_button->setOnPressEnd([](){ SceneFunctions::Push(new TouchCalibScene()); });
    addToTab(Tab::Display, calib_button);
    addToTab(Tab::Display, makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(5) + 2), label_w, "タッチ位置の補正"));
}
