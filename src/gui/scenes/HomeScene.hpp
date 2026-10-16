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
        // ラベルに出しているページ(キーでグリッドがページを送ったときに追いつく)
        int shown_page = -1;
        int shown_pages = -1;
        void openFocusedMenu();
        // 並べ替えた後、並びの seq_pos にあるウィジェットの placed[] の番号(無ければ-1)
        static int findWidgetAfterMove(int seq_pos);

        // 項目の長押し(またはコントローラーのX/Y・キーのm)で出すメニュー。
        // アプリ: 前へ/後ろへ/先頭へ/隠す/ウィジェットを追加。ウィジェット: 移動/大きさ/削除/ウィジェットを追加。
        // 空いた所の長押しはウィジェットの追加だけ。隠したアプリを戻すのは設定アプリ(他タブ)
        void openItemMenu(const AppGrid::LongPress& target);
        // ウィジェットの種類と大きさを選ぶメニュー。並びの seq_pos の前へ足す
        void openAddMenu(int seq_pos);
        // 隠してよいか(設定アプリは隠すと戻せなくなるので不可、見えている最後の1つも不可)
        static bool canHide(int app_index);
        // 開いているメニュー(シーンの切り替えで消えることがあるのでIDで持つ)
        WidgetId app_menu = WidgetIdTools::Invalid();
        // 次のフレームで開くメニュー。長押しで押し込みの絵を戻したフレームにダイアログを重ねると、
        // 半透明の下は描き直されず押し込みの絵が残る(「ダイアログは1フレーム空ける」と同じ理由)
        bool has_pending_menu = false;
        AppGrid::LongPress pending_menu;
        // 次のフレームで開く追加のメニュー(-1=なし。値は並びの位置)
        int pending_add = -1;

    public:
        // 並べ替えのメニューを出すまでの長押しの時間
        static constexpr unsigned long kLongPressMs = 600;

        const char* getName() const override { return "Home"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;
};
