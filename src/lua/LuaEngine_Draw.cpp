// pico.* のうち直接描画(図形・文字・画像・描画の範囲)とラスタキャンバス
#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"
#include "services/Power_Functions.hpp"
#include "system/Mem_Functions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/LuaCanvas.hpp"
#include "gui/widgets/ImageView.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/icons/icon_render.h"
#include "gui/icons/Bmp_Codec.hpp"
#include "gui/icons/Image_Codec.hpp"
#include "gui/Fill4bpp.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Scene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "system/Config_Functions.hpp"
#include "net/Http_Request.hpp"
#include "util/Url.hpp"
#include "system/Time_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "sound/Note_Name.hpp"
#include "sound/Mml_Compiler.hpp"
#include "lua/LuaDebugger.hpp"
#include "lua/LuaJson.hpp"
#include "lua/LuaBuiltinModules.hpp"
#include "crypto/Secret_Aead.hpp"
#include "devtools/Profiler_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"
#include <Arduino.h>


using namespace LuaInternal;

// ---------------- 直接描画 ----------------
// クラスコメント「直接描画」参照。ウィジェットを介さずOSData::frameへ直接描き、
// 描いた範囲だけPICO_GFX::MarkDirty()する(既存の各種render()実装と同じ流儀)。
// 色は既存プロパティと同じくPICO 4bitパレット番号をそのままint8_tへキャストするだけで、
// 範囲チェックはしない(WidgetProperty::Setの色プロパティと同じ)。

int LuaEngine::l_draw_pixel(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int8_t color = (int8_t)luaL_checkinteger(L, 3);

    OSData::frame->drawPixel(x, y, color);
    LuaMarkDirty({x, y, 1, 1});
    return 0;
}

int LuaEngine::l_draw_line(lua_State* L) {
    const int16_t x0 = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y0 = (int16_t)luaL_checkinteger(L, 2);
    const int16_t x1 = (int16_t)luaL_checkinteger(L, 3);
    const int16_t y1 = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);
    // 6番目の引数: 線の太さ(px、既定1)。2以上は両端の円+胴体の三角形で塗る
    // (drawWideLine()は4bppパレットでアンチエイリアスのブレンドに入りクラッシュするため使わない)
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 6, 1), 1, 64);

    if (width <= 1) {
        OSData::frame->drawLine(x0, y0, x1, y1, color);
        LuaMarkDirty({
            (int16_t)std::min(x0, x1), (int16_t)std::min(y0, y1),
            (int16_t)(std::abs(x1 - x0) + 1), (int16_t)(std::abs(y1 - y0) + 1)
        });
        return 0;
    }
    const float radius = width * 0.5f;
    CanvasRaster::DrawThickLine(OSData::frame, x0, y0, x1, y1, radius, color);
    const int pad = width / 2 + 1;
    LuaMarkDirty({
        (int16_t)(std::min(x0, x1) - pad), (int16_t)(std::min(y0, y1) - pad),
        (int16_t)(std::abs(x1 - x0) + 1 + pad * 2), (int16_t)(std::abs(y1 - y0) + 1 + pad * 2)
    });
    return 0;
}

int LuaEngine::l_draw_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);

    OSData::frame->drawRect(x, y, w, h, color);
    LuaMarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_fill_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);

    if (!Fill4bpp::FillRect(x, y, w, h, color)) OSData::frame->fillRect(x, y, w, h, color);
    LuaMarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_draw_circle(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t r = (int16_t)luaL_checkinteger(L, 3);
    const int8_t color = (int8_t)luaL_checkinteger(L, 4);

    OSData::frame->drawCircle(x, y, r, color);
    LuaMarkDirty({(int16_t)(x - r), (int16_t)(y - r), (int16_t)(r * 2 + 1), (int16_t)(r * 2 + 1)});
    return 0;
}

int LuaEngine::l_fill_circle(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t r = (int16_t)luaL_checkinteger(L, 3);
    const int8_t color = (int8_t)luaL_checkinteger(L, 4);

    if (!Fill4bpp::FillCircle(x, y, r, color)) OSData::frame->fillCircle(x, y, r, color);
    LuaMarkDirty({(int16_t)(x - r), (int16_t)(y - r), (int16_t)(r * 2 + 1), (int16_t)(r * 2 + 1)});
    return 0;
}

int LuaEngine::l_clear_rect(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const int16_t w = (int16_t)luaL_checkinteger(L, 3);
    const int16_t h = (int16_t)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_optinteger(L, 5, PICO_BACKGROUND);

    if (!Fill4bpp::FillRect(x, y, w, h, color)) OSData::frame->fillRect(x, y, w, h, color);
    LuaMarkDirty({x, y, w, h});
    return 0;
}

int LuaEngine::l_draw_text(lua_State* L) {
    const int16_t x = (int16_t)luaL_checkinteger(L, 1);
    const int16_t y = (int16_t)luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    const int8_t color = (int8_t)luaL_optinteger(L, 4, PICO_FORECOLOR);
    const FontFn::FontSize size = (FontFn::FontSize)luaL_optinteger(L, 5, (lua_Integer)FontFn::Normal);
    // 6番目: 揃え。"left"(既定)はxが左端、"center"はxが中心、"right"はxが右端
    const char* align = luaL_optstring(L, 6, "left");

    int16_t draw_x = x;
    if (strcmp(align, "left") != 0) {
        const int tw = Label<PICO_STR_M>::GetTextWidth(size, text);
        if (strcmp(align, "center") == 0) draw_x = (int16_t)(x - tw / 2);
        else if (strcmp(align, "right") == 0) draw_x = (int16_t)(x - tw);
        else return luaL_error(L, "pico.draw_text: align は left / center / right です");
    }

    // 右端をはみ出さないよう、幅は残りスクリーン幅に自動で収める(AppGrid::drawName()等と
    // 同じ理由でmaxWidth=0以下はDrawPlain側がクリップ無しとして扱ってしまうため先に弾く)
    const int16_t max_w = (int16_t)(SCREEN_WIDTH - draw_x);
    if (max_w <= 0) return 0;

    Label<PICO_STR_M>::DrawPlain(size, color, draw_x, y, max_w, text);

    const int16_t line_h = (int16_t)Label<PICO_STR_M>::GetLineHeight(size);
    LuaMarkDirty({draw_x, y, max_w, line_h});
    return 0;
}

