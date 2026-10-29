#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ProgressBar.hpp"
#include "gui/widgets/WidgetID.hpp"
#include "services/Firmware_Functions.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// 「本体の更新」の画面(ランチャ。ファイルアプリで .pfw を開いたときもここへ来る)。
// 中身の処理は FirmwareFunctions。この画面は今の版・進み具合の表示と、確認のダイアログだけを持つ。
//
//   [Webから]  … Webの版の一覧(GitHub Pages)から選び、SDの /firmware/ へ落としてからSDと同じ流れで書く
//   [SDから]   … /firmware/ から .pfw を選ぶ
//   [受け取る] … PCからの更新(script/firmware_tool.py push / pio run -t fwpush)を待ち受ける
//
// 更新の途中・取得中・待ち受け中は別の画面へ移らない(keepForeground)。画面を離れるとやめる
// (準備ができて再起動を待っているときだけはそのまま)。
class FirmwareScene : public Scene {
    public:
        static constexpr const char* kAppName = "本体の更新";

        FirmwareScene() = default;
        // SDの更新ファイルを開いた状態で始める(ファイルアプリから)
        explicit FirmwareScene(const char* path){ this->initial_path.assign(path ? path : ""); }

        const char* getName() const override { return "Firmware"; }
        bool keepForeground() const override {
            return FirmwareFunctions::Busy() || FirmwareFunctions::WebBusy() ||
                   FirmwareFunctions::Listening() || FirmwareFunctions::RebootPending();
        }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;

    private:
        static constexpr int MARGIN = 6;

        Label<PICO_STR_L>* version_label = nullptr;
        Label<PICO_STR_LL>* key_label = nullptr;
        Label<PICO_STR_LL>* status_label = nullptr;
        Label<PICO_STR_L>* detail_label = nullptr;
        ProgressBar* progress = nullptr;
        Button* web_button = nullptr;
        Button* sd_button = nullptr;
        Button* listen_button = nullptr;
        Button* back_button = nullptr;

        FixedString<PICO_PATH_LEN> initial_path;
        bool start_pending = false;
        bool confirm_open = false;
        WidgetId confirm_dialog = WidgetIdTools::Invalid();
        int shown_state = -1;
        uint32_t shown_done = 0xFFFFFFFFu;
        bool shown_listening = false;
        // 一覧を取り終えたら選ぶダイアログを出す(取り始めたときに立てる)
        bool web_pick_pending = false;
        bool picker_open = false;

        void startFromSd(const char* path);
        void startWeb();
        void openWebPicker();
        void openPicker();
        void toggleListen();
        void openConfirm();
        void refresh(bool force);
        void goBack();
        void showError(const char* text);
};
