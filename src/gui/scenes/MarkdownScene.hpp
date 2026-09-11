#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/MarkdownView.hpp"
#include "gui/widgets/Button.hpp"

// MarkdownViewを1枚だけ載せたシーン。
// 遷移して抜けた時点でMarkdownViewのプールごと解放されるため、
// 「重いウィジェットをシーンの寿命に縛る」例になっている
class MarkdownScene : public Scene {
    private:
        MarkdownView* view = nullptr;
        Button* back_button = nullptr;

        //Pop()で戻ってきた時に同じ文書を開き直すため、パスはシーン側に持っておく
        FixedString<PICO_PATH_LEN> doc_path;

        constexpr static int MARGIN = 5;
        constexpr static int BUTTON_HEIGHT = 30;

    public:
        MarkdownScene(const char* path = "tmp/doc.md"){
            this->doc_path.assign(path);
        }

        const char* getName() const override { return "Markdown"; }

        void onEnter() override;
        void onExit() override;
};
