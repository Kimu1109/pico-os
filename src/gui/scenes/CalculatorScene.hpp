#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/apps/CalculatorKeypad.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// 標準アプリの電卓。四則演算+丸括弧+√(平方根)+π(円周率)+計算履歴。
//
// ClocksSceneと同じ形: 上部に[戻る]+ページ切替のTabBar、本体はページに応じて
// 「電卓ページ(式+結果+キーパッド)」と「履歴ページ(ScrollList)」を丸ごと出し分ける。
// ウィジェットはどちらのページ分も onEnter() で作っておき、切替はsetVisible()だけで済ませる
// (ClocksSceneのapplyVisibility()と同じ狙い: 切り替えのたびにnew/deleteしない)。
class CalculatorScene : public Scene {
    public:
        // 上部のTabBarのタブ順と対応(index == (int)Page)
        enum class Page : uint8_t {
            Calculator = 0,
            History    = 1
        };

    private:
        // 履歴1件ぶん。式は電卓キーが組み立てる生の式(FixedString<PICO_STR_L>と同じ長さ)、
        // 結果は表示用に整形済みの文字列
        struct HistoryEntry {
            FixedString<PICO_STR_L> expression;
            FixedString<PICO_STR_M> result;
        };

        // 15件で約2.3KB(96+48Bぶんが15件)。電卓の履歴として十分な件数で、
        // シーンが破棄されれば(ランチャへ戻れば)一緒に解放される
        static constexpr int kMaxHistory = 15;

        Button* back_button = nullptr;
        TabBar* page_tab    = nullptr; // 電卓/履歴(常に出す)

        //電卓ページ
        Label<PICO_STR_L>* expr_label   = nullptr; // 入力中の式
        Label<PICO_STR_M>* result_label = nullptr; // 確定した結果 or 入力中のプレビュー
        CalculatorKeypad*  keypad       = nullptr;

        //履歴ページ
        Button*     history_clear_button = nullptr;
        ScrollList* history_list         = nullptr;

        Page page = Page::Calculator;

        // 入力中の式。KeyboardNumのinputsと同じくFixedString<PICO_STR_L>(96B)で持つ
        FixedString<PICO_STR_L> expression;

        // 直前に"="で確定した結果の文字列。"="の直後に演算子を押した場合、
        // ここから続けて計算する(一般的な電卓の挙動)
        FixedString<PICO_STR_M> result_value_text;

        // "="を押した直後かどうか。直後に数字/(/√/πを押すと新しい式として上書きし、
        // 演算子を押すと結果から続ける
        bool last_was_result = false;

        HistoryEntry history[kMaxHistory];
        int history_count = 0;

        constexpr static int MARGIN = 3;

        int top_row_h = 0;

        Rect bodyRect() const;

        // page(電卓/履歴)から全ウィジェットの表示/非表示を決める唯一の場所
        void applyPage();

        // キーパッドからの1キー分の入力を式へ反映する
        void onKey(const char* key);

        // 現在開いている(閉じていない)"("の数。0以下なら")"は無視する
        int openParenCount() const;

        // 式ラベル/結果ラベルを今のexpressionへ合わせて更新する。
        // 結果ラベルは"="が押されるまでは常に「入力中のプレビュー」で、
        // 式が不完全な間は評価に失敗して当然なのでエラーは出さず空に留める
        void refreshDisplays();

        // "="を押した時の確定処理。評価に失敗したら結果欄へ理由を出すだけで履歴には残さない
        void commitCalculation();

        // 履歴の先頭へ1件積む(古い順で末尾から溢れる)
        void pushHistory(const FixedString<PICO_STR_L>& expr, const FixedString<PICO_STR_M>& result);
        void clearHistory();
        void refreshHistoryList();

    public:
        const char* getName() const override { return "Calculator"; }

        void onEnter() override;
        void onExit() override;
};
