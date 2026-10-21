// ホーム画面のウィジェットを検証するテスト。
//
//   - HomeLayout: アプリとウィジェットの並び、マス目への詰め方、足す/消す/動かす/大きさ/アプリを隠す、home.cfgの読み書き
//   - AppGrid: ウィジェットのマスの当たり判定・中のボタン・押すとアプリが開く・見た目の印(stamp)が変わったら描き直す
//   - フォルダ: 並びに1マスで出る・作る/入れる/隠すでウィジェットが同じ項目の前に残る・AppGridで開く/閉じる/中を並べる
//   - TodoCache: 今日までのタスクだけを覚える・/sys/todo_cache.tsv の往復
//   - HomeAgenda: /calendar/*.ics から今日から7日間の予定を拾う(終わった予定・7日より先は入れない・ファイルごとの色)
#include "apps/home/Home_Layout.hpp"
#include "apps/home/Home_Widget.hpp"
#include "apps/home/Home_Agenda.hpp"
#include "apps/home/Home_Time.hpp"
#include "apps/todo/Todo_Cache.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "apps/home/AppGrid.hpp"
#include "gui/core/Scene.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- モック ----
static std::vector<Rect> dirty_rects;
void PICO_GFX::MarkDirty(const Rect& r){ dirty_rects.push_back(r); }
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static Scene* pushed_scene = nullptr;
void SceneFunctions::Push(Scene* next){
    delete pushed_scene;
    pushed_scene = next;
}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq(long actual, long expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %-50s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

class DummyScene : public Scene {
    public:
        const char* getName() const override { return "Dummy"; }
        void onEnter() override {}
};

static void registerApps(int n){
    AppFunctions::Clear();
    for(int i = 0; i < n; i++){
        char name[16];
        snprintf(name, sizeof(name), "a%02d", i + 1);
        AppFunctions::Register(name, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
    }
}

using HomeLayout::ItemType;
using HomeLayout::Kind;

// 並べた結果から項目を探す
static const HomeLayout::Item* findItem(const HomeLayout::Item* items, int n, ItemType type, int index){
    for(int i = 0; i < n; i++) if(items[i].type == type && items[i].index == index) return &items[i];
    return nullptr;
}

// 押している間の印(本物は WidgetFunctions::UpdateAll() が立てる)を立てられるように
struct TestGrid : public AppGrid {
    using AppGrid::AppGrid;
    void press(){ this->is_pressing = true; causeOnPressStart(); }
    void release(){ causeOnPressEnd(); this->is_pressing = false; }
};

// 押されたボタン・描いた回数を数える偽物のウィジェット
class FakeWidget : public HomeWidget {
    public:
        uint32_t st = 1;
        int pressed = -1;
        int draws = 0;
        const char* appName() const override { return "a02"; }
        uint32_t stamp(uint8_t, uint8_t) override { return st; }
        void draw(const Rect&, uint8_t, uint8_t, int) override { draws++; }
        int buttonAt(const Rect& r, uint8_t, uint8_t, int x, int y) override {
            //右下の20x20がボタン
            return (x >= r.x + r.w - 20 && y >= r.y + r.h - 20 && x < r.x + r.w && y < r.y + r.h) ? 0 : -1;
        }
        void press(int b) override { pressed = b; }
};

static void testLayout(){
    printf("---- HomeLayout ----\n");
    HomeLayout::Test::Reset();
    HostSd::files.clear();
    registerApps(5);
    HomeLayout::Item items[HomeLayout::kMaxItems];
    int pages = 0;

    int n = HomeLayout::Compute(3, items, HomeLayout::kMaxItems, &pages);
    eq(n, 5, "ウィジェット無し: アプリ5つ");
    eq(pages, 1, "ウィジェット無し: 1ページ");
    eq(items[3].row * 2 + items[3].col, 3, "ウィジェット無し: 4つ目は4マス目");

    //先頭に横長の時計
    eq(HomeLayout::Add(Kind::Clock, 2, 1, 0), 0, "Add: 時計(横長)を先頭へ");
    n = HomeLayout::Compute(3, items, HomeLayout::kMaxItems, &pages);
    eq(n, 6, "Add: 項目は6つ");
    check(items[0].type == ItemType::Widget && items[0].row == 0 && items[0].col == 0 && items[0].w == 2,
          "Add: 時計は1行目いっぱい");
    const HomeLayout::Item* a1 = findItem(items, n, ItemType::App, AppFunctions::VisibleAt(0));
    check(a1 && a1->row == 1 && a1->col == 0, "Add: 最初のアプリは2行目の左");
    eq(pages, 2, "Add: 2+5マスで2ページ");

    //大きさに合わない指定は断る
    eq(HomeLayout::Add(Kind::Clock, 2, 2, 0), -1, "Add: 時計の大は無い");
    check(!HomeLayout::Resize(0, 2, 2), "Resize: 時計の大は無い");

    //2x2 を1つ目のアプリの後ろへ: 1つ目のアプリの右の空きを後ろのアプリが埋める
    HomeLayout::Test::Reset();
    registerApps(5);
    eq(HomeLayout::Add(Kind::Agenda, 2, 2, 1), 0, "Add: 予定(大)を1つ目のアプリの後ろへ");
    n = HomeLayout::Compute(3, items, HomeLayout::kMaxItems, &pages);
    const HomeLayout::Item* w = findItem(items, n, ItemType::Widget, 0);
    check(w && w->page == 0 && w->row == 1 && w->col == 0 && w->h == 2, "詰め方: 2x2は2行目から");
    const HomeLayout::Item* a2 = findItem(items, n, ItemType::App, AppFunctions::VisibleAt(1));
    check(a2 && a2->page == 0 && a2->row == 0 && a2->col == 1, "詰め方: 2つ目のアプリは1行目の右の空きへ");
    const HomeLayout::Item* a3 = findItem(items, n, ItemType::App, AppFunctions::VisibleAt(2));
    check(a3 && a3->page == 1 && a3->row == 0 && a3->col == 0, "詰め方: 3つ目のアプリは次のページ");

    //ページの残りに入らない2x2は次のページへ(重ならない)
    HomeLayout::Test::Reset();
    registerApps(3);
    HomeLayout::Add(Kind::Music, 2, 2, 3);
    n = HomeLayout::Compute(3, items, HomeLayout::kMaxItems, &pages);
    w = findItem(items, n, ItemType::Widget, 0);
    check(w && w->page == 1 && w->row == 0, "詰め方: 2行目からは2x2が入らないので次のページ");
    eq(pages, 2, "詰め方: 2ページ");
    {
        //どのページでもマスは重ならない
        bool overlap = false;
        for(int i = 0; i < n; i++) for(int j = i + 1; j < n; j++){
            if(items[i].page != items[j].page) continue;
            const bool sep = items[i].col + items[i].w <= items[j].col || items[j].col + items[j].w <= items[i].col
                          || items[i].row + items[i].h <= items[j].row || items[j].row + items[j].h <= items[i].row;
            if(!sep) overlap = true;
        }
        check(!overlap, "詰め方: マスが重ならない");
    }

    //並び: 動かす
    HomeLayout::Test::Reset();
    registerApps(4);
    HomeLayout::Add(Kind::Todo, 2, 1, 2);       // [a1 a2 W a3 a4]
    HomeLayout::SeqEntry seq[HomeLayout::kMaxItems];
    int sn = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    eq(sn, 5, "並び: 5つ");
    check(seq[2].type == ItemType::Widget, "並び: ウィジェットは3番目");
    check(HomeLayout::Move(ItemType::Widget, 0, -1), "Move: ウィジェットを前へ");
    sn = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    check(seq[1].type == ItemType::Widget && seq[2].type == ItemType::App && seq[2].index == AppFunctions::VisibleAt(1),
          "Move: アプリと入れ替わる");
    const int first_app = AppFunctions::VisibleAt(0);
    check(HomeLayout::Move(ItemType::App, first_app, +1), "Move: アプリを後ろへ(ウィジェットを飛び越す)");
    sn = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    check(seq[0].type == ItemType::Widget && seq[1].index == first_app, "Move: ウィジェットが先頭、アプリが2番目");
    check(HomeLayout::Move(ItemType::App, first_app, +1), "Move: アプリ同士の入れ替え");
    eq(AppFunctions::VisiblePosition(first_app), 1, "Move: アプリの並び(launcher)も変わる");
    check(!HomeLayout::Move(ItemType::Widget, 0, -1), "Move: 先頭より前へは動かない");

    //見た目が変わるまで進める: [a1 W(2x1) a2 a3 a4] は a1 の右が空いて a2 が入る。
    //Wを1つ後ろ([a1 a2 W ...])へ動かしても見た目が同じなので、もう1つ進む
    HomeLayout::Test::Reset();
    registerApps(4);
    HomeLayout::Add(Kind::Clock, 2, 1, 1);
    n = HomeLayout::Compute(3, items, HomeLayout::kMaxItems, &pages);
    w = findItem(items, n, ItemType::Widget, 0);
    check(w && w->row == 1, "見た目: 横長は2行目");
    int new_index = -1;
    check(HomeLayout::Move(ItemType::Widget, 0, +1, 3, &new_index), "見た目: 後ろへ");
    eq(new_index, 0, "見た目: 番号は0のまま");
    n = HomeLayout::Compute(3, items, HomeLayout::kMaxItems, &pages);
    w = findItem(items, n, ItemType::Widget, 0);
    check(w && w->row == 2, "見た目: 1回で3行目まで動く");

    //アプリを隠しても、ウィジェットは隣のアプリとの位置を保つ
    HomeLayout::Test::Reset();
    registerApps(4);
    HomeLayout::Add(Kind::Clock, 1, 1, 3);       // [a1 a2 a3 W a4]
    const int a2i = AppFunctions::VisibleAt(1);
    check(HomeLayout::HideApp(a2i), "HideApp: 2つ目を隠す");
    sn = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    eq(sn, 4, "HideApp: 並びは4つ");
    check(seq[2].type == ItemType::Widget, "HideApp: ウィジェットは a3 の後ろのまま");

    //先頭へ
    HomeLayout::Test::Reset();
    registerApps(3);
    HomeLayout::Add(Kind::Clock, 1, 1, 1);       // [a1 W a2 a3]
    const int a3i = AppFunctions::VisibleAt(2);
    check(HomeLayout::MoveToFront(ItemType::App, a3i), "MoveToFront: アプリ");
    sn = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    check(seq[0].type == ItemType::App && seq[0].index == a3i && seq[2].type == ItemType::Widget,
          "MoveToFront: [a3 a1 W a2]");
    check(HomeLayout::MoveToFront(ItemType::Widget, 0), "MoveToFront: ウィジェット");
    sn = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    check(seq[0].type == ItemType::Widget, "MoveToFront: ウィジェットが先頭");

    //上限
    HomeLayout::Test::Reset();
    registerApps(2);
    for(int i = 0; i < HomeLayout::kMaxWidgets; i++) HomeLayout::Add(Kind::Clock, 1, 1, 0);
    eq(HomeLayout::placed_count, HomeLayout::kMaxWidgets, "上限: 8つまで");
    eq(HomeLayout::Add(Kind::Clock, 1, 1, 0), -1, "上限: 9つ目は断る");
    check(HomeLayout::Remove(3), "Remove");
    eq(HomeLayout::placed_count, HomeLayout::kMaxWidgets - 1, "Remove: 1つ減る");

    //アプリより大きい anchor は最後へ
    HomeLayout::Test::Reset();
    registerApps(2);
    HomeLayout::placed[0] = HomeLayout::Placed{ Kind::Clock, 1, 1, 40 };
    HomeLayout::placed_count = 1;
    sn = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    check(sn == 3 && seq[2].type == ItemType::Widget, "anchorが大きすぎるものは最後へ");
}

static void testPersist(){
    printf("---- home.cfg ----\n");
    OSData::SD_usable = true;
    HostSd::files.clear();
    HomeLayout::Test::Reset();
    registerApps(3);
    HomeLayout::Add(Kind::Clock, 2, 1, 0);
    HomeLayout::Add(Kind::Music, 2, 2, 2);
    HomeLayout::Add(Kind::Todo, 2, 1, 99);
    const std::string cfg = HostSd::files[PICO_Path::FILE::CFG::SYS_HOME_CFG];
    check(cfg.find("widget1 = clock,2x1,0") != std::string::npos, "Save: 1行目");
    check(cfg.find("widget2 = music,2x2,1") != std::string::npos, "Save: 2行目(並びの位置2 = アプリ1つ目の後ろ)");
    check(cfg.find("widget3 = todo,2x1,3") != std::string::npos, "Save: 3行目(最後)");

    HomeLayout::placed_count = 0;
    HomeLayout::Load();
    eq(HomeLayout::placed_count, 3, "Load: 3つ");
    check(HomeLayout::placed[1].kind == Kind::Music && HomeLayout::placed[1].w == 2 && HomeLayout::placed[1].h == 2
          && HomeLayout::placed[1].anchor == 1, "Load: 中身が戻る");

    //読めない行・知らない種類・使えない大きさは飛ばす。番号の順に並べる
    HostSd::files[PICO_Path::FILE::CFG::SYS_HOME_CFG] =
        "widget3 = agenda,2x2,1\n"
        "widget1 = clock,1x1,0\n"
        "widget2 = weather,1x1,0\n"
        "widget4 = clock,2x2,0\n"
        "widget5 = todo,2x1\n"
        "foo = bar\n";
    HomeLayout::Load();
    eq(HomeLayout::placed_count, 2, "Load: 使えるのは2つ");
    check(HomeLayout::placed[0].kind == Kind::Clock && HomeLayout::placed[1].kind == Kind::Agenda, "Load: 番号の順");
    check(HomeLayout::HasKind(Kind::Agenda) && !HomeLayout::HasKind(Kind::Todo), "HasKind");
}

static void testGrid(){
    printf("---- AppGrid ----\n");
    HomeLayout::Test::Reset();
    HostSd::files.clear();
    registerApps(4);
    FakeWidget fake;
    HomeLayout::SetImpl(Kind::Music, &fake);
    HomeLayout::Add(Kind::Music, 2, 1, 0);

    TestGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT - 28);
    int launched = -1;
    g.setOnLaunch([&launched](int i){ launched = i; });
    eq(g.rowsPerPage(), 3, "1ページ3行");
    check(g.wantsFrameUpdate(), "ウィジェットがあると毎フレーム見張る");

    const Rect w = g.cellRect(0, 0, 2, 1);
    eq(g.hitTile(w.x + 10, w.y + 10), -1, "ウィジェットのマスはアプリではない");
    const Rect t = g.tileRect(2);
    eq(g.hitTile(t.x + t.w / 2, t.y + t.h / 2), AppFunctions::VisibleAt(0), "ウィジェットの下はアプリ1つ目");

    //本体を押すとアプリ(a02 = 登録簿の1)を開く
    OSData::touchX = w.x + 20;
    OSData::touchY = STATUSBAR_HEIGHT + w.y + 20;
    g.causeOnPressStart();
    g.causeOnPressEnd();
    eq(launched, 1, "本体を押すと appName のアプリ");
    eq(fake.pressed, -1, "本体ではボタンは押さない");

    //ボタンを押すと press() だけ
    launched = -1;
    OSData::touchX = w.x + w.w - 5;
    OSData::touchY = STATUSBAR_HEIGHT + w.y + w.h - 5;
    g.causeOnPressStart();
    g.causeOnPressEnd();
    eq(fake.pressed, 0, "ボタンを押すと press(0)");
    eq(launched, -1, "ボタンではアプリを開かない");

    //ボタンで押して外で離したら何もしない
    fake.pressed = -1;
    g.causeOnPressStart();
    OSData::touchX = w.x + 20;
    g.causeOnPressEnd();
    eq(fake.pressed, -1, "ボタンの外で離すと押さない");
    eq(launched, -1, "ボタンの外で離してもアプリは開かない");

    //見た目の印が変わったら、そのウィジェットの矩形だけを描き直す
    PICO_GFX::isDirtyDeactivates = false;
    g.render(); //1回目は全体
    dirty_rects.clear();
    g.render();
    check(dirty_rects.empty(), "印が同じなら描き直さない");
    fake.st++;
    g.render();
    check(dirty_rects.size() == 1 && dirty_rects[0].w == w.w && dirty_rects[0].y == STATUSBAR_HEIGHT + w.y,
          "印が変わるとウィジェットの矩形だけ");

    //描く(ASanで範囲外を見る)
    fake.draws = 0;
    PICO_GFX::isDirtyDeactivates = true;
    g.renderForce();
    PICO_GFX::isDirtyDeactivates = false;
    eq(fake.draws, 1, "合成で描く");

    //長押し
    OSData::touchX = w.x + 20;
    OSData::touchY = STATUSBAR_HEIGHT + w.y + 20;
    g.press();
    AppGrid::LongPress lp;
    check(!g.takeLongPress(millis(), 600, lp), "長押し: すぐには出ない");
    check(g.takeLongPress(millis() + 700, 600, lp), "長押し: 600ms後");
    check(lp.on_item && lp.type == ItemType::Widget && lp.index == 0, "長押し: ウィジェット");
    launched = -1;
    g.release();
    eq(launched, -1, "長押しの後は開かない");

    //空いた所の長押し
    //(マスは全部埋まっているので、外周の余白を押す)
    OSData::touchX = 1;
    OSData::touchY = STATUSBAR_HEIGHT + 1;
    g.press();
    check(g.takeLongPress(millis() + 700, 600, lp) && !lp.on_item, "長押し: 空いた所");
    g.release();

    //キー: 決定でアプリを開く、下へ移る
    launched = -1;
    g.showItem(ItemType::Widget, 0);
    g.onFocusKey(FocusKey::Activate);
    eq(launched, 1, "キー: ウィジェットで決定するとアプリ");
    check(g.onFocusKey(FocusKey::Down), "キー: 下へ");
    eq(g.focusedApp(), AppFunctions::VisibleAt(0), "キー: 下はアプリ1つ目");

    HomeLayout::Test::Reset();
}

static void testTodoCache(){
    printf("---- TodoCache ----\n");
    OSData::SD_usable = true;
    HostSd::files.clear();
    TodoCache::Test::Reset();
    const int32_t today = 20742;
    Todoist::Task tasks[5];
    auto set = [&](int i, const char* name, int32_t day, int sec, int pri){
        tasks[i].content.assign(name);
        tasks[i].due.has = day != 0;
        tasks[i].due.day = day;
        tasks[i].due.has_time = sec >= 0;
        tasks[i].due.sec = sec < 0 ? 0 : sec;
        tasks[i].priority = (uint8_t)pri;
    };
    set(0, "昨日の\tタスク", today - 1, -1, 4);
    set(1, "今日15時", today, 15 * 3600, 1);
    set(2, "明日", today + 1, -1, 1);
    set(3, "期限なし", 0, -1, 1);
    set(4, "今日", today, -1, 2);
    TodoCache::Store(tasks, 5, false, today, 1792130000);
    eq(TodoCache::Count(), 3, "Store: 今日までの3つだけ");
    eq(TodoCache::Total(), 3, "Store: 総数");
    check(strcmp(TodoCache::At(0).content.c_str(), "昨日の タスク") == 0, "Store: タブは空白へ");
    check(HostSd::files[PICO_Path::FILE::SYS_TODO_CACHE].find("#todo 1792130000 3") == 0, "Store: ファイルの見出し");

    TodoCache::Test::Reset();
    check(!TodoCache::Has(), "Reset: 無い");
    TodoCache::EnsureLoaded();
    check(TodoCache::Has(), "Load: 読める");
    eq(TodoCache::Count(), 3, "Load: 3つ");
    eq((long)TodoCache::FetchedEpoch(), 1792130000L, "Load: 取った時刻");
    check(TodoCache::At(1).due.has_time && TodoCache::At(1).due.sec == 15 * 3600 && TodoCache::At(1).due.day == today,
          "Load: 期限の時刻");
    check(!TodoCache::At(2).due.has_time && TodoCache::At(2).priority == 2, "Load: 時刻なし・優先度");
    check(strcmp(TodoCache::At(1).content.c_str(), "今日15時") == 0, "Load: 名前");

    //「今日」の表示から取ったものは全部(件数の上限まで)
    Todoist::Task many[9];
    for(int i = 0; i < 9; i++){ many[i].content.assign("x"); many[i].due.has = true; many[i].due.day = today + 3; }
    TodoCache::Store(many, 9, true, today, 1);
    eq(TodoCache::Count(), TodoCache::kMax, "Store: 上限まで");
    eq(TodoCache::Total(), 9, "Store: 総数は全部");
}

static void testAgenda(){
    printf("---- HomeAgenda ----\n");
    eq(HomeTime::DaysFromCivil(2026, 10, 16), 20742, "DaysFromCivil");
    eq(HomeTime::DaysFromCivil(1970, 1, 1), 0, "DaysFromCivil 1970");
    OSData::SD_usable = true;
    HostSd::files.clear();
    HostSd::dirs_enabled = true;
    HostSd::files["/calendar/a.ics"] =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\nUID:a1\r\nDTSTART;VALUE=DATE:20261016\r\nDTEND;VALUE=DATE:20261017\r\nSUMMARY:終日の予定\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:a2\r\nDTSTART:20261016T080000\r\nDTEND:20261016T090000\r\nSUMMARY:終わった予定\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:a3\r\nDTSTART:20261016T130000\r\nDTEND:20261016T140000\r\nSUMMARY:午後の予定\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:a4\r\nDTSTART:20261030T100000\r\nDTEND:20261030T110000\r\nSUMMARY:遠い予定\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:a5\r\nDTSTART:20261014T100000\r\nRRULE:FREQ=WEEKLY\r\nDTEND:20261014T110000\r\nSUMMARY:毎週水曜\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    HostSd::files["/calendar/b.ics"] =
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\nUID:b1\r\nDTSTART:20261017T093000\r\nDTEND:20261017T103000\r\nSUMMARY:明日の予定\r\nEND:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    HostSd::files["/calendar/sources.cfg"] = "";

    const int32_t today = 20742;
    HomeAgenda::Refresh(today, 10 * 3600, 0);
    check(!HomeAgenda::NoFiles(), "ファイルがある");
    eq(HomeAgenda::Count(), 4, "終日・午後・明日・水曜の4つ");
    if(HomeAgenda::Count() == 4){
        check(strcmp(HomeAgenda::At(0).title.c_str(), "終日の予定") == 0 && HomeAgenda::At(0).start_sec < 0, "1つ目は終日");
        check(strcmp(HomeAgenda::At(1).title.c_str(), "午後の予定") == 0 && HomeAgenda::At(1).start_sec == 13 * 3600
              && HomeAgenda::At(1).end_sec == 14 * 3600, "2つ目は午後(時刻)");
        check(HomeAgenda::At(2).day == today + 1 && HomeAgenda::At(2).color == PICO_BLUE, "3つ目は明日、2つ目のファイルの色");
        check(HomeAgenda::At(3).day == today + 5 && HomeAgenda::At(3).color == PICO_DARKGREEN, "4つ目は水曜(繰り返し)");
    }
    eq(HomeAgenda::Day(), today, "読んだ日");

    HostSd::files.clear();
    HostSd::files["/calendar/sources.cfg"] = "";
    HomeAgenda::Refresh(today, 0, 0);
    check(HomeAgenda::NoFiles() && HomeAgenda::Count() == 0, ".ics が無い");
    HostSd::dirs_enabled = false;
}

static void testFolders(){
    printf("---- フォルダ ----\n");
    HomeLayout::Test::Reset();
    HostSd::files.clear();
    registerApps(6);  //登録簿 0..5 = a01..a06
    //[W] a01 a02 a03 [V] a04 a05 a06
    HomeLayout::Add(Kind::Clock, 1, 1, 0);
    HomeLayout::Add(Kind::Analog, 1, 1, 4);
    HomeLayout::SeqEntry seq[HomeLayout::kMaxItems];

    //a02 の所にフォルダを作る: [W] a01 [F:a02] a03 [V] a04 ...
    const int f = HomeLayout::CreateFolder(1, "F");
    check(f >= 0, "作る");
    int n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    eq(n, 8, "並びは8つ(アプリ1つがフォルダに替わっただけ)");
    check(seq[2].type == ItemType::Folder && seq[2].index == f, "フォルダは a02 の位置");
    check(seq[0].type == ItemType::Widget && seq[4].type == ItemType::Widget, "ウィジェットの位置は同じ");

    //a03 をフォルダへ: [W] a01 [F:a02 a03] [V] a04 ...(Vは a04 の前のまま)
    check(HomeLayout::MoveToFolder(2, f), "入れる");
    n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    eq(n, 7, "並びは7つ");
    check(seq[3].type == ItemType::Widget && seq[4].type == ItemType::App && seq[4].index == 3,
          "Vは a04 の前に残る");

    //マス目では1x1
    HomeLayout::Item items[HomeLayout::kMaxItems];
    int pages = 0;
    n = HomeLayout::Compute(3, items, HomeLayout::kMaxItems, &pages);
    const HomeLayout::Item* fi = findItem(items, n, ItemType::Folder, f);
    check(fi && fi->w == 1 && fi->h == 1, "フォルダは1マス");
    //中だけを並べる
    n = HomeLayout::ComputeFolder(f, 3, items, HomeLayout::kMaxItems, &pages);
    eq(n, 2, "中は2つ");
    check(items[0].type == ItemType::App && items[0].index == 1 && items[1].index == 2, "中は a02 a03");

    //フォルダをトップで動かす(ウィジェットとも入れ替わる): [W] [F] a01 [V] a04 ...
    check(HomeLayout::Move(ItemType::Folder, f, -1, 3), "フォルダを前へ");
    n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    check(seq[1].type == ItemType::Folder && seq[2].type == ItemType::App && seq[2].index == 0, "a01 と入れ替わる");
    check(HomeLayout::MoveToFront(ItemType::Folder, f), "フォルダを先頭へ");
    n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    check(seq[0].type == ItemType::Folder, "フォルダが先頭");

    //中のアプリを隠しても並びは同じ。全部隠すとフォルダが消え、後ろのウィジェットは残る
    check(HomeLayout::HideApp(1), "中の a02 を隠す");
    eq(HomeLayout::Sequence(seq, HomeLayout::kMaxItems), 7, "並びは同じ");
    check(HomeLayout::HideApp(2), "中の a03 も隠す");
    n = HomeLayout::Sequence(seq, HomeLayout::kMaxItems);
    eq(n, 6, "フォルダがトップから消える");
    check(seq[0].type == ItemType::Widget, "先頭はウィジェット(フォルダの後ろの項目の前)");
    AppFunctions::SetHidden(1, false);
    AppFunctions::SetHidden(2, false);

    //AppGrid: フォルダを押すと中へ、戻るでトップのフォルダへ
    TestGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT - 28);
    int launched = -1;
    g.setOnLaunch([&launched](int i){ launched = i; });
    g.layoutChanged();
    g.showItem(ItemType::Folder, f);
    HomeLayout::ItemType ft;
    int fidx = -1;
    check(g.focusedItem(ft, fidx) && ft == ItemType::Folder && fidx == f, "フォルダを選べる");
    check(g.onFocusKey(FocusKey::Activate), "決定でフォルダを開く");
    eq(g.openedFolder(), f, "開いている");
    eq(launched, -1, "開くだけでアプリは起動しない");
    check(!g.wantsFrameUpdate(), "フォルダの中にウィジェットは無い");
    const Rect t1 = g.tileRect(1);
    eq(g.hitTile(t1.x + 5, t1.y + 5), 2, "中の2つ目は a03");
    OSData::touchX = t1.x + 5;
    OSData::touchY = STATUSBAR_HEIGHT + t1.y + 5;
    g.causeOnPressStart();
    g.causeOnPressEnd();
    eq(launched, 2, "中のアプリを押すと起動");
    check(g.onFocusKey(FocusKey::Back), "戻るで閉じる");
    eq(g.openedFolder(), -1, "トップへ戻る");
    check(g.focusedItem(ft, fidx) && ft == ItemType::Folder && fidx == f, "閉じるとフォルダを選んでいる");
    check(!g.onFocusKey(FocusKey::Back), "トップで戻るは使わない");

    //タップで開く。長押しは中のアプリ(隠して戻したのでウィジェットWがフォルダの前にいる。選んでいる枠で場所を取る)
    const Rect fr = g.focusRect();
    eq(g.hitTile(fr.x + 5, fr.y + 5 - STATUSBAR_HEIGHT), -1, "フォルダのタイルはアプリではない");
    OSData::touchX = fr.x + 5;
    OSData::touchY = fr.y + 5;
    g.causeOnPressStart();
    g.causeOnPressEnd();
    eq(g.openedFolder(), f, "タップで開く");
    g.press();
    AppGrid::LongPress lp;
    check(g.takeLongPress(millis() + 10000, 600, lp) && lp.on_item && lp.type == ItemType::App && lp.index == g.hitTile(fr.x + 5, fr.y + 5 - STATUSBAR_HEIGHT) && lp.index >= 0,
          "中で長押しするとアプリ");
    g.release();

    //中を全部出すとトップへ戻る
    HomeLayout::RemoveFromFolder(1);
    HomeLayout::RemoveFromFolder(2);
    eq(g.openedFolder(), -1, "空になったらトップへ");
    eq(AppFunctions::FolderCount(), 0, "フォルダは消える");
    HomeLayout::Test::Reset();
    AppFunctions::ResetLayout();
}

int main(){
    testLayout();
    testPersist();
    testGrid();
    testFolders();
    testTodoCache();
    testAgenda();
    printf("\n%s (失敗 %d 件)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
