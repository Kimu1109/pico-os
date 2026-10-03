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
#include "gui/widgets/apps/GraphView.hpp"
#include <cmath>
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
static void tapAt(Widget* w, int x, int y){
    OSData::touchX = x; OSData::touchY = y;
    w->causeOnPressStart();
}

static void testKeypadHitTest(){
    // 呼び出し側の表示領域次第で高さが7で割り切れないことがあるので、
    // わざと割り切れない高さ(181px)で確かめる(余りは最後の行が吸収する)
    CalculatorKeypad kp(0, 20, SCREEN_WIDTH, 181);

    const char* pressed = nullptr;
    char last[16] = "";
    kp.setOnKey([&](const char* key){ pressed = key; snprintf(last, sizeof(last), "%s", key); });

    struct Expect { const char* label; const char* insert; };
    static const Expect kExpect[] = {
        { "(", "(" }, { ")", ")" }, { "AC", "AC" }, { "DEG", "DRG" },
        { "sin", "sin(" }, { "cos", "cos(" }, { "tan", "tan(" }, { "log", "log(" }, { "ln", "ln(" }, { "DEL", "DEL" },
        { "√", "√(" }, { "x^2", "^2" }, { "x^y", "^" }, { "x^-1", "^(-1)" }, { "x!", "!" }, { "π", "π" },
        { "7", "7" }, { "8", "8" }, { "9", "9" }, { "nCr", "C" }, { "mod", "mod" }, { "÷", "÷" },
        { "4", "4" }, { "5", "5" }, { "6", "6" }, { ",", "," }, { "e", "e" }, { "×", "×" },
        { "1", "1" }, { "2", "2" }, { "3", "3" }, { "EXP", "E" }, { "Ans", "Ans" }, { "-", "-" },
        { "0", "0" }, { ".", "." }, { "%", "%" }, { "=", "=" }, { "+", "+" },
    };

    bool all_hit = true;
    for(const Expect& e : kExpect){
        int x = 0, y = 0;
        if(!kp.keyCenter(e.label, x, y)){ printf("  キーが無い: %s\n", e.label); all_hit = false; continue; }
        pressed = nullptr;
        tapAt(&kp, x, y);
        if(!pressed || strcmp(last, e.insert) != 0){ printf("  %s → %s\n", e.label, pressed ? last : "(なし)"); all_hit = false; }
    }
    check(all_hit, "キーパッド: 各キーの中心を叩くとそのキーの文字列が返る(高さが7で割り切れない場合も)");

    // 隅: 左上のキーの左上隅と、右下のキーの右下隅
    const Rect g = kp.getScreenRect();
    pressed = nullptr;
    tapAt(&kp, g.x + g.w - 1, g.y + g.h - 1);
    check(pressed && strcmp(last, "+") == 0, "キーパッド: 右下の隅は\"+\"(最終行が余りを吸収する)");

    // SHIFT: 裏の機能に変わり、1回押すと戻る
    int sx, sy, x, y;
    check(kp.keyCenter("SHIFT", sx, sy), "キーパッド: SHIFTがある");
    pressed = nullptr;
    tapAt(&kp, sx, sy);
    check(pressed == nullptr && kp.isShift(), "SHIFT: 押しても文字は送らず、SHIFT中になる");
    check(kp.keyCenter("asin", x, y) && kp.keyCenter("10^x", x, y) && kp.keyCenter("nPr", x, y),
          "SHIFT中: asin/10^x/nPrが見える");
    kp.keyCenter("asin", x, y);
    tapAt(&kp, x, y);
    check(pressed && strcmp(last, "asin(") == 0, "SHIFT+sin → asin(");
    check(!kp.isShift(), "SHIFTは1回きりで戻る");

    // HYP / SHIFT+HYP
    int hx, hy;
    kp.keyCenter("HYP", hx, hy);
    tapAt(&kp, hx, hy);
    kp.keyCenter("cosh", x, y);
    tapAt(&kp, x, y);
    check(strcmp(last, "cosh(") == 0, "HYP+cos → cosh(");
    tapAt(&kp, sx, sy);
    tapAt(&kp, hx, hy);
    check(kp.keyCenter("atanh", x, y), "SHIFT+HYP中: atanhが見える");
    tapAt(&kp, x, y);
    check(strcmp(last, "atanh(") == 0, "SHIFT+HYP+tan → atanh(");

    // グラフのときは mod の位置が x、= が描画
    kp.setGraphMode(true);
    check(kp.keyCenter("x", x, y), "グラフ: xキーがある");
    tapAt(&kp, x, y);
    check(strcmp(last, "x") == 0, "グラフ: xキーでxが入る");
    check(kp.keyCenter("描画", x, y), "グラフ: =は[描画]");
    tapAt(&kp, sx, sy);
    check(kp.keyCenter("mod", x, y), "グラフ: SHIFT+x はmod");
    tapAt(&kp, x, y);
    check(strcmp(last, "mod") == 0, "グラフ: SHIFT+xでmodが入る");
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
static void findDisplays(Label<PICO_STR_LL>*& expr, Label<PICO_STR_M>*& result){
    std::vector<Widget*> labels;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() == WidgetType::Label && w->getVisible()) labels.push_back(w);
    }
    std::sort(labels.begin(), labels.end(), [](Widget* a, Widget* b){
        return a->getScreenRect().y < b->getScreenRect().y;
    });
    expr   = (labels.size() > 0) ? static_cast<Label<PICO_STR_LL>*>(labels[0]) : nullptr;
    result = (labels.size() > 1) ? static_cast<Label<PICO_STR_M>*>(labels[1]) : nullptr;
}

