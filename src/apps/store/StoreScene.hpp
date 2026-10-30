#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ProgressBar.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "apps/store/App_Store.hpp"

#include <cstdint>

// アプリストア。配布元(docs/APP_REPO.md、既定は GitHub Pages の /apps/)からLuaアプリを取ってきて入れる。
//
//   [戻る] アプリストア      [設定][更新]
//   一覧(入っている=緑、更新あり=青、入れられない=赤)。取り下げたアプリは、入っているときだけ並べる
//   (危険の印が付いて入っているものは先頭に並べ、状態の行で削除をすすめる)。
//   まとめて更新できるものがあれば、一覧の先頭に「すべて更新 (N件)」の行を出す
//   (権限が増える更新・前の版へ戻して見送っている版・本体が古くて入れられないものは入れない)
//   選んだアプリの欄(名前・版・大きさ / 権限 / 説明)
//   進み具合のバー + 状態の1行
//   [入れる/更新] [開く] [削除](前の版が残っていれば [戻す/削除]。選ぶダイアログを出す)
//
// 通信とインストールは AppStore(apps/store/App_Store)。約27KBあるので、この画面が開いている間だけ new する
// (シーン本体は数十バイト)。HTTPSの接続(TLSの約40KB)は画面を離れるときに閉じる。
// 入れる前に、一覧に書かれた権限を確認のダイアログで見せる(インストール時に app.cfg と突き合わせ、
// それより強い権限が付いていれば入れない)。
class StoreScene : public Scene {
    public:
        static constexpr const char* kAppName = AppStore::kAppName;

    private:
        Button* back_button = nullptr;
        Button* refresh_button = nullptr;
        Button* settings_button = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        ScrollList* app_list = nullptr;
        Label<PICO_STR_512B>* detail_label = nullptr;
        ProgressBar* progress = nullptr;
        Label<PICO_STR_LL>* status_label = nullptr;
        Button* install_button = nullptr;
        Button* open_button = nullptr;
        Button* remove_button = nullptr;

        AppStore* store = nullptr;

        // 一覧で選んでいるアプリ(AppStoreの番号)。idで覚えて、一覧を作り直しても戻す
        FixedString<PICO_STR_S> selected_id;
        // 確認ダイアログの間に一覧が変わっても別のアプリを触らないよう、idで覚える
        FixedString<PICO_STR_S> confirm_id;

        // 一覧の行 → AppStoreの番号(取り下げたアプリを飛ばすので、そのままの番号ではない)。kRowUpdateAll は「すべて更新」の行
        constexpr static int8_t kRowUpdateAll = -2;
        int8_t rows[AppRepo::kMaxEntries + 1] = {};
        int row_count = 0;
        // 危険の印が付いて入っているアプリの知らせ(無ければ空)
        FixedString<PICO_STR_LL> blocked_warning;

        FixedString<PICO_STR_LL> notice;
        int8_t notice_color = PICO_DARKGREY;

        uint32_t seen_revision = 0;
        AppStore::State seen_state = AppStore::State::Idle;
        int frames_since_enter = 0;
        bool auto_refresh = false;
        bool pending_launch = false;

        constexpr static int MARGIN = 3;

        int selectedIndex() const;
        void refreshList();
        void refreshDetail();
        void refreshStatus();
        void refreshButtons();
        void setNotice(const char* text, int8_t color = PICO_DARKGREY);
        void startRefresh();
        void confirmInstall();
        void confirmRemove();
        void confirmUpdateAll();
        void chooseRollbackOrRemove();
        void confirmRollback();
        void openSettings();

    public:
        const char* getName() const override { return "Store"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};
