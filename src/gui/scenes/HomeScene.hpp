#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"

// 起動直後のシーン。各シーンへの入口を並べるだけのランチャ
class HomeScene : public Scene {
    private:
        Label<PICO_STR_M>* title = nullptr;
        Button* markdown_button = nullptr;
        Button* input_button = nullptr;

        constexpr static int MARGIN = 10;
        constexpr static int BUTTON_WIDTH = SCREEN_WIDTH - MARGIN * 2 - 10;
        constexpr static int BUTTON_HEIGHT = 30;
        constexpr static int BUTTON_GAP = 8;

    public:
        const char* getName() const override { return "Home"; }

        void onEnter() override;
        void onExit() override;
};