int LuaEngine::l_draw_image(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const int16_t x = (int16_t)luaL_checkinteger(L, 2);
    const int16_t y = (int16_t)luaL_checkinteger(L, 3);

    size_t index;
    if (!self->images().resolve(handle, index)) {
        return luaL_error(L, "pico.draw_image: 無効なイメージハンドル");
    }

    LuaImages::Slot& slot = self->images()[index];
    IconRender::DrawPimgSprite(slot.sprite, x, y);
    LuaMarkDirty({x, y, (int16_t)slot.sprite.width, (int16_t)slot.sprite.height});
    if (LuaImages::FullColor* fc = self->images().fullColorOf(index)) {
        ArmFullColor(self, *fc, slot, x, y);
    }
    return 0;
}

// ---- フルカラーの直描き(pico.image_load の full_color) ----
// 16色の絵を frame へ描いた上で、FlushDirty() の最後に SD から解きながら液晶へそのままの色で描かせる
// (ImageView と同じ PICO_GFX::ArmDirectPaint。docs/design/gui.md「画像の形式とフルカラーの直描き」)。
// 頼むのは Canvas の render の中で、今描き直している所(dirty 矩形)に画像がかかったときだけ。
// 1秒ごとに再生位置の欄だけ描き直すような使い方で、かからない画像を毎回SDから解き直さないように。
// 頼むときは画像の見えている所全体(dirty 矩形の外も)を描かせる。

void LuaEngine::PaintFullColor(void* ctx, const Rect& clip) {
    const LuaImages::FullColor* fc = static_cast<const LuaImages::FullColor*>(ctx);
    if (!fc->used) return;
    const int w = ImageCodec::ScaledW(fc->info, fc->opt.scale);
    const int h = ImageCodec::ScaledH(fc->info, fc->opt.scale);
    if (!ImageView::PaintToLcd(fc->path.c_str(), fc->info, fc->opt, fc->x, fc->y, w, h, clip)) {
        LOG_APP_WARN("pico.draw_image: %s をフルカラーで描けませんでした", fc->path.c_str());
    }
}

void LuaEngine::ArmFullColor(LuaEngine* self, LuaImages::FullColor& fc, const LuaImages::Slot& slot, int16_t x, int16_t y) {
    LuaCanvas* canvas = LuaCanvas::rendering;
    // Canvas の render の外(loop から素で描いた等)・画像へ描いている間は16色のまま
    if (!canvas || LuaOffscreen::active || !PICO_GFX::render_clip_active) return;
    const Rect img = { x, y, (int16_t)slot.sprite.width, (int16_t)slot.sprite.height };
    if (!img.intersects(PICO_GFX::render_clip)) return;
    const Rect area = img.intersection(canvas->clippedScreenRect());
    if (area.w <= 0 || area.h <= 0) return;
    // 上にダイアログ・ほかの部品が載っていれば16色の絵のまま(直描きは上に載ったものを塗りつぶすため)。
    // どけばその跡の描き直しでまたここへ来る
    if (WidgetFunctions::IsCoveredAbove(canvas, area)) return;

    // 液晶に直描きできるのは画面に1枚だけ。ほかの持ち主が描いている(描こうとしている)間は奪わない
    // (奪うと、奪われた側の跡が frame の絵へ戻されて描き直され、また奪い返す…を繰り返す)。
    // このアプリの別の画像が持ち主でも、それを描いた Canvas が消えていれば譲ってもらう
    const PICO_GFX::DirectPaint& d = PICO_GFX::directPaint;
    if (d.fn && d.ctx != &fc && (d.pending || d.painted.w > 0)) {
        bool owner_gone = false;
        if (self->images().isFullColorOwner(d.ctx)) {
            Widget* w = WidgetRegistry::Resolve(static_cast<const LuaImages::FullColor*>(d.ctx)->canvas);
            owner_gone = !d.pending && (!w || !w->getVisible());
        }
        if (!owner_gone) return;
    }

    fc.x = x;
    fc.y = y;
    fc.canvas = canvas->getId();
    PICO_GFX::ArmDirectPaint(&LuaEngine::PaintFullColor, &fc, area);
}

