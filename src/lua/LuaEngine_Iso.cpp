// pico.iso: 2.5D(斜め上から見た)ボクセルの箱庭のエンジン(src/iso/Iso_World)を Lua へ出す。
// Luaアプリ「ブロック」の view.lua / world.lua を C++ へ移したもの。ワールド(チャンク)の持ち方・生成・保存、
// 描画(影・松明の光・昼と夜・半透明の水・隠れたブロックの省略)、タップ位置の引き当て、描き直す範囲をここで受け持ち、
// Lua 側は画面の流れと操作だけを書く。
//
// 面の絵(faces.pimg)は Lua が pico.image_load したものを pico.iso.set_image(handle) で渡す。
// 描くときは 4bpp のバッファどうしで写す(Iso::FaceBlitter)。画面か画像が 4bpp でなければ(ホストテストのスタブ)、
// pico.draw_image_part と同じ遅い道で描く。
// ファイル: create/open/migrate に渡すディレクトリは、pico.sd_write と同じ権限の確認を通す
// (world.dat とチャンクのファイルはそのディレクトリの直下にしか作らない)。

#include "lua/LuaEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include "iso/Iso_World.hpp"
#include "iso/Iso_Blit.hpp"
#include "iso/Iso_Path.hpp"
#include "iso/Iso_Flow.hpp"
#include "gui/Fill4bpp.hpp"
#include "gui/icons/icon_render.h"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

struct LuaEngine::IsoState {
    Iso::World world;
    Iso::Flow flow;                     // 流れの場(pico.iso.flow_*)
    Iso::FaceBlitter blit;
    uint32_t image = 0;                 // faces.pimg のハンドル
    const void* analyzed = nullptr;     // blit.setSource() を済ませた画像のバッファ
    int sky = 11;
};

void LuaEngine::DestroyIso() {
    delete iso_;
    iso_ = nullptr;
}

struct LuaEngineIso {
    static LuaEngine* Self(lua_State* L) {
        return static_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    }

    static LuaEngine::IsoState& St(lua_State* L) {
        LuaEngine* self = Self(L);
        if (!self->iso_) {
            self->iso_ = new (std::nothrow) LuaEngine::IsoState();
            if (!self->iso_) luaL_error(L, "pico.iso: メモリが足りません");
        }
        return *self->iso_;
    }

    static int Int(lua_State* L, int idx) { return (int)luaL_checkinteger(L, idx); }

