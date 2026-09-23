// カレンダーアプリ(CalendarScene / MonthGrid)のGUI配線のテスト。
//
// 予定の中身(どの日に出るか)は ical_test の担当で、ここでは
//   - MonthGrid のタップ位置 → 日付の逆算(曜日の見出し・前後の月の空きマスは反応しない)
//   - CalendarScene が生成したウィジェットを解放漏れなく片付けること(ASan)
//   - SDが無い/ .ics が無い場合に案内を出すこと
// を見る。SD上の .ics を実際に読んで並べるところは、ホストのSdFatスタブが
// ディレクトリの走査(openNext)を持たないので、PCビルドの --shot で確認する。
#include "gui/scenes/CalendarScene.hpp"
#include "gui/widgets/apps/MonthGrid.hpp"
#include "gui/widgets/dialogs/EventDetailDialog.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ---- モック ----
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
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}

// ---- MonthGrid: タップ位置から日付を逆算する ----
static void testGridHitTest(){
    // 7列 x (見出し18px + 6行 x 24px)。1列34px(238/7)
    MonthGrid grid(1, 30, 238, MonthGrid::kHeaderH + 6 * 24);
    grid.setMonth(2026, 9); // 2026-09-01 は火曜(3列目)

    int last = -1;
    int calls = 0;
    grid.setOnSelectDay([&](int d){ last = d; calls++; });

    auto tap = [&](int col, int row){ // row=-1 は見出し
        OSData::touchX = 1 + col * 34 + 17;
        OSData::touchY = 30 + MonthGrid::kHeaderH + row * 24 + 12;
        if(row < 0) OSData::touchY = 30 + MonthGrid::kHeaderH / 2;
        grid.causeOnPressStart();
    };

    tap(2, 0);
    eq_int(last, 1, "1行目の火曜 = 1日");
    tap(3, 3);
    eq_int(last, 23, "4行目の水曜 = 23日");
    eq_int(grid.getSelected(), 23, "タップした日が選択になる");
    tap(4, 4);          // 5行目の木曜 = 10/1 → この月には無い
    tap(6, 5);          // 6行目は丸ごと空き
    eq_int(calls, 2, "月の外の空きマスは反応しない");
    tap(1, -1);
    eq_int(calls, 2, "曜日の見出しは反応しない");
    tap(0, 0);
    eq_int(calls, 2, "1日より前の空きマスは反応しない");

    // 右端の列は余りを吸収する(238 = 34*7 なので今回は余り0。幅を変えて確かめる)
    MonthGrid wide(0, 0, 240, MonthGrid::kHeaderH + 6 * 24);
    wide.setMonth(2026, 9);
    int d2 = 0;
    wide.setOnSelectDay([&](int d){ d2 = d; });
    OSData::touchX = 239; // 最後の1px
    OSData::touchY = MonthGrid::kHeaderH + 12;
    wide.causeOnPressStart();
    eq_int(d2, 5, "右端の1pxも土曜の列として拾う");

    // 月を変えると日数に合わせて選択を詰める
    grid.setSelected(31);
    eq_int(grid.getSelected(), 0, "9月に31日は無いので選べない");
    MonthGrid g3(0, 0, 238, 162);
    g3.setMonth(2026, 1);
    g3.setSelected(31);
    g3.setMonth(2026, 2);
    eq_int(g3.getSelected(), 28, "1/31 → 2月へ移ると28日へ詰める");
}

// ---- CalendarScene: 生成と解放、案内の文言 ----
static const char* dayLabelText(){
    // 「日付/案内」の1行は格子より下にある唯一のLabel(Label<PICO_STR_M>)。
    // 上部のタイトルは Label<PICO_STR_S> なので、型を取り違えないよう位置で見分ける
    const char* found = nullptr;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() != WidgetType::Label) continue;
        if(w->getLocalRect().y < 150) continue;
        found = static_cast<Label<PICO_STR_M>*>(w)->getText()->c_str();
    }
    return found ? found : "";
}

static void testScene(){
    // 時計が合っている状態にする(NTP同期後に相当)
    TimeFunctions::timeinfo = {};
    TimeFunctions::timeinfo.tm_year = 2026 - 1900;
    TimeFunctions::timeinfo.tm_mon = 8;
    TimeFunctions::timeinfo.tm_mday = 23;

    auto* scene = new CalendarScene();

    OSData::SD_usable = false;
    scene->onEnter();
    check(!WidgetFunctions::widgets.empty(), "onEnter(): ウィジェットが並ぶ");
    check(strcmp(dayLabelText(), "SDカードがありません") == 0, "SDが無ければその旨を出す");

    bool has_grid = false;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() == WidgetType::MonthGrid){
            auto* g = static_cast<MonthGrid*>(w);
            has_grid = (g->getYear() == 2026 && g->getMonth() == 9 && g->getSelected() == 23);
        }
    }
    check(has_grid, "今日の月を開き、今日を選んでいる");

    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    check(WidgetFunctions::widgets.empty(), "onExit()後: ウィジェットが全て解放される");

    // SDはあるが /calendar/ が無い
    OSData::SD_usable = true;
    scene->onEnter();
    check(strcmp(dayLabelText(), "/calendar/ に .ics を置いてください") == 0, ".icsが無ければ置き場所を案内する");
    scene->onExit();
    WidgetFunctions::ClearSceneWidgets();
    OSData::SD_usable = false;

    delete scene;
}

// ---- EventDetailDialog: 生成・中身の差し替え・閉じる・解放(ASan) ----
static void testDetailDialog(){
    auto* dialog = new EventDetailDialog();
    std::string body;
    for(int i = 0; i < 40; i++) body += "説明文の行\n"; //スクロールが要る長さ
    dialog->setContent("定例ミーティング", body.c_str());
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);

    bool closed = false;
    dialog->setOnClosed([&](bool is_ok){ closed = !is_ok; WidgetFunctions::DestroyLater(dialog); });

    //「閉じる」ボタンを押したことにする(子のうちButtonを探す)
    Button* close = nullptr;
    for(Widget* w : dialog->getChildren()) if(w->getWidgetType() == WidgetType::Button) close = static_cast<Button*>(w);
    check(close != nullptr, "詳細: 閉じるボタンがある");
    if(close) close->causeOnPressStart();
    check(closed, "詳細: 閉じると on_closed(false) が呼ばれる");
    check(!dialog->getVisible(), "詳細: 閉じると見えなくなる");
    WidgetFunctions::ProcessPendingDeletes();
    WidgetFunctions::ClearSceneWidgets();
    check(true, "詳細: 解放で落ちない(漏れはASanが見る)");

    // MonthGrid::setDots: 同じ中身なら描き直さない(needs_redrawはprotectedなので覗き窓を作る)
    struct GridProbe : MonthGrid {
        using MonthGrid::MonthGrid;
        bool dirty() const { return needs_redraw; }
    };
    GridProbe grid(0, 0, 238, 162);
    grid.setMonth(2026, 9);
    MonthGrid::DayDots dots[32];
    dots[23].count = 2;
    dots[23].colors[0] = PICO_RED;
    dots[23].colors[1] = PICO_BLUE;
    grid.render();
    grid.setDots(dots);
    check(grid.dirty(), "点が変われば描き直しを求める");
    grid.render();
    grid.setDots(dots);
    check(!grid.dirty(), "点が同じなら描き直さない");
}

int main(){
    testGridHitTest();
    testScene();
    testDetailDialog();

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