// 画像の一部(sx,sy,w,h)だけを(x,y)へ描く。スプライトシート(同じ大きさのタイルを並べた
// 1枚の画像)から1枚ずつ取り出して描くためのもの。画像は4枚までしか持てないので、
// 部品の多い絵(テトリスのミノ等)は1枚にまとめて読み、これで切り出す。
// 実装は「今のクリップ(FlushDirty()のdirty矩形)と描き先の矩形の重なり」へクリップを
// 一時的に狭めてから画像全体をずらしてpushSprite()するだけ(描かれるのは重なりの中だけ)。
// クリップは元へ戻すので、renderコールバックの中で何回呼んでもdirty矩形の外へははみ出さない。
// 8・9番目の引数(flip_x / flip_y)が真なら左右/上下を反転して描く(1画素ずつ。pico.gameのスプライト用)
int LuaEngine::l_draw_image_part(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const int32_t x = (int32_t)luaL_checkinteger(L, 2);
    const int32_t y = (int32_t)luaL_checkinteger(L, 3);
    int32_t sx = (int32_t)luaL_checkinteger(L, 4);
    int32_t sy = (int32_t)luaL_checkinteger(L, 5);
    int32_t w = (int32_t)luaL_checkinteger(L, 6);
    int32_t h = (int32_t)luaL_checkinteger(L, 7);

    size_t index;
    if (!self->images().resolve(handle, index)) {
        return luaL_error(L, "pico.draw_image_part: 無効なイメージハンドル");
    }
    LuaImages::Slot& slot = self->images()[index];
    const bool flip_x = lua_toboolean(L, 8) != 0;
    const bool flip_y = lua_toboolean(L, 9) != 0;

    if (w <= 0 || h <= 0) return 0;
    // 速い道: 4bppのバッファどうしで直接写す(反転も含む。IconRender::Blit4bpp)
    if (IconRender::Blit4bpp(slot.sprite.sprite, sx, sy, w, h, x, y, slot.sprite.transparent, flip_x, flip_y)) {
        LuaMarkDirty({ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h });
        return 0;
    }

    if (flip_x || flip_y) {
        // 反転はpushSprite()ではできないので、描き先の1画素ごとに元の画素を引く
        // (ゲームのキャラクター1体ぶん程度の大きさを想定。draw_image_exと同じ書き方)
        int32_t kx = 0, ky = 0, kw = 0, kh = 0;
        OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
        const int32_t x0 = std::max({x, kx, (int32_t)0});
        const int32_t y0 = std::max({y, ky, (int32_t)0});
        const int32_t x1 = std::min({x + w, kx + kw, (int32_t)SCREEN_WIDTH});
        const int32_t y1 = std::min({y + h, ky + kh, (int32_t)SCREEN_HEIGHT});
        const int32_t iw = slot.sprite.width, ih = slot.sprite.height;
        for (int32_t py = y0; py < y1; py++) {
            const int32_t v = py - y;
            const int32_t iy = sy + (flip_y ? (h - 1 - v) : v);
            if (iy < 0 || iy >= ih) continue;
            for (int32_t px = x0; px < x1; px++) {
                const int32_t u = px - x;
                const int32_t ix = sx + (flip_x ? (w - 1 - u) : u);
                if (ix < 0 || ix >= iw) continue;
                const uint32_t col = slot.sprite.sprite.readPixelValue(ix, iy);
                if (slot.sprite.transparent && col == 0) continue; // index0は透過(DrawPimgSprite()と同じ)
                OSData::frame->writePixel(px, py, (int)col);
            }
        }
        LuaMarkDirty({ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h });
        return 0;
    }

    // 画像の外を指す分は削る(負のsx/syは描き先を右/下へずらして吸収する)
    int32_t dx = x, dy = y;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > slot.sprite.width) w = slot.sprite.width - sx;
    if (sy + h > slot.sprite.height) h = slot.sprite.height - sy;
    if (w <= 0 || h <= 0) return 0;

    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
    const Rect clip = Rect{ (int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch }
        .intersection({ (int16_t)dx, (int16_t)dy, (int16_t)w, (int16_t)h });
    if (clip.w > 0 && clip.h > 0) {
        OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);
        IconRender::DrawPimgSprite(slot.sprite, dx - sx, dy - sy);
        OSData::frame->setClipRect(cx, cy, cw, ch);
    }
    LuaMarkDirty({ (int16_t)dx, (int16_t)dy, (int16_t)w, (int16_t)h });
    return 0;
}

// ---------------- 拡張API(Love2Dとの比較で足したもの) ----------------
// 図形(楕円・三角形・多角形・円弧)、画像の拡大縮小/回転/反転、文字幅、WAVの一時停止/シーク、
// バッテリー・経過時間、ファイルの情報/部分読み、物理キーボード。
// 描画は他のpico.draw_*と同じく`Canvas`のrenderコールバック内で使う。色は4bitパレット番号。

namespace {
    constexpr int kMaxPolyPoints = 32;

    // 描いた範囲(両端を含む座標)をdirtyにする
    void MarkBounds(int minx, int miny, int maxx, int maxy) {
        LuaMarkDirty({(int16_t)minx, (int16_t)miny,
                             (int16_t)(maxx - minx + 1), (int16_t)(maxy - miny + 1)});
    }

    // {x1,y1,x2,y2,...}の平らな配列から点列を読む。点が3つ未満/上限超過/数値以外はエラー
    int ReadPoints(lua_State* L, int idx, int* xs, int* ys, const char* api) {
        luaL_checktype(L, idx, LUA_TTABLE);
        const lua_Integer len = (lua_Integer)lua_rawlen(L, idx);
        if (len % 2 != 0 || len < 6) luaL_error(L, "%s: 点は{x1,y1,x2,y2,x3,y3,...}の形で3つ以上必要です", api);
        if (len / 2 > kMaxPolyPoints) luaL_error(L, "%s: 点は%d個までです", api, kMaxPolyPoints);
        for (lua_Integer i = 0; i < len; i++) {
            lua_rawgeti(L, idx, i + 1);
            if (!lua_isnumber(L, -1)) luaL_error(L, "%s: 点の座標は数値です", api);
            const int v = (int)lua_tonumber(L, -1);
            lua_pop(L, 1);
            if (i % 2 == 0) xs[i / 2] = v; else ys[i / 2] = v;
        }
        return (int)(len / 2);
    }

    void Bounds(const int* xs, const int* ys, int n, int& minx, int& miny, int& maxx, int& maxy) {
        minx = maxx = xs[0];
        miny = maxy = ys[0];
        for (int i = 1; i < n; i++) {
            minx = std::min(minx, xs[i]); maxx = std::max(maxx, xs[i]);
            miny = std::min(miny, ys[i]); maxy = std::max(maxy, ys[i]);
        }
    }

    void DrawPolyOutline(const int* xs, const int* ys, int n, int8_t color, int width) {
        for (int i = 0; i < n; i++) {
            const int j = (i + 1) % n;
            if (width > 1) CanvasRaster::DrawThickLine(OSData::frame, xs[i], ys[i], xs[j], ys[j], width * 0.5f, color);
            else OSData::frame->drawLine(xs[i], ys[i], xs[j], ys[j], color);
        }
    }

