// シーンアリーナ(gui/widgets/WidgetArena.hpp)と、ウィジェットの中の確保を減らす部品
// (util/ChunkArena.hpp・util/SmallVector.hpp・util/InplaceFunction.hpp)のテスト。
//
// - ChunkArena: 末尾の巻き戻し・同じ大きさの使い回し・塊が空いたら返す・溢れたらnullptr
// - WidgetArena: 開いて閉じたダイアログの跡を次のダイアログが使う/シーンを離れると塊が0個に戻る
// - SmallVector: N個までは確保しない・超えたらヒープへ移る・移動で中身を奪う
// - InplaceFunction: キャプチャを箱に持ち、コピー・nullptrの扱いがstd::functionと同じ
// ASan付きで回すので、アリーナの空きに触ると(毒を塗ってあるので)ここで落ちる。
#include "util/ChunkArena.hpp"
#include "util/SmallVector.hpp"
#include "util/InplaceFunction.hpp"
#include "gui/widgets/WidgetArena.hpp"
#include "gui/core/Error_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/Button.hpp"
#include <cstdio>

// ---- モック(error_functions_test.cppと同じ方針) ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static void testChunkArena(){
    using Arena = ChunkArena<1024, 2>;
    Arena a;
    void* p1 = a.alloc(40);
    void* p2 = a.alloc(40);
    check(p1 && p2 && a.stats().chunks == 1, "ChunkArena: 小さい物は1つの塊に入る");
    check(a.owns(p1) && a.owns(p2), "ChunkArena: 渡した物はアリーナの物");
    check(a.alloc(Arena::kMaxObject + 1) == nullptr, "ChunkArena: 塊の半分を超える物はnullptr(mallocへ回す)");

    // p2は末尾なので巻き戻る → 次の確保が同じ場所になる
    check(a.release(p2, 40), "ChunkArena: 自分の物はreleaseがtrue");
    void* p3 = a.alloc(40);
    check(p3 == p2, "ChunkArena: 末尾を返すと次の確保が同じ場所");

    // 末尾でない物は同じ大きさの空きとして使い回す。違う大きさには使わない
    void* p4 = a.alloc(100);
    a.release(p1, 40);
    void* p5 = a.alloc(100);
    check(p5 != p1, "ChunkArena: 大きさの違う空きは使わない");
    void* p6 = a.alloc(33);   // 丸めると40と同じ大きさ
    check(p6 == p1, "ChunkArena: 同じ大きさ(丸めた後)の空きを使い回す");
    check(a.stats().reused == 1, "ChunkArena: 使い回しを数える");

    // 末尾を返したとき、その手前の空きも続けて巻き戻る
    a.release(p6, 33);   // 先頭(空きへ)
    a.release(p4, 100);  // 途中(空きへ)
    a.release(p5, 100);  // 末尾 → p4 も巻き戻る
    // 生きているのはp3だけ
    check(a.stats().live == 1, "ChunkArena: 生きている数");
    a.release(p3, 40);
    check(a.stats().live == 0 && a.stats().chunks == 1, "ChunkArena: 最後の塊は空になっても持っておく");
    void* q = a.alloc(40);
    check(q == p1, "ChunkArena: 空にした塊は先頭から使い直す");
    a.release(q, 40);

    // 塊が足りなくなったらnullptr
    void* big[8];
    int n = 0;
    while (n < 8) { void* p = a.alloc(Arena::kMaxObject); if (!p) break; big[n++] = p; }
    check(n == 4 && a.stats().chunks == 2, "ChunkArena: 塊は MaxChunks 個まで");
    check(a.stats().fallback >= 2, "ChunkArena: 溢れた回数を数える");
    // 古い方の塊(big[0],big[1])を空けると、その塊はすぐヒープへ返す
    a.release(big[0], Arena::kMaxObject);
    a.release(big[1], Arena::kMaxObject);
    check(a.stats().chunks == 1, "ChunkArena: 切り出し中でない塊は空いたら返す");
    check(!a.release(&n, sizeof(n)), "ChunkArena: アリーナの物でなければfalse");
    a.release(big[2], Arena::kMaxObject);
    a.release(big[3], Arena::kMaxObject);
    a.releaseEmpty();
    check(a.stats().chunks == 0, "ChunkArena: releaseEmpty()で空の塊を全部返す");
}

