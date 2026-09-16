// ウィジェットの「ヒープ確保パターン」をホスト上で実測するプロファイラ。
// 実機(script/host_test/run_mem.sh からビルド)ではなくPCで動かす。
//
// 狙い:
//   メモリプール化の議論では「ウィジェット1個 = 確保1回」と考えがちだが、実際には
//   Widget基底のstd::function 4つ、Labelのlines/line_offsets/cursor_slots等の
//   std::vector が内部で個別にヒープを叩いている。
//   ウィジェット本体だけをプールへ移しても、残った小さな確保が寿命バラバラのまま
//   ヒープに散らばり続ける = 断片化は止まらない。
//   そこを数字で確認するために、グローバルなoperator new/deleteを差し替えて
//   「ウィジェット1個あたり何回・何バイト確保されるか」を数える。
//
// 使い方: sh script/host_test/run_mem.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <string>

// ---- 確保カウンタ(operator new/delete を差し替えて集計する) ----
namespace Probe {

    constexpr int kBucketCount = 9;
    // サイズ分布のバケツ上限(バイト)。最後は「それ以上」
    constexpr size_t kBucketMax[kBucketCount] = {16, 32, 64, 128, 256, 512, 1024, 4096, (size_t)-1};
    constexpr const char* kBucketLabel[kBucketCount] = {
        "<=16B", "<=32B", "<=64B", "<=128B", "<=256B", "<=512B", "<=1KB", "<=4KB", ">4KB"
    };

    // 集計カウンタ。
    // live_bytes だけはウィンドウをまたいで積み上げる(途中でリセットすると、
    // 計測開始前に確保されていたものの解放でマイナスに振れ、ピークが実態とずれるため)。
    inline long alloc_count = 0;
    inline long free_count = 0;
    inline long long alloc_bytes = 0;
    inline long long live_bytes = 0;  // 現在の生存バイト数(リセットしない)
    inline long long peak_live = 0;   // ResetWindow()以降の live_bytes の最大値
    inline long buckets[kBucketCount] = {0};

    inline bool enabled = false;

    // 確保サイズを覚えておくための簡易ハッシュ表
    // (sizedでない operator delete 経由でもサイズを引けるようにするため)
    constexpr int kTableSize = 8192;
    struct Entry { void* ptr; size_t size; };
    inline Entry table[kTableSize];

    inline int Hash(void* p){
        return (int)(((uintptr_t)p >> 4) % kTableSize);
    }

    inline void Remember(void* p, size_t n){
        const int i = Hash(p);
        for(int probe = 0; probe < kTableSize; probe++){
            Entry& e = table[(i + probe) % kTableSize];
            if(e.ptr == nullptr){ e.ptr = p; e.size = n; return; }
        }
    }

    inline size_t Forget(void* p){
        const int i = Hash(p);
        for(int probe = 0; probe < kTableSize; probe++){
            Entry& e = table[(i + probe) % kTableSize];
            if(e.ptr == p){ const size_t n = e.size; e.ptr = nullptr; return n; }
            if(e.ptr == nullptr) return 0; //未登録(計測開始前の確保)
        }
        return 0;
    }

    // 計測ウィンドウを開始する。live_bytesは引き継ぎ、ピークだけ現在値から測り直す
    inline void ResetWindow(){
        alloc_count = 0;
        free_count = 0;
        alloc_bytes = 0;
        peak_live = live_bytes;
        for(int i = 0; i < kBucketCount; i++) buckets[i] = 0;
    }

    inline void Record(void* p, size_t n){
        if(!enabled) return;
        alloc_count++;
        alloc_bytes += (long long)n;
        live_bytes += (long long)n;
        if(live_bytes > peak_live) peak_live = live_bytes;
        for(int i = 0; i < kBucketCount; i++){
            if(n <= kBucketMax[i]){ buckets[i]++; break; }
        }
        Remember(p, n);
    }

