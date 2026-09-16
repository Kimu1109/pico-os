#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/systems/AppGrid.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"

// 起動直後のシーン。登録簿(AppFunctions)に並んだアプリをグリッドで見せるランチャ。
//
// 以前はアプリごとのButtonをメンバとして持っていたため、アプリを1つ足すたびに
// このファイルの .hpp と .cpp の両方を編集する必要があった。
// 今はどのアプリを載せるかを一切知らず、App_List.cpp の一覧をそのまま表示する。
class HomeScene : public Scene {
    private:
        AppGrid* grid = nullptr;

        //アプリが1ページに収まらない時だけ出すページ送り
        Button* prev_button = nullptr;
        Button* next_button = nullptr;
        Label<PICO_STR_S>* page_label = nullptr;

        //アプリが1つも登録されていない時の案内
        Label<PICO_STR_L>* empty_label = nullptr;

        constexpr static int MARGIN = 6;
        constexpr static int PAGER_H = 28;
        constexpr static int PAGER_BUTTON_W = 44;

        void updatePageLabel();

    public:
        const char* getName() const override { return "Home"; }

        void onEnter() override;
        void onExit() override;
};