    static void MarkDirty(int x, int y, int w, int h) {
        if (!LuaOffscreen::active) PICO_GFX::MarkDirty({(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h});
    }

    // <dir>/world.dat を書いてよいか(pico.sd_write と同じ確認)
    static bool DirAllowed(LuaEngine* self, const char* dir, const char* api) {
        FixedString<PICO_PATH_LEN> p(dir);
        p.append("/world.dat");
        return self->SdWriteAllowed(p.c_str(), api);
    }

    // ---------------- 描き先 ----------------

    // 画像のスロット(無効なら nullptr)
    static LuaEngine::ImageSlot* Image(LuaEngine* self, LuaEngine::IsoState& st) { return Slot(self, st.image); }
    static LuaEngine::ImageSlot* Slot(LuaEngine* self, uint32_t handle) {
        size_t index;
        if (!handle || !self->ResolveImageHandle(handle, index)) return nullptr;
        LuaEngine::ImageSlot& slot = self->images_[index];
        if (!slot.sprite.usable) return nullptr;
        return &slot;
    }

    static bool Is4bpp(LGFX_Sprite& s) {
        return s.getBuffer() && ((int)s.getColorDepth() & 0xFF) == 4 && s.getRotation() == 0;
    }

    // 画像が変わっていたら、面の絵の形と、透けないブロックを調べ直す
    static void Analyze(LuaEngine::IsoState& st, LuaEngine::ImageSlot& slot) {
        LGFX_Sprite& sp = slot.sprite.sprite;
        const void* buf = sp.getBuffer();
        if (buf && buf == st.analyzed) return;
        st.analyzed = buf;
        if (Is4bpp(sp)) {
            st.blit.setSource(static_cast<const uint8_t*>(buf), slot.sprite.width, slot.sprite.height);
            const int stride = (slot.sprite.width + 1) >> 1;
            struct Ctx { const uint8_t* b; int stride; };
            Ctx c{static_cast<const uint8_t*>(buf), stride};
            st.world.setOccluders(Iso::World::ComputeOccluders([](void* p, int x, int y) {
                const Ctx* c = static_cast<const Ctx*>(p);
                return (int)Iso::FaceBlitter::Get(c->b + (size_t)y * c->stride, x);
            }, &c));
        } else {
            st.blit.setSource(nullptr, 0, 0);
            st.world.setOccluders(Iso::World::ComputeOccluders([](void* p, int x, int y) {
                return (int)static_cast<LGFX_Sprite*>(p)->readPixelValue(x, y);
            }, &sp));
        }
    }

    // 描き先の文脈。速い道(4bpp どうし)は FaceBlitter、遅い道は pico.draw_image_part と同じやり方で描く
    struct Ctx {
        LuaEngine* self;
        LuaEngine::IsoState* st;
        LuaEngine::ImageSlot* faces;
        bool fast;
        int cx0, cy0, cx1, cy1;
        // 人や物の絵(直前に引いたもの)
        int32_t img = 0;
        LuaEngine::ImageSlot* islot = nullptr;
        const uint8_t* ibuf = nullptr;
        int istride = 0;
    };

    // 遅い道: クリップを狭めて画像全体をずらして描く
    static void Draw(void* p, int sx, int sy, int w, int h, int dx, int dy) {
        Ctx* c = static_cast<Ctx*>(p);
        if (c->fast) { c->st->blit.draw(sx, sy, w, h, dx, dy); return; }
        const int x0 = std::max(dx, c->cx0), y0 = std::max(dy, c->cy0);
        const int x1 = std::min(dx + w, c->cx1), y1 = std::min(dy + h, c->cy1);
        if (x0 >= x1 || y0 >= y1) return;
        int32_t kx, ky, kw, kh;
        OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
        OSData::frame->setClipRect(x0, y0, x1 - x0, y1 - y0);
        IconRender::DrawPimgSprite(c->faces->sprite, dx - sx, dy - sy);
        OSData::frame->setClipRect(kx, ky, kw, kh);
    }
    // 遅い道のディザ: 1画素ずつ読んで、模様の画素だけ書く
    static void Dither(void* p, int sx, int sy, int w, int h, int dx, int dy, int level) {
        Ctx* c = static_cast<Ctx*>(p);
        if (c->fast) { c->st->blit.dither(sx, sy, w, h, dx, dy, level); return; }
        LGFX_Sprite& src = c->faces->sprite.sprite;
        const int x0 = std::max(dx, c->cx0), y0 = std::max(dy, c->cy0);
        const int x1 = std::min(dx + w, c->cx1), y1 = std::min(dy + h, c->cy1);
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                if (!Iso::World::DitherOn(level, x - dx, y - dy)) continue;
                const int v = (int)src.readPixelValue(sx + x - dx, sy + y - dy);
                if (v) OSData::frame->writePixel(x, y, v);
            }
        }
    }
    static int FacePx(void* p, int sx, int sy) {
        Ctx* c = static_cast<Ctx*>(p);
        if (c->fast) return c->st->blit.pixel(sx, sy);
        const IconRender::PimgSprite& sp = c->faces->sprite;
        if ((unsigned)sx >= sp.width || (unsigned)sy >= sp.height) return 0;
        return (int)c->faces->sprite.sprite.readPixelValue(sx, sy);
    }
    static void Put(void* p, int x, int y, int col) {
        Ctx* c = static_cast<Ctx*>(p);
        if (c->fast) { c->st->blit.put(x, y, col); return; }
        if (x < c->cx0 || x >= c->cx1 || y < c->cy0 || y >= c->cy1) return;
        OSData::frame->writePixel(x, y, col & 15);
    }
    // 人や物の絵の画素(透過・範囲外・無効な画像は -1)
    static int ImagePx(void* p, int32_t image, int x, int y) {
        Ctx* c = static_cast<Ctx*>(p);
        if (image != c->img) {
            c->img = image;
            c->islot = nullptr;
            c->ibuf = nullptr;
            size_t index;
            if (image > 0 && c->self->ResolveImageHandle((uint32_t)image, index)) {
                LuaEngine::ImageSlot& slot = c->self->images_[index];
                if (slot.sprite.usable) {
                    c->islot = &slot;
                    if (Is4bpp(slot.sprite.sprite)) {
                        c->ibuf = static_cast<const uint8_t*>(slot.sprite.sprite.getBuffer());
                        c->istride = (slot.sprite.width + 1) >> 1;
                    }
                }
            }
        }
        const LuaEngine::ImageSlot* s = c->islot;
        if (!s || (unsigned)x >= s->sprite.width || (unsigned)y >= s->sprite.height) return -1;
        const int v = c->ibuf ? (int)Iso::FaceBlitter::Get(c->ibuf + (size_t)y * c->istride, x)
                              : (int)const_cast<LuaEngine::ImageSlot*>(s)->sprite.sprite.readPixelValue(x, y);
        return (v == 0 && s->sprite.transparent) ? -1 : v;
    }
    static void Dirty(void*, int x, int y, int w, int h) { MarkDirty(x, y, w, h); }

    static Iso::World::Sink MakeSink(Ctx& c) {
        Iso::World::Sink sink;
        sink.ctx = &c;
        sink.draw = Draw;
        sink.dirty = Dirty;
        sink.dither = Dither;
        sink.face_px = FacePx;
        sink.image_px = ImagePx;
        sink.put = Put;
        return sink;
    }

    // 描き直しを頼むだけの描き先(人や物の絵を読む口も付ける。entity_at 用)
    static Iso::World::Sink DirtySink(Ctx& c) {
        Iso::World::Sink sink;
        sink.ctx = &c;
        sink.dirty = Dirty;
        sink.image_px = ImagePx;
        return sink;
    }

    // クリップ [x0, x1) x [y0, y1) の中へ描く準備をして fn を呼ぶ
    template <class F>
    static void WithSink(LuaEngine* self, LuaEngine::IsoState& st, int x0, int y0, int x1, int y1, F&& fn) {
        LuaEngine::ImageSlot* slot = Image(self, st);
        if (!slot || x0 >= x1 || y0 >= y1) return;
        Analyze(st, *slot);
        LGFX_Sprite& frame = *OSData::frame;
        Ctx c{self, &st, slot, false, x0, y0, x1, y1};
        if (Is4bpp(frame) && Is4bpp(slot->sprite.sprite)) {
            const int fw = frame.width(), fh = frame.height();
            st.blit.setTarget(static_cast<uint8_t*>(frame.getBuffer()), ((fw + 1) & ~1) >> 1,
                              std::max(x0, 0), std::max(y0, 0), std::min(x1, fw), std::min(y1, fh));
            c.fast = true;
        }
        fn(MakeSink(c));
    }

    // 今のクリップ(無ければ画面全体)
    static void CurrentClip(int& x0, int& y0, int& x1, int& y1) {
        int32_t kx = 0, ky = 0, kw = 0, kh = 0;
        OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
        if (kw <= 0 || kh <= 0) { kx = 0; ky = 0; kw = OSData::frame->width(); kh = OSData::frame->height(); }
        x0 = kx; y0 = ky; x1 = kx + kw; y1 = ky + kh;
    }

    // ---------------- ワールド ----------------

    // pico.iso.create(dir, kind, seed[, k]) -> x, y, z | nil, 理由
    static int l_create(lua_State* L) {
        LuaEngine* self = Self(L);
        const char* dir = luaL_checkstring(L, 1);
        const int kind = Int(L, 2);
        const uint32_t seed = (uint32_t)luaL_checkinteger(L, 3);
        const int k = (int)luaL_optinteger(L, 4, Iso::kNewK);
        if (kind < 0 || kind > Iso::ARENA) return luaL_error(L, "pico.iso.create: kind は 0〜4 です");
        if (k < 1 || k > Iso::kMaxK) return luaL_error(L, "pico.iso.create: k は 1〜%d です", Iso::kMaxK);
        LuaEngine::IsoState& st = St(L);
        if (!DirAllowed(self, dir, "pico.iso.create")) {
            lua_pushnil(L);
            lua_pushstring(L, "権限がありません");
            return 2;
        }
        int x, y, z;
        if (!st.world.create(dir, (uint8_t)kind, seed, k, x, y, z)) {
            lua_pushnil(L);
            lua_pushstring(L, "メモリが足りません");
            return 2;
        }
        lua_pushinteger(L, x);
        lua_pushinteger(L, y);
        lua_pushinteger(L, z);
        return 3;
    }

    // pico.iso.open(dir) -> x, y, z, ブロック | nil, 理由
    static int l_open(lua_State* L) {
        LuaEngine* self = Self(L);
        const char* dir = luaL_checkstring(L, 1);
        LuaEngine::IsoState& st = St(L);
        const char* err = "権限がありません";
        int x, y, z, cur;
        if (!OSData::SD_usable) err = "SDカードがありません";
        else if (DirAllowed(self, dir, "pico.iso.open") && st.world.open(dir, x, y, z, cur, err)) {
            lua_pushinteger(L, x);
            lua_pushinteger(L, y);
            lua_pushinteger(L, z);
            lua_pushinteger(L, cur);
            return 4;
        }
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }

    // pico.iso.info(path) -> 種類, 1辺のマス数 | nil
    static int l_info(lua_State* L) {
        LuaEngine* self = Self(L);
        const char* path = luaL_checkstring(L, 1);
        int kind, width;
        if (!OSData::SD_usable || !self->SdPathAllowed(path) || !Iso::World::Info(path, kind, width)) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, kind);
        lua_pushinteger(L, width);
        return 2;
    }

    // pico.iso.save(x, y, z, ブロック) -> bool
    static int l_save(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        lua_pushboolean(L, st.world.save(Int(L, 1), Int(L, 2), Int(L, 3), Int(L, 4)));
        return 1;
    }

    static int l_close(lua_State* L) {
        St(L).world.close();
        St(L).flow.clear();
        return 0;
    }

    // pico.iso.migrate(古いファイル, dir) -> true | nil, 理由
    static int l_migrate(lua_State* L) {
        LuaEngine* self = Self(L);
        const char* old = luaL_checkstring(L, 1);
        const char* dir = luaL_checkstring(L, 2);
        LuaEngine::IsoState& st = St(L);
        const char* err = "権限がありません";
        if (self->SdWriteAllowed(old, "pico.iso.migrate", true) && DirAllowed(self, dir, "pico.iso.migrate")
            && st.world.migrate(old, dir, err)) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }

    static int l_get(lua_State* L) {
        lua_pushinteger(L, St(L).world.get(Int(L, 1), Int(L, 2), Int(L, 3)));
        return 1;
    }

    static int l_set(lua_State* L) {
        const int b = Int(L, 4);
        if (b < 0 || b > Iso::kBlockCount) return luaL_error(L, "pico.iso.set: ブロックは 0〜%d です", Iso::kBlockCount);
        St(L).world.set(Int(L, 1), Int(L, 2), Int(L, 3), (uint8_t)b);
        return 0;
    }

    static int l_first_air(lua_State* L) {
        lua_pushinteger(L, St(L).world.firstAir(Int(L, 1), Int(L, 2)));
        return 1;
    }

    // pico.iso.size() -> 1辺のマス数, 高さ, 種類 | nil(開いていない)
    static int l_size(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        if (!st.world.isOpen()) { lua_pushnil(L); return 1; }
        lua_pushinteger(L, st.world.width());
        lua_pushinteger(L, Iso::H);
        lua_pushinteger(L, st.world.kind());
        return 3;
    }

    // pico.iso.pump(max[, ms]) -> 読み込んだ数
    static int l_pump(lua_State* L) {
        const int max = Int(L, 1);
        const lua_Integer ms = luaL_optinteger(L, 2, 0);
        lua_pushinteger(L, St(L).world.pump(max, (uint32_t)(ms > 0 ? ms : 0)));
        return 1;
    }

    static int l_pending(lua_State* L) {
        lua_pushinteger(L, St(L).world.pending());
        return 1;
    }

    // pico.iso.stats() -> { chunks, bytes(置き場), faces(直前に描いた面), pending }
    static int l_stats(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, st.world.loadedCount());
        lua_setfield(L, -2, "chunks");
        lua_pushinteger(L, st.world.isOpen() ? (lua_Integer)Iso::World::PoolBytes() : 0);
        lua_setfield(L, -2, "bytes");
        lua_pushinteger(L, st.world.lastFaces());
        lua_setfield(L, -2, "faces");
        lua_pushinteger(L, st.world.pending());
        lua_setfield(L, -2, "pending");
        return 1;
    }

    // ---------------- 表示 ----------------

    static int l_set_image(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        // 確かめてから差し替える(エラーのときは今の絵のまま)
        const uint32_t handle = (uint32_t)luaL_checkinteger(L, 1);
        LuaEngine::ImageSlot* slot = Slot(self, handle);
        if (!slot) return luaL_error(L, "pico.iso.set_image: 無効なイメージハンドル");
        if (slot->sprite.width < Iso::kSheetW || slot->sprite.height < (Iso::kCursorRow + 1) * Iso::kRowH) {
            return luaL_error(L, "pico.iso.set_image: 面の絵の大きさが違います(%dx%d 以上)", Iso::kSheetW,
                              (Iso::kCursorRow + 1) * Iso::kRowH);
        }
        st.image = handle;
        st.analyzed = nullptr;
        Analyze(st, *slot);
        return 0;
    }

    static int l_view(lua_State* L) {
        St(L).world.setView(Int(L, 1), Int(L, 2), Int(L, 3), Int(L, 4));
        return 0;
    }

    // pico.iso.origin([ox, oy]) -> ox, oy(ブロック(0,0,0)の絵の左上)
    static int l_origin(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        if (!lua_isnoneornil(L, 1)) st.world.setOrigin(Int(L, 1), Int(L, 2));
        lua_pushinteger(L, st.world.originX());
        lua_pushinteger(L, st.world.originY());
        return 2;
    }

    static int l_cursor(lua_State* L) {
        const bool show = lua_isnoneornil(L, 4) ? true : lua_toboolean(L, 4) != 0;
        St(L).world.setCursor(Int(L, 1), Int(L, 2), Int(L, 3), show);
        return 0;
    }

    static int l_sky(lua_State* L) {
        St(L).sky = Int(L, 1) & 15;
        return 0;
    }

    // pico.iso.sunlight([on]) -> on。false で夜(どの面も影の絵、松明の光だけで明るくなる)。描き直しは呼び出し側
    static int l_sunlight(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        if (!lua_isnoneornil(L, 1)) st.world.setSunlight(lua_toboolean(L, 1) != 0);
        lua_pushboolean(L, st.world.sunlight());
        return 1;
    }

    // pico.iso.light(x, y, z) -> 松明の明るさ(0〜3)
    static int l_light(lua_State* L) {
        lua_pushinteger(L, St(L).world.light(Int(L, 1), Int(L, 2), Int(L, 3)));
        return 1;
    }

    static int l_culling(lua_State* L) {
        St(L).world.setCulling(lua_toboolean(L, 1) != 0);
        return 0;
    }

    // pico.iso.render([x, y, w, h]): 空で塗って、ワールドとカーソルを描く。Canvas の render の中で呼ぶ
    // (省略すると今のクリップ = 描き直す範囲)
    static int l_render(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        int x0, y0, x1, y1;
        CurrentClip(x0, y0, x1, y1);
        if (!lua_isnoneornil(L, 1)) {
            const int ax = Int(L, 1), ay = Int(L, 2), aw = Int(L, 3), ah = Int(L, 4);
            x0 = std::max(x0, ax); y0 = std::max(y0, ay);
            x1 = std::min(x1, ax + aw); y1 = std::min(y1, ay + ah);
        }
        if (x0 >= x1 || y0 >= y1) return 0;
        if (!Fill4bpp::FillRect(x0, y0, x1 - x0, y1 - y0, st.sky)) {
            OSData::frame->fillRect(x0, y0, x1 - x0, y1 - y0, st.sky);
        }
        WithSink(self, st, x0, y0, x1, y1, [&](const Iso::World::Sink& sink) {
            st.world.render(sink, x0, y0, x1, y1);
        });
        return 0;
    }

    // pico.iso.draw_icon(ブロック, x, y): 1個のブロックをまるごと(日なたの3面。松明は松明の絵)描く
    static int l_draw_icon(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        const int id = Int(L, 1), px = Int(L, 2), py = Int(L, 3);
        if (id < 1 || id > Iso::kCursorRow + 1) return luaL_error(L, "pico.iso.draw_icon: ブロックは 1〜%d です", Iso::kCursorRow + 1);
        int x0, y0, x1, y1;
        CurrentClip(x0, y0, x1, y1);
        WithSink(self, st, x0, y0, x1, y1, [&](const Iso::World::Sink& sink) {
            Iso::World::DrawIcon(sink, id, px, py);
        });
        MarkDirty(px, py, 32, 31);
        return 0;
    }

    static int l_block_pos(lua_State* L) {
        int px, py;
        St(L).world.blockPos(Int(L, 1), Int(L, 2), Int(L, 3), px, py);
        lua_pushinteger(L, px);
        lua_pushinteger(L, py);
        return 2;
    }

    // pico.iso.pick(px, py) -> x, y, z, "top"|"left"|"right" | nil
    static int l_pick(lua_State* L) {
        int x, y, z;
        const Iso::Face f = St(L).world.pick(Int(L, 1), Int(L, 2), x, y, z);
        if (f == Iso::Face::None) { lua_pushnil(L); return 1; }
        lua_pushinteger(L, x);
        lua_pushinteger(L, y);
        lua_pushinteger(L, z);
        lua_pushstring(L, f == Iso::Face::Top ? "top" : (f == Iso::Face::Left ? "left" : "right"));
        return 4;
    }

    static int l_dirty_block(lua_State* L) {
        Iso::World::Sink sink{nullptr, nullptr, Dirty};
        St(L).world.dirtyBlock(sink, Int(L, 1), Int(L, 2), Int(L, 3));
        return 0;
    }

    static int l_dirty_edit(lua_State* L) {
        Iso::World::Sink sink{nullptr, nullptr, Dirty};
        St(L).world.dirtyEdit(sink, Int(L, 1), Int(L, 2), Int(L, 3));
        return 0;
    }
    // ---------------- 人や物(エンティティ) ----------------

    static float Num(lua_State* L, int idx) { return (float)luaL_checknumber(L, idx); }

    // opts の数(無ければ def)。整数でなければエラー
    static int OptInt(lua_State* L, int t, const char* key, int def) {
        lua_getfield(L, t, key);
        int v = def;
        if (!lua_isnil(L, -1)) {
            if (!lua_isinteger(L, -1)) {
                lua_Number n = lua_tonumber(L, -1);
                if (!lua_isnumber(L, -1) || n != (lua_Number)(lua_Integer)n) luaL_error(L, "pico.iso: %s は整数です", key);
                v = (int)n;
            } else {
                v = (int)lua_tointeger(L, -1);
            }
        }
        lua_pop(L, 1);
        return v;
    }
    static float OptNum(lua_State* L, int t, const char* key, float def) {
        lua_getfield(L, t, key);
        float v = def;
        if (!lua_isnil(L, -1)) {
            if (!lua_isnumber(L, -1)) luaL_error(L, "pico.iso: %s は数です", key);
            v = (float)lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
        return v;
    }
    static bool OptBool(lua_State* L, int t, const char* key, bool def) {
        lua_getfield(L, t, key);
        const bool v = lua_isnil(L, -1) ? def : lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        return v;
    }
    static bool Has(lua_State* L, int t, const char* key) {
        lua_getfield(L, t, key);
        const bool v = !lua_isnil(L, -1);
        lua_pop(L, 1);
        return v;
    }

    // 画像のハンドルを確かめて大きさを返す
    static void CheckImage(lua_State* L, int32_t image, int& w, int& h) {
        LuaEngine* self = Self(L);
        size_t index;
        if (image <= 0 || !self->ResolveImageHandle((uint32_t)image, index) || !self->images_[index].sprite.usable) {
            luaL_error(L, "pico.iso: 無効なイメージハンドル");
        }
        w = self->images_[index].sprite.width;
        h = self->images_[index].sprite.height;
    }

    // opts(表 t。0 なら無し)を e へ当てる。adding なら省いた値は既定(画像全体・足元は下の真ん中・箱は絵の大きさから)
    static void ApplyOpts(lua_State* L, int t, Iso::Entity& e, bool adding) {
        int iw, ih;
        bool image_changed = adding;
        if (t && Has(L, t, "image")) {
            e.image = (int32_t)OptInt(L, t, "image", 0);
            image_changed = true;
        }
        CheckImage(L, e.image, iw, ih);
        if (t && Has(L, t, "x")) e.x = OptNum(L, t, "x", e.x);
        if (t && Has(L, t, "y")) e.y = OptNum(L, t, "y", e.y);
        if (t && Has(L, t, "z")) e.z = OptNum(L, t, "z", e.z);
        const bool rect = t && (Has(L, t, "sx") || Has(L, t, "sy") || Has(L, t, "w") || Has(L, t, "h"));
        if (rect || image_changed) {
            const int sx = t ? OptInt(L, t, "sx", image_changed && !rect ? 0 : e.sx) : 0;
            const int sy = t ? OptInt(L, t, "sy", image_changed && !rect ? 0 : e.sy) : 0;
            const int sw = t ? OptInt(L, t, "w", rect && !adding ? e.sw : iw - sx) : iw - sx;
            const int sh = t ? OptInt(L, t, "h", rect && !adding ? e.sh : ih - sy) : ih - sy;
            if (sx < 0 || sy < 0 || sw < 1 || sh < 1 || sx + sw > iw || sy + sh > ih) {
                luaL_error(L, "pico.iso: 絵の範囲 (%d,%d,%d,%d) が画像 %dx%d の外です", sx, sy, sw, sh, iw, ih);
            }
            if (sw > 240 || sh > 320) luaL_error(L, "pico.iso: 絵が大きすぎます(240x320 まで)");
            const bool size_changed = sw != e.sw || sh != e.sh;
            e.sx = (int16_t)sx; e.sy = (int16_t)sy; e.sw = (int16_t)sw; e.sh = (int16_t)sh;
            if (adding || size_changed) {
                // 足元の点・箱は絵の大きさから決め直す(opts で指定があればそちら)
                e.ax = (int16_t)(sw / 2);
                e.ay = (int16_t)(sh - 1);
                if (adding) {
                    float r = (float)sw / 64.0f;
                    e.r = r < 0.05f ? 0.05f : (r > 0.49f ? 0.49f : r);
                    e.h = (float)sh / 16.0f;
                }
            }
        }
        if (t) {
            e.ax = (int16_t)OptInt(L, t, "ax", e.ax);
            e.ay = (int16_t)OptInt(L, t, "ay", e.ay);
            const float r = OptNum(L, t, "r", e.r), h = OptNum(L, t, "height", e.h);
            if (!(r > 0.0f && r <= 4.0f)) luaL_error(L, "pico.iso: r は 0 より大きく 4 以下です");
            if (!(h > 0.0f && h <= 16.0f)) luaL_error(L, "pico.iso: height は 0 より大きく 16 以下です");
            e.r = r; e.h = h;
            e.shadow = OptBool(L, t, "shadow", e.shadow);
            e.flip = OptBool(L, t, "flip", e.flip);
            e.visible = OptBool(L, t, "visible", e.visible);
            e.shadow_color = (uint8_t)(OptInt(L, t, "shadow_color", e.shadow_color) & 15);
            // 一番上に描く飾り。false で消す
            lua_getfield(L, t, "bar");
            if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) e.bar = -1;
            else if (!lua_isnil(L, -1)) {
                const lua_Number b = luaL_checknumber(L, -1);
                e.bar = (int8_t)(b < 0 ? 0 : (b > 100 ? 100 : (int)(b + 0.5)));
            }
            lua_pop(L, 1);
            e.bar_color = (uint8_t)(OptInt(L, t, "bar_color", e.bar_color) & 15);
            lua_getfield(L, t, "mark");
            if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) e.mark = -1;
            else if (!lua_isnil(L, -1)) e.mark = (int8_t)(luaL_checkinteger(L, -1) & 15);
            lua_pop(L, 1);
            const int tag = OptInt(L, t, "tag", e.tag);
            if (tag < 0 || tag > 31) luaL_error(L, "pico.iso: tag は 0〜31 です");
            e.tag = (uint8_t)tag;
            lua_getfield(L, t, "crowd");
            if (lua_isboolean(L, -1)) e.crowd = lua_toboolean(L, -1) ? 1 : 0;
            else if (lua_isstring(L, -1)) {
                const char* c = lua_tostring(L, -1);
                if (!strcmp(c, "none")) e.crowd = 0;
                else if (!strcmp(c, "move")) e.crowd = 1;
                else if (!strcmp(c, "fixed")) e.crowd = 2;
                else luaL_error(L, "pico.iso: crowd は \"none\" / \"move\" / \"fixed\" です");
            } else if (!lua_isnil(L, -1)) luaL_error(L, "pico.iso: crowd は文字列か真偽値です");
            lua_pop(L, 1);
            const float mass = OptNum(L, t, "mass", e.mass);
            if (!(mass > 0.0f && mass <= 1000.0f)) luaL_error(L, "pico.iso: mass は 0 より大きく 1000 以下です");
            e.mass = mass;
        }
        if (!(e.x > -1e6f && e.x < 1e6f && e.y > -1e6f && e.y < 1e6f && e.z > -1e6f && e.z < 1e6f)) {
            luaL_error(L, "pico.iso: 位置が大きすぎます");
        }
    }

    static Iso::Entity* CheckEntity(lua_State* L, int idx, int& handle) {
        handle = Int(L, idx);
        Iso::Entity* e = St(L).world.entity(handle);
        if (!e) luaL_error(L, "pico.iso: 無効な人や物のハンドル");
        return e;
    }

    // pico.iso.entity_add(image, x, y, z[, opts]) -> id | nil, 理由
    static int l_entity_add(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        Iso::Entity e;
        e.image = (int32_t)luaL_checkinteger(L, 1);
        e.x = Num(L, 2); e.y = Num(L, 3); e.z = Num(L, 4);
        const int t = lua_isnoneornil(L, 5) ? 0 : (luaL_checktype(L, 5, LUA_TTABLE), 5);
        ApplyOpts(L, t, e, true);
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        const int h = st.world.entityAdd(DirtySink(c), e);
        if (!h) {
            lua_pushnil(L);
            if (st.world.entityCount() >= Iso::kMaxEntities) lua_pushfstring(L, "置けるのは %d 個までです", Iso::kMaxEntities);
            else lua_pushstring(L, "メモリが足りません");
            return 2;
        }
        lua_pushinteger(L, h);
        return 1;
    }

    // pico.iso.entity_set(id, opts)
    static int l_entity_set(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        int h;
        Iso::Entity* e = CheckEntity(L, 1, h);
        luaL_checktype(L, 2, LUA_TTABLE);
        Iso::Entity tmp = *e;
        ApplyOpts(L, 2, tmp, false);   // エラーなら何も変えない
        *e = tmp;
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        st.world.entityChanged(DirtySink(c), h);
        return 0;
    }

    // pico.iso.entity_move(id, x, y, z)
    static int l_entity_move(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        int h;
        Iso::Entity* e = CheckEntity(L, 1, h);
        const float x = Num(L, 2), y = Num(L, 3), z = Num(L, 4);
        if (!(x > -1e6f && x < 1e6f && y > -1e6f && y < 1e6f && z > -1e6f && z < 1e6f)) {
            return luaL_error(L, "pico.iso.entity_move: 位置が大きすぎます");
        }
        if (x == e->x && y == e->y && z == e->z) return 0;
        e->x = x; e->y = y; e->z = z;
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        st.world.entityChanged(DirtySink(c), h);
        return 0;
    }

    // pico.iso.entity_get(id) -> x, y, z, { ... } | nil
    static int l_entity_get(lua_State* L) {
        const Iso::Entity* e = St(L).world.entity(Int(L, 1));
        if (!e) { lua_pushnil(L); return 1; }
        lua_pushnumber(L, e->x);
        lua_pushnumber(L, e->y);
        lua_pushnumber(L, e->z);
        lua_createtable(L, 0, 14);
        lua_pushinteger(L, e->image); lua_setfield(L, -2, "image");
        lua_pushinteger(L, e->sx); lua_setfield(L, -2, "sx");
        lua_pushinteger(L, e->sy); lua_setfield(L, -2, "sy");
        lua_pushinteger(L, e->sw); lua_setfield(L, -2, "w");
        lua_pushinteger(L, e->sh); lua_setfield(L, -2, "h");
        lua_pushinteger(L, e->ax); lua_setfield(L, -2, "ax");
        lua_pushinteger(L, e->ay); lua_setfield(L, -2, "ay");
        lua_pushnumber(L, e->r); lua_setfield(L, -2, "r");
        lua_pushnumber(L, e->h); lua_setfield(L, -2, "height");
        lua_pushboolean(L, e->shadow); lua_setfield(L, -2, "shadow");
        lua_pushinteger(L, e->shadow_color); lua_setfield(L, -2, "shadow_color");
        lua_pushboolean(L, e->flip); lua_setfield(L, -2, "flip");
        lua_pushboolean(L, e->visible); lua_setfield(L, -2, "visible");
        if (e->bar >= 0) { lua_pushinteger(L, e->bar); lua_setfield(L, -2, "bar"); }
        lua_pushinteger(L, e->bar_color); lua_setfield(L, -2, "bar_color");
        if (e->mark >= 0) { lua_pushinteger(L, e->mark); lua_setfield(L, -2, "mark"); }
        lua_pushinteger(L, e->tag); lua_setfield(L, -2, "tag");
        lua_pushstring(L, e->crowd == 1 ? "move" : (e->crowd == 2 ? "fixed" : "none")); lua_setfield(L, -2, "crowd");
        lua_pushnumber(L, e->mass); lua_setfield(L, -2, "mass");
        return 4;
    }

    // pico.iso.entity_pos(id) -> x, y, z | nil(entity_get より軽い。表を作らない。押し合いの後に位置を読み戻す)
    static int l_entity_pos(lua_State* L) {
        const Iso::Entity* e = St(L).world.entity(Int(L, 1));
        if (!e) { lua_pushnil(L); return 1; }
        lua_pushnumber(L, e->x);
        lua_pushnumber(L, e->y);
        lua_pushnumber(L, e->z);
        return 3;
    }

    // pico.iso.entity_remove(id) -> bool
    static int l_entity_remove(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        lua_pushboolean(L, st.world.entityRemove(DirtySink(c), Int(L, 1)));
        return 1;
    }

    static int l_entity_clear(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        st.world.entityClear(DirtySink(c));
        return 0;
    }

    // pico.iso.entity_at(px, py) -> id | nil(絵の不透明な画素で判定)
    static int l_entity_at(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        const int h = st.world.entityAt(DirtySink(c), Int(L, 1), Int(L, 2));
        if (h) lua_pushinteger(L, h); else lua_pushnil(L);
        return 1;
    }

    // pico.iso.ground(x, z[, y]) -> 地面の高さ | nil(y より下で一番上の、ブロックの上面)
    static int l_ground(lua_State* L) {
        const float y = lua_isnoneornil(L, 3) ? (float)Iso::H : Num(L, 3);
        const int g = St(L).world.ground(Num(L, 1), y, Num(L, 2));
        if (g < 0) lua_pushnil(L); else lua_pushinteger(L, g);
        return 1;
    }

    // pico.iso.loaded(x, z) -> bool(その柱のチャンクを読み込んでいるか)
    static int l_loaded(lua_State* L) {
        lua_pushboolean(L, St(L).world.loadedAt(Num(L, 1), Num(L, 2)));
        return 1;
    }

    // pico.iso.to_screen(x, y, z) -> sx, sy(点の画面の位置。小数のまま)
    static int l_to_screen(lua_State* L) {
        float sx, sy;
        St(L).world.project(Num(L, 1), Num(L, 2), Num(L, 3), sx, sy);
        lua_pushnumber(L, sx);
        lua_pushnumber(L, sy);
        return 2;
    }

    // ---------------- 経路探索 ----------------

    struct EdgeCtx { lua_State* L; int fn; };
    // opts.edge(x, y, z, nx, ny, nz, floor) -> 数(足す値段) | false(通れない) | nil/true(0)
    static float Edge(void* p, int x, int y, int z, int nx, int ny, int nz, uint8_t floor, bool& abort) {
        EdgeCtx* c = static_cast<EdgeCtx*>(p);
        lua_State* L = c->L;
        lua_pushvalue(L, c->fn);
        const int a[7] = {x, y, z, nx, ny, nz, floor};
        for (int v : a) lua_pushinteger(L, v);
        if (lua_pcall(L, 7, 1, 0) != LUA_OK) { abort = true; return -1; }   // エラーはスタックに残して呼び出し側で投げ直す
        float r = 0;
        if (lua_isnumber(L, -1)) r = (float)lua_tonumber(L, -1);
        else if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) r = -1;
        lua_pop(L, 1);
        return r;
    }

    // ブロック番号の配列(1〜kBlockCount)をビットへ
    static uint32_t BlockMask(lua_State* L, int t, const char* key) {
        lua_getfield(L, t, key);
        uint32_t m = 0;
        if (!lua_isnil(L, -1)) {
            luaL_argexpected(L, lua_istable(L, -1), t, "テーブル");
            const lua_Integer n = luaL_len(L, -1);
            for (lua_Integer i = 1; i <= n; i++) {
                lua_rawgeti(L, -1, i);
                const lua_Integer b = lua_tointeger(L, -1);
                if (!lua_isinteger(L, -1) || b < 1 || b > Iso::kBlockCount) luaL_error(L, "pico.iso: %s の %d 番目が正しいブロック番号ではありません", key, (int)i);
                m |= 1u << b;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        return m;
    }

    static int OptY(lua_State* L, int idx) {
        return lua_isnoneornil(L, idx) ? -1 : (int)floorf(Num(L, idx) + 0.001f);
    }

    // ブロックごとの値段の表(1〜kBlockCount)
    static void CostTable(lua_State* L, int t, const char* key, float* out, const char* api) {
        lua_getfield(L, t, key);
        if (!lua_isnil(L, -1)) {
            if (!lua_istable(L, -1)) luaL_error(L, "%s: %s はテーブルです", api, key);
            for (int b = 1; b <= Iso::kBlockCount; b++) {
                lua_rawgeti(L, -1, b);
                if (!lua_isnil(L, -1)) {
                    if (!lua_isnumber(L, -1) || lua_tonumber(L, -1) < 0) luaL_error(L, "%s: %s[%d] は0以上の数です", api, key, b);
                    out[b] = (float)lua_tonumber(L, -1);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    // 経路・流れの場で共通の規則(表 t)
    static void ParseRules(lua_State* L, int t, Iso::PathRules& r, const char* api) {
        r.max_up = OptInt(L, t, "max_up", r.max_up);
        r.max_down = OptInt(L, t, "max_down", r.max_down);
        r.height = OptInt(L, t, "height", r.height);
        r.diagonal = OptBool(L, t, "diagonal", r.diagonal);
        r.swim = OptBool(L, t, "swim", r.swim);
        r.step = OptNum(L, t, "step", r.step);
        r.up_cost = OptNum(L, t, "up_cost", r.up_cost);
        r.down_cost = OptNum(L, t, "down_cost", r.down_cost);
        if (!(r.step > 0) || r.up_cost < 0 || r.down_cost < 0) luaL_error(L, "%s: step は正、up_cost/down_cost は0以上です", api);
        if (r.max_up < 0 || r.max_down < 0 || r.height < 1) luaL_error(L, "%s: max_up/max_down は0以上、height は1以上です", api);
        r.avoid = BlockMask(L, t, "avoid");
        r.pass = BlockMask(L, t, "pass");
        CostTable(L, t, "block_cost", r.block_cost, api);
        CostTable(L, t, "body_cost", r.body_cost, api);
    }

    // pico.iso.path(x0, y0, z0, x1, y1, z1 [, opts]) -> 道 {{x=,y=,z=}, ...}, 値段, "found"|"partial"
    //                                              | nil, 理由("no_path"|"limit"|"start"|"goal")
    static int l_path(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        if (!st.world.isOpen()) luaL_error(L, "pico.iso.path: ワールドを開いていません");
        const int sx = (int)floorf(Num(L, 1)), sy = OptY(L, 2), sz = (int)floorf(Num(L, 3));
        const int gx = (int)floorf(Num(L, 4)), gy = OptY(L, 5), gz = (int)floorf(Num(L, 6));
        Iso::PathRules r;
        EdgeCtx ec{L, 0};
        if (!lua_isnoneornil(L, 7)) {
            luaL_checktype(L, 7, LUA_TTABLE);
            ParseRules(L, 7, r, "pico.iso.path");
            r.partial = OptBool(L, 7, "partial", r.partial);
            r.max_nodes = OptInt(L, 7, "max_nodes", r.max_nodes);
            if (r.max_nodes < 1 || r.max_nodes > Iso::kMaxPathNodes) luaL_error(L, "pico.iso.path: max_nodes は 1〜%d です", Iso::kMaxPathNodes);
            lua_getfield(L, 7, "edge");
            if (!lua_isnil(L, -1)) {
                luaL_checktype(L, -1, LUA_TFUNCTION);
                ec.fn = lua_gettop(L);   // 呼ぶ間はスタックに置いたまま
                r.edge = Edge;
                r.ctx = &ec;
            } else {
                lua_pop(L, 1);
            }
        }
        Iso::PathPoint* out = static_cast<Iso::PathPoint*>(malloc(sizeof(Iso::PathPoint) * (size_t)r.max_nodes));
        if (!out) luaL_error(L, "pico.iso.path: メモリが足りません");
        const Iso::PathResult res = Iso::FindPath(st.world, sx, sy, sz, gx, gy, gz, r, out, r.max_nodes);
        if (res.status == Iso::PathStatus::Aborted) {
            free(out);
            return lua_error(L);   // edge のエラー(スタックの一番上)をそのまま
        }
        if (res.status != Iso::PathStatus::Found && res.status != Iso::PathStatus::Partial) {
            free(out);
            if (res.status == Iso::PathStatus::NoMemory) luaL_error(L, "pico.iso.path: メモリが足りません");
            lua_pushnil(L);
            const char* why = res.status == Iso::PathStatus::Limit ? "limit"
                            : res.status == Iso::PathStatus::BadStart ? "start"
                            : res.status == Iso::PathStatus::BadGoal ? "goal" : "no_path";
            lua_pushstring(L, why);
            return 2;
        }
        // 表を作る間に Lua のメモリが足りなくなると out が漏れるので、先に Lua の文字列へ移す
        const int n = res.length < r.max_nodes ? res.length : r.max_nodes;
        lua_pushlstring(L, reinterpret_cast<const char*>(out), sizeof(Iso::PathPoint) * (size_t)n);
        free(out);
        const Iso::PathPoint* pts = reinterpret_cast<const Iso::PathPoint*>(lua_tostring(L, -1));
        lua_createtable(L, n, 0);
        for (int i = 0; i < n; i++) {
            Iso::PathPoint p;
            memcpy(&p, pts + i, sizeof p);
            lua_createtable(L, 0, 3);
            lua_pushinteger(L, p.x); lua_setfield(L, -2, "x");
            lua_pushinteger(L, p.y); lua_setfield(L, -2, "y");
            lua_pushinteger(L, p.z); lua_setfield(L, -2, "z");
            lua_rawseti(L, -2, i + 1);
        }
        lua_pushnumber(L, res.cost);
        lua_pushstring(L, res.status == Iso::PathStatus::Found ? "found" : "partial");
        return 3;
    }

    // pico.iso.stand(x, z[, y[, opts]]) -> 立てる高さ | nil(path と同じ規則で、y 以下で一番上)
    static int l_stand(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        Iso::PathRules r;
        if (!lua_isnoneornil(L, 4)) {
            luaL_checktype(L, 4, LUA_TTABLE);
            r.height = OptInt(L, 4, "height", r.height);
            r.swim = OptBool(L, 4, "swim", r.swim);
            r.avoid = BlockMask(L, 4, "avoid");
            r.pass = BlockMask(L, 4, "pass");
            if (r.height < 1) luaL_error(L, "pico.iso.stand: height は1以上です");
        }
        const int y = Iso::StandAt(st.world, (int)floorf(Num(L, 1)), OptY(L, 3), (int)floorf(Num(L, 2)), r);
        if (y < 0) lua_pushnil(L); else lua_pushinteger(L, y);
        return 1;
    }

    // pico.iso.arena() -> {base = {x=, y=, z=}, spawns = {{x=, y=, z=}, ...}} | nil(ARENA のワールドでない)
    // y は立つ高さ(一番上のブロックの上)
    static int l_arena(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        if (!st.world.isOpen() || st.world.kind() != Iso::ARENA) { lua_pushnil(L); return 1; }
        int bx, bz, sx[Iso::kArenaSpawns], sz[Iso::kArenaSpawns];
        st.world.arenaLayout(bx, bz, sx, sz);
        auto point = [&](int x, int z) {
            lua_createtable(L, 0, 3);
            lua_pushinteger(L, x); lua_setfield(L, -2, "x");
            lua_pushinteger(L, st.world.arenaHeight(x, z) + 1); lua_setfield(L, -2, "y");
            lua_pushinteger(L, z); lua_setfield(L, -2, "z");
        };
        lua_createtable(L, 0, 2);
        point(bx, bz);
        lua_setfield(L, -2, "base");
        lua_createtable(L, Iso::kArenaSpawns, 0);
        for (int i = 0; i < Iso::kArenaSpawns; i++) { point(sx[i], sz[i]); lua_rawseti(L, -2, i + 1); }
        lua_setfield(L, -2, "spawns");
        return 1;
    }

    // ---------------- 流れの場(フローフィールド) ----------------

    // pico.iso.flow_build(goals, opts[, now]) -> true | nil, 理由
    //   goals = {{x, z}, ...}(柱。{x=, z=} でもよい)。opts は pico.iso.path と同じ規則(max_nodes/partial/edge は無し)。
    //   now なら出来上がるまでその場で作る。そうでなければ pico.iso.flow_step で少しずつ
    static int l_flow_build(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        if (!st.world.isOpen()) luaL_error(L, "pico.iso.flow_build: ワールドを開いていません");
        luaL_checktype(L, 1, LUA_TTABLE);
        Iso::PathRules r;
        if (!lua_isnoneornil(L, 2)) {
            luaL_checktype(L, 2, LUA_TTABLE);
            ParseRules(L, 2, r, "pico.iso.flow_build");
        }
        const bool now = lua_toboolean(L, 3) != 0;
        const lua_Integer n = luaL_len(L, 1);
        if (n < 1 || n > Iso::kMaxFlowGoals) luaL_error(L, "pico.iso.flow_build: 目的地は 1〜%d 個です", Iso::kMaxFlowGoals);
        // 目的地は先に全部読んで確かめる(誤りなら作り直しを始めない)
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1 && !st.flow.begin(st.world, r)) {
                lua_pushnil(L);
                lua_pushstring(L, st.world.width() > Iso::kMaxFlowWidth ? "世界が広すぎます" : "メモリが足りません");
                return 2;
            }
            for (lua_Integer i = 1; i <= n; i++) {
                lua_rawgeti(L, 1, i);
                if (!lua_istable(L, -1)) luaL_error(L, "pico.iso.flow_build: 目的地の %d 番目が {x, z} ではありません", (int)i);
                const int t = lua_gettop(L);
                lua_rawgeti(L, t, 1);
                if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, t, "x"); }
                lua_rawgeti(L, t, 2);
                if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_getfield(L, t, "z"); }
                if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1)) luaL_error(L, "pico.iso.flow_build: 目的地の %d 番目が {x, z} ではありません", (int)i);
                if (pass == 1) st.flow.addGoal((int)floor(lua_tonumber(L, -2)), (int)floor(lua_tonumber(L, -1)));
                lua_pop(L, 3);
            }
        }
        if (now) {
            while (!st.flow.step(st.world, 1 << 20)) {}
        }
        lua_pushboolean(L, 1);
        return 1;
    }

    // pico.iso.flow_step([budget]) -> この呼び出しで出来上がったら true
    static int l_flow_step(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        const int budget = (int)luaL_optinteger(L, 1, 512);
        if (budget < 1) luaL_error(L, "pico.iso.flow_step: budget は1以上です");
        lua_pushboolean(L, st.flow.step(st.world, budget));
        return 1;
    }

    // pico.iso.flow_get(x, z) -> 値段, 次の柱の x, z, その立つ高さ | 値段, nil(目的地) | nil(届かない)
    static int l_flow_get(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        const int x = (int)floorf(Num(L, 1)), z = (int)floorf(Num(L, 2));
        const float d = st.flow.dist(x, z);
        if (d < 0) { lua_pushnil(L); return 1; }
        lua_pushnumber(L, d);
        int nx, nz;
        bool goal;
        if (!st.flow.next(x, z, nx, nz, &goal)) { lua_pushnil(L); return 2; }
        lua_pushinteger(L, nx);
        lua_pushinteger(L, nz);
        lua_pushinteger(L, st.flow.standY(nx, nz));
        return 4;
    }

    // pico.iso.flow_info() -> {ready=, building=, revision=}
    static int l_flow_info(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        lua_createtable(L, 0, 3);
        lua_pushboolean(L, st.flow.ready()); lua_setfield(L, -2, "ready");
        lua_pushboolean(L, st.flow.building()); lua_setfield(L, -2, "building");
        lua_pushinteger(L, (lua_Integer)st.flow.revision()); lua_setfield(L, -2, "revision");
        return 1;
    }

    static int l_flow_clear(lua_State* L) {
        St(L).flow.clear();
        return 0;
    }

    // ---------------- タワーディフェンス向けの道具 ----------------

    // pico.iso.keep_all(on) -> bool(世界全体を読み込んだままにする。広すぎれば false)
    static int l_keep_all(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        lua_pushboolean(L, st.world.setKeepAll(lua_toboolean(L, 1) != 0));
        return 1;
    }

    // tags: nil(全部)/ 数(1つ)/ 数の配列 → ビット
    static uint32_t TagMask(lua_State* L, int idx) {
        if (lua_isnoneornil(L, idx)) return 0;
        if (lua_isnumber(L, idx)) {
            const lua_Integer t = luaL_checkinteger(L, idx);
            if (t < 0 || t > 31) luaL_error(L, "pico.iso: tag は 0〜31 です");
            return 1u << t;
        }
        luaL_checktype(L, idx, LUA_TTABLE);
        uint32_t m = 0;
        const lua_Integer n = luaL_len(L, idx);
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, idx, i);
            const lua_Integer t = lua_tointeger(L, -1);
            if (!lua_isinteger(L, -1) || t < 0 || t > 31) luaL_error(L, "pico.iso: tag は 0〜31 です");
            m |= 1u << t;
            lua_pop(L, 1);
        }
        return m;
    }

    static constexpr int kMaxNearby = 32;

    // pico.iso.nearby(x, z, range[, tags[, max]]) -> {id, ...}(近い順、32個まで)
    static int l_nearby(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        const float x = Num(L, 1), z = Num(L, 2), range = Num(L, 3);
        const uint32_t mask = TagMask(L, 4);
        // 返すのは近い順に kMaxNearby 個まで(結果をスタックに置くので小さく抑える)
        int max = (int)luaL_optinteger(L, 5, kMaxNearby);
        if (max < 1) max = 1;
        if (max > kMaxNearby) max = kMaxNearby;
        int32_t out[kMaxNearby];
        const int n = st.world.nearby(x, z, range, mask, out, max);
        lua_createtable(L, n, 0);
        for (int i = 0; i < n; i++) { lua_pushinteger(L, out[i]); lua_rawseti(L, -2, i + 1); }
        return 1;
    }

    // pico.iso.crowd([iterations[, opts{height=, pass=}]]) -> 動いた数
    static int l_crowd(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        const int it = (int)luaL_optinteger(L, 1, 2);
        if (it < 1 || it > 8) luaL_error(L, "pico.iso.crowd: iterations は 1〜8 です");
        int height = 2;
        uint32_t pass = 0;
        if (!lua_isnoneornil(L, 2)) {
            luaL_checktype(L, 2, LUA_TTABLE);
            height = OptInt(L, 2, "height", height);
            pass = BlockMask(L, 2, "pass");
            if (height < 1) luaL_error(L, "pico.iso.crowd: height は1以上です");
        }
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        lua_pushinteger(L, st.world.crowdStep(DirtySink(c), it, height, pass));
        return 1;
    }

    // pico.iso.sight(x0, y0, z0, x1, y1, z1[, pass]) -> bool(ブロックに遮られないか)
    static int l_sight(lua_State* L) {
        LuaEngine::IsoState& st = St(L);
        uint32_t pass = 0;
        if (!lua_isnoneornil(L, 7)) {
            luaL_checktype(L, 7, LUA_TTABLE);
            const lua_Integer n = luaL_len(L, 7);
            for (lua_Integer i = 1; i <= n; i++) {
                lua_rawgeti(L, 7, i);
                const lua_Integer b = lua_tointeger(L, -1);
                if (!lua_isinteger(L, -1) || b < 1 || b > Iso::kBlockCount) luaL_error(L, "pico.iso.sight: pass の %d 番目が正しいブロック番号ではありません", (int)i);
                pass |= 1u << b;
                lua_pop(L, 1);
            }
        }
        lua_pushboolean(L, st.world.lineOfSight(Num(L, 1), Num(L, 2), Num(L, 3), Num(L, 4), Num(L, 5), Num(L, 6), pass));
        return 1;
    }

    // pico.iso.shot_add(x, y, z, opts{target= | tx=,ty=,tz=, speed=, arc=, color=, size=, tag=}) -> id | nil, 理由
    static int l_shot_add(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        Iso::Shot sh;
        sh.x = Num(L, 1); sh.y = Num(L, 2); sh.z = Num(L, 3);
        luaL_checktype(L, 4, LUA_TTABLE);
        sh.target = OptInt(L, 4, "target", 0);
        if (sh.target && !st.world.entity(sh.target)) luaL_error(L, "pico.iso.shot_add: 無効な人や物のハンドル");
        if (!sh.target) {
            if (!Has(L, 4, "tx") || !Has(L, 4, "ty") || !Has(L, 4, "tz")) luaL_error(L, "pico.iso.shot_add: target か tx,ty,tz が要ります");
            sh.tx = OptNum(L, 4, "tx", 0); sh.ty = OptNum(L, 4, "ty", 0); sh.tz = OptNum(L, 4, "tz", 0);
        }
        sh.speed = OptNum(L, 4, "speed", sh.speed);
        sh.arc = OptNum(L, 4, "arc", sh.arc);
        if (!(sh.speed > 0.0f && sh.speed <= 1000.0f)) luaL_error(L, "pico.iso.shot_add: speed は 0 より大きく 1000 以下です");
        if (!(sh.arc >= 0.0f && sh.arc <= 4.0f)) luaL_error(L, "pico.iso.shot_add: arc は 0〜4 です");
        sh.color = (uint8_t)(OptInt(L, 4, "color", 0) & 15);
        const int size = OptInt(L, 4, "size", 2);
        if (size < 1 || size > 6) luaL_error(L, "pico.iso.shot_add: size は 1〜6 です");
        sh.size = (uint8_t)size;
        sh.tag = (int32_t)OptInt(L, 4, "tag", 0);
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        const int h = st.world.shotAdd(DirtySink(c), sh);
        if (!h) {
            lua_pushnil(L);
            lua_pushfstring(L, "弾は %d 個までです", Iso::kMaxShots);
            return 2;
        }
        lua_pushinteger(L, h);
        return 1;
    }

    // pico.iso.shots_step(dt) -> 当たった弾の配列 {{id=, target=, tag=, lost=, x=, y=, z=}, ...}
    static int l_shots_step(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        const float dt = Num(L, 1);
        if (!(dt >= 0.0f && dt <= 10.0f)) luaL_error(L, "pico.iso.shots_step: dt は 0〜10 秒です");
        // 当たった弾は少しずつ受け取る(全部をスタックに置かない)。2回目からは dt=0 で、返しきれなかった分だけを受け取る
        Iso::ShotHit hits[8];
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        lua_newtable(L);
        int total = 0, n;
        float step_dt = dt;
        do {
            n = st.world.shotsStep(DirtySink(c), step_dt, hits, 8);
            step_dt = 0;
            for (int i = 0; i < n; i++) {
                lua_createtable(L, 0, 7);
                lua_pushinteger(L, hits[i].shot); lua_setfield(L, -2, "id");
                if (hits[i].target) { lua_pushinteger(L, hits[i].target); lua_setfield(L, -2, "target"); }
                lua_pushinteger(L, hits[i].tag); lua_setfield(L, -2, "tag");
                lua_pushboolean(L, hits[i].lost); lua_setfield(L, -2, "lost");
                lua_pushnumber(L, hits[i].x); lua_setfield(L, -2, "x");
                lua_pushnumber(L, hits[i].y); lua_setfield(L, -2, "y");
                lua_pushnumber(L, hits[i].z); lua_setfield(L, -2, "z");
                lua_rawseti(L, -2, ++total);
            }
        } while (n == 8);
        return 1;
    }

    static int l_shot_remove(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        lua_pushboolean(L, st.world.shotRemove(DirtySink(c), Int(L, 1)));
        return 1;
    }

    static int l_shot_clear(lua_State* L) {
        LuaEngine* self = Self(L);
        LuaEngine::IsoState& st = St(L);
        Ctx c{self, &st, nullptr, false, 0, 0, 0, 0};
        st.world.shotClear(DirtySink(c));
        return 0;
    }

    static int l_shot_count(lua_State* L) {
        lua_pushinteger(L, St(L).world.shotCount());
        return 1;
    }
};

