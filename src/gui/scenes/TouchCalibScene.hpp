#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/apps/CalibView.hpp"
#include "util/TouchCalib.hpp"
#include "consts.hpp"

#include <cstdint>

// タッチ補正の画面(設定アプリの「時刻」タブの「タッチ位置の補正」から開く)。画面の5か所に順に出る十字を実際に押してもらい、
// そのときの生の値から最小二乗法で変換を求める(util/TouchCalib.hpp)。
//
//   説明 → 5点を順に押す → 結果(ずれの大きさ)→ 確かめ(押した所に青い点が出る)→ 保存 / 破棄
//
// 求めた変換は確かめの間だけ仮に使い、保存しないまま画面を離れれば元に戻す。
class TouchCalibScene : public Scene {
    public:
        const char* getName() const override { return "TouchCalib"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

        // 点の数と画面上の位置(画面の四隅の少し内側と真ん中)
        static constexpr int kPoints = 5;
        static int PointX(int i){ static const int xs[kPoints] = { 20, SCREEN_WIDTH - 20, 20, SCREEN_WIDTH - 20, SCREEN_WIDTH / 2 }; return xs[i]; }
        static int PointY(int i){ static const int ys[kPoints] = { STATUSBAR_HEIGHT + 22, STATUSBAR_HEIGHT + 22, SCREEN_HEIGHT - 20, SCREEN_HEIGHT - 20, (STATUSBAR_HEIGHT + SCREEN_HEIGHT) / 2 }; return ys[i]; }

        // 求めた変換を採用してよい誤差(画素)。これを超えたら押し間違いとみなしてやり直し
        static constexpr float kMaxAcceptErr = 10.0f;

    private:
        enum class Phase : uint8_t { Intro, Capture, Result, Failed };

        static constexpr int MARGIN = 8;
        static constexpr int kSkipFrames = 4;   // 触れ始めの不安定な数フレームは平均に入れない
        static constexpr int kMinSamples = 6;

        Phase phase = Phase::Intro;

        CalibView* view = nullptr;
        Label<PICO_STR_LL>* message = nullptr;
        Button* btn_a = nullptr;   // 開始 / 保存 / やり直し
        Button* btn_b = nullptr;   // 既定に戻す / やり直し / 戻る
        Button* btn_c = nullptr;   // 戻る / 破棄

        int index = 0;             // 今押してもらう点
        float raw_u[kPoints] = {};
        float raw_v[kPoints] = {};
        float sum_u = 0, sum_v = 0;
        int samples = 0;
        int touch_frames = 0;
        bool was_touching = false;
        bool wait_release = false;
        bool saved = false;
        TouchCalib::Affine backup = {};
        TouchCalib::Affine fitted = {};
        float err_max = 0, err_mean = 0;

        void showPhase();
        void startCapture();
        void finishCapture();
        void setMessage(const char* text, int8_t color);
        void restoreBackup();
};
