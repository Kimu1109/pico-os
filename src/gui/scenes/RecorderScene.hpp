#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ProgressBar.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// 録音(ランチャの「録音」)。マイク(SPH0645、MicFunctions)の音を /recordings/rec_NNNN.wav へ録る。
//
//   上の段 : [戻る] 録音 … [16kHz](周波数を切り替え) [+18dB](増幅を切り替え)
//   時間   : 録音中は録った長さ、止めている間は 0:00
//   状態   : 待機中 / 録音中(ファイル名) / マイクの信号がありません / 取りこぼし
//   メーター: 音の大きさ(-60〜0dBFS)。この画面を開いている間はずっと聞いている
//   [録音] / [停止]
//   録ったものの一覧(新しい順)。2回タップで開く(ファイルビューワーで鳴らす)、[削除]
//
// - 録音中は別の画面へ移らない(keepForeground)・スリープしない。画面を離れたら録音を止めて保存する
// - キー: Space / r = 録音・停止、Esc = 戻る(録音中なら止めてから)
class RecorderScene : public Scene {
    public:
        static constexpr const char* kAppName = "録音";

        const char* getName() const override { return "Recorder"; }
        bool keepForeground() const override;
        bool onKey(const KeyInputFunctions::Event& ev) override;

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

    private:
        static constexpr int MARGIN = 6;
        static constexpr int kMaxFiles = 48;
        static constexpr uint32_t kMeterMs = 50;
        static constexpr uint32_t kStatusMs = 250;

        Label<PICO_STR_S>* time_label = nullptr;
        Label<PICO_STR_LL>* status_label = nullptr;
        ProgressBar* meter = nullptr;
        Button* rec_button = nullptr;
        Button* rate_button = nullptr;
        Button* gain_button = nullptr;
        Button* delete_button = nullptr;
        ScrollList* list = nullptr;

        // 一覧の項目が指すファイル名(rec_0001.wav)。一覧と同じ並び
        FixedString<PICO_STR_S> files[kMaxFiles];
        int file_count = 0;

        bool was_recording = false;
        bool confirming = false;
        float meter_level = 0;
        uint32_t last_meter_ms = 0, last_status_ms = 0;
        uint32_t shown_sec = 0xFFFFFFFFu;

        void toggleRecording();
        void cycleRate();
        void cycleGain();
        void refreshButtons();
        void refreshStatus();
        void updateMeter(uint32_t now);
        void reloadList();
        void openSelected();
        void deleteSelected();
};