void LuaEngine::RegisterIsoApi() {
    // スタックのトップは pico テーブル。pico.iso を作ってそこへ並べる
    lua_newtable(L);
    registerFn("create", LuaEngineIso::l_create);
    registerFn("open", LuaEngineIso::l_open);
    registerFn("info", LuaEngineIso::l_info);
    registerFn("save", LuaEngineIso::l_save);
    registerFn("close", LuaEngineIso::l_close);
    registerFn("migrate", LuaEngineIso::l_migrate);
    registerFn("get", LuaEngineIso::l_get);
    registerFn("set", LuaEngineIso::l_set);
    registerFn("first_air", LuaEngineIso::l_first_air);
    registerFn("size", LuaEngineIso::l_size);
    registerFn("pump", LuaEngineIso::l_pump);
    registerFn("pending", LuaEngineIso::l_pending);
    registerFn("stats", LuaEngineIso::l_stats);
    registerFn("set_image", LuaEngineIso::l_set_image);
    registerFn("view", LuaEngineIso::l_view);
    registerFn("origin", LuaEngineIso::l_origin);
    registerFn("cursor", LuaEngineIso::l_cursor);
    registerFn("sky", LuaEngineIso::l_sky);
    registerFn("culling", LuaEngineIso::l_culling);
    registerFn("sunlight", LuaEngineIso::l_sunlight);
    registerFn("light", LuaEngineIso::l_light);
    registerFn("render", LuaEngineIso::l_render);
    registerFn("draw_icon", LuaEngineIso::l_draw_icon);
    registerFn("block_pos", LuaEngineIso::l_block_pos);
    registerFn("pick", LuaEngineIso::l_pick);
    registerFn("dirty_block", LuaEngineIso::l_dirty_block);
    registerFn("dirty_edit", LuaEngineIso::l_dirty_edit);
    registerFn("entity_add", LuaEngineIso::l_entity_add);
    registerFn("entity_set", LuaEngineIso::l_entity_set);
    registerFn("entity_move", LuaEngineIso::l_entity_move);
    registerFn("entity_get", LuaEngineIso::l_entity_get);
    registerFn("entity_pos", LuaEngineIso::l_entity_pos);
    registerFn("entity_remove", LuaEngineIso::l_entity_remove);
    registerFn("entity_clear", LuaEngineIso::l_entity_clear);
    registerFn("entity_at", LuaEngineIso::l_entity_at);
    registerFn("ground", LuaEngineIso::l_ground);
    registerFn("loaded", LuaEngineIso::l_loaded);
    registerFn("to_screen", LuaEngineIso::l_to_screen);
    registerFn("path", LuaEngineIso::l_path);
    registerFn("stand", LuaEngineIso::l_stand);
    registerFn("arena", LuaEngineIso::l_arena);
    registerFn("flow_build", LuaEngineIso::l_flow_build);
    registerFn("flow_step", LuaEngineIso::l_flow_step);
    registerFn("flow_get", LuaEngineIso::l_flow_get);
    registerFn("flow_info", LuaEngineIso::l_flow_info);
    registerFn("flow_clear", LuaEngineIso::l_flow_clear);
    registerFn("keep_all", LuaEngineIso::l_keep_all);
    registerFn("nearby", LuaEngineIso::l_nearby);
    registerFn("crowd", LuaEngineIso::l_crowd);
    registerFn("sight", LuaEngineIso::l_sight);
    registerFn("shot_add", LuaEngineIso::l_shot_add);
    registerFn("shots_step", LuaEngineIso::l_shots_step);
    registerFn("shot_remove", LuaEngineIso::l_shot_remove);
    registerFn("shot_clear", LuaEngineIso::l_shot_clear);
    registerFn("shot_count", LuaEngineIso::l_shot_count);
    lua_setfield(L, -2, "iso");
}
