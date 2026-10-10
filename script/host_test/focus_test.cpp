// ウィジェットのフォーカス(FocusFunctions)。
// 読む順(Tab)・矢印の向きの移り先・候補にならないもの・押す(真ん中のタップ)・消えたとき・
// タッチで枠が消えること・ダイアログを開いた/閉じたときの範囲・各ウィジェットのキーの扱いを確かめる
#include "functions/Focus_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/Label.hpp"
#include "OS_Data.hpp"
#include <cstdio>

// ---- モック ----
static int dirty_count = 0;
void PICO_GFX::MarkDirty(const Rect&){ dirty_count++; }
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::HideAll(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

using FocusFunctions::Move;

static Button* MakeButton(int x, int y){
    Button* b = new Button(x, y, "B");
    b->setW(40);
    b->setH(20);
    WidgetFunctions::Add(b);
    return b;
}

int main(){
    // ---- 読む順と矢印 ----
    //   a(10,30)  b(100,30)
    //   c(10,80)  d(100,80)
    Button* a = MakeButton(10, 30);
    Button* b = MakeButton(100, 30);
    Button* c = MakeButton(10, 80);
    Button* d = MakeButton(100, 80);
    Label<PICO_STR_S>* label = new Label<PICO_STR_S>(10, 150, "label");
    WidgetFunctions::Add(label);

    check(FocusFunctions::Current() == nullptr, "最初はフォーカスが無い");
    check(FocusFunctions::Navigate(Move::Next) && FocusFunctions::focused == a && FocusFunctions::ring_visible,
          "最初のTabは読む順の最初に枠を出すだけ");
    FocusFunctions::Navigate(Move::Next);
    check(FocusFunctions::focused == b, "Tab: 同じ高さの右へ");
    FocusFunctions::Navigate(Move::Next);
    check(FocusFunctions::focused == c, "Tab: 次の行の左へ");
    FocusFunctions::Navigate(Move::Next);
    FocusFunctions::Navigate(Move::Next);
    check(FocusFunctions::focused == a, "Tab: 最後から最初へ回る(ラベルは候補にならない)");
    FocusFunctions::Navigate(Move::Prev);
    check(FocusFunctions::focused == d, "Shift+Tab: 最初から最後へ回る");

    FocusFunctions::Set(a, true);
    FocusFunctions::SendKey(FocusKey::Right);
    check(FocusFunctions::focused == b, "→");
    FocusFunctions::SendKey(FocusKey::Down);
    check(FocusFunctions::focused == d, "↓");
    FocusFunctions::SendKey(FocusKey::Left);
    check(FocusFunctions::focused == c, "←");
    check(!FocusFunctions::SendKey(FocusKey::Left) && FocusFunctions::focused == c, "その向きに何も無ければ動かない");
    FocusFunctions::SendKey(FocusKey::Up);
    check(FocusFunctions::focused == a, "↑");

    // ---- 候補にならないもの ----
    b->setEnabled(false);
    FocusFunctions::Set(a, true);
    FocusFunctions::SendKey(FocusKey::Right);
    check(FocusFunctions::focused == d, "無効のものへは移らない(その向きの次に近いものへ)");
    b->setEnabled(true);
    b->setVisible(false);
    FocusFunctions::Set(a, true);
    FocusFunctions::Navigate(Move::Next);
    check(FocusFunctions::focused == c, "見えないものは飛ばす");
    b->setVisible(true);
    d->setFocusable(false);
    FocusFunctions::Set(c, true);
    FocusFunctions::SendKey(FocusKey::Right);
    check(FocusFunctions::focused == b && !d->isFocusable(), "focusable=falseへは移らない");
    d->setFocusable(true);
    label->setFocusable(true);
    check(label->isFocusable(), "focusable=trueで受けない種類も受ける");
    label->setFocusable(false);

    // ---- 押す ----
    int starts = 0, ends = 0, seen_x = -1, seen_y = -1;
    a->setOnPressStart([&](){ starts++; seen_x = OSData::touchX; seen_y = OSData::touchY; });
    a->setOnPressEnd([&](){ ends++; });
    OSData::touchX = 1; OSData::touchY = 2;
    FocusFunctions::Set(a, true);
    check(FocusFunctions::Activate() && starts == 1 && ends == 1, "決定で押した/離したが1回ずつ");
    const Rect ar = a->getScreenRect();
    check(seen_x == ar.x + ar.w / 2 && seen_y == ar.y + ar.h / 2, "押した位置は真ん中");
    check(OSData::touchX == 1 && OSData::touchY == 2, "押した後はタッチの位置を戻す");

    // 枠が出ていなければ、最初の決定は枠を出すだけ(押さない)
    FocusFunctions::OnTouchStart(a);
    check(!FocusFunctions::ring_visible && FocusFunctions::focused == a, "タッチで枠が消える(フォーカスは残る)");
    FocusFunctions::Activate();
    check(starts == 1 && FocusFunctions::ring_visible, "枠が無いときの決定は枠を出すだけ");
    FocusFunctions::OnTouchStart(c);
    check(FocusFunctions::focused == c && !FocusFunctions::ring_visible, "タッチしたものへフォーカスが移る");
    FocusFunctions::OnTouchStart(label);
    check(FocusFunctions::focused == c, "受けないものをタッチしてもフォーカスはそのまま");

    // ---- 消える ----
    FocusFunctions::Set(d, true);
    WidgetFunctions::Destroy(d);
    check(FocusFunctions::focused == nullptr && !FocusFunctions::ring_visible, "消えたらフォーカスも外れる");
    Button* e = MakeButton(100, 80);
    FocusFunctions::Set(e, true);
    delete e; // WidgetFunctionsを通らずに消されても
    check(FocusFunctions::focused == nullptr, "~Widget()でも外れる");
    WidgetFunctions::widgets.erase(std::find(WidgetFunctions::widgets.begin(), WidgetFunctions::widgets.end(), (Widget*)e));

    // ---- ダイアログの範囲 ----
    FocusFunctions::Set(b, true);
    Button* dlg = new Button(60, 200, "OK");
    WidgetFunctions::AddDialog(dlg);
    check(!FocusFunctions::InScope(b), "ダイアログが開くと画面のものは範囲の外");
    FocusFunctions::Update();
    check(FocusFunctions::focused == dlg && FocusFunctions::ring_visible, "枠が出ていればダイアログの中へ移る");
    FocusFunctions::Navigate(Move::Next);
    check(FocusFunctions::focused == dlg, "ダイアログの外へは移らない");
    int dlg_pressed = 0;
    dlg->setOnPressStart([&](){ dlg_pressed++; WidgetFunctions::DestroyLater(dlg); });
    FocusFunctions::Activate();
    check(dlg_pressed == 1, "ダイアログのボタンを押せる");
    check(FocusFunctions::focused == b && FocusFunctions::ring_visible, "閉じたら開く前のものへ戻る");
    WidgetFunctions::ProcessPendingDeletes();

    // タッチで閉じた場合は戻すが枠は出さない
    Button* dlg2 = new Button(60, 200, "OK");
    WidgetFunctions::AddDialog(dlg2);
    FocusFunctions::Update();
    FocusFunctions::OnTouchStart(dlg2);
    WidgetFunctions::Destroy(dlg2);
    FocusFunctions::Update();
    check(FocusFunctions::focused == b && !FocusFunctions::ring_visible, "タッチで閉じたら枠は出さずに戻す");

    // ---- 各ウィジェット ----
    Checkbox* cb = new Checkbox(10, 230, "check");
    WidgetFunctions::Add(cb);
    int cb_changes = 0;
    cb->setOnChangeChecked([&](){ cb_changes++; });
    const bool cb_before = cb->getIsChecked();
    FocusFunctions::Set(cb, true);
    FocusFunctions::Activate();
    check(cb->getIsChecked() != cb_before && cb_changes == 1, "Checkbox: 決定で切り替わる(幅が広くても)");

    TabBar* tabs = new TabBar(0, 260, 200, 20);
    tabs->addTab("A"); tabs->addTab("B"); tabs->addTab("C");
    WidgetFunctions::Add(tabs);
    int tab_changes = 0;
    tabs->setOnChanged([&](int){ tab_changes++; });
    FocusFunctions::Set(tabs, true);
    FocusFunctions::SendKey(FocusKey::Right);
    check(tabs->getSelected() == 1 && tab_changes == 1 && FocusFunctions::focused == tabs, "TabBar: →で次のタブ");
    FocusFunctions::SendKey(FocusKey::Left);
    FocusFunctions::SendKey(FocusKey::Left);
    check(tabs->getSelected() == 0 && FocusFunctions::focused != tabs, "TabBar: 端より先はフォーカスが隣へ");

    NumberSlider* slider = new NumberSlider(10, 290, 200);
    WidgetFunctions::Add(slider);
    slider->setValue(50);
    FocusFunctions::Set(slider, true);
    FocusFunctions::SendKey(FocusKey::Right);
    check(slider->getValue() > 54.9f && slider->getValue() < 55.1f, "NumberSlider: →で範囲の1/20増える");
    FocusFunctions::SendKey(FocusKey::Left);
    FocusFunctions::SendKey(FocusKey::Left);
    check(slider->getValue() > 44.9f && slider->getValue() < 45.1f, "NumberSlider: ←で減る");

    // ScrollList(1項目 18px 前後。高さ40なので2項目ほどしか見えない)
    ScrollList* list = new ScrollList(130, 140, 100, 40);
    for(int i = 0; i < 6; i++) { ScrollListTools::Item it; it.text.assign("item"); list->add(it); }
    WidgetFunctions::Add(list);
    int sel_calls = 0, last_index = -9; bool last_already = false;
    list->setOnSelectItem([&](int idx, bool already){ sel_calls++; last_index = idx; last_already = already; });
    FocusFunctions::Set(list, true);
    FocusFunctions::SendKey(FocusKey::Down);
    check(list->getSelectedIndex() == 0 && last_index == 0 && !last_already, "ScrollList: ↓で最初の項目を選ぶ(1回目のタップ)");
    for(int i = 0; i < 5; i++) FocusFunctions::SendKey(FocusKey::Down);
    check(list->getSelectedIndex() == 5 && list->getScrollY() > 0, "ScrollList: 見えない項目まで動くとスクロールする");
    FocusFunctions::Activate();
    check(last_index == 5 && last_already && sel_calls == 7, "ScrollList: 決定は同じ項目の2回目のタップ");
    FocusFunctions::SendKey(FocusKey::Down);
    check(FocusFunctions::focused != list, "ScrollList: 最後より下はフォーカスが隣へ");

    DropdownMenu* dd = new DropdownMenu(10, 10, 100);
    dd->add("x"); dd->add("y"); dd->add("z");
    WidgetFunctions::Add(dd);
    int dd_changes = 0;
    dd->setOnChanged([&](){ dd_changes++; });
    FocusFunctions::Set(dd, true);
    FocusFunctions::Activate();
    check(dd->getH() > 30, "DropdownMenu: 決定で開く");
    FocusFunctions::SendKey(FocusKey::Down);
    FocusFunctions::Activate();
    check(dd_changes == 1 && dd->getH() == 30, "DropdownMenu: ↓→決定で2番目を選んで閉じる");
    FocusFunctions::Activate();
    FocusFunctions::SendKey(FocusKey::Back);
    check(dd->getH() == 30 && dd_changes == 1, "DropdownMenu: 戻るで選ばずに閉じる");
    FocusFunctions::Activate();
    FocusFunctions::Set(a, true);
    check(dd->getH() == 30, "DropdownMenu: フォーカスが外れたら閉じる");

    // ScrollContainer: 見えない所の子へ移るとスクロールする
    ScrollContainer* sc = new ScrollContainer(0, 0, 100, 60);
    Button* inner_top = new Button(0, 0, "T");
    Button* inner_bottom = new Button(0, 200, "U");
    sc->add(inner_top);
    sc->add(inner_bottom);
    sc->refreshContentBounds();
    FocusFunctions::Set(inner_bottom, true);
    check(sc->getScrollY() > 0, "ScrollContainer: 隠れた子へ移ると見える所までスクロールする");
    delete sc;

    WidgetFunctions::ClearSceneWidgets();
    check(FocusFunctions::focused == nullptr && FocusFunctions::saved_id == 0, "画面を片付けるとフォーカスも消える");

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
