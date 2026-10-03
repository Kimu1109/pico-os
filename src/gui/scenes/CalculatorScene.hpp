#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/apps/CalculatorKeypad.hpp"
#include "gui/widgets/apps/GraphView.hpp"
#include "util/Calc_Eval.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// 標準アプリの電卓(関数電卓+グラフ電卓+計算履歴)。
//
// ClocksSceneと同じ形: 上部に[戻る]+ページ切替のTabBar、本体はページに応じて
// 「電卓ページ(式+結果+キーパッド)」「グラフページ」「履歴ページ(ScrollList)」を丸ごと出し分ける。
// ウィジェットは全ページ分を onEnter() で作っておき、切替はsetVisible()だけで済ませる
// (ClocksSceneのapplyVisibility()と同じ狙い: 切り替えのたびにnew/deleteしない)。
//
// グラフページは「式を入れる」(3本の式の一覧+キーパッド。=の位置が[描画])と
// 「グラフを見る」(GraphView+拡大/縮小/初期化)の2つの表示を切り替える。
// 式の評価はどれも util/Calc_Eval.hpp。角度の単位(DEG/RAD/GRA)はどのページでも共通。
class CalculatorScene : public Scene {
    public:
        // 上部のTabBarのタブ順と対応(index == (int)Page)
        enum class Page : uint8_t {
            Calculator = 0,
            Graph      = 1,
            History    = 2
        };

        using Expr = FixedString<PICO_STR_LL>;

    private:
        // 履歴1件ぶん。式は電卓キーが組み立てる生の式、結果は表示用に整形済みの文字列
        struct HistoryEntry {
            Expr expression;
            FixedString<PICO_STR_M> result;
        };

        // 15件で約3.6KB。シーンが破棄されれば(ランチャへ戻れば)一緒に解放される
        static constexpr int kMaxHistory = 15;
        static constexpr int kGraphFns = GraphView::kMaxFunctions;

        Button* back_button = nullptr;
        TabBar* page_tab    = nullptr; // 電卓/グラフ/履歴(常に出す)

        //電卓ページ
        Label<PICO_STR_LL>* expr_label   = nullptr; // 入力中の式
        Label<PICO_STR_M>*  result_label = nullptr; // 確定した結果 or 入力中のプレビュー
        CalculatorKeypad*   keypad       = nullptr;

        //グラフページ(式を入れる)
        ScrollList*        graph_list    = nullptr; // y1〜y3。タップで編集する式を選ぶ
        Label<PICO_STR_M>* graph_status  = nullptr; // 選んだ式の誤り等
        CalculatorKeypad*  graph_keypad  = nullptr;
        //グラフページ(グラフを見る)
        GraphView*         graph_view    = nullptr;
        Button*            graph_edit_button  = nullptr; // 「式」へ戻る
        Button*            graph_zoomin_button  = nullptr;
        Button*            graph_zoomout_button = nullptr;
        Button*            graph_reset_button   = nullptr;

        //履歴ページ
        Button*     history_clear_button = nullptr;
        ScrollList* history_list         = nullptr;

        Page page = Page::Calculator;
        bool graph_plotting = false; // グラフページで「グラフを見る」側を出しているか

        CalcEval::AngleMode angle = CalcEval::AngleMode::Deg;
        // グラフの角度の単位は電卓とは別に持つ。既定はラジアン
        // (度のままだとx=-10〜10でsin(x)がほぼ平らな線になり、グラフとして読めないため)
        CalcEval::AngleMode graph_angle = CalcEval::AngleMode::Rad;
        double ans = 0.0; // 直前に=で確定した答え(Ans)

        // 入力中の式
        Expr expression;

        // "="を押した直後かどうか。直後に数字/(/関数を押すと新しい式として上書きし、
        // 演算子を押すと"Ans"から続ける(一般的な関数電卓の挙動)
        bool last_was_result = false;

        Expr graph_exprs[kGraphFns];
        int graph_sel = 0;

        HistoryEntry history[kMaxHistory];
        int history_count = 0;

        constexpr static int MARGIN = 3;

        int top_row_h = 0;

        Rect bodyRect() const;

        // page/graph_plottingから全ウィジェットの表示/非表示を決める唯一の場所
        void applyPage();

        CalcEval::Context context() const;

        // キーパッド(または物理キーボード)からの1キー分の入力
        void handleKey(const char* key);
        void handleGraphKey(const char* key);

        // 角度の単位を DEG→RAD→GRA→DEG と切り替える。グラフのページでは
        // グラフ専用の単位(graph_angle)を、それ以外のページでは電卓の単位(angle)を切り替える
        void cycleAngle();

        // 式ラベル/結果ラベルを今のexpressionへ合わせて更新する。
        // 結果ラベルは"="が押されるまでは常に「入力中のプレビュー」で、
        // 式が不完全な間は評価に失敗して当然なのでエラーは出さず空に留める
        void refreshDisplays();
        // 結果欄へ文字を出す(長ければ字を小さくして収める)
        void setResultText(const char* text, int8_t color, bool is_error);

        // "="を押した時の確定処理。評価に失敗したら結果欄へ理由を出すだけで履歴には残さない
        void commitCalculation();

        void refreshGraphList();
        void refreshGraphStatus();
        void showGraph(bool plotting);

        // 履歴の先頭へ1件積む(古い順で末尾から溢れる)
        void pushHistory(const Expr& expr, const FixedString<PICO_STR_M>& result);
        void clearHistory();
        void refreshHistoryList();

    public:
        // ---- 式の組み立て(ホストテストからも使う) ----
        // 開いている(閉じていない)"("の数
        static int OpenParenCount(const char* expr);
        // 閉じていない括弧を閉じた式を out へ(=を押したときに自動で閉じる)
        static void CloseParens(const Expr& in, Expr& out);
        // 末尾の1つぶん(関数名なら"sin("ごと)を消す
        static void RemoveLastToken(Expr& e);
        // 1キーぶんを足す。")"は開いている"("が無ければ足さない。入りきらなければfalse
        static bool AppendToken(Expr& e, const char* token);
        // 数値を表示用に("1e+20" は式へ戻せるよう "1E20" にする)
        static void FormatNumber(double v, FixedString<PICO_STR_M>& out);

        const char* getName() const override { return "Calculator"; }

        void onEnter() override;
        void onExit() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;
};
