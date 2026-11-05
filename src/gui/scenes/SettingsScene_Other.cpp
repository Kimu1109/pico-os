// 設定アプリ(SettingsScene)の「他」タブ(ランチャ・ブラウザのホーム・バックグラウンド・追加の漢字フォント・開発者向け)。画面の組み立ての全体は SettingsScene.cpp の onEnter()
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

#include "devtools/DevTools_Functions.hpp"
#include "services/PowerMenu_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "system/I18n_Functions.hpp"
#include "gui/core/JpFont_Ext.hpp"
#include "system/Log_Functions.hpp"
#include "services/FontDownload.hpp"
#include "system/Config_Functions.hpp"
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
    constexpr const char*   kSleepPresetLabels[] = { TR_NOOP("しない"), TR_NOOP("1分"), TR_NOOP("2分"), TR_NOOP("5分"), TR_NOOP("10分"), TR_NOOP("30分") };
    constexpr int kSleepPresetCount = sizeof(kSleepPresetsSec) / sizeof(kSleepPresetsSec[0]);

    // 音の周波数(sound.cfgの sample-rate)。44.1kHzはライン出力のDAC(PCM5102A等)向けで、2コア目の計算が倍になる
    constexpr uint32_t    kRatePresets[]      = { SoundFunctions::kSampleRateLow, SoundFunctions::kSampleRateHigh };
    constexpr const char* kRatePresetLabels[] = { "22kHz", "44.1kHz" };
    constexpr int kRatePresetCount = sizeof(kRatePresets) / sizeof(kRatePresets[0]);
}

// ---------------------------------------------------------------------------
// ランチャで隠したアプリ
// ---------------------------------------------------------------------------

void SettingsScene::refreshLauncherLabel(){
    if(!this->launcher_label) return;
    char buf[PICO_STR_L];
    const int hidden = AppFunctions::HiddenCount();
    if(hidden > 0) snprintf(buf, sizeof(buf), TR("ランチャで隠したアプリ: %d"), hidden);
    else           snprintf(buf, sizeof(buf), TR("ランチャで隠したアプリ: なし"));
    this->launcher_label->setText(buf);
}

void SettingsScene::openLauncherDialog(){
    //隠したアプリの一覧(選ぶとランチャへ戻す)+ 最後に「並びを元に戻す」
    auto* dialog = new PickerDialog(PickerDialog::Mode::Choice, TR("隠したアプリ"), "", TR("閉じる"));
    if(!dialog) return;
    const int hidden = AppFunctions::HiddenCount();
    for(int i = 0; i < hidden; i++){
        const AppEntry* e = AppFunctions::Get(AppFunctions::HiddenAt(i));
        char buf[PICO_STR_L];
        snprintf(buf, sizeof(buf), TR("表示: %s"), e ? AppFunctions::DisplayName(*e) : "?");
        dialog->addChoice(buf);
    }
    dialog->addChoice(TR("並びを元に戻す"));
    dialog->layout();
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, hidden](bool is_ok){
        const int sel = dialog->getSelectedIndex();
        WidgetFunctions::DestroyLater(dialog);
        if(!is_ok || sel < 0) return;
        if(sel < hidden){
            //開いている間に並びが変わることは無い(ランチャはこの画面の下で止まっている)
            AppFunctions::SetHidden(AppFunctions::HiddenAt(sel), false);
        }else{
            AppFunctions::ResetLayout();
            this->showMessageLater(TR("ランチャの並びを登録順に戻しました(フォルダも解除)"));
        }
        this->refreshLauncherLabel();
    });
}

// ---------------------------------------------------------------------------
// 追加の漢字フォント(docs/design/performance.md「漢字の一部をSDへ」)
// ---------------------------------------------------------------------------