    // 偶奇規則のスキャンライン塗り(凹多角形も塗れる)。ピクセルの中心(x+0.5,y+0.5)が内側なら塗る
    void FillPoly(const int* xs, const int* ys, int n, int8_t color) {
        int minx, miny, maxx, maxy;
        Bounds(xs, ys, n, minx, miny, maxx, maxy);
        const int y_from = std::max(miny, 0);
        const int y_to = std::min(maxy, (int)SCREEN_HEIGHT - 1);
        float cross[kMaxPolyPoints];
        const Fill4bpp::Span span(color);  // 横線はバッファへ直接書く(4bppでなければLovyanGFXで)
        for (int y = y_from; y <= y_to; y++) {
            const float cy = y + 0.5f;
            int cnt = 0;
            for (int i = 0; i < n; i++) {
                const int j = (i + 1) % n;
                const float ya = (float)ys[i], yb = (float)ys[j];
                if ((ya <= cy && cy < yb) || (yb <= cy && cy < ya)) {
                    const float t = (cy - ya) / (yb - ya);
                    cross[cnt++] = xs[i] + t * (xs[j] - xs[i]);
                }
            }
            for (int a = 1; a < cnt; a++) { // 挿入ソート(高々32個)
                const float v = cross[a];
                int b = a - 1;
                while (b >= 0 && cross[b] > v) { cross[b + 1] = cross[b]; b--; }
                cross[b + 1] = v;
            }
            for (int a = 0; a + 1 < cnt; a += 2) {
                const int xl = (int)std::ceil(cross[a] - 0.5f);
                const int xr = (int)std::ceil(cross[a + 1] - 0.5f) - 1;
                if (xr < xl) continue;
                if (span.ok) span.hline(xl, y, xr - xl + 1);
                else OSData::frame->drawFastHLine(xl, y, xr - xl + 1, color);
            }
        }
    }

    // 円弧の点列(中心x,y・半径r・角度a0〜a1ラジアン。0=右、増えると時計回り(y下向き))
    int ArcPoints(float cx, float cy, float r, float a0, float a1, int max_points, int* xs, int* ys) {
        int n = (int)(std::fabs(a1 - a0) * r / 3.0f) + 1;
        n = std::clamp(n, 2, max_points - 1);
        for (int i = 0; i <= n; i++) {
            const float a = a0 + (a1 - a0) * i / n;
            xs[i] = (int)std::lround(cx + r * std::cos(a));
            ys[i] = (int)std::lround(cy + r * std::sin(a));
        }
        return n + 1;
    }
}

int LuaEngine::l_draw_ellipse(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int rx = (int)luaL_checkinteger(L, 3), ry = (int)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);
    if (rx < 0 || ry < 0) return luaL_error(L, "pico.draw_ellipse: 半径は0以上です");
    OSData::frame->drawEllipse(x, y, rx, ry, color);
    MarkBounds(x - rx, y - ry, x + rx, y + ry);
    return 0;
}

int LuaEngine::l_fill_ellipse(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int rx = (int)luaL_checkinteger(L, 3), ry = (int)luaL_checkinteger(L, 4);
    const int8_t color = (int8_t)luaL_checkinteger(L, 5);
    if (rx < 0 || ry < 0) return luaL_error(L, "pico.fill_ellipse: 半径は0以上です");
    if (!Fill4bpp::FillEllipse(x, y, rx, ry, color)) OSData::frame->fillEllipse(x, y, rx, ry, color);
    MarkBounds(x - rx, y - ry, x + rx, y + ry);
    return 0;
}

int LuaEngine::l_draw_triangle(lua_State* L) {
    int xs[3], ys[3];
    for (int i = 0; i < 3; i++) {
        xs[i] = (int)luaL_checkinteger(L, 1 + i * 2);
        ys[i] = (int)luaL_checkinteger(L, 2 + i * 2);
    }
    const int8_t color = (int8_t)luaL_checkinteger(L, 7);
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 8, 1), 1, 64);
    DrawPolyOutline(xs, ys, 3, color, width);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, 3, minx, miny, maxx, maxy);
    const int pad = width > 1 ? width / 2 + 1 : 0;
    MarkBounds(minx - pad, miny - pad, maxx + pad, maxy + pad);
    return 0;
}

int LuaEngine::l_fill_triangle(lua_State* L) {
    int xs[3], ys[3];
    for (int i = 0; i < 3; i++) {
        xs[i] = (int)luaL_checkinteger(L, 1 + i * 2);
        ys[i] = (int)luaL_checkinteger(L, 2 + i * 2);
    }
    const int8_t color = (int8_t)luaL_checkinteger(L, 7);
    if (!Fill4bpp::FillTriangle(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], color))
        OSData::frame->fillTriangle(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], color);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, 3, minx, miny, maxx, maxy);
    MarkBounds(minx, miny, maxx, maxy);
    return 0;
}

int LuaEngine::l_draw_polygon(lua_State* L) {
    int xs[kMaxPolyPoints], ys[kMaxPolyPoints];
    const int n = ReadPoints(L, 1, xs, ys, "pico.draw_polygon");
    const int8_t color = (int8_t)luaL_checkinteger(L, 2);
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 3, 1), 1, 64);
    DrawPolyOutline(xs, ys, n, color, width);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, n, minx, miny, maxx, maxy);
    const int pad = width > 1 ? width / 2 + 1 : 0;
    MarkBounds(minx - pad, miny - pad, maxx + pad, maxy + pad);
    return 0;
}

int LuaEngine::l_fill_polygon(lua_State* L) {
    int xs[kMaxPolyPoints], ys[kMaxPolyPoints];
    const int n = ReadPoints(L, 1, xs, ys, "pico.fill_polygon");
    const int8_t color = (int8_t)luaL_checkinteger(L, 2);
    FillPoly(xs, ys, n, color);
    int minx, miny, maxx, maxy;
    Bounds(xs, ys, n, minx, miny, maxx, maxy);
    MarkBounds(minx, miny, maxx, maxy);
    return 0;
}