    // Widget::operator new のようにグローバルoperator newを経由しない確保を、
    // サイズだけ受け取って同じカウンタへ積む
    inline void RecordExternal(size_t n){
        if(!enabled) return;
        alloc_count++;
        alloc_bytes += (long long)n;
        live_bytes += (long long)n;
        if(live_bytes > peak_live) peak_live = live_bytes;
        for(int i = 0; i < kBucketCount; i++){
            if(n <= kBucketMax[i]){ buckets[i]++; break; }
        }
    }

    inline void ReleaseExternal(size_t n){
        if(!enabled) return;
        free_count++;
        live_bytes -= (long long)n;
    }

    inline void Release(void* p){
        if(!p) return;
        const size_t n = Forget(p);
        if(n == 0) return; //計測対象外の確保
        free_count++;
        live_bytes -= (long long)n;
    }
}

void* operator new(size_t n){
    void* p = malloc(n ? n : 1);
    if(!p) throw std::bad_alloc();
    Probe::Record(p, n);
    return p;
}
void* operator new[](size_t n){ return operator new(n); }
void operator delete(void* p) noexcept { Probe::Release(p); free(p); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }

// ---- ここから下は計測対象。includeはoperator newの定義より後でよい ----
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/Icon.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/apps/MarkdownView.hpp"
#include "gui/scenes/Scene.hpp"
#include "OS_Data.hpp"

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}

void KeyboardFunctions::HideAll(){}
void KeyboardFunctions::Setup(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}

//ログは計測ノイズになるので黙らせる(確保をしない実装にする)
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

// ---- 計測ユーティリティ ----
static void PrintHeader(const char* title){
    printf("\n=== %s ===\n", title);
}

// 1ケースぶんの結果
struct Case {
    const char* name;
    size_t object_bytes;   // sizeof(ウィジェット)
    long alloc_count;      // 生成〜破棄までに走った確保回数
    long long alloc_bytes; // 同、総バイト数
    long long peak_bytes;  // 同時生存のピーク(ケース開始時からの増分)
    long long residue;     // 破棄後に残った量(0でなければ解放漏れ)
};

static Case cases[32];
static int case_count = 0;

// レイアウトを確定させる。
// Labelはsetterではレイアウトせず、結果が必要になった時点(render/getW/getH)で
// 1回だけ計算する。計測でこれを呼ばないと「まだ誰も結果を要求していない」状態のまま
// 破棄することになり、実際には描画時に走るはずの仕事が数字から抜け落ちる
static void ForceLayout(Widget* w){
    (void)w->getH();
    (void)w->getW();
}

// ウィジェットの生成から破棄までを1ケースとして計測する
template<typename F>
static void Measure(const char* name, size_t object_bytes, F&& body){
    const long long live_at_start = Probe::live_bytes;
    Probe::ResetWindow();

    body();

    Case& c = cases[case_count++];
    c.name = name;
    c.object_bytes = object_bytes;
    c.alloc_count = Probe::alloc_count;
    c.alloc_bytes = Probe::alloc_bytes;
    c.peak_bytes = Probe::peak_live - live_at_start;
    c.residue = Probe::live_bytes - live_at_start;
}

// residueが残っているケースの件数
static int leaking_cases = 0;

static void PrintCaseTable(){
    PrintHeader("ウィジェット1個あたりの確保パターン(生成->破棄)");
    //列見出しはASCIIで揃える(日本語は幅指定とバイト数がずれて崩れるため)
    printf("%-24s %8s %7s %7s %10s %10s %8s\n",
        "widget", "sizeof", "allocs", "inner", "alloc_sum", "peak", "residue");
    for(int i = 0; i < case_count; i++){
        const Case& c = cases[i];
        printf("%-24s %7zuB %7ld %7ld %9lldB %9lldB %7lldB%s\n",
            c.name, c.object_bytes, c.alloc_count, c.alloc_count - 1,
            c.alloc_bytes, c.peak_bytes, c.residue,
            c.residue ? "  <- 解放漏れ" : "");
        if(c.residue != 0) leaking_cases++;
    }
    printf("\nallocs=生成〜破棄で走った確保回数 / inner=そのうちオブジェクト本体以外\n");
    printf("innerが0でない = 本体をプールへ移してもその回数ぶんの小確保はヒープに残る。\n");
    printf("Labelのlines/line_offsets/cursor_slots(std::vector)と\n");
    printf("Widget基底のstd::function 4つが、その正体。\n");
}

