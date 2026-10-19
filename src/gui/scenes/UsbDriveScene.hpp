#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "services/UsbDrive_Functions.hpp"
#include "consts.hpp"

#include <cstdint>

// USBドライブの画面(ランチャの「USBドライブ」)。開くとSDをPCへ貸し出し、「終了」で取り戻す。
// 中身の処理は UsbDriveFunctions。この画面は状態の表示と、終わるときの確認だけを持つ。
//
//   - 開いた次のフレームで Start()(画面を1回描いてから。USBのつなぎ直しでシリアルが一瞬切れる)
//   - PCが読み書きしている間に「終了」を押したら、取り出したか確かめる(MsgDialog)
//   - 画面を離れる(戻る・電源メニューの再起動)と必ず Stop() する
//   - 貸し出している間はスリープしない・別の画面へ移らない(keepForeground)
class UsbDriveScene : public Scene {
    public:
        static constexpr const char* kAppName = "USBドライブ";

        const char* getName() const override { return "UsbDrive"; }
        bool keepForeground() const override { return true; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

    private:
        static constexpr int MARGIN = 6;
        static constexpr uint32_t kRefreshMs = 250;

        Label<PICO_STR_L>* state_label = nullptr;
        Label<PICO_STR_LL>* help_label = nullptr;
        Label<PICO_STR_L>* count_label = nullptr;
        Label<PICO_STR_LL>* diag_label = nullptr;   // USBの診断(PCに見えないとき、どこで止まったか)
        Button* exit_button = nullptr;

        bool start_pending = false;
        bool start_failed = false;
        bool confirming = false;
        UsbDriveFunctions::State shown_state = UsbDriveFunctions::State::Off;
        uint32_t shown_read = 0xFFFFFFFFu, shown_written = 0xFFFFFFFFu;
        uint32_t last_refresh_ms = 0;

        void refresh(bool force);
        void requestExit();
        void finish();
};