int LuaEngine::l_draw_arc(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int r = (int)luaL_checkinteger(L, 3);
    const float a0 = (float)luaL_checknumber(L, 4), a1 = (float)luaL_checknumber(L, 5);
    const int8_t color = (int8_t)luaL_checkinteger(L, 6);
    const int width = (int)std::clamp<lua_Integer>(luaL_optinteger(L, 7, 1), 1, 64);
    if (r < 0) return luaL_error(L, "pico.draw_arc: 半径は0以上です");
    int xs[kMaxPolyPoints * 2], ys[kMaxPolyPoints * 2];
    const int n = ArcPoints((float)x, (float)y, (float)r, a0, a1, kMaxPolyPoints * 2, xs, ys);
    for (int i = 0; i + 1 < n; i++) {
        if (width > 1) CanvasRaster::DrawThickLine(OSData::frame, xs[i], ys[i], xs[i + 1], ys[i + 1], width * 0.5f, color);
        else OSData::frame->drawLine(xs[i], ys[i], xs[i + 1], ys[i + 1], color);
    }
    const int pad = r + (width > 1 ? width / 2 + 1 : 0); // 円全体を覆う(範囲の計算を簡単にするため)
    MarkBounds(x - pad, y - pad, x + pad, y + pad);
    return 0;
}

// 扇形(パイ)。中心と円弧の点で作る多角形を塗る
int LuaEngine::l_fill_arc(lua_State* L) {
    const int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    const int r = (int)luaL_checkinteger(L, 3);
    const float a0 = (float)luaL_checknumber(L, 4), a1 = (float)luaL_checknumber(L, 5);
    const int8_t color = (int8_t)luaL_checkinteger(L, 6);
    if (r < 0) return luaL_error(L, "pico.fill_arc: 半径は0以上です");
    int xs[kMaxPolyPoints], ys[kMaxPolyPoints];
    xs[0] = x; ys[0] = y;
    const int n = ArcPoints((float)x, (float)y, (float)r, a0, a1, kMaxPolyPoints - 1, xs + 1, ys + 1);
    FillPoly(xs, ys, n + 1, color);
    MarkBounds(x - r, y - r, x + r, y + r);
    return 0;
}

int LuaEngine::l_text_width(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    const FontFn::FontSize size = (FontFn::FontSize)luaL_optinteger(L, 2, (lua_Integer)FontFn::Normal);
    lua_pushinteger(L, Label<PICO_STR_M>::GetTextWidth(size, text));
    return 1;
}