static void pressKeypad(CalculatorKeypad* kp, const char* key){
    int x = 0, y = 0;
    if(!kp->keyCenter(key, x, y)){
        printf("[FAIL] pressKeypad: 未知のキー %s\n", key);
        failures++;
        return;
    }
    tapAt(kp, x, y);
}

static void testCalculatorScene(){
    WidgetFunctions::widgets.clear();
    WidgetFunctions::dialog_roots.clear();

    CalculatorScene* scene = new CalculatorScene();
    scene->onEnter();

    CalculatorKeypad* kp = findByType<CalculatorKeypad>(WidgetType::CalculatorKeypad);
    TabBar* page_tab      = findByType<TabBar>(WidgetType::TabBar);
    CalculatorKeypad* gkp = findByType<CalculatorKeypad>(WidgetType::CalculatorKeypad, 1);
    ScrollList* glist     = findByType<ScrollList>(WidgetType::ScrollList, 0);
    ScrollList* history   = findByType<ScrollList>(WidgetType::ScrollList, 1);
    GraphView* gview      = findByType<GraphView>(WidgetType::GraphView);
    Button* back_button   = findButtonByText("戻る");
    Button* clear_button  = findButtonByText("履歴を消去");

    check(kp && gkp && glist && gview && page_tab && history && back_button && clear_button,
          "onEnter(): 想定したウィジェットが揃っている");
    if(!gkp || !glist || !gview) return;

    Label<PICO_STR_LL>* expr = nullptr;
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
    check(result->getTextColor() == PICO_DARKGREY, "入力中: プレビューは灰色(未確定と分かる)");

    pressKeypad(kp, "=");
    eq_str(expr->getText()->c_str(), "7+3", "\"=\": 式はそのまま残る");
    eq_str(result->getText()->c_str(), "10", "\"=\": 結果が確定する");
    check(result->getTextColor() == PICO_FORECOLOR, "\"=\": 確定した答えは黒くはっきり出る");
    check(countItems(history) == 1, "\"=\": 履歴が1件増える");

    // \"=\"の直後に演算子を押すと結果から続けて計算できる
    pressKeypad(kp, "×");
    pressKeypad(kp, "2");
    eq_str(expr->getText()->c_str(), "Ans×2", "\"=\"の直後に演算子を押すとAnsから続けて計算する");
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

    // ---- DEL(削除) ----
    pressKeypad(kp, "1");
    pressKeypad(kp, "2");
    pressKeypad(kp, "DEL");
    eq_str(expr->getText()->c_str(), "1", "DEL: 末尾の1文字が消える");
    pressKeypad(kp, "sin");
    eq_str(expr->getText()->c_str(), "1sin(", "関数キーは括弧ごと入る");
    pressKeypad(kp, "DEL");
    eq_str(expr->getText()->c_str(), "1", "DEL: 関数は\"sin(\"ごと消える");

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
    check(result->getTextColor() == PICO_RED, "不完全な式で\"=\": エラーは赤色で区別される");
    check(result->getFontSize() == FontFn::Small,
          "不完全な式で\"=\": エラー文言はBig(32px)だと画面に収まらないためSmallへ縮む");
    //Smallなら日本語1文字16pxなので、最長のエラー文言でも表示幅(maxWidth)に収まる。
    //textWidth()はOSData::frameへ最後に適用したフォントに依存するため、明示的にSmallへ
    //してから測る(Labelのrelayout()自身がfontApply()/fontDefault()で行うのと同じ手順)
    FontFn::SetSmall();
    const int error_text_w = OSData::frame->textWidth(result->getText()->c_str());
    FontFn::SetDefault();
    check(error_text_w <= result->getMaxWidth(),
          "不完全な式で\"=\": エラー文言の実測幅が表示領域に収まる");
    check(countItems(history) == before_history, "不完全な式で\"=\": 履歴は増えない");

    // ---- エラーの後に数字を打つとBig(32px)のプレビューへ戻る ----
    pressKeypad(kp, "AC");
    pressKeypad(kp, "5");
    check(result->getFontSize() == FontFn::Big, "エラーの後でも数字を打てばプレビューは通常のフォントへ戻る");

    // ---- 関数電卓 ----
    pressKeypad(kp, "AC");
    pressKeypad(kp, "sin");
    pressKeypad(kp, "3");
    pressKeypad(kp, "0");
    eq_str(result->getText()->c_str(), "0.5", "閉じていない括弧でもプレビューが出る(sin(30)、度)");
    pressKeypad(kp, "=");
    eq_str(expr->getText()->c_str(), "sin(30)", "\"=\"で閉じ忘れた括弧が自動で閉じる");
    eq_str(result->getText()->c_str(), "0.5", "sin(30°) = 0.5");

    pressKeypad(kp, "DEG"); // → RAD
    pressKeypad(kp, "AC");
    pressKeypad(kp, "SHIFT");
    pressKeypad(kp, "e"); // SHIFTのπの位置はe
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "2.718281828", "SHIFT+π → e");
    pressKeypad(kp, "AC");
    pressKeypad(kp, "cos");
    pressKeypad(kp, "π");
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "-1", "RAD: cos(π) = -1");
    pressKeypad(kp, "RAD"); // → GRA
    pressKeypad(kp, "GRA"); // → DEG
    pressKeypad(kp, "AC");
    pressKeypad(kp, "5");
    pressKeypad(kp, "nCr");
    pressKeypad(kp, "2");
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "10", "5C2 = 10");
    pressKeypad(kp, "x^2");
    pressKeypad(kp, "=");
    eq_str(expr->getText()->c_str(), "Ans^2", "=の後のx^2はAnsの2乗");
    eq_str(result->getText()->c_str(), "100", "Ans^2 = 100");
    pressKeypad(kp, "AC");
    pressKeypad(kp, "1");
    pressKeypad(kp, "0");
    pressKeypad(kp, "x^y");
    pressKeypad(kp, "2");
    pressKeypad(kp, "5");
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "1E25", "大きな答えは指数表記(E)で出る");
    check(result->getFontSize() == FontFn::Big, "短い指数表記はBigのまま");
    pressKeypad(kp, "AC");
    pressKeypad(kp, "2");
    pressKeypad(kp, "÷");
    pressKeypad(kp, "3");
    pressKeypad(kp, "=");
    eq_str(result->getText()->c_str(), "0.6666666667", "2÷3");
    {
        FontFn::SetBig();
        const int w_big = OSData::frame->textWidth(result->getText()->c_str());
        FontFn::SetDefault();
        check(w_big <= result->getMaxWidth() || result->getFontSize() != FontFn::Big,
              "長い答えはBigで収まらなければ字を小さくする");
    }

    pressKeypad(kp, "AC");
    pressKeypad(kp, "5");
    pressKeypad(kp, "=");
    check(result->getFontSize() == FontFn::Big, "確定した数値の答えは通常のフォントで出る");

    // ---- 履歴ページの表示切替と読み戻し ----
    page_tab->setSelected(2, true); // 「履歴」タブへ切り替え(実際のタップと同じくnotify=trueで呼ぶ)
    check(!kp->getVisible() && history->getVisible(), "履歴タブ: キーパッドが隠れ履歴一覧が出る");

    history->setSelectedIndex(0);
    history->causeOnSelectItem(true); // 選択済みの項目をもう一度タップした状態を再現(2回タップの流儀)
    check(page_tab->getSelected() == 0, "履歴の再選択: 電卓ページへ自動的に戻る");
    eq_str(expr->getText()->c_str(), "5", "履歴の再選択: 直近の式が読み戻される");

    // ---- グラフ ----
    page_tab->setSelected(1, true);
    check(gkp->getVisible() && glist->getVisible() && !gview->getVisible() && !kp->getVisible(),
          "グラフタブ: 式の一覧とキーパッドが出る");
    pressKeypad(gkp, "2");
    pressKeypad(gkp, "x");
    pressKeypad(gkp, "+");
    pressKeypad(gkp, "1");
    eq_str(glist->itemAt(0)->text.c_str(), "y1=2x+1", "グラフ: y1の式を入れる");
    glist->setSelectedIndex(1);
    glist->causeOnSelectItem(false);
    pressKeypad(gkp, "sin");
    pressKeypad(gkp, "x");
    eq_str(glist->itemAt(1)->text.c_str(), "y2=sin(x", "グラフ: 一覧で選んだy2へ入る");
    pressKeypad(gkp, "描画");
    eq_str(glist->itemAt(1)->text.c_str(), "y2=sin(x)", "[描画]で括弧が閉じる");
    check(gview->getVisible() && !gkp->getVisible(), "[描画]: グラフが出る");
    {
        // 初期の範囲は横-10〜10。列の中心のxでの値になる
        const Rect g = gview->getScreenRect();
        const int col = g.w / 2;
        const double x = gview->getXMin() + (gview->getXMax() - gview->getXMin()) * (col + 0.5) / g.w;
        const float y1 = gview->sampleAt(0, col);
        check(std::fabs(y1 - (2 * x + 1)) < 1e-4, "グラフ: y1の値が2x+1");
        const float y3 = gview->sampleAt(2, col);
        check(std::isnan(y3), "グラフ: 空のy3は描かない");
        const float y2 = gview->sampleAt(1, col);
        check(std::fabs(y2 - std::sin(x)) < 1e-4, "グラフ: 角度の単位は既定でラジアン(sin(x)が普通の波になる)");

        // 電卓側の単位(この時点ではDEG)とは別で、グラフのページでDRGを押すとグラフだけが変わる
        int dx, dy;
        check(gkp->keyCenter("RAD", dx, dy), "グラフ: キーパッドの単位表示はRAD");
        check(kp->keyCenter("DEG", dx, dy), "グラフ: 電卓側の単位表示はDEGのまま");
        pressKeypad(gkp, "RAD"); // → GRA
        check(gkp->keyCenter("GRA", dx, dy) && kp->keyCenter("DEG", dx, dy), "グラフのDRG: グラフだけが次の単位へ");
        pressKeypad(gkp, "GRA"); // → DEG
        const float y2deg = gview->sampleAt(1, col);
        check(std::fabs(y2deg - std::sin(x * 3.14159265358979 / 180)) < 1e-4, "グラフ: DEGにすると度で評価される");
        pressKeypad(gkp, "DEG"); // → RAD(元へ)
        check(std::fabs(gview->sampleAt(1, col) - std::sin(x)) < 1e-4, "グラフ: RADへ戻る");

        // ドラッグで範囲が動く
        const double xmin0 = gview->getXMin();
        OSData::touchX = g.x + 100; OSData::touchY = g.y + 50;
        gview->causeOnPressStart();
        OSData::touchX = g.x + 120;
        gview->causeOnPressMove();
        gview->causeOnPressEnd();
        check(gview->getXMin() < xmin0 && !gview->isTracing(), "グラフ: 右へドラッグすると左側が見え、トレースはしない");

        // タップでトレース
        OSData::touchX = g.x + 60; OSData::touchY = g.y + 50;
        gview->causeOnPressStart();
        gview->causeOnPressEnd();
        check(gview->isTracing(), "グラフ: タップでトレース");

        Button* zin = findButtonByText("拡大");
        Button* reset = findButtonByText("初期化");
        Button* edit = findButtonByText("式");
        check(zin && reset && edit, "グラフ: 拡大/初期化/式のボタン");
        if(zin && reset && edit){
            const double w0 = gview->getXMax() - gview->getXMin();
            zin->causeOnPressEnd();
            check(std::fabs((gview->getXMax() - gview->getXMin()) - w0 / 2) < 1e-9, "拡大: 範囲が半分");
            reset->causeOnPressEnd();
            check(gview->getXMin() == -10.0 && gview->getXMax() == 10.0 && !gview->isTracing(), "初期化: -10〜10に戻りトレースも消える");
            edit->causeOnPressEnd();
            check(gkp->getVisible() && !gview->getVisible(), "[式]: 式の入力へ戻る");
        }
    }
    page_tab->setSelected(2, true);

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
