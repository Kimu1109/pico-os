#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/MarkdownView.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "net/Doc_Fetch.hpp"
#include "net/Discovery.hpp"
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
        Button* home_button = nullptr;
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

        // 履歴の現在地(history_pos)は**相対リンクを解決する基準**でもあるため、
        // 「表示中の文書」と食い違わせてはいけない。開けなかった場所を現在地の
        // まま残すと、画面には前の文書が出ているのに次に踏んだリンクだけが
        // 開けなかった場所を基準に解決される、という状態になる。
        //   → 遷移が失敗したら commitNavigation() ではなく abortNavigation() を
        //      通して、表示中の位置まで巻き戻すこと。
        int shown_pos = -1;        // 実際に表示できている履歴の位置
        bool pushed_for_nav = false; // 今の遷移で履歴を1つ積んだか(失敗時に捨てる)

        // ---------- 取得 ----------
        // 文書を取り、続けて**表示前に**画像を取る。
        //
        // 画像を先に揃えるのは、MarkdownView::layoutBlocks()が画像ファイルの
        // ヘッダを読んでブロックの高さを決めているため。表示してから届けると
        // 再レイアウトが要る(全ブロックの整形をやり直すことになる)。
        //
        // ただし「表示前」であって「固まる」ではない。1フレーム1枚ずつ進めるので
        // ループは回り続け、フッタに進捗を出せる。
        enum class Phase : uint8_t {
            Idle,
            Discovery, // サーバ情報(/.well-known/pico-os)を問い合わせ中
            Document,  // 文書そのものを取得中
            Images,    // 画像を取得中
        };

        // 1ページで取りに行く画像の上限。病的な文書で延々と待たされないため
        constexpr static int kMaxPrefetchImages = 8;

        DocFetch fetch;
        Phase phase = Phase::Idle;

        // 今のサーバの情報。ホストが変わったときだけ問い合わせ直す
        // (404でもcheckedが立つので、ページごとに問い合わせ直さない)
        ServerInfo server_info;

        // ホームボタンの行き先(network.cfgのbrowser-home、または開始時の場所)。
        // リモートではサーバが申告したhomeを優先する
        FixedString<PICO_STR_LL> initial_home;

        Url doc_url;                                  // 画像の解決基準
        FixedString<PICO_PATH_LEN> doc_cache_path;    // 最後にload()するパス
        FixedString<PICO_STR_L> pending_images[kMaxPrefetchImages];
        int pending_count = 0;
        int pending_index = 0;

        // 文書の取得を始める(discoveryの後、または最初から)
        bool startDocumentFetch();
        // ホームへ移動する
        void goHome();
        // ホームボタンの行き先。無ければ空
        bool homeTarget(FixedString<PICO_STR_LL>& out) const;

        // 文書を走査して、まだキャッシュに無い画像参照を pending_images へ積む
        void collectMissingImages();
        // pending_index の画像の取得を始める。始められなければ false
        bool startNextImage();
        // 画像をあきらめて(あるいは全部揃って)本文を表示する
        void showDocument();

        constexpr static int MARGIN = 5;
        constexpr static int HEADER_H = 28;
        constexpr static int FOOTER_H = 18;
        constexpr static int NAV_BUTTON_W = 24;
        constexpr static int HOME_BUTTON_W = 48;
        constexpr static int EXIT_BUTTON_W = 40;
        constexpr static int BUTTON_H = 18;

        bool pushHistory(const char* location);
        bool openCurrent();

        // 表示できたので、履歴の現在地を「表示中の位置」として確定する
        void commitNavigation();
        // 遷移に失敗したので、表示中の文書の位置まで巻き戻す
        void abortNavigation();

        void refreshChrome();
        // ヘッダのボタンの色だけ更新する(フッタに出した失敗の理由は消さない)
        void refreshNavButtons();
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