// pico.draw_image_ex(handle, x, y [, r [, sx [, sy [, ox [, oy]]]]])
// Love2Dのlove.graphics.draw(image, x, y, r, sx, sy, ox, oy)と同じ並び:
// 画像の(ox,oy)を(x,y)に置き、rラジアン(時計回り)回して(sx,sy)倍に拡大縮小する。
// sxが負なら左右反転、syが負なら上下反転(syを省くとsxと同じ)。最近傍で、補間はしない。
// 描き先ごとに元の画素を引く逆変換なので隙間ができない。描くのは今のクリップの内側だけ
int LuaEngine::l_draw_image_ex(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
    const double x = luaL_checknumber(L, 2);
    const double y = luaL_checknumber(L, 3);
    const double r = luaL_optnumber(L, 4, 0.0);
    const double sx = luaL_optnumber(L, 5, 1.0);
    const double sy = luaL_optnumber(L, 6, sx);
    const double ox = luaL_optnumber(L, 7, 0.0);
    const double oy = luaL_optnumber(L, 8, 0.0);

    size_t index;
    if (!self->images().resolve(handle, index)) {
        return luaL_error(L, "pico.draw_image_ex: 無効なイメージハンドル");
    }
    if (std::fabs(sx) < 1e-6 || std::fabs(sy) < 1e-6) return 0;
    if (std::fabs(sx) > 64 || std::fabs(sy) > 64) return luaL_error(L, "pico.draw_image_ex: 倍率は64倍までです");

    LuaImages::Slot& slot = self->images()[index];
    const int iw = slot.sprite.width, ih = slot.sprite.height;
    const double c = std::cos(r), s = std::sin(r);

    // 元画像の4隅を描き先へ写して外接矩形を出す
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    const double cx[4] = {0, (double)iw, 0, (double)iw};
    const double cy[4] = {0, 0, (double)ih, (double)ih};
    for (int i = 0; i < 4; i++) {
        const double ux = (cx[i] - ox) * sx, uy = (cy[i] - oy) * sy;
        const double dx = x + c * ux - s * uy;
        const double dy = y + s * ux + c * uy;
        minx = std::min(minx, dx); maxx = std::max(maxx, dx);
        miny = std::min(miny, dy); maxy = std::max(maxy, dy);
    }
    int32_t kx = 0, ky = 0, kw = 0, kh = 0;
    OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
    const Rect area = Rect{(int16_t)kx, (int16_t)ky, (int16_t)kw, (int16_t)kh}
        .intersection({0, 0, (int16_t)SCREEN_WIDTH, (int16_t)SCREEN_HEIGHT});
    const int x0 = std::max((int)std::floor(minx), (int)area.x);
    const int y0 = std::max((int)std::floor(miny), (int)area.y);
    const int x1 = std::min((int)std::ceil(maxx), (int)area.x + area.w);
    const int y1 = std::min((int)std::ceil(maxy), (int)area.y + area.h);

    // 実機では1画素ごとのdoubleの掛け算・割り算・floorが支配的だったので、
    // 逆変換を16.16の固定小数点に直し、1行の中は加算だけで進める(行の頭だけdoubleで出す)。
    // 画素の読み書きも「同じ元画素が続くあいだは読み直さない」「同じ色が続けば横線1本にまとめる」
    constexpr double kFix = 65536.0;
    const double dux = c / sx, dvx = -s / sy;   // 描き先を右へ1画素進めたときの元画像上の動き
    const int64_t stepU = (int64_t)std::llround(dux * kFix);
    const int64_t stepV = (int64_t)std::llround(dvx * kFix);
    const int64_t fw = (int64_t)iw << 16, fh = (int64_t)ih << 16;
    const bool transparent = slot.sprite.transparent;
    LGFX_Sprite& src = slot.sprite.sprite;
    LGFX_Sprite& dst = *OSData::frame;

    // 速い道: 元も描き先も4bpp(回転無し)なら、バッファを直接読み書きする。
    // readPixelValue()/drawFastHLine()は1回ごとに回転・クリップ・色変換を通るので、
    // 回した画像のように色の続かない画素ではそれが支配的だった。
    // さらに各行で「元画像の中に入る区間」を割り算で先に求め、外接矩形の空き(回すと半分近く)を回らない。
    // ホストテストのスタブ(1画素1バイト)は下の遅い道を通る
    uint8_t* sbuf = static_cast<uint8_t*>(src.getBuffer());
    uint8_t* dbuf = static_cast<uint8_t*>(dst.getBuffer());
    if (sbuf && dbuf && (((int)src.getColorDepth() & 0xFF) == 4) && (((int)dst.getColorDepth() & 0xFF) == 4)
        && src.getRotation() == 0 && dst.getRotation() == 0 && iw < 16384 && ih < 16384) {
        const int sstride = ((iw + 1) & ~1) >> 1;
        const int dw = dst.width(), dh = dst.height();
        const int dstride = ((dw + 1) & ~1) >> 1;
        const int fx0 = std::max(x0, 0), fx1 = std::min(x1, dw);
        const int fy0 = std::max(y0, 0), fy1 = std::min(y1, dh);
        // u0 + k*step が [0, lim) に入るkの範囲を[lo, hi)へ狭める
        auto narrow = [](int64_t u0, int64_t step, int64_t lim, int& lo, int& hi) {
            if (step == 0) { if (u0 < 0 || u0 >= lim) hi = lo; return; }
            auto fdiv = [](int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; };
            int64_t a, b; // a <= k <= b
            if (step > 0) { a = -fdiv(u0, step); b = fdiv(lim - 1 - u0, step); }   // ceil(-u0/step)
            else          { a = -fdiv(u0 - (lim - 1), step); b = fdiv(u0, -step); }
            if (a > lo) lo = (int)std::min<int64_t>(a, hi);
            if (b + 1 < hi) hi = (int)std::max<int64_t>(b + 1, lo);
        };
        for (int py = fy0; py < fy1; py++) {
            const double rx0 = fx0 + 0.5 - x, ry = py + 0.5 - y;
            const int64_t u0 = (int64_t)std::llround(((c * rx0 + s * ry) / sx + ox) * kFix);
            const int64_t v0 = (int64_t)std::llround(((-s * rx0 + c * ry) / sy + oy) * kFix);
            int lo = 0, hi = fx1 - fx0;
            narrow(u0, stepU, fw, lo, hi);
            narrow(v0, stepV, fh, lo, hi);
            if (lo >= hi) continue;
            // 区間の中はu,vとも[0, 16384<<16)に収まるので、1画素ごとの計算は32bitで足りる(M33は64bitの加算が2命令)
            int32_t u = (int32_t)(u0 + stepU * lo), v = (int32_t)(v0 + stepV * lo);
            const int32_t su = (int32_t)stepU, sv = (int32_t)stepV;
            uint8_t* drow = dbuf + (size_t)py * dstride;
            for (int px = fx0 + lo, pe = fx0 + hi; px < pe; px++, u += su, v += sv) {
                const int ix = (int)(u >> 16), iy = (int)(v >> 16);
                const uint8_t sb = sbuf[(size_t)iy * sstride + (ix >> 1)];
                const uint8_t col = (ix & 1) ? (sb & 0x0F) : (sb >> 4);
                if (transparent && col == 0) continue;  // index0は透過(DrawPimgSprite()と同じ)
                uint8_t& d = drow[px >> 1];
                d = (px & 1) ? (uint8_t)((d & 0xF0) | col) : (uint8_t)((d & 0x0F) | (col << 4));
            }
        }
        if (x1 > x0 && y1 > y0) MarkBounds((int)std::floor(minx), (int)std::floor(miny), (int)std::ceil(maxx), (int)std::ceil(maxy));
        return 0;
    }

    for (int py = y0; py < y1; py++) {
        const double rx0 = x0 + 0.5 - x, ry = py + 0.5 - y;
        int64_t u = (int64_t)std::llround(((c * rx0 + s * ry) / sx + ox) * kFix);
        int64_t v = (int64_t)std::llround(((-s * rx0 + c * ry) / sy + oy) * kFix);

        int run_x = 0, run_len = 0, run_col = -1;
        int last_ix = -1, last_iy = -1;
        uint32_t last_col = 0;
        for (int px = x0; px < x1; px++, u += stepU, v += stepV) {
            int col = -1;  // -1は描かない
            if (u >= 0 && v >= 0 && u < fw && v < fh) {
                const int ix = (int)(u >> 16), iy = (int)(v >> 16);
                if (ix != last_ix || iy != last_iy) {
                    last_col = src.readPixelValue(ix, iy);
                    last_ix = ix; last_iy = iy;
                }
                if (!(transparent && last_col == 0)) col = (int)last_col; // index0は透過(DrawPimgSprite()と同じ)
            }
            if (run_len > 0 && col == run_col) { run_len++; continue; }
            if (run_len > 0) OSData::frame->drawFastHLine(run_x, py, run_len, run_col);
            if (col >= 0) { run_x = px; run_len = 1; run_col = col; }
            else run_len = 0;
        }
        if (run_len > 0) OSData::frame->drawFastHLine(run_x, py, run_len, run_col);
    }
    if (x1 > x0 && y1 > y0) MarkBounds((int)std::floor(minx), (int)std::floor(miny), (int)std::ceil(maxx), (int)std::ceil(maxy));
    return 0;
}