static void PrintBuckets(const char* title){
    PrintHeader(title);
    printf("%-10s %8s\n", "size", "件数");
    for(int i = 0; i < Probe::kBucketCount; i++){
        if(Probe::buckets[i] == 0) continue;
        printf("%-10s %8ld\n", Probe::kBucketLabel[i], Probe::buckets[i]);
    }
    printf("小さいバケツに件数が集中しているほど、ヒープは細切れになりやすい。\n");
}

// ---- 計測用シーン(HomeSceneと同じ構成のウィジェットを作る) ----
class ProbeScene : public Scene {
    public:
        const char* getName() const override { return "Probe"; }

        void onEnter() override {
            auto* title = new Label<PICO_STR_M>(8, 24, "pico-os");
            title->setFontSize(FontFn::Big);
            WidgetFunctions::Add(title);

            for(int i = 0; i < 2; i++){
                auto* b = new Button(8, 64 + i * 48, "Markdownを開く");
                b->setW(200);
                b->setH(40);
                b->setOnPressEnd([](){});
                WidgetFunctions::Add(b);
            }

            auto* tb = new Textbox<PICO_STR_LL>("", 8, 180, 200, 60, false);
            WidgetFunctions::Add(tb);

        }
};

// 計測用のダミー文書。SDスタブ(HostSd::files)へ登録して load() に読ませる
static const char* kProbeDocPath = "probe/doc.md";

static void RegisterProbeDoc(){
    std::string doc;
    doc += "# 見出し1\n\n";
    doc += "これは段落です。**太字**と_下線_を含みます。\n\n";
    doc += "## 見出し2\n\n";
    for(int i = 0; i < 12; i++){
        doc += "- リスト項目";
        doc += std::to_string(i);
        doc += " 折り返しが起きる程度の長さの日本語テキストを入れておきます。\n";
    }
    doc += "\n> 引用文\n\n";
    doc += "| 列A | 列B |\n| --- | --- |\n| 値1 | 値2 |\n\n";
    doc += "---\n\n";
    doc += "最後の段落です。\n";
    HostSd::files[kProbeDocPath] = doc;
}

// Widget::operator new/delete をProbeのカウンタへ流し込む
static void ObserveWidgetAlloc(size_t bytes, bool is_alloc){
    if(is_alloc) Probe::RecordExternal(bytes);
    else         Probe::ReleaseExternal(bytes);
}

