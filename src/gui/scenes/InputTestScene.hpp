#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/NumberInput.hpp"
#include "gui/widgets/Button.hpp"

// キーボード(オーバーレイ常駐)がシーンをまたいで使い回せることの確認用シーン。
// Textbox/NumberInputはシーンの所有物なので遷移で破棄されるが、
// キーボード側は破棄されず、遷移直前にHideAll()で閉じられる
class InputTestScene : public Scene {
    private:
        Label<PICO_STR_M>* caption = nullptr;
        Textbox<PICO_STR_LL>* textbox = nullptr;
        NumberInput* number = nullptr;
        Button* back_button = nullptr;

        //Pop()で戻ってきた時に入力内容を復元するための退避先
        FixedString<PICO_STR_LL> saved_text;

        constexpr static int MARGIN = 10;
        constexpr static int ROW_GAP = 12;
        constexpr static int TEXTBOX_HEIGHT = 60;
        constexpr static int BUTTON_HEIGHT = 30;

    public:
        const char* getName() const override { return "InputTest"; }

        void onEnter() override;
        void onExit() override;
};