// ---------------- 直接描画エリア ----------------
// クラスコメント(ヘッダ)参照。OSData::frameのクリップ矩形を差し替えるだけの薄いラッパー。

int LuaEngine::l_set_draw_area(lua_State* L) {
    const int32_t x = (int32_t)luaL_checkinteger(L, 1);
    const int32_t y = (int32_t)luaL_checkinteger(L, 2);
    const int32_t w = (int32_t)luaL_checkinteger(L, 3);
    const int32_t h = (int32_t)luaL_checkinteger(L, 4);

    // renderの中(FlushDirty()の合成中)は、dirty矩形とこのCanvasの重なり(PICO_GFX::render_clip)の
    // 外へは広げない。広げると、送られない所まで描いてframeと液晶の中身が食い違う
    if (PICO_GFX::render_clip_active) {
        const Rect r = Rect{(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h}.intersection(PICO_GFX::render_clip);
        OSData::frame->setClipRect(r.x, r.y, r.w > 0 ? r.w : 0, r.h > 0 ? r.h : 0);
    } else {
        OSData::frame->setClipRect(x, y, w, h);
    }
    return 0;
}

int LuaEngine::l_clear_draw_area(lua_State*) {
    if (PICO_GFX::render_clip_active) {
        const Rect& r = PICO_GFX::render_clip;
        OSData::frame->setClipRect(r.x, r.y, r.w, r.h);
    } else {
        OSData::frame->clearClipRect();
    }
    return 0;
}

// 今のクリップ矩形を返す。Canvasのrenderコールバックの中では、FlushDirty()が
// 「そのCanvasとdirty矩形の重なり」を設定しているので、そこだけ描けば足りる
// (盤面のように部品の多い絵で、変わったところだけを描き直すためのもの)
int LuaEngine::l_get_draw_area(lua_State* L) {
    int32_t x = 0, y = 0, w = 0, h = 0;
    OSData::frame->getClipRect(&x, &y, &w, &h);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
}

// ---------------- 画像 ----------------
// ヘッダのクラスコメント「画像」参照。デコードそのものは ImageCodec::LoadToSprite()
// (.pimg / BMP / JPEG / PNG。ImageView・Image と同じ経路)をそのまま使い、
// このLuaEngineインスタンスの固定長スロットで持つだけ。

int LuaEngine::l_image_load(lua_State* L) {
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);

    if (!OSData::SD_usable) { lua_pushnil(L); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.image_load: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        return 1;
    }

    const int free_slot = self->images().findFree();
    if (free_slot < 0) {
        LOG_APP_WARN("pico.image_load: 同時に保持できる画像数の上限(%zu枚)に達しています: %s",
            LuaImages::kMax, path);
        lua_pushnil(L);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushnil(L); return 1; }

    // 形式はファイルの先頭で見分ける(.pimg / BMP / JPEG / PNG)。BMP・JPEG・PNG はOSのパレットへ寄せて4bppにする。
    // 2番目の引数 {dither=, raw=(BMPだけ), scale=/max_w=/max_h=(JPEG・PNGだけ), transparent=(PNGだけ)}
    ImageCodec::Info info;
    if (!ImageCodec::ReadInfo(f, info)) {
        f.close();
        LOG_APP_WARN("pico.image_load: %s は画像として読めません(JPEGのプログレッシブは非対応)", path);
        lua_pushnil(L);
        return 1;
    }
    ImageCodec::Options opt;
    bool full_color = false;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "dither");
        if (!lua_isnil(L, -1)) opt.dither = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "raw");
        opt.raw_indices = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "transparent");
        if (!lua_isnil(L, -1)) opt.transparent = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);
        lua_getfield(L, 2, "full_color");
        full_color = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "background");
        if (!lua_isnil(L, -1)) opt.background = (uint8_t)(luaL_checkinteger(L, -1) & 0x0F);
        lua_pop(L, 1);
    }
    if (ImageCodec::CanScale(info)) {
        // 縮め方: scale(1/2/4/8)を指定すればそれ。無ければ max_w/max_h(既定は画面の大きさ)と
        // 画像用メモリの残りに収まる一番大きいもの
        int scale_div = 0, max_w = SCREEN_WIDTH, max_h = SCREEN_HEIGHT;
        if (lua_istable(L, 2)) {
            lua_getfield(L, 2, "scale");
            if (!lua_isnil(L, -1)) scale_div = (int)luaL_checkinteger(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, 2, "max_w");
            if (!lua_isnil(L, -1)) max_w = (int)luaL_checkinteger(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, 2, "max_h");
            if (!lua_isnil(L, -1)) max_h = (int)luaL_checkinteger(L, -1);
            lua_pop(L, 1);
        }
        if (scale_div) {
            if (scale_div != 1 && scale_div != 2 && scale_div != 4 && scale_div != 8) {
                f.close();
                return luaL_error(L, "pico.image_load: scale は 1 / 2 / 4 / 8 のどれかです");
            }
            opt.scale = (uint8_t)(scale_div == 1 ? 0 : scale_div == 2 ? 1 : scale_div == 4 ? 2 : 3);
        } else {
            opt.scale = ImageCodec::FitScale(info, max_w, max_h, self->images().room());
        }
    }

    // 4bpp(1ピクセル半バイト)なので端数切り上げでバイト数を見積もる。
    // LGFX_Sprite側の実際の確保量は多少前後し得るが、予算チェックとしては十分な精度
    const size_t need_bytes = ImageCodec::Bytes4bpp(ImageCodec::ScaledW(info, opt.scale), ImageCodec::ScaledH(info, opt.scale));
    if (!self->images().fits(need_bytes)) {
        f.close();
        LOG_APP_WARN("pico.image_load: %s の読み込みで画像用メモリの上限(%uB)を超えます",
            path, (unsigned)LuaImages::kMaxBytes);
        lua_pushnil(L);
        return 1;
    }

    const size_t index = (size_t)free_slot;
    LuaImages::Slot& slot = self->images()[index];
    const bool ok = ImageCodec::LoadToSprite(f, info, slot.sprite, opt);
    f.close();
    if (!ok) { lua_pushnil(L); return 1; }

    const uint32_t handle = self->images().commit(index, need_bytes);
    // full_color: 16色に収まらない絵(JPEG・PNG・8bpp以上のBMP)は、pico.draw_image で液晶へそのままの色でも描く。
    // .pimg・4bpp以下のBMPは16色の絵そのものなので覚えない(エラーにもしない)
    if (full_color && ImageCodec::IsFullColor(info) && !self->images().addFullColor(index, path, info, opt)) {
        LOG_APP_WARN("pico.image_load: フルカラーで描ける画像は%zu枚までです(16色で描きます): %s",
            LuaImages::kMaxFullColor, path);
    }
    lua_pushinteger(L, (lua_Integer)handle);
    return 1;
}

