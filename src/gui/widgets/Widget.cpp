#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetArena.hpp"
#include "util/ChunkArena.hpp"
#include "OS_Data.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "system/Mem_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Focus_Functions.hpp"

#include <cstdlib>
#include <new>

namespace {
    ChunkArena<WidgetArena::kChunkBytes, WidgetArena::kMaxChunks> scene_arena;
    bool arena_enabled = false;

    void PublishArenaStats() {
        const auto& st = scene_arena.stats();
        MemFunctions::arena_chunks = st.chunks;
        MemFunctions::arena_peak_chunks = st.peak_chunks;
        MemFunctions::arena_reused = st.reused;
        MemFunctions::arena_served = st.served;
        MemFunctions::arena_fallback = st.fallback;
    }
}

void WidgetArena::Enable(bool enable) { arena_enabled = enable; }
bool WidgetArena::IsEnabled() { return arena_enabled; }

void WidgetArena::OnSceneExit() {
    scene_arena.releaseEmpty();
    // シーンの物は全部消えたはず。残るのはシーンをまたいで生きる物(シーンの途中で作った常駐の部品)だけで、
    // その塊はヒープへ返せない。ふつうは0
    if (scene_arena.stats().live > 0) {
        LOG_SYS_DEBUG("[MEM] シーン破棄後もアリーナに%lu個残っています(塊%lu個)",
            (unsigned long)scene_arena.stats().live, (unsigned long)scene_arena.stats().chunks);
    }
    PublishArenaStats();
}

size_t WidgetArena::ChunkCount() { return scene_arena.stats().chunks; }
size_t WidgetArena::LiveCount() { return scene_arena.stats().live; }
size_t WidgetArena::BorrowedCount() { return scene_arena.stats().borrowed; }

void* Widget::operator new(size_t bytes) noexcept {
    if (bytes == 0) bytes = 1;
    void* ptr = arena_enabled ? scene_arena.alloc(bytes) : nullptr;
    if (arena_enabled) PublishArenaStats();
    if (!ptr) ptr = malloc(bytes);
    if(!ptr) {
        // 組み込みでは例外を投げずnullptrを返す(呼び出し側で落ちる)。
        // 全ウィジェットの確保がここを通る唯一の入口なので、失敗を1箇所でログしておく
        // — 個々のnew Xxx(...)側にnullチェックが無くても、原因がここだと分かるようにする。
        LOG_SYS_FAIL("Widget::operator new: %luバイトの確保に失敗", (unsigned long)bytes);
        return nullptr;
    }
    MemFunctions::OnWidgetAlloc(bytes);
    return ptr;
}

void Widget::operator delete(void* ptr, size_t bytes) noexcept {
    if(!ptr) return;
    if (bytes == 0) bytes = 1;
    MemFunctions::OnWidgetFree(bytes);
    // アリーナを切った後でも、切る前にアリーナへ置いた物はアリーナへ返す
    if (scene_arena.release(ptr, bytes)) { PublishArenaStats(); return; }
    free(ptr);
}

Widget::~Widget() {
    FocusFunctions::OnWidgetGone(this);
    if (WidgetIdTools::IsValid(cached_id)) {
        WidgetRegistry::Unregister(cached_id);
    }
}

WidgetId Widget::getId() const {
    if (!WidgetIdTools::IsValid(cached_id)) {
        cached_id = WidgetRegistry::Register(getWidgetType(), const_cast<Widget*>(this));
    }
    return cached_id;
}

void Widget::update() {
    if (OSData::isTouchMove && this->is_pressing) {
        this->causeOnPressMove();
    }
    if (OSData::isTouchEnd && this->is_pressing) {
        this->causeOnPressEnd();
        is_pressing = false;
    }
    if (OSData::isTouchStart && !this->is_pressing){
        this->causeOnPressOut();
    }

    // 合成の外のrender()。状態の確認とdirtyの積み上げだけが目的で、描いた絵は直後のFlushDirty()が
    // 背景ごと塗り直して捨てる(以前は変化したウィジェットを毎フレーム2回描いていた)。
    // クリップを空にして書き込みを全部切り捨て、文字を描く部品はrender_suppressedを見て早めに戻る
    int32_t cx, cy, cw, ch;
    OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
    OSData::frame->setClipRect(0, 0, 0, 0);
    const bool was_suppressed = PICO_GFX::render_suppressed;
    PICO_GFX::render_suppressed = true;
    render();
    PICO_GFX::render_suppressed = was_suppressed;
    OSData::frame->setClipRect(cx, cy, cw, ch);
}

void Widget::causeOnPressStart() {
    if (on_press_start) on_press_start();
}
void Widget::clearOnPressStart(){
    on_press_start = nullptr;
}
void Widget::setOnPressStart(InplaceFunction<void()> callback) {
    this->on_press_start = callback;
}

void Widget::causeOnPressEnd(){
    if (on_press_end) on_press_end();
}
void Widget::clearOnPressEnd(){
    on_press_end = nullptr;
}
void Widget::setOnPressEnd(InplaceFunction<void()> callback) {
    this->on_press_end = callback;
}

void Widget::causeOnPressMove(){
    if (on_press_move) on_press_move();
}
void Widget::clearOnPressMove(){
    on_press_move = nullptr;
}
void Widget::setOnPressMove(InplaceFunction<void()> callback) {
    this->on_press_move = callback;
}

void Widget::causeOnPressOut(){
    if (on_press_out) on_press_out();
}
void Widget::clearOnPressOut(){
    on_press_out = nullptr;
}
void Widget::setOnPressOut(InplaceFunction<void()> callback){
    this->on_press_out = callback;
}

void Widget::setVisible(bool visible){
    this->visitAll([&visible](Widget* w) {
        w->visible = visible;
    });
    this->needsRender();
}
bool Widget::getVisible(){
    return this->visible;
}

void Widget::needsRender(){
    if(disable_markdirty) return;
    PICO_GFX::MarkDirty(this->getScreenRect());
    this->needs_redraw = true;
}

void Widget::markdirty(Rect rect){
    if(disable_markdirty) return;
    PICO_GFX::MarkDirty(rect);
}