// 本物のシーンを何十回も往復して、シーンを抜けた直後のメモリが毎回同じ値へ戻るかを確かめるテスト。run.sh から呼ぶ。
//
// performance.md「メモリ計測の結論」の「ヒープ下限」(シーン破棄直後の使用量)をCIで見張る回帰テスト。
//   - 初めの kWarmupCycles 周は捨てる(遅延して作る表・キャッシュ・std::vector の伸びが初めのうちだけ確保されるため)。
//     標準出力はバッファ無しにする(初めての printf が4KiBのバッファを確保し、測った値に混ざるため)
//   - その後の値を基準に、残りの周で
//       ウィジェットの生存数・バイト数 … 1つも増えないこと(子をデストラクタで消し忘れると増える。過去に何度も起きた)
//       シーンアリーナの塊           … 全部返っていること
//       ヒープの使用量               … 周の数に比例して増えないこと(kHeapSlackPerCycle 以内)
//   - シーンの往復は SceneFunctions の Change/Push/Pop を本物で通す(フレームの境目で切り替わる経路)
//
// PCビルドで見つかった増加(LovyanGFXのSDLのパネル。performance.md の解決済みの節)はSDLの中なのでここでは捕まらない。
// このテストが見るのは src/ のシーン・ウィジェットの解放漏れ。
//
// シーンを足すときは kScenes に1行足す(ホストのスタブで作れないシーンは CMakeLists.txt の HOST_EXCLUDE を参照)。
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/widgets/WidgetArena.hpp"
#include "system/Log_Functions.hpp"
#include "system/Mem_Functions.hpp"
#include "OS_Data.hpp"
#include "services/Display_Functions.hpp"
#include "hal/Network_Functions.hpp"

#include "gui/scenes/ClocksScene.hpp"
#include "gui/scenes/FileExplorerScene.hpp"
#include "gui/scenes/NotificationScene.hpp"
#include "gui/scenes/RecentFilesScene.hpp"
#include "gui/scenes/StorageScene.hpp"
#include "gui/scenes/BackgroundScene.hpp"
#include "apps/calculator/CalculatorScene.hpp"
#include "apps/notes/NotesScene.hpp"
#include "apps/texteditor/TextEditorScene.hpp"
#include "apps/dict/DictScene.hpp"
#include "apps/calendar/CalendarScene.hpp"
#include "apps/todo/TodoScene.hpp"
#include "apps/reader/ReaderLibraryScene.hpp"
#include "apps/browser/MarkdownScene.hpp"
#include "apps/store/StoreScene.hpp"

#include <cstdio>
#include <cstring>
#include <malloc.h>

#if defined(__has_feature)
    #if __has_feature(address_sanitizer)
        #define SCENE_CYCLE_ASAN 1
    #endif
#endif
#if defined(__SANITIZE_ADDRESS__)
    #define SCENE_CYCLE_ASAN 1
#endif
#ifdef SCENE_CYCLE_ASAN
    // <sanitizer/allocator_interface.h> は入っていない環境があるので自分で宣言する(ASanのランタイムが持つ)
    extern "C" size_t __sanitizer_get_current_allocated_bytes();