int LuaEngine::l_image_size(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);

    size_t index;
    if (!self->images().resolve(handle, index)) {
        return luaL_error(L, "pico.image_size: 無効なイメージハンドル");
    }

    const LuaImages::Slot& slot = self->images()[index];
    lua_pushinteger(L, slot.sprite.width);
    lua_pushinteger(L, slot.sprite.height);
    return 2;
}

int LuaEngine::l_image_free(lua_State* L) {
    LuaEngine* self = Self(L);
    const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);

    size_t index;
    // 既に無効なハンドル(未割り当て/解放済み)はpico.destroyと同じく黙って無視し、
    // 二重解放をエラーにしない
    if (!self->images().resolve(handle, index)) return 0;

    // 描き先にしている画像なら先に画面へ戻す
    if (self->isImageTarget(index)) self->EndImageTarget();
    // 解放の中でgenerationを進めるので、解放済みの古いハンドルが次の画像を指さない(LuaImages::release)
    self->images().release(index);
    return 0;
}

// ---------------- ラスタキャンバス(CanvasRaster) ----------------
// ヘッダのクラスコメント「ラスタキャンバスの保存/読み込み」参照。

namespace {
    // 3関数共通: idを解決し、CanvasRaster以外ならluaL_error。
    // 呼び出し側は戻り値nullptrをチェックする必要は無い(エラーはここで飛ぶ)
    CanvasRaster* ResolveCanvasRasterOrError(lua_State* L, int arg_index, const char* fn_name) {
        const WidgetId id = (WidgetId)luaL_checkinteger(L, arg_index);
        Widget* w = WidgetRegistry::Resolve(id);
        if (!w) {
            luaL_error(L, "%s: 無効なID", fn_name);
            return nullptr; // 到達しない(luaL_errorはlongjmpする)
        }
        if (w->getWidgetType() != WidgetType::CanvasRaster) {
            luaL_error(L, "%s: CanvasRaster以外には使えません", fn_name);
            return nullptr;
        }
        return static_cast<CanvasRaster*>(w);
    }

    // 実機の上限に関わらず「画面に収まらないサイズを.pimgから復元して確保する」
    // 事故を防ぐための上限(SCREEN_WIDTH/HEIGHT基準)。不正/悪意あるファイルが
    // 巨大なwidth/heightを名乗っていても、ここで弾けばcreateSprite()の
    // 大量確保まで進まない
    bool CanvasSizeSane(uint16_t w, uint16_t h) {
        return w > 0 && h > 0 && w <= SCREEN_WIDTH && h <= SCREEN_HEIGHT;
    }
}

int LuaEngine::l_canvas_clear(lua_State* L) {
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_clear");
    cr->canvasClear();
    return 0;
}

int LuaEngine::l_canvas_save(lua_State* L) {
    LuaEngine* self = Self(L);
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_save");
    const char* path = luaL_checkstring(L, 2);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdWriteAllowed(path, "pico.canvas_save")) {
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) { lua_pushboolean(L, false); return 1; }

    const bool ok = IconRender::EncodePimg(*cr->getSprite(), (uint16_t)cr->getW(), (uint16_t)cr->getH(), f);
    f.close();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_canvas_load(lua_State* L) {
    LuaEngine* self = Self(L);
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_load");
    const char* path = luaL_checkstring(L, 2);
    const bool keep_size = lua_toboolean(L, 3);

    if (!OSData::SD_usable) { lua_pushboolean(L, false); return 1; }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.canvas_load: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushboolean(L, false);
        return 1;
    }

    FsFile f = OSData::SD.open(path, O_RDONLY);
    if (!f) { lua_pushboolean(L, false); return 1; }

    IconRender::PimgHeader header;
    if (!IconRender::ReadPimgHeader(f, header) || !CanvasSizeSane(header.width, header.height)) {
        f.close();
        lua_pushboolean(L, false);
        return 1;
    }

    if (keep_size) {
        // 大きさは変えず、白紙にしてから左上に合わせて読む。DecodePimgBody()は
        // 画像の幅で行を折り返し、スプライトの外へ出た画素はwritePixel()のクリップで
        // 捨てられるので、大きい画像は右/下が切れ、小さい画像は余白が白で残る
        cr->saveUndoPoint();
        cr->getSprite()->clear(PICO_WHITE);
    } else {
        // 保存時と現在のw/hが食い違っていても読み込めるよう、先にキャンバス自体を
        // 画像のサイズへ合わせる(CanvasRaster::resize()。この時点で旧内容は消える)
        cr->resize((int16_t)header.width, (int16_t)header.height);
    }

    const bool ok = IconRender::DecodePimgBody(f, *cr->getSprite(), header.width, header.height);
    f.close();
    if (ok) cr->needsRender();
    lua_pushboolean(L, ok);
    return 1;
}

int LuaEngine::l_canvas_undo(lua_State* L) {
    CanvasRaster* cr = ResolveCanvasRasterOrError(L, 1, "pico.canvas_undo");
    lua_pushboolean(L, cr->undo());
    return 1;
}