void SettingsScene::refreshFontLabel(){
    if(!this->font_label) return;
    char buf[PICO_STR_L];
    uint32_t key;
    if(this->font_download && this->font_download->busy()){
        const uint32_t total = this->font_download->progressTotal();
        const uint32_t pct = total ? (uint32_t)((uint64_t)this->font_download->progressDone() * 100 / total) : 0;
        key = 0x80000000u | pct;
        snprintf(buf, sizeof(buf), TR("追加の漢字: 取得 %lu%%"), (unsigned long)pct);
    }else{
        JpFontExt::Ready();
        const auto s16 = JpFontExt::Store(16).state();
        const auto s24 = JpFontExt::Store(24).state();
        key = ((uint32_t)s16 << 8) | (uint32_t)s24;
        // 2つの状態が違えば、使えない方を出す
        const auto shown = (s16 != ExtGlyphStore::State::Ready) ? s16 : s24;
        snprintf(buf, sizeof(buf), TR("追加の漢字: %s"), JpFontExt::StateText(shown));
    }
    if(key == this->font_shown) return;
    this->font_shown = key;
    this->font_label->setText(buf);
}

void SettingsScene::startFontDownload(){
    if(this->font_download && this->font_download->busy()){
        this->font_download->cancel();
        this->endFontDownload();
        this->showMessageLater(TR("追加の漢字フォントの取得を取りやめました"));
        return;
    }
    if(JpFontExt::Ready()){
        this->showMessageLater(TR("追加の漢字フォントは入っています"));
        return;
    }
    this->endFontDownload();
    this->font_download = new FontDownload();
    if(!this->font_download){
        this->showMessageLater(TR("メモリが足りません"));
        return;
    }
    // 置き場は /sys/firmware.cfg の font-url で変えられる(手元のサーバで試す用。ファームの url と同じ考え方)
    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_FIRMWARE_CFG)){
        FontDownload* dl = this->font_download;
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_FIRMWARE_CFG, [dl](const char* key, const char* value){
            if(strcmp(key, "font-url") == 0 && !dl->setBaseUrl(value)){
                LOG_SYS_WARN("追加の漢字: firmware.cfg の font-url が読めません (%s)", value);
            }
        });
    }
    if(!this->font_download->begin()){
        this->showMessageLater(this->font_download->message());
        this->endFontDownload();
        return;
    }
    if(this->font_button) this->font_button->setText(TR("中止"));
    this->font_shown = 0xFFFFFFFFu;
    this->refreshFontLabel();
}

void SettingsScene::pollFontDownload(){
    if(!this->font_download){
        if(this->tab == Tab::Other) this->refreshFontLabel();
        return;
    }
    if(this->font_download->busy()){
        PowerFunctions::KeepAwake();  // 取得中はスリープさせない
        this->font_download->update();
    }
    if(!this->font_download->busy()){
        const bool ok = this->font_download->state() == FontDownload::State::Done;
        this->showMessageLater(this->font_download->message());
        this->endFontDownload();
        if(ok) this->font_shown = 0xFFFFFFFFu;
    }
    this->refreshFontLabel();
}

void SettingsScene::endFontDownload(){
    delete this->font_download;  // デストラクタが取得をやめて接続を閉じる
    this->font_download = nullptr;
    if(this->font_button) this->font_button->setText(TR("取得"));
}

// ---------------------------------------------------------------------------
// 組み立て
// ---------------------------------------------------------------------------