#endif

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
// 画面の消灯とWi-Fiの省電力(実物は液晶・Wi-Fiのスタブに無い口を使う。電源のサービスが呼ぶ)
void DisplayFunctions::SetSleeping(bool){}
bool DisplayFunctions::IsDimmed(){ return false; }
void NetworkFunctions::SetLowPower(bool){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// いまヒープに置かれているバイト数(ASanの下ではASanの数え方、そうでなければmallinfo2)
static size_t heapUsed(){
#ifdef SCENE_CYCLE_ASAN
    return __sanitizer_get_current_allocated_bytes();
#else
    return mallinfo2().uordblks;
#endif
}

// 何もしない画面(往復の行き先と戻り先)
class EmptyScene : public Scene {
    public:
        const char* getName() const override { return "Empty"; }
        void onEnter() override {}
};

template<typename T> static Scene* make(){ return new T(); }
static Scene* makeStorage(){ return new StorageScene("/"); }

struct SceneCase {
    const char* name;
    Scene* (*create)();
};

static const SceneCase kScenes[] = {
    {"時計",           &make<ClocksScene>},
    {"ファイル",       &make<FileExplorerScene>},
    {"通知",           &make<NotificationScene>},
    {"最近のファイル", &make<RecentFilesScene>},
    {"ストレージ",     &makeStorage},
    {"バックグラウンド", &make<BackgroundScene>},
    {"電卓",           &make<CalculatorScene>},
    {"メモ",           &make<NotesScene>},
    {"テキスト",       &make<TextEditorScene>},
    {"辞書",           &make<DictScene>},
    {"カレンダー",     &make<CalendarScene>},
    {"TODO",           &make<TodoScene>},
    {"読書",           &make<ReaderLibraryScene>},
    {"Markdown",       &make<MarkdownScene>},
    {"ストア",         &make<StoreScene>},
};
constexpr int kSceneCount = sizeof(kScenes) / sizeof(kScenes[0]);

constexpr int kWarmupCycles = 2;
constexpr int kCycles = 12;            // 1周 = 全部のシーンを Change と Push/Pop で1回ずつ
constexpr int kFramesPerScene = 5;     // シーンにいる間に回すフレーム
constexpr size_t kHeapSlackPerCycle = 64;  // 1周あたり許す増加(この規模はシーン1つの部品にも満たない)

// 1フレーム分(main.cpp の loop() のうち、シーンとウィジェットの部分)
static void frame(){
    SceneFunctions::Update();
    WidgetFunctions::UpdateAll();
    WidgetFunctions::ProcessPendingDeletes();
}

static void stay(){
    for(int i = 0; i < kFramesPerScene; i++) frame();
}

// シーンにいる間のウィジェットの数の最大(数え方が生きていることを確かめる)
static uint32_t widget_peak = 0;

// 全部のシーンを1回ずつ、Change で入れ替える往復と、Push/Pop で上へ積む往復で通す
static void cycle(){
    for(int i = 0; i < kSceneCount; i++){
        SceneFunctions::Change(kScenes[i].create());
        stay();
        if(MemFunctions::widget_live_count > widget_peak) widget_peak = MemFunctions::widget_live_count;
        SceneFunctions::Change(new EmptyScene());
        stay();

        SceneFunctions::Push(kScenes[i].create());
        stay();
        SceneFunctions::Pop();
        stay();
    }
}

struct Sample {
    uint32_t widget_count;
    uint32_t widget_bytes;
    uint32_t arena_chunks;
    size_t heap;
    size_t scene_widgets;
};

static Sample sample(){
    return Sample{
        MemFunctions::widget_live_count,
        MemFunctions::widget_live_bytes,
        MemFunctions::arena_chunks,
        heapUsed(),
        WidgetFunctions::widgets.size() + WidgetFunctions::dialog_roots.size(),
    };
}

int main(){
    setvbuf(stdout, nullptr, _IONBF, 0);
    SceneFunctions::Setup(new EmptyScene());
    frame();

    // 初めの数周は捨てる(初めのうちだけの確保)。その後を基準にする
    for(int c = 0; c < kWarmupCycles; c++) cycle();
    const Sample base = sample();
    printf("基準(%d周の後): ", kWarmupCycles);
    printf("ウィジェット%u個 %uB / アリーナの塊%u / ヒープ%zuB\n",
           base.widget_count, base.widget_bytes, base.arena_chunks, base.heap);

    Sample last = base;
    size_t heap_max = base.heap;
    for(int c = kWarmupCycles; c < kCycles; c++){
        cycle();
        last = sample();
        if(last.heap > heap_max) heap_max = last.heap;
        printf("  %2d周目: ウィジェット%u個 %uB / アリーナの塊%u / ヒープ%zuB (%+ld)\n",
               c + 1, last.widget_count, last.widget_bytes, last.arena_chunks, last.heap,
               (long)last.heap - (long)base.heap);
    }

    const int measured = kCycles - kWarmupCycles;
    printf("シーンにいる間のウィジェットの最大: %u個\n", widget_peak);
    check(widget_peak > 0, "シーンにいる間はウィジェットが数えられている(数え方が生きている)");
    check(last.scene_widgets == 0, "空の画面に戻るとシーンのウィジェットは1つも積まれていない");
    check(last.widget_count == base.widget_count, "ウィジェットの生存数が周を重ねても増えない");
    check(last.widget_bytes == base.widget_bytes, "ウィジェットのバイト数が周を重ねても増えない");
    check(last.arena_chunks == 0, "シーンアリーナの塊を全部返している");
    char label[160];
    snprintf(label, sizeof(label), "ヒープ下限が周の数に比例して増えない(%d周で%+ldB、許すのは%zuB)",
             measured, (long)heap_max - (long)base.heap, kHeapSlackPerCycle * measured);
    check(heap_max <= base.heap + kHeapSlackPerCycle * measured, label);

    printf("%s (%d件の失敗)\n", failures == 0 ? "全部通った" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
