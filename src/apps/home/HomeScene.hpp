#pragma once

#include "gui/core/Scene.hpp"
#include "apps/home/AppGrid.hpp"
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
        //ページ(フォルダを開いている間はフォルダの名前とページ)
        Label<PICO_STR_L>* page_label = nullptr;
        //フォルダを開いている間だけ出す、トップへ戻るボタン(ページ送りの行の左。ページ送りは右へ寄せる)
        Button* back_button = nullptr;

        //アプリが1つも登録されていない時の案内
        Label<PICO_STR_L>* empty_label = nullptr;

        constexpr static int MARGIN = 6;
        constexpr static int PAGER_H = 28;
        constexpr static int PAGER_BUTTON_W = 44;

        void updatePageLabel();
        // ラベルに出しているページとフォルダ(キーでグリッドがページを送ったとき・フォルダを開いたときに追いつく)
        int shown_page = -1;
        int shown_pages = -1;
        int shown_folder = -2;
        // アプリを開いて戻ってきたときに、開いていたフォルダとページへ戻す
        int reopen_folder = -1;
        int reopen_page = 0;
        void openFocusedMenu();
        // 並べ替えた後、並びの seq_pos にあるウィジェットの placed[] の番号(無ければ-1)
        static int findWidgetAfterMove(int seq_pos);

        // 項目の長押し(またはコントローラーのX/Y・キーのm)で出すメニュー。
        // アプリ: 前へ/後ろへ/先頭へ/フォルダへ入れる/隠す/ウィジェットを追加。ウィジェット: 移動/大きさ/削除/ウィジェットを追加。
        // フォルダ: 移動/名前を変える/解除/ウィジェットを追加。フォルダの中のアプリ: 移動(フォルダの中で)/フォルダから出す/
        // 別のフォルダへ/隠す。空いた所の長押しはウィジェットの追加だけ(フォルダの中では何もしない)。
        // 隠したアプリを戻すのは設定アプリ(他タブ)
        void openItemMenu(const AppGrid::LongPress& target);
        // アプリを入れるフォルダを選ぶメニュー(新しいフォルダ/今あるフォルダ)
        void openFolderPicker(int app_index);
        // フォルダの名前を聞く。folder < 0 なら app_index を入れた新しいフォルダを作る、そうでなければ名前を変える
        void openFolderName(int app_index, int folder);
        // ウィジェットの種類と大きさを選ぶメニュー。並びの seq_pos の前へ足す
        void openAddMenu(int seq_pos);
        // 隠してよいか(設定アプリは隠すと戻せなくなるので不可、見えている最後の1つも不可)
        static bool canHide(int app_index);
        // 開いているメニュー(シーンの切り替えで消えることがあるのでIDで持つ)
        WidgetId app_menu = WidgetIdTools::Invalid();
        // 開いているメニュー(上のapp_menu、1度に1つ)の「行 → すること」と、閉じたときに要る値。
        // 閉じたときのコールバックはthisだけを持ってここを読む(配列を値でキャプチャすると
        // コールバックの箱(InplaceFunction)に収まらない。以前はstd::functionがヒープへ逃がしていた)
        struct MenuRow { int8_t value; uint8_t w, h; };   // value: すること / フォルダの番号(-1=新しく作る) / ウィジェットの種類
        static constexpr int kMaxMenuRows = (HomeLayout::kKindCount * 3 > AppFunctions::kMaxFolders + 1)
                                          ? HomeLayout::kKindCount * 3 : AppFunctions::kMaxFolders + 1;
        MenuRow menu_rows[kMaxMenuRows < 16 ? 16 : kMaxMenuRows];
        int menu_count = 0;
        AppGrid::LongPress menu_target;  // openItemMenu: 長押しした項目
        int menu_value = 0;              // openItemMenu: 並びの中の位置 / openFolderPicker: アプリの番号 / openAddMenu: 足す位置
        bool menu_in_folder = false;     // openItemMenu: フォルダの中で開いたか
        int menu_folder = -1;            // openFolderName: 名前を変えるフォルダ(-1 = 新しく作る)
        // 次のフレームで開くメニュー。長押しで押し込みの絵を戻したフレームにダイアログを重ねると、
        // 半透明の下は描き直されず押し込みの絵が残る(「ダイアログは1フレーム空ける」と同じ理由)
        bool has_pending_menu = false;
        AppGrid::LongPress pending_menu;
        // メニューを閉じた次のフレームで開くもの
        enum class Pending : uint8_t { None, Add, FolderPick, NewFolder, Rename };
        Pending pending = Pending::None;
        int pending_value = -1;   // Add: 並びの位置 / FolderPick・NewFolder: 登録簿の番号 / Rename: フォルダの番号
        void setPending(Pending kind, int value){ pending = kind; pending_value = value; }

    public:
        // 並べ替えのメニューを出すまでの長押しの時間
        static constexpr unsigned long kLongPressMs = 600;

        const char* getName() const override { return "Home"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;
};
