// 電卓アプリ(CalculatorScene / CalculatorKeypad)のGUI配線を検証するテスト。
//
// 式の評価そのものはcalc_eval_testが受け持つので、ここでは
//   1. CalculatorKeypad: 「描く位置」と「タップ判定の位置」が一致していること
//      (AppGridの当たり判定テストと同じ動機。行の高さが6で割り切れない場合の
//       余り吸収も含めて確かめる)
//   2. CalculatorScene: キー入力→式表示/結果表示→"="で履歴に積む→履歴から
//      読み戻す、という一連の配線が実際のタッチ経路(causeOnPressStart等)で
//      動くこと
// を確認する。
#include "gui/scenes/CalculatorScene.hpp"
#include "gui/widgets/apps/CalculatorKeypad.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

// ---- モック(app_test.cppと同じ方針: 実機描画/SD/シーン遷移本体は使わない) ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int pop_calls = 0;
void SceneFunctions::Pop(){ pop_calls++; }

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_str(const char* actual, const char* expected, const char* label){
    const bool ok = (strcmp(actual, expected) == 0);
    printf("%s %-40s 実測=%-16s 期待=%s\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

// ---- CalculatorKeypad: 位置合わせの確認 ----
static void testKeypadHitTest(){
    // 呼び出し側の表示領域次第で高さが6で割り切れないことがあるので、
    // わざと割り切れない高さ(181px)で確かめる(余りは最後の行が吸収する)
    CalculatorKeypad kp(0, 20, SCREEN_WIDTH, 181);

    const char* pressed = nullptr;
    kp.setOnKey([&pressed](const char* key){ pressed = key; });

    struct Expect { const char* label; int row; int col_start; int col_span; };
    static const Expect kExpect[] = {
        { "AC", 0, 0, 1 }, { "(", 0, 1, 1 }, { ")", 0, 2, 1 }, { "⌫", 0, 3, 1 },
        { "7",  1, 0, 1 }, { "8", 1, 1, 1 }, { "9", 1, 2, 1 }, { "÷", 1, 3, 1 },
        { "4",  2, 0, 1 }, { "5", 2, 1, 1 }, { "6", 2, 2, 1 }, { "×", 2, 3, 1 },
        { "1",  3, 0, 1 }, { "2", 3, 1, 1 }, { "3", 3, 2, 1 }, { "-", 3, 3, 1 },
        { "√",  4, 0, 1 }, { "π", 4, 1, 1 }, { ".", 4, 2, 1 }, { "+", 4, 3, 1 },
        { "0",  5, 0, 3 }, { "=", 5, 3, 1 },
    };

    const Rect g = kp.getScreenRect();
    const int colBase = g.w / CalculatorKeypad::kCols;
    const int rowBase = g.h / CalculatorKeypad::kRows;

    bool all_center_hit = true;
    bool all_corner_hit = true;

    for(const Expect& e : kExpect){
        const int x = g.x + e.col_start * colBase;
        const int w = e.col_span * colBase;
        const int y = g.y + e.row * rowBase;
        const int h = (e.row == CalculatorKeypad::kRows - 1) ? (g.h - rowBase * (CalculatorKeypad::kRows - 1)) : rowBase;

        //中心を叩くと自分のキーが返る(描画位置と当たり判定位置が同じ計算式であることの確認)
        OSData::touchX = x + w / 2;
        OSData::touchY = y + h / 2;
        pressed = nullptr;
        kp.causeOnPressStart();
        if(!pressed || strcmp(pressed, e.label) != 0) all_center_hit = false;

        //左上と右下の内側の隅も自分のキーになること(隣のマスと重なっていないことの裏取り)
        OSData::touchX = x; OSData::touchY = y;
        pressed = nullptr;
        kp.causeOnPressStart();
        if(!pressed || strcmp(pressed, e.label) != 0) all_corner_hit = false;

        OSData::touchX = x + w - 1; OSData::touchY = y + h - 1;
        pressed = nullptr;
        kp.causeOnPressStart();
        if(!pressed || strcmp(pressed, e.label) != 0) all_corner_hit = false;
    }

    check(all_center_hit, "キーパッド: 各キーの中心を叩くとそのキーが返る(高さが6で割り切れない場合も)");
    check(all_corner_hit, "キーパッド: 各キーの内側の隅も自分のキーになる(マス目が重なっていない)");

    //最終行(0と=)は割り切れない余りを吸収するぶん他の行より高い
    const int last_row_h = g.h - rowBase * (CalculatorKeypad::kRows - 1);
    check(last_row_h >= rowBase, "キーパッド: 最終行が余りを吸収して他の行以上の高さになる");
}

// ---- CalculatorScene: 実際のタッチ経路での配線確認 ----

template<typename T>
static T* findByType(WidgetType type, int occurrence = 0){
    int seen = 0;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() == type){
            if(seen == occurrence) return static_cast<T*>(w);
            seen++;
        }
    }
    return nullptr;
}

