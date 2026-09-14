#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/MarkdownView.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "util/FixedString.hpp"

// Markdownブラウザ。
//
// 文書内のリンクをタップすると、同じシーンのまま次の文書を開く。戻る/進むで履歴を辿れる。
//
// **シーンを積み上げない**(リンクごとに SceneFunctions::Push しない)のは、
// シーンスタックの上限が kMaxSceneDepth=4 しかなく、リンクを4回たどると詰むため。
// 履歴はこのシーンが自前の固定長配列で持つ。
//
// 画面の構成:
//   [<] [>]                 [終了]   ← ヘッダ
//   --------------------------------
//              MarkdownView          ← 本文(ここだけスクロールする)
//   --------------------------------
//   /docs/pico/intro.md              ← フッタ(現在のパス。エラー時はメッセージ)
//
// ヘッダ/フッタは MarkdownView の**外側**に置いた素のウィジェットで、Viewの高さを
// その分だけ詰めている。View側に「ヘッダ領域」の概念は持たせていない
// (View内へ入れるとタップ判定も自前になるうえ、ビューポートの計算が
//  View中の7〜8箇所に散っているのを先に集約する必要が出る)。
class MarkdownScene : public Scene {
    private:
        MarkdownView* view = nullptr;
        Button* back_button = nullptr;      // 履歴を戻る
        Button* forward_button = nullptr;   // 履歴を進む
        Button* exit_button = nullptr;      // アプリ終了(ランチャへ戻る)
        Label<PICO_PATH_LEN>* status_label = nullptr; // 現在のパス / エラーメッセージ

        // ---------- 履歴 ----------
        // ブラウザと同じ扱い:
        //   - 新しい文書へ進むと、その時点より前方の履歴は捨てる
        //   - 上限に達したら最も古い1件を押し出す
        //   - 離れるときのスクロール位置を控えておき、戻ったら復元する
        struct HistoryEntry {
            FixedString<PICO_STR_L> path;
            int32_t scroll_y = 0;
        };

        constexpr static int kMaxHistory = 8;

        HistoryEntry history[kMaxHistory];
        int history_count = 0;  // 有効な件数
        int history_pos = -1;   // 今表示している位置(-1 = まだ何も開いていない)

        constexpr static int MARGIN = 5;
        constexpr static int HEADER_H = 28;
        constexpr static int FOOTER_H = 18;
        constexpr static int NAV_BUTTON_W = 24;
        constexpr static int EXIT_BUTTON_W = 40;
        constexpr static int BUTTON_H = 18;

        // 履歴へ1件積む。前方履歴は捨てる。
        // 同じ文書を続けて積むことはしない。パスが長すぎて収まらない場合はfalse
        bool pushHistory(const char* path);

        // history_posの文書を開き、控えてあったスクロール位置まで戻す
        bool openCurrent();

        // 現在の履歴位置に合わせてボタンの見た目とフッタを更新する
        void refreshChrome();

        // フッタへメッセージを出す(次に文書を開くまで残る)
        void showStatus(const char* message, int8_t color);

        // 離れる前に、今のスクロール位置を履歴へ控える
        void rememberScroll();

        bool canGoBack() const { return history_pos > 0; }
        bool canGoForward() const { return history_pos >= 0 && history_pos + 1 < history_count; }

        void goBack();
        void goForward();
        void onLinkTap(const FixedString<PICO_PATH_LEN>& url);

    public:
        // 開始する文書。ここが履歴の1件目になる
        MarkdownScene(const char* path = "/tmp/doc.md"){
            this->pushHistory(path);
        }

        const char* getName() const override { return "Markdown"; }

        void onEnter() override;
        void onExit() override;
};
