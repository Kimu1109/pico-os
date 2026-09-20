#pragma once

#include "gui/widgets/Widget.hpp"

// Luaの直接描画(pico.draw_*)向けの、中身を持たない最小限のウィジェット。
// 自身では何も描かず、render()のたびにLua側が登録したコールバック
// (pico.on(id, "render", fn))を呼ぶだけ。
//
// なぜこれが要るか(CLAUDE.md「直接描画」参照): PICO_GFX::FlushDirty()はdirty矩形を
// 処理するたび、それを覆うウィジェットが無ければ背景色で塗りつぶしてから、そこに
// 重なるウィジェットだけを再描画する。ウィジェットに属さない場所への直接描画
// (pico.draw_*をloop()やコールバックから素で呼ぶだけ)は、次にその領域がdirtyに
// なった瞬間(シーン遷移時の全画面dirty化を含め、ほぼ必ず起きる)に背景色で
// 消され、誰も描き直さないので二度と戻らない(実際にfill_rectが跡形もなく
// 消えるのをPCビルドで確認した)。
//
// このウィジェットへLua側がrenderコールバックを登録し、その中でpico.draw_*を
// 呼べば、render()が呼ばれるたびに全部を描き直すので正しく合成サイクルに参加できる
// (CanvasRasterが自前スプライトへの描画+pushSpriteで同じ問題を解決しているのと
// 同じ理屈。こちらは私有スプライトを持たず、直接OSData::frameへ描く分だけ軽い)。
// クラス名を"Canvas"にしなかったのは、CanvasRaster.hppが既に`namespace Canvas`
// (Canvas::Mode)を使っているため衝突を避けたかったから。Lua側から見える種別名は
// WidgetFactory::TypeFromName()で"Canvas"に割り当てている(pico.create("Canvas"))。
class LuaCanvas : public Widget {
    private:
        std::function<void()> on_render = nullptr;

    public:
        LuaCanvas(int16_t x, int16_t y, int16_t w, int16_t h);

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::LuaCanvas; }

        // OPAQUE: FlushDirty()がrender()を呼ぶ前に自分の矩形をbackground_colorで
        // 塗りつぶしてくれるので、render()側(=Luaのrenderコールバック)は
        // 背景クリアを気にせず前景だけ描けばよい
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        // Widget基底にsetW/setHが無い(ウィジェットごとに意味が違うため)ので、
        // LayoutContainer等と同じ形でここへ足す
        void setW(int w) { this->l_rect.w = (int16_t)w; this->needsRender(); }
        void setH(int h) { this->l_rect.h = (int16_t)h; this->needsRender(); }

        void setOnRender(std::function<void()> callback) { this->on_render = callback; }
        void clearOnRender() { this->on_render = nullptr; }
};