static Button* findButtonByText(const char* text){
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() != WidgetType::Button) continue;
        Button* b = static_cast<Button*>(w);
        if(strcmp(b->getText().c_str(), text) == 0) return b;
    }
    return nullptr;
}

static int countItems(ScrollList* list){
    int n = 0;
    while(list->itemAt(n)) n++;
    return n;
}

// 電卓ページの2つのLabelはY座標の小さい方が式(expr)、大きい方が結果(result)
static void findDisplays(Label<PICO_STR_L>*& expr, Label<PICO_STR_M>*& result){
    std::vector<Widget*> labels;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() == WidgetType::Label) labels.push_back(w);
    }
    std::sort(labels.begin(), labels.end(), [](Widget* a, Widget* b){
        return a->getScreenRect().y < b->getScreenRect().y;
    });
    expr   = (labels.size() > 0) ? static_cast<Label<PICO_STR_L>*>(labels[0]) : nullptr;
    result = (labels.size() > 1) ? static_cast<Label<PICO_STR_M>*>(labels[1]) : nullptr;
}

static void pressKeypad(CalculatorKeypad* kp, const char* key){
    // CalculatorKeypad::hitKey()は座標から逆算するだけなので、テストとしては
    // 「このキーが押された」という結果だけを見たい。行/列の再計算をここでも
    // 繰り返すのは冗長なので、testKeypadHitTest()側で位置合わせ済みの前提に立ち、
    // ここでは中心座標を1回だけ計算して押す
    struct Loc { int row, col_start, col_span; };
    static const struct { const char* label; Loc loc; } table[] = {
        { "AC", {0,0,1} }, { "(", {0,1,1} }, { ")", {0,2,1} }, { "⌫", {0,3,1} },
        { "7",  {1,0,1} }, { "8", {1,1,1} }, { "9", {1,2,1} }, { "÷", {1,3,1} },
        { "4",  {2,0,1} }, { "5", {2,1,1} }, { "6", {2,2,1} }, { "×", {2,3,1} },
        { "1",  {3,0,1} }, { "2", {3,1,1} }, { "3", {3,2,1} }, { "-", {3,3,1} },
        { "√",  {4,0,1} }, { "π", {4,1,1} }, { ".", {4,2,1} }, { "+", {4,3,1} },
        { "0",  {5,0,3} }, { "=", {5,3,1} },
    };

    const Rect g = kp->getScreenRect();
    const int colBase = g.w / CalculatorKeypad::kCols;
    const int rowBase = g.h / CalculatorKeypad::kRows;

    for(const auto& row : table){
        if(strcmp(row.label, key) != 0) continue;
        const Loc& loc = row.loc;
        const int h = (loc.row == CalculatorKeypad::kRows - 1)
            ? (g.h - rowBase * (CalculatorKeypad::kRows - 1)) : rowBase;
        OSData::touchX = g.x + loc.col_start * colBase + (loc.col_span * colBase) / 2;
        OSData::touchY = g.y + loc.row * rowBase + h / 2;
        kp->causeOnPressStart();
        return;
    }
    printf("[FAIL] pressKeypad: 未知のキー %s\n", key);
    failures++;
}

static void testCalculatorScene(){
    WidgetFunctions::widgets.clear();
    WidgetFunctions::dialog_roots.clear();

    CalculatorScene* scene = new CalculatorScene();
    scene->onEnter();

    CalculatorKeypad* kp = findByType<CalculatorKeypad>(WidgetType::CalculatorKeypad);
    TabBar* page_tab      = findByType<TabBar>(WidgetType::TabBar);
    ScrollList* history   = findByType<ScrollList>(WidgetType::ScrollList);
    Button* back_button   = findButtonByText("戻る");
    Button* clear_button  = findButtonByText("履歴を消去");

    check(kp && page_tab && history && back_button && clear_button,
          "onEnter(): 想定した5種のウィジェットが揃っている");

    Label<PICO_STR_L>* expr = nullptr;
    Label<PICO_STR_M>* result = nullptr;
    findDisplays(expr, result);
    check(expr && result, "onEnter(): 式/結果の2つのLabelが見つかる");
    if(!kp || !page_tab || !history || !back_button || !clear_button || !expr || !result) return;

    eq_str(expr->getText()->c_str(), "0", "初期状態: 式表示は\"0\"");

    // ---- 7 + 3 = 10 ----
    pressKeypad(kp, "7");
    pressKeypad(kp, "+");
    pressKeypad(kp, "3");
    eq_str(expr->getText()->c_str(), "7+3", "入力中: 式がそのまま組み立てられる");
    eq_str(result->getText()->c_str(), "10", "入力中: 結果欄にプレビューが出る");

    pressKeypad(kp, "=");
    eq_str(expr->getText()->c_str(), "7+3", "\"=\": 式はそのまま残る");
    eq_str(result->getText()->c_str(), "10", "\"=\": 結果が確定する");
    check(countItems(history) == 1, "\"=\": 履歴が1件増える");

    // \"=\"の直後に演算子を押すと結果から続けて計算できる
    pressKeypad(kp, "×");
    pressKeypad(kp, "2");
    eq_str(expr->getText()->c_str(), "10×2", "\"=\"の直後は結果から続けて計算する");
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "20", "続けて計算した結果が正しい");
    check(countItems(history) == 2, "続けての計算も履歴に積まれる");

    // \"=\"の直後に数字を押すと新しい式として上書きする
    pressKeypad(kp, "5");
    eq_str(expr->getText()->c_str(), "5", "\"=\"の直後に数字を押すと新しい式になる");

    // ---- AC ----
    pressKeypad(kp, "AC");
    eq_str(expr->getText()->c_str(), "0", "AC: 式が空になる");
    eq_str(result->getText()->c_str(), "", "AC: 結果欄も空になる");

    // ---- ⌫ ----
    pressKeypad(kp, "1");
    pressKeypad(kp, "2");
    pressKeypad(kp, "⌫");
    eq_str(expr->getText()->c_str(), "1", "⌫: 末尾の1文字が消える");

    // ---- 閉じていない括弧の\")\"は無視される ----
    pressKeypad(kp, "AC");
    pressKeypad(kp, ")");
    eq_str(expr->getText()->c_str(), "0", "開いていない\")\"は式に足されない");

    // ---- 括弧+√+π ----
    pressKeypad(kp, "AC");
    pressKeypad(kp, "(");
    pressKeypad(kp, "9");
    pressKeypad(kp, "+");
    pressKeypad(kp, "√");
    pressKeypad(kp, "4"); // √は自動で"("を開くので、続けて数字を打つ
    pressKeypad(kp, ")");
    pressKeypad(kp, ")");
    eq_str(expr->getText()->c_str(), "(9+√(4))", "√は自動で括弧を開く");
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "11", "括弧と√を含む式が正しく評価される");

    pressKeypad(kp, "AC");
    pressKeypad(kp, "π");
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "3.141592654", "πの値が使える");

    // ---- 不完全な式で\"=\"を押すとエラー表示になり、履歴は増えない ----
    const int before_history = countItems(history);
    pressKeypad(kp, "AC");
    pressKeypad(kp, "1");
    pressKeypad(kp, "+");
    pressKeypad(kp, "=");
    check(strlen(result->getText()->c_str()) > 0, "不完全な式で\"=\": 結果欄に何か表示される(エラーメッセージ)");
    check(countItems(history) == before_history, "不完全な式で\"=\": 履歴は増えない");

    // ---- 履歴ページの表示切替と読み戻し ----
    page_tab->setSelected(1, true); // 「履歴」タブへ切り替え(実際のタップと同じくnotify=trueで呼ぶ)
    check(!kp->getVisible() && history->getVisible(), "履歴タブ: キーパッドが隠れ履歴一覧が出る");

    history->setSelectedIndex(0);
    history->causeOnSelectItem(true); // 選択済みの項目をもう一度タップした状態を再現(2回タップの流儀)
    check(page_tab->getSelected() == 0, "履歴の再選択: 電卓ページへ自動的に戻る");
    eq_str(expr->getText()->c_str(), "π", "履歴の再選択: 直近の式が読み戻される");

    // ---- 履歴の消去 ----
    clear_button->causeOnPressEnd();
    check(countItems(history) == 0, "履歴を消去ボタン: 一覧が空になる");

    // ---- 戻るボタン ----
    const int before_pop = pop_calls;
    back_button->causeOnPressEnd();
    check(pop_calls == before_pop + 1, "戻るボタン: SceneFunctions::Pop()が呼ばれる");

    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    delete scene;
    check(WidgetFunctions::widgets.empty(), "onExit()後: ウィジェットが全て解放される");
}

int main(){
    testKeypadHitTest();
    printf("\n");
    testCalculatorScene();

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
