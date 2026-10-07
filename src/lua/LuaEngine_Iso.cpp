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
#include <cstring>
#include <new>

#include "iso/Iso_World.hpp"
#include "iso/Iso_Blit.hpp"
#include "gui/Fill4bpp.hpp"
#include "gui/icons/icon_render.h"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

struct LuaEngine::IsoState {
    Iso::World world;
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
    static LuaEngine::ImageSlot* Image(LuaEngine* self, LuaEngine::IsoState& st) {
        size_t index;
        if (!st.image || !self->ResolveImageHandle(st.image, index)) return nullptr;
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

    // 遅い道(画面か画像が 4bpp でないとき): pico.draw_image_part と同じく、クリップを狭めて画像全体をずらして描く
    struct SlowCtx {
        LuaEngine::ImageSlot* slot;
        int cx0, cy0, cx1, cy1;
    };
    static void SlowDraw(void* p, int sx, int sy, int w, int h, int dx, int dy) {
        SlowCtx* c = static_cast<SlowCtx*>(p);
        const int x0 = std::max(dx, c->cx0), y0 = std::max(dy, c->cy0);
        const int x1 = std::min(dx + w, c->cx1), y1 = std::min(dy + h, c->cy1);
        if (x0 >= x1 || y0 >= y1) return;
        int32_t kx, ky, kw, kh;
        OSData::frame->getClipRect(&kx, &ky, &kw, &kh);
        OSData::frame->setClipRect(x0, y0, x1 - x0, y1 - y0);
        IconRender::DrawPimgSprite(c->slot->sprite, dx - sx, dy - sy);
        OSData::frame->setClipRect(kx, ky, kw, kh);
    }
    // 遅い道のディザ: 1画素ずつ読んで、模様の画素だけ書く
    static void SlowDither(void* p, int sx, int sy, int w, int h, int dx, int dy, int level) {
        SlowCtx* c = static_cast<SlowCtx*>(p);
        LGFX_Sprite& src = c->slot->sprite.sprite;
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
    static void FastDraw(void* p, int sx, int sy, int w, int h, int dx, int dy) {
        static_cast<const Iso::FaceBlitter*>(p)->draw(sx, sy, w, h, dx, dy);
    }
    static void FastDither(void* p, int sx, int sy, int w, int h, int dx, int dy, int level) {
        static_cast<const Iso::FaceBlitter*>(p)->dither(sx, sy, w, h, dx, dy, level);
    }
    static void Dirty(void*, int x, int y, int w, int h) { MarkDirty(x, y, w, h); }

    // クリップ [x0, x1) x [y0, y1) の中へ描く準備をして fn を呼ぶ
    template <class F>
    static void WithSink(LuaEngine* self, LuaEngine::IsoState& st, int x0, int y0, int x1, int y1, F&& fn) {
        LuaEngine::ImageSlot* slot = Image(self, st);
        if (!slot || x0 >= x1 || y0 >= y1) return;
        Analyze(st, *slot);
        LGFX_Sprite& frame = *OSData::frame;
        if (Is4bpp(frame) && Is4bpp(slot->sprite.sprite)) {
            const int fw = frame.width(), fh = frame.height();
            st.blit.setTarget(static_cast<uint8_t*>(frame.getBuffer()), ((fw + 1) & ~1) >> 1,
                              std::max(x0, 0), std::max(y0, 0), std::min(x1, fw), std::min(y1, fh));
            Iso::World::Sink sink{&st.blit, FastDraw, Dirty, FastDither};
            fn(sink);
        } else {
            SlowCtx c{slot, x0, y0, x1, y1};
            Iso::World::Sink sink{&c, SlowDraw, Dirty, SlowDither};
            fn(sink);
        }
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
        if (kind < 0 || kind > Iso::EMPTY) return luaL_error(L, "pico.iso.create: kind は 0〜3 です");
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
        st.image = (uint32_t)luaL_checkinteger(L, 1);
        st.analyzed = nullptr;
        LuaEngine::ImageSlot* slot = Image(self, st);
        if (!slot) return luaL_error(L, "pico.iso.set_image: 無効なイメージハンドル");
        if (slot->sprite.width < Iso::kSheetW || slot->sprite.height < (Iso::kCursorRow + 1) * Iso::kRowH) {
            return luaL_error(L, "pico.iso.set_image: 面の絵の大きさが違います(%dx%d 以上)", Iso::kSheetW,
                              (Iso::kCursorRow + 1) * Iso::kRowH);
        }
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
    lua_setfield(L, -2, "iso");
}