static void testSmallVector(){
    SmallVector<int, 2> v;
    v.push_back(1);
    v.push_back(2);
    check(v.size() == 2 && !v.onHeap(), "SmallVector: N個までは内側");
    v.push_back(3);
    check(v.size() == 3 && v.onHeap() && v[2] == 3, "SmallVector: 超えるとヒープへ移る");
    v.push_back(v[0]);   // 自分の中身を積む(広げる途中で参照先が消えても壊れない)
    check(v.size() == 4 && v[3] == 1, "SmallVector: 自分の要素をpush_back");
    v.erase(v.begin() + 1);
    check(v.size() == 3 && v[0] == 1 && v[1] == 3 && v[2] == 1, "SmallVector: eraseで後ろを詰める");

    SmallVector<int, 2> moved(static_cast<SmallVector<int, 2>&&>(v));
    check(moved.size() == 3 && moved.onHeap() && v.size() == 0 && !v.onHeap(), "SmallVector: 移動でヒープの中身を奪う");
    SmallVector<int, 2> copied = moved;
    check(copied.size() == 3 && copied[1] == 3 && copied.data() != moved.data(), "SmallVector: コピーは別の領域");
    copied.assign(5, 7);
    check(copied.size() == 5 && copied[4] == 7, "SmallVector: assign");
    copied.clear();
    check(copied.empty() && copied.capacity() >= 5, "SmallVector: clearは領域を持ったまま");

    SmallVector<int, 0> z;
    z.push_back(9);
    check(z.size() == 1 && z[0] == 9, "SmallVector: N=0はstd::vectorと同じく初回から確保");
}

static int g_called = 0;
static void Plain(){ g_called++; }

static void testInplaceFunction(){
    InplaceFunction<void()> f;
    check(!f && f == nullptr, "InplaceFunction: 既定は空");
    int count = 0;
    int* pc = &count;
    f = [pc](){ (*pc)++; };
    check((bool)f && f != nullptr, "InplaceFunction: ラムダを入れると空でない");
    f();
    InplaceFunction<void()> g = f;   // コピー
    g();
    check(count == 2, "InplaceFunction: コピーしても同じ物を呼ぶ");
    f = nullptr;
    check(!f && (bool)g, "InplaceFunction: nullptrの代入で空になる(コピーは残る)");

    InplaceFunction<int(int, int)> add = [](int a, int b){ return a + b; };
    check(add(2, 3) == 5, "InplaceFunction: 引数と戻り値");

    InplaceFunction<void()> fp = &Plain;
    fp();
    check(g_called == 1, "InplaceFunction: 関数ポインタ");
    void (*null_fp)() = nullptr;
    InplaceFunction<void()> nf = null_fp;
    check(!nf, "InplaceFunction: nullの関数ポインタは空");

    // ポインタ3つぶんまではキャプチャできる(Widgetのコールバックの決まり)
    void* a = &count; void* b = &g_called; void* c = &add;
    InplaceFunction<void*()> three = [a, b, c](){ return (void*)((char*)a + ((char*)b - (char*)c) * 0); };
    check(three() == a, "InplaceFunction: ポインタ3つのキャプチャ");
    check(sizeof(InplaceFunction<void()>) == 4 * sizeof(void*), "InplaceFunction: 大きさはポインタ4つぶん");
}

static void testWidgetArena(){
    check(!WidgetArena::IsEnabled(), "WidgetArena: ホストテストでは既定で切ってある");
    WidgetArena::Enable(true);

    // シーンの途中で開いて閉じるダイアログ: 閉じた跡を次に開いたダイアログが使う
    ErrorFunctions::ShowFatal("1回目");
    Widget* first = WidgetFunctions::dialog_roots.back();
    const size_t chunks = WidgetArena::ChunkCount();
    check(chunks >= 1 && WidgetArena::LiveCount() > 0, "WidgetArena: ダイアログの部品はアリーナへ置かれる");
    static_cast<MsgDialog*>(first)->causeOnClosed(true);
    WidgetFunctions::ProcessPendingDeletes();
    check(WidgetArena::LiveCount() == 0, "WidgetArena: 閉じると部品が全部アリーナへ返る");

    ErrorFunctions::ShowFatal("2回目");
    Widget* second = WidgetFunctions::dialog_roots.back();
    check(second == first, "WidgetArena: 2回目のダイアログは1回目と同じ場所");
    check(WidgetArena::ChunkCount() == chunks, "WidgetArena: 開き直しても塊は増えない");

    // 開いている間に作った別のウィジェットは、閉じても残る
    Button* keep = new Button(0, 0, "残る");
    static_cast<MsgDialog*>(second)->causeOnClosed(true);
    WidgetFunctions::ProcessPendingDeletes();
    check(WidgetArena::LiveCount() == 1, "WidgetArena: 生きている物だけ残る");
    delete keep;

    // シーンを離れる(ClearSceneWidgets → OnSceneExit)と塊は全部ヒープへ戻る
    WidgetFunctions::ClearSceneWidgets();
    WidgetArena::OnSceneExit();
    check(WidgetArena::ChunkCount() == 0 && WidgetArena::LiveCount() == 0, "WidgetArena: シーンを離れると塊は0個");

    // 切った後も、切る前にアリーナへ置いた物はアリーナへ返る(freeへ渡さない)
    Button* before = new Button(0, 0, "前");
    WidgetArena::Enable(false);
    Button* after = new Button(0, 0, "後");
    delete before;
    delete after;
    WidgetArena::OnSceneExit();
    check(WidgetArena::ChunkCount() == 0, "WidgetArena: 切る前の物は切った後もアリーナへ返る");
}

int main(){
    testChunkArena();
    testSmallVector();
    testInplaceFunction();
    testWidgetArena();

    if(failures){
        printf("\n%d件失敗\n", failures);
        return 1;
    }
    printf("\n全件成功\n");
    return 0;
}
