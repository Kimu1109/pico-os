#pragma once

#include "gui/core/Scene.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;
template<size_t N> class Label;

// バックグラウンドの仕事の一覧(設定アプリの「他」→「バックグラウンド」[一覧])。
//
//   [戻る] バックグラウンド
//   ♪ ミュージック          動作中      ← BackgroundFunctions の登録順。動いているものは青
//   💬 チャットの未読       入
//   ▢ 天気                  切
//   ─────────────────────
//   選んだ仕事の状態(2行)
//   [停止 / 切る / 入れる] [今すぐ] [開く]
//
// - ミュージックのような「今だけ動いている仕事」は[停止]、定期の仕事は[切る]/[入れる](/sys/background.cfg へ覚える)
// - [今すぐ] は定期の仕事を待たずに1回動かす。[開く] はその仕事のアプリを開く
// - 一覧と状態は0.5秒ごとに見直す(変わった行だけ書き直す)
class BackgroundScene : public Scene {
    public:
        const char* getName() const override { return "Background"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

    private:
        void rebuildList();
        void refreshTexts(bool force);
        void refreshButtons();
        void onToggle();
        void onRunNow();
        void onOpen();
        int selected() const;

        Button* back_button = nullptr;
        Button* toggle_button = nullptr;
        Button* run_button = nullptr;
        Button* open_button = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        Label<PICO_STR_512B>* detail_label = nullptr;
        ScrollList* list = nullptr;

        uint32_t seen_revision = 0;
        uint32_t last_refresh_ms = 0;
        int last_selected = -2;

        constexpr static int MARGIN = 6;
        constexpr static int kDetailLines = 3;
};
