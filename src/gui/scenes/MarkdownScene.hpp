#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/MarkdownView.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "net/Doc_Fetch.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"

// Markdownブラウザ。
//
// 履歴に載るのは「場所(location)」で、**SD上のパスとURLのどちらもあり得る**。
// 見分けは `UrlTools::Parse()` が通るかどうかの1箇所だけで、
// "http://" の判定を各所へ撒かないようにしてある。
//
//   ローカル: "/tmp/doc.md"                  → そのままMarkdownViewへ渡す
//   リモート: "http://host/docs/intro.md"    → DocFetchでSDのキャッシュへ落としてから渡す
//
// どちらの場合もMarkdownViewが受け取るのはSD上のパスなので、**View側は
// ネットワークの存在を知らない**。
//
// **シーンを積み上げない**(リンクごとに SceneFunctions::Push しない)のは、
// シーンスタックの上限が kMaxSceneDepth=4 しかなく、リンクを4回たどると詰むため。
//
// 画面の構成:
//   [<] [>]                 [終了]   ← ヘッダ
//   --------------------------------
//              MarkdownView          ← 本文(ここだけスクロールする)
//   --------------------------------
//   http://host/docs/intro.md        ← フッタ(現在地。取得中やエラーはここへ出す)
class MarkdownScene : public Scene {
    private:
        MarkdownView* view = nullptr;
        Button* back_button = nullptr;
        Button* forward_button = nullptr;
        Button* exit_button = nullptr;
        Label<PICO_PATH_LEN>* status_label = nullptr;

        // ---------- 履歴 ----------
        // ブラウザと同じ扱い: 前方履歴は捨てる / 上限で最古を押し出す /
        // 離れるときのスクロール位置を控えて戻ったら復元する
        struct HistoryEntry {
            //URLは "http://" + host:port + path で96Bを超えうるのでLLを使う
            FixedString<PICO_STR_LL> location;
            int32_t scroll_y = 0;
        };

        constexpr static int kMaxHistory = 8;

        HistoryEntry history[kMaxHistory];
        int history_count = 0;
        int history_pos = -1;

        // ---------- 取得 ----------
        DocFetch fetch;
        bool fetching = false;

        constexpr static int MARGIN = 5;
        constexpr static int HEADER_H = 28;
        constexpr static int FOOTER_H = 18;
        constexpr static int NAV_BUTTON_W = 24;
        constexpr static int EXIT_BUTTON_W = 40;
        constexpr static int BUTTON_H = 18;

        bool pushHistory(const char* location);
        bool openCurrent();
        void refreshChrome();
        void showStatus(const char* message, int8_t color);
        void rememberScroll();

        // 現在地がリモートなら out へ入れて true
        bool currentAsUrl(Url& out) const;
        // 取得が終わったキャッシュを表示する
        void onFetchFinished();

        bool canGoBack() const { return history_pos > 0; }
        bool canGoForward() const { return history_pos >= 0 && history_pos + 1 < history_count; }

        void goBack();
        void goForward();
        void onLinkTap(const FixedString<PICO_PATH_LEN>& ref);

    public:
        // 開始する場所。nullptr/空なら network.cfg の browser-home、
        // それも無ければ同梱のサンプル文書を開く
        explicit MarkdownScene(const char* location = nullptr);

        const char* getName() const override { return "Markdown"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
};