int main(){
    RegisterProbeDoc();
    MemFunctions::widget_alloc_observer = &ObserveWidgetAlloc;

    //以降の確保をすべて集計対象にする
    Probe::enabled = true;

    OSData::frame = new LGFX_Sprite();
    FontFn::SetDefault();
    WidgetFunctions::Setup();

    // --- (1) ウィジェット単体 ---
    Measure("Button", sizeof(Button), [](){
        auto* w = new Button(0, 0, "ボタン");
        w->setOnPressEnd([](){});
        ForceLayout(w);
        delete w;
    });
    Measure("Label<M> 短文", sizeof(Label<PICO_STR_M>), [](){
        auto* w = new Label<PICO_STR_M>(0, 0, "pico-os");
        ForceLayout(w);
        delete w;
    });
    Measure("Label<M> 装飾つき", sizeof(Label<PICO_STR_M>), [](){
        auto* w = new Label<PICO_STR_M>(0, 0, "**太字**と_下線_と~~打消~~");
        ForceLayout(w);
        delete w;
    });
    Measure("Label<1KiB> 折返しあり", sizeof(Label<PICO_STR_1KiB>), [](){
        auto* w = new Label<PICO_STR_1KiB>(0, 0,
            "これは折り返しの発生する長めの日本語テキストです。"
            "Labelは行ごとにvectorを確保するため、行数が増えるほど確保回数も増えます。");
        w->setMaxWidth(200);
        ForceLayout(w);
        delete w;
    });
    Measure("Textbox<LL>", sizeof(Textbox<PICO_STR_LL>), [](){
        auto* w = new Textbox<PICO_STR_LL>("入力欄", 0, 0, 200, 60, false);
        ForceLayout(w);
        delete w;
    });
    //Checkboxはメンバテンプレートの実体化漏れで他の翻訳単位から使えなかった時期があり、
    //その間このケースを外していた。ここに置くこと自体がリンクの回帰検出になる
    Measure("Checkbox", sizeof(Checkbox), [](){
        auto* w = new Checkbox(0, 0, "チェック");
        ForceLayout(w);
        delete w;
    });
    Measure("Icon", sizeof(Icon), [](){
        auto* w = new Icon(0, 0, IconID::Folder, IconSize::Px16);
        ForceLayout(w);
        delete w;
    });

    // 子ウィジェットや内部バッファを自前で確保する重量級。
    // デストラクタで解放し損ねるとここのresidueが0でなくなる
    Measure("ScrollList", sizeof(ScrollList), [](){
        auto* w = new ScrollList(0, 0, 200, 100, 8);
        for(int i = 0; i < 8; i++){
            ScrollListTools::Item item;
            item.text.assign("項目");
            w->add(item);
        }
        delete w;
    });
    Measure("CanvasRaster", sizeof(CanvasRaster), [](){
        auto* w = new CanvasRaster(0, 0, 120, 80);
        delete w;
    });
    Measure("MarkdownView", sizeof(MarkdownView), [](){
        //Label16 + Image2 + Icon16 + measure_label をコンストラクタで確保する
        auto* w = new MarkdownView(0, 0, 240, 240);
        delete w;
    });
    Measure("MarkdownView + load()", sizeof(MarkdownView), [](){
        //実機に近い値を得るため、文書を読み込んでレイアウトまで済ませた状態で測る。
        //コンストラクタだけの行と比べると、doc_textの中身とLabelの行データが乗る
        auto* w = new MarkdownView(0, 0, 240, 240);
        w->load(kProbeDocPath);
        delete w;
    });

    // --- (2) 既存ウィジェットへの再設定(スクロールや入力で毎回走る経路) ---
    //MarkdownView::bindLabelSlot()と同じ順でプロパティを設定するケース。
    //遅延が効いていればレイアウトは3回ではなく1回で済む
    Measure("Label<512B> bind相当", sizeof(Label<PICO_STR_512B>), [](){
        auto* w = new Label<PICO_STR_512B>(0, 0, "");
        w->setMaxWidth(200);
        w->setFontSize(FontFn::Small);
        w->setText("これは折り返しの発生する日本語のテキストです。"
                   "Labelは1文字ずつ折返し位置を決めています。");
        ForceLayout(w);
        delete w;
    });

    Measure("Label<M> setText x10", sizeof(Label<PICO_STR_M>), [](){
        auto* w = new Label<PICO_STR_M>(0, 0, "");
        for(int i = 0; i < 10; i++){
            char buf[32];
            snprintf(buf, sizeof(buf), "行%d: テキスト更新", i);
            w->setText(buf);
        }
        //10回まとめて更新した後に1回だけレイアウトが走るのが期待値
        ForceLayout(w);
        delete w;
    });

    PrintCaseTable();

    // --- (3) シーン1枚ぶん ---
    const long long scene_start_live = Probe::live_bytes;
    Probe::ResetWindow();
    SceneFunctions::Setup(new ProbeScene());
    const long long scene_peak = Probe::peak_live - scene_start_live;
    const long long scene_live = Probe::live_bytes - scene_start_live;
    const long scene_allocs = Probe::alloc_count;
    PrintBuckets("シーン1枚(Label1 + Button2 + Textbox1)の確保サイズ分布");
    printf("\nシーン生成の確保回数=%ld / 生成直後の生存=%lldB / 同時生存ピーク=%lldB\n",
        scene_allocs, scene_live, scene_peak);
    printf("-> アリーナ枠は、この「同時生存ピーク」を全シーンぶん比べた最大値から決める。\n");

    // --- (4) 遷移を繰り返したときの累積 ---
    constexpr int kLoops = 50;

    const long long loop_start_live = Probe::live_bytes;
    Probe::ResetWindow();

    for(int i = 0; i < kLoops; i++){
        SceneFunctions::Change(new ProbeScene());
        SceneFunctions::Update();
    }

    const long long loop_end_live = Probe::live_bytes;
    const long long loop_peak = Probe::peak_live;
    const long loop_allocs = Probe::alloc_count;
    const long loop_frees = Probe::free_count;

    PrintHeader("シーン遷移を50回繰り返したときの累積");
    printf("確保回数=%ld / 解放回数=%ld\n", loop_allocs, loop_frees);
    printf("1遷移あたりの確保回数=%ld回\n", loop_allocs / kLoops);
    printf("開始時の生存=%lldB -> 50回後の生存=%lldB (差%+lldB)\n",
        loop_start_live, loop_end_live, loop_end_live - loop_start_live);
    printf("期間中の生存ピーク=%lldB\n", loop_peak);
    printf("\n差が0付近ならリークなし。単調増加するなら解放漏れで、\n");
    printf("その場合は断片化対策より先に解放漏れを潰すこと。\n");
    printf("\n-> 「1遷移あたり%ld回のmalloc/free」がシーンアリーナで消せる対象。\n",
        loop_allocs / kLoops);
    printf("   ただしアリーナ化で0回になるのはウィジェット本体だけで、内部の\n");
    printf("   vector/std::functionは上の表のinner列ぶんだけ残る。\n");
    printf("   そこまで消すにはSceneAllocatorや固定長化が要る。\n");

    // 後片付け
    while(SceneFunctions::CanPop()){
        SceneFunctions::Pop();
        SceneFunctions::Update();
    }
    WidgetFunctions::ClearSceneWidgets();

    // シーンアリーナが実際に抱えることになる量(= ウィジェット本体だけ)を出す。
    // シーン全体のピークには内部のstd::vector/std::functionも含まれており、
    // そちらを枠の根拠にすると倍近く過大になる
    {
        const uint32_t widget_before = MemFunctions::widget_live_bytes;
        const long long live_before = Probe::live_bytes;

        MarkdownView* view = new MarkdownView(0, 0, 240, 240);
        view->load(kProbeDocPath);

        const uint32_t widget_bytes = MemFunctions::widget_live_bytes - widget_before;
        const uint32_t widget_count = MemFunctions::widget_live_count;
        const long long total = Probe::live_bytes - live_before;

        delete view;

        PrintHeader("シーンアリーナが抱える量(MarkdownView + load())");
        printf("ウィジェット本体   %8uB (%u個)\n", widget_bytes, widget_count);
        printf("内部のvector等     %8lldB\n", total - (long long)widget_bytes);
        printf("合計               %8lldB\n", total);
        printf("-> アリーナ枠の根拠にできるのは上段だけ(%.0f%%)。\n",
            widget_bytes * 100.0 / (double)(total ? total : 1));
        printf("   下段はグローバルヒープに残るので、枠に足しても無駄になる。\n");
    }

    // load()が読み込み・パース・レイアウト・バインドまで通っているかの確認。
    // SDスタブが空ファイルしか返さなかった頃はこの経路が一度も実行されておらず、
    // チャンク読み込みへの書き換えが検証できていなかった
    {
        MarkdownView view(0, 0, 240, 240);
        const bool loaded = view.load(kProbeDocPath);
        int visible_children = 0;
        for(Widget* c : view.getChildren()){
            if(c->getVisible()) visible_children++;
        }
        PrintHeader("load()の経路確認");
        printf("load()=%s / 表示状態になった子ウィジェット=%d個\n",
            loaded ? "true" : "false", visible_children);
        if(!loaded || visible_children == 0){
            printf("load()が文書を読めていません(SDスタブの登録漏れか読み込み処理の不具合)\n");
            return 1;
        }
    }

    if(leaking_cases > 0){
        printf("\n解放漏れのあるウィジェットが%d件あります(上の表のresidue列)\n", leaking_cases);
        return 1;
    }

    printf("\n計測完了(解放漏れなし)\n");
    return 0;
}
