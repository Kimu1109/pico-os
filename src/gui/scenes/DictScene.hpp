#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "dict/Word_Dict.hpp"

// 標準アプリの辞書(英和/和英)。
//
// 検索ロジック本体はWordDictionary(src/dict/)が持つ。このシーンは
// 「入力欄+検索ボタン+状態表示+結果一覧+詳細表示」を並べるだけの薄い皮
// (ClocksScene/FileExplorerSceneと同じ形)。
//
// **前方一致はsearch()が同期的に即返すが、語の途中の一致はupdate()を
// 毎フレーム呼んで少しずつ拾う**ため、onUpdate()でdict_.update()を回し、
// 新しく見つかった分だけ一覧へ追記する(全部揃うまで画面が止まらない)。
//
// MarkdownSceneと同じく、このシーンのオブジェクトは「数十バイト」の
// 他シーンと違いWordDictionary(~43KB)を持つため大きい。辞書アプリは
// 自分の上へ別シーンをPushしないので実害は無いが、将来詳細表示を
// 別シーンに分けるような変更をする場合は思い出すこと。
class DictScene : public Scene {
    private:
        Button* back_button       = nullptr;
        // Textboxはコード量削減のため明示インスタンス化した型しか使えない
        // (Textbox.cpp参照)。PICO_STR_LLは一般的なテキスト入力欄で
        // 既に使われている型なのでこれに合わせる
        Textbox<PICO_STR_LL>* search_box = nullptr;
        Button* search_button     = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        ScrollList* result_list   = nullptr;
        Label<PICO_STR_2KiB>* detail_label = nullptr;

        WordDictionary dict_;
        // dict_.hit()のうち、まだresult_listへ積んでいない分を判別するための
        // 反映済み件数(count()は増える一方なので差分はこれだけで分かる)
        int shown_count_ = 0;

        // Pop()で戻ってきた時に検索語を復元するための退避先(ウィジェットは
        // onExit()で解放されるため、シーンのメンバ側に持っておく必要がある)
        FixedString<PICO_STR_LL> saved_query_;

        constexpr static int MARGIN = 4;
        constexpr static int BACK_BUTTON_H = 20;
        constexpr static int SEARCH_ROW_H = 22;
        constexpr static int SEARCH_BUTTON_W = 44;
        constexpr static int STATUS_H = 16;
        constexpr static int LIST_H = 108;

        void startSearch();
        void refreshResults();
        void showDetail(int index);

    public:
        const char* getName() const override { return "Dict"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
};
