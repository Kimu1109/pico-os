// ホスト上でシーン遷移の実コード(Scene_Functions.cpp / Widget_Functions.cpp)を動かす検証用テスト
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <vector>
#include <string>

// ---- モック ----
static int mark_dirty_calls = 0;
static Rect last_dirty = {0,0,0,0};
void PICO_GFX::MarkDirty(const Rect& rect){
    if(!isDirtyDeactivates){ mark_dirty_calls++; last_dirty = rect; }
}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}

static int hide_all_calls = 0;
void KeyboardFunctions::HideAll(){ hide_all_calls++; }
void KeyboardFunctions::Setup(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}

static std::vector<std::string> logs;
void LogFunctions::Log(LogType type, const char* fmt, ...){
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    logs.push_back(std::string(GetPrefix(type)) + buf);
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

// ---- テスト用ウィジェット ----
static int widget_alive = 0;
class TestWidget : public Widget {
    private:
        std::vector<Widget*> children_;
    public:
        TestWidget(){ widget_alive++; l_rect = {0, 0, 10, 10}; }
        ~TestWidget() override {
            for(Widget* c : children_) delete c;
            widget_alive--;
        }
        void addChild(Widget* c){ c->setParent(this); children_.push_back(c); }
        const std::vector<Widget*>& getChildren() const override { return children_; }
        void removeChild(Widget* child) override {
            for(size_t i = 0; i < children_.size(); i++){
                if(children_[i] == child){ children_.erase(children_.begin() + i); return; }
            }
        }
        void render() override {}
        WidgetType getWidgetType() const override { return WidgetType::Button; }
};

// ---- テスト用シーン ----
static int scene_alive = 0;
class TestScene : public Scene {
    public:
        const char* name;
        int child_count;
        int enter_count = 0;
        int exit_count = 0;
        Widget* root = nullptr;

        TestScene(const char* name, int child_count) : name(name), child_count(child_count){ scene_alive++; }
        ~TestScene() override { scene_alive--; }

        const char* getName() const override { return name; }
        void onEnter() override {
            enter_count++;
            TestWidget* r = new TestWidget();
            for(int i = 0; i < child_count; i++) r->addChild(new TestWidget());
            root = r;
            WidgetFunctions::Add(r);
        }
        void onExit() override { exit_count++; root = nullptr; }
};

// ボタンのコールバックから遷移を要求するシーン(遅延適用が効いているかの検証用)
class CallbackScene : public Scene {
    public:
        int enter_count = 0;
        TestWidget* button = nullptr;

        CallbackScene(){ scene_alive++; }
        ~CallbackScene() override { scene_alive--; }

        const char* getName() const override { return "Callback"; }
        void onEnter() override {
            enter_count++;
            button = new TestWidget();
            //押し終わりに自分自身を含むシーンを破棄する要求を出す
            button->setOnPressEnd([](){ SceneFunctions::Change(new TestScene("next", 0)); });
            WidgetFunctions::Add(button);
        }
        void onExit() override { button = nullptr; }
};

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    WidgetFunctions::Setup();

    TestScene* a = new TestScene("A", 2);
    SceneFunctions::Setup(a);
    check(WidgetFunctions::widgets.size() == 3, "初期シーン: ルート+子2がwidgetsに登録される");
    check(widget_alive == 3, "初期シーン: ウィジェット3つが生存");
    check(a->enter_count == 1, "初期シーン: onEnterが1回");

    // 常駐ウィジェット(オーバーレイ)を1つ置き、遷移で残ることを確認する
    TestWidget* overlay = new TestWidget();
    overlay->setY(300); //タッチ判定はオーバーレイが最優先なので、後段のテストの当たり判定から外しておく
    WidgetFunctions::AddOverlay(overlay);

    // Push要求は即時実行されない
    TestScene* b = new TestScene("B", 0);
    SceneFunctions::Push(b);
    check(a->exit_count == 0 && WidgetFunctions::widgets.size() == 3, "Push要求は即時実行されない(フレーム境界まで保留)");

    mark_dirty_calls = 0;
    SceneFunctions::Update();
    check(a->exit_count == 1, "Push: 前シーンのonExitが呼ばれる");
    check(hide_all_calls == 1, "Push: キーボードが閉じられる");
    check(widget_alive == 2, "Push: 前シーンのウィジェット3つが解放され、B(1)+overlay(1)のみ生存");
    check(WidgetFunctions::widgets.size() == 1, "Push: widgetsは新シーンのルートのみ");
    check(WidgetFunctions::overlays.size() == 1, "Push: オーバーレイは破棄されない");
    check(SceneFunctions::Depth() == 1 && SceneFunctions::CanPop(), "Push: スタック深さ1");
    check(scene_alive == 2, "Push: 退避したシーンオブジェクトは生存(ウィジェットのみ解放)");
    check(mark_dirty_calls == 1 && last_dirty.w == SCREEN_WIDTH && last_dirty.h == SCREEN_HEIGHT,
          "Push: dirtyは全画面1枚だけ登録される");

    // ダイアログもシーンの所有物として破棄される
    TestWidget* dialog = new TestWidget();
    WidgetFunctions::AddDialog(dialog);
    SceneFunctions::Pop();
    SceneFunctions::Update();
    check(WidgetFunctions::dialog_roots.empty(), "Pop: 開いていたダイアログも破棄される");
    check(scene_alive == 1 && SceneFunctions::Current() == a, "Pop: 現シーンは破棄され、退避シーンへ戻る");
    check(a->enter_count == 2, "Pop: 戻り先のonEnterで作り直される");
    check(WidgetFunctions::widgets.size() == 3 && widget_alive == 4, "Pop: 戻り先のウィジェットが復元される(+overlay)");
    check(SceneFunctions::Depth() == 0, "Pop: スタックが空に戻る");

    // 空スタックのPopは何もしない
    SceneFunctions::Pop();
    SceneFunctions::Update();
    check(SceneFunctions::Current() == a && a->exit_count == 1, "空スタックのPopは無視される");

    // 同一フレーム内の2件目の要求は破棄される(シーンオブジェクトをリークしない)
    TestScene* c = new TestScene("C", 1);
    TestScene* d = new TestScene("D", 1);
    SceneFunctions::Change(c);
    SceneFunctions::Change(d);
    check(scene_alive == 2, "重複した遷移要求は受け付けず、却下したシーンは即deleteされる");
    SceneFunctions::Update();
    check(SceneFunctions::Current() == c && scene_alive == 1, "Change: 前シーンは破棄され新シーンへ置き換わる");
    check(WidgetFunctions::widgets.size() == 2, "Change: 新シーンのウィジェットのみ");

    // スタック上限
    for(int i = 0; i < SceneFunctions::kMaxSceneDepth; i++){
        SceneFunctions::Push(new TestScene("stack", 0));
        SceneFunctions::Update();
    }
    check(SceneFunctions::Depth() == SceneFunctions::kMaxSceneDepth, "スタックは上限まで積める");
    const int before = scene_alive;
    SceneFunctions::Push(new TestScene("overflow", 0));
    SceneFunctions::Update();
    check(scene_alive == before && SceneFunctions::Depth() == SceneFunctions::kMaxSceneDepth,
          "上限超過のPushは破棄され、現シーンは維持される");

    // ボタンのコールバック内から遷移を要求しても、実行中のウィジェットをdeleteしない
    while(SceneFunctions::CanPop()){
        SceneFunctions::Pop();
        SceneFunctions::Update();
    }
    CallbackScene* cb = new CallbackScene();
    SceneFunctions::Change(cb);
    SceneFunctions::Update();
    check(SceneFunctions::Current() == cb && WidgetFunctions::widgets.size() == 1,
          "コールバック検証: シーンを入れ替え");

    //フレーム1: タッチ開始
    OSData::touchX = 5; OSData::touchY = 5;
    OSData::isTouchStart = true; OSData::isTouched = true; OSData::isTouchEnd = false;
    SceneFunctions::Update();
    WidgetFunctions::UpdateAll();
    check(WidgetFunctions::pressingWidget == cb->button, "コールバック検証: 押下中のウィジェットが確定");

    //フレーム2: タッチ終了 -> on_press_endからChange()が呼ばれる
    Widget* pressed = cb->button;
    OSData::isTouchStart = false; OSData::isTouchEnd = true; OSData::isTouched = false;
    SceneFunctions::Update();
    WidgetFunctions::UpdateAll();
    check(SceneFunctions::Current() == cb, "コールバック内のChangeは即時実行されない");
    check(widget_alive == 2 && WidgetFunctions::widgets.size() == 1 && WidgetFunctions::widgets[0] == pressed,
          "コールバック実行中のウィジェットは生存したまま(use-after-freeしない)");

    //フレーム3: 遷移が適用される
    OSData::isTouchEnd = false;
    SceneFunctions::Update();
    check(SceneFunctions::Current() != cb && scene_alive == 1, "次フレームで遷移が適用され、前シーンは破棄される");
    check(WidgetFunctions::pressingWidget == nullptr, "遷移後にpressingWidgetがクリアされる");
    WidgetFunctions::UpdateAll();
    check(widget_alive == 2, "遷移後のUpdateAllでダングリング参照を踏まない");

    // 計測フック(Mem_Functions)の配線確認。
    // BeforeSceneEnter -> onEnter -> AfterSceneEnter の順で呼ばれていないと、
    // シーンごとの必要バイト数が取れずアリーナの枠を決められない。
    //
    // なおバイト数そのものはここでは検証できない。ASanはmallocごと差し替えるため
    // mallinfo()が実際の確保を反映せず、常に0バイトに見える。
    // 確保量の妥当性は script/host_test/run_mem.sh (ASan無し)側で確認する
    check(MemFunctions::transition_count >= 5, "計測フック: シーン遷移が記録されている");
    check(MemFunctions::scene_stat_count > 0, "計測フック: シーン別統計が作られている");
    {
        const MemFunctions::SceneStat* stat_a = nullptr;
        for(int i = 0; i < MemFunctions::scene_stat_count; i++){
            if(MemFunctions::scene_stats[i].name == "A") stat_a = &MemFunctions::scene_stats[i];
        }
        //シーンAは Setup() と Pop()で戻った時の計2回enterしている
        check(stat_a && stat_a->visits == 2, "計測フック: 同名シーンの訪問回数が積算される");
    }

    // 後片付け(リーク確認)
    while(SceneFunctions::CanPop()){
        SceneFunctions::Pop();
        SceneFunctions::Update();
    }
    WidgetFunctions::ClearSceneWidgets();
    check(widget_alive == 1, "最終状態: 残るウィジェットはオーバーレイのみ");
    WidgetFunctions::Destroy(overlay);
    check(widget_alive == 0, "最終状態: ウィジェットのリークなし");

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