void SettingsScene::buildOtherTab(const Layout& lay){
    const Rect content = lay.content;
    const int16_t left = lay.left;
    const int full_w = lay.full_w;
    auto rowY = [&lay](int i) -> int16_t { return lay.rowY(i); };
    const int label_w = this->labelW(content);
    // =====================================================================
    // その他
    // =====================================================================
    // ランチャで隠したアプリ(並べ替え・隠すのはランチャのタイルの長押し)。1行目
    // (表示言語はビルド時に決めるので、ここでは選ばない。docs/design/i18n.md「ビルド時に選ぶ言語」)
    this->launcher_button = this->makeEditButton(rowY(0));
    this->launcher_button->setOnPressEnd([this](){ this->openLauncherDialog(); });
    addToTab(Tab::Other, this->launcher_button);
    this->launcher_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(0) + 2), label_w, "");
    addToTab(Tab::Other, this->launcher_label);
    this->refreshLauncherLabel();

    // ブラウザのホーム。2行目
    this->home_edit_button = this->makeEditButton(rowY(1));
    this->home_edit_button->setOnPressEnd([this](){
        // 空欄で決定すると同梱サンプル文書に戻る(commitEdit()参照)
        this->openEditDialog(EditField::BrowserHome, TR("ブラウザのホームURL"), this->home_value.c_str());
    });
    addToTab(Tab::Other, this->home_edit_button);

    this->home_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(1) + 2), label_w, "");
    addToTab(Tab::Other, this->home_label);
    this->refreshHomeLabel();

    // バックグラウンドの仕事(BackgroundFunctions)。3行目。一覧の画面で止める/入り切りする
    {
        Button* bg_button = this->makeEditButton(rowY(2));
        bg_button->setText(TR("一覧"));
        bg_button->setOnPressEnd([](){ SceneFunctions::Push(new BackgroundScene()); });
        addToTab(Tab::Other, bg_button);
        int active = 0;
        for(int i = 0; i < BackgroundFunctions::Count(); i++) if(BackgroundFunctions::At(i)->active()) active++;
        char text[64];
        if(active > 0) snprintf(text, sizeof(text), TR("バックグラウンド: %d件"), active);
        else snprintf(text, sizeof(text), TR("バックグラウンド: なし"));
        addToTab(Tab::Other, makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(2) + 2), label_w, text));
    }

    // 追加の漢字フォント(SDの /sys/fonts/)。4行目。[取得]でWebから落として置く(取得中は[中止])
    this->font_button = this->makeEditButton(rowY(3));
    this->font_button->setText(TR("取得"));
    this->font_button->setIcon(IconID::Refresh, IconSize::Px16);
    this->font_button->setOnPressEnd([this](){ this->startFontDownload(); });
    addToTab(Tab::Other, this->font_button);
    this->font_label = makeRowLabel<PICO_STR_L>(left, (int16_t)(rowY(3) + 2), label_w, "");
    addToTab(Tab::Other, this->font_label);
    this->font_shown = 0xFFFFFFFFu;
    this->refreshFontLabel();

    // 開発者向け(/sys/debug.cfg。DevToolsFunctions)と起動時の自己診断。見出しの下に4行を並べる
    // (チェックボックスだけの行なので ROW_H より少し詰める。入りきらない分はスクロール)
    constexpr int kDevRowH = 32;
    const int16_t dev_y = (int16_t)(rowY(4) + 2);
    auto* dev_title = makeRowLabel<PICO_STR_M>(left, dev_y, full_w, TR("開発者向け"));
    dev_title->setTextColor(PICO_DARKGREY);
    addToTab(Tab::Other, dev_title);
    auto devRowY = [&](int i) -> int16_t { return (int16_t)(dev_y + 20 + i * kDevRowH); };
    // 起動時セルフチェック(本体はloadValues()より前で生成済み)
    this->run_test_checkbox->setY(devRowY(0));
    addToTab(Tab::Other, this->run_test_checkbox);
    auto makeDevCheckbox = [&](int row, const char* text, bool checked) -> Checkbox* {
        Checkbox* cb = new Checkbox(left, devRowY(row - 3), text);
        cb->setFontSize(FontFn::Small);
        cb->setText(text); // 小さいフォントで幅を測り直す(Checkboxの幅は構築時のフォントで決まるため)
        cb->setIsChecked(checked);
        addToTab(Tab::Other, cb);
        return cb;
    };
    this->perf_overlay_checkbox = makeDevCheckbox(4, TR("フレーム時間を表示"), DevToolsFunctions::PerfOverlay());
    this->perf_overlay_checkbox->setOnChangeChecked([this](){
        DevToolsFunctions::SetPerfOverlay(this->perf_overlay_checkbox->getIsChecked());
    });
    this->lua_debugger_checkbox = makeDevCheckbox(5, TR("Luaデバッガ"), DevToolsFunctions::LuaDebuggerEnabled());
    this->lua_debugger_checkbox->setOnChangeChecked([this](){
        // 次に開いたLuaアプリから効く(今動いているLuaEngineには後から付けない)
        DevToolsFunctions::SetLuaDebugger(this->lua_debugger_checkbox->getIsChecked());
    });
    this->watchdog_checkbox = makeDevCheckbox(6, TR("固まったら再起動する"), DevToolsFunctions::Watchdog());
    this->watchdog_checkbox->setOnChangeChecked([this](){
        DevToolsFunctions::SetWatchdog(this->watchdog_checkbox->getIsChecked());
    });
}
