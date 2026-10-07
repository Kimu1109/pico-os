#pragma once
// 2.5D(斜め上から見た)ボクセルの箱庭のエンジン。Luaアプリ「ブロック」(TheScienceElf/Blocks-TI-84 の移植、MIT)の
// 描画・影・タップ位置の引き当て・ワールド(チャンク)の持ち方・地形の生成・保存を、Lua(view.lua / world.lua)から
// C++へ移したもの。Luaからは pico.iso.*(src/lua/LuaEngine_Iso.cpp)で使う。
//
// 座標: ブロック(x, y, z) の絵(32x31)の左上は 画面の (OX + 16*(x - z), OY - 8*(x + z) - 16*y)。
// u = x - z、s = x + z と書く。描く順(画家のアルゴリズム)は s の大きい(奥の)順、同じ s の中は y の小さい順。
//
// ワールド: 1辺 W = K*8 マス・高さ H=16。8x8 の柱を1つのチャンク(1024バイト、(lx, y, lz) は y*64 + lx*8 + lz)にし、
// 見えている所のまわりのチャンクだけを固定長の置き場(kMaxChunks 個)に読み込む。地形は種と位置だけで決まるので、
// SDへ書くのは書き換えたチャンクだけ。ファイルの形式は Lua 版と同じ(既存のセーブがそのまま読める):
//   <dir>/world.dat = "BLK2" 種類(1) K(2) 種(4) カーソルx(2) y(1) z(2) ブロック(1) の17バイト + 書き出したチャンクの印(K*K ビット)
//   <dir>/c_<cx>_<cz>.dat = チャンクの中身(上の空気だけの段を落としたもの)
//
// 描画・dirty の積み方は Sink(関数ポインタ)越しにするので、このファイルは LovyanGFX を知らない(ホストテストで中身を見られる)。

#include <cstdint>
#include <cstddef>

#include "util/FixedString.hpp"
#include "consts.hpp"

namespace Iso {

constexpr int H = 16;                 // 高さ
constexpr int kLayer = 64;            // 1段(8x8)のバイト数
constexpr int kChunkBytes = H * kLayer;
constexpr int kMaxK = 128;            // 1辺のチャンク数の上限(新しく作るワールドは 128 = 1024マス)
constexpr int kNewK = 128;
constexpr int kMaxChunks = 56;        // 同時に読み込めるチャンクの数(240x204 の表示で最大約50個。1個 約1.1KB)
constexpr int kBlockCount = 24;       // ブロックの種類(水を含む。0=空気)
constexpr int kRowH = 23;             // faces.pimg の1種類ぶんの段の高さ
constexpr int kCursorRow = 24;        // カーソルの段

enum Block : uint8_t {
    AIR = 0, WATER = 1, STONE = 2, GRASS = 3, DIRT = 4, COBBLE = 5, PLANKS = 6, BRICKS = 7,
    SLABS = 8, WOOD = 9, LEAVES = 10, SAND = 11, BOOKS = 12, TNT = 13, CRAFTING = 14,
    FURNACE = 15, JUKEBOX = 16, SPONGE = 17, GRAVEL = 18, MOSS = 19, COAL_ORE = 20,
    IRON_ORE = 21, BEDROCK = 22, IRON = 23, GOLD = 24,
};

enum Kind : uint8_t { NATURAL = 0, FLAT = 1, DEMO = 2, EMPTY = 3 };

// タップした面
enum class Face : uint8_t { None, Top, Left, Right };

struct Chunk {
    int16_t cx = -1, cz = -1;   // cx < 0 なら空き
    uint8_t top = 0;            // 空気でない段の数(一番上の空気でない段 + 1)
    bool dirty = false;         // 書き換えて、まだ書き出していない
    uint8_t col[kLayer];        // 柱ごとの「空気でない一番上の y + 1」(0 = 空気だけ)
    uint8_t b[kChunkBytes];     // ブロック
};

class World {
public:
    // 描き先。draw は faces.pimg の (sx, sy, w, h) を画面の (dx, dy) へ透過つきで写す。
    // dirty は画面の矩形を描き直す予定に入れる(表示範囲で切ってから呼ぶ)
    struct Sink {
        void* ctx = nullptr;
        void (*draw)(void* ctx, int sx, int sy, int w, int h, int dx, int dy) = nullptr;
        void (*dirty)(void* ctx, int x, int y, int w, int h) = nullptr;
    };

    World() = default;
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ---------------- ワールドを開く・作る・保存する ----------------
    // 新しいワールド(dir は既にあるディレクトリ)。始めのカーソルの位置を返す。置き場を確保できなければ false
    bool create(const char* dir, uint8_t kind, uint32_t seed, int k, int& px, int& py, int& pz);
    // 開く。読めなければ false と理由(err)
    bool open(const char* dir, int& px, int& py, int& pz, int& cur, const char*& err);
    // 見出しだけ読む(ワールドを選ぶ画面に出す)
    static bool Info(const char* world_dat, int& kind, int& width);
    // 書き換えたチャンクと見出しを書き出す
    bool save(int px, int py, int pz, int cur);
    // 閉じる(書き出さない。置き場も返す)
    void close();
    // 前の版(48x16x48 を1ファイル、"BLK1")を、チャンクに分けた今の形(K=6)へ移す。成功したら古いファイルを消す
    bool migrate(const char* old_path, const char* dir, const char*& err);
    bool isOpen() const { return pool_ != nullptr; }

    // ---------------- ブロック ----------------
    // 世界の外と読み込んでいないチャンクは空気
    uint8_t get(int x, int y, int z) const {
        if ((unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_ || (unsigned)y >= (unsigned)H) return 0;
        const Chunk* c = find(x >> 3, z >> 3);
        return c ? c->b[y * kLayer + (x & 7) * 8 + (z & 7)] : 0;
    }
    // 書き換える(読み込んでいなければその場で読み込む)
    void set(int x, int y, int z, uint8_t b);
    // 下から見て最初の空気のマス
    int firstAir(int x, int z);
    int width() const { return W_; }
    int k() const { return K_; }
    uint8_t kind() const { return kind_; }

    // ---------------- チャンクの読み込み ----------------
    // 視点(origin)が変わっていれば読み込む範囲を決め直し、予定のチャンクを最大 max 個読み込む。
    // ms(0 なら無制限)を過ぎたら途中でやめる。読み込んだ数を返す
    int pump(int max, uint32_t ms);
    int pending() const { return qlen_ - qpos_; }
    int loadedCount() const;
    // 読み込む(書き出したものはファイルから、無ければ生成)。置き場が足りなければ nullptr
    Chunk* loadChunk(int cx, int cz);
    const Chunk* find(int cx, int cz) const {
        const uint8_t s = map_[((cx & 31) << 5) | (cz & 31)];
        if (!s) return nullptr;
        const Chunk* c = &pool_[s - 1];
        return (c->cx == cx && c->cz == cz) ? c : nullptr;
    }
    // u = x - z、s = x + z の範囲にかかるチャンクを読み込む予定に入れ、それより1チャンクより外は手放す
    void window(int umin, int umax, int smin, int smax);
    // 種類・種・位置だけで決まるチャンクの中身(テスト・作り直しに使う)。out は kChunkBytes
    void generate(int cx, int cz, uint8_t* out) const;
    int height(int x, int z) const;
    uint32_t hash(int32_t a, int32_t b, int32_t c) const;

    // ---------------- 表示 ----------------
    void setView(int x, int y, int w, int h);
    void setOrigin(int ox, int oy);
    int originX() const { return OX_; }
    int originY() const { return OY_; }
    void setCursor(int x, int y, int z, bool show) { cx_ = x; cy_ = y; cz_ = z; show_cursor_ = show; }
    void setCulling(bool on) { cull_ = on; }   // 手前のブロックに完全に隠れるブロックを描かない(既定 true)
    // 絵に透けた所が無い(手前に置くと後ろを完全に隠す)ブロックの印(ビット = ブロック番号)。
    // 既定は水と葉(穴の開いた絵)以外。ComputeOccluders() で faces.pimg から求め直せる
    void setOccluders(uint32_t mask) { occluders_ = mask; }
    uint32_t occluders() const { return occluders_; }
    // faces.pimg の各ブロックの日なたの3面が、絵(32x31)の六角形を隙間なく埋めるかを調べる。
    // px(ctx, x, y) は画像の画素(0 = 透過)
    static uint32_t ComputeOccluders(int (*px)(void* ctx, int x, int y), void* ctx);
    void blockPos(int x, int y, int z, int& px, int& py) const {
        px = OX_ + 16 * (x - z);
        py = OY_ - 8 * (x + z) - 16 * y;
    }
    // 画面の矩形 [x0, x1) x [y0, y1) にかかるブロックを描く(空の塗りは呼び出し側)
    void render(const Sink& sink, int x0, int y0, int x1, int y1);
    // 1個のブロックをまるごと(日なたの3面)描く
    static void DrawIcon(const Sink& sink, int id, int px, int py);
    // 画面の点に見えている一番手前のブロックと面
    Face pick(int px, int py, int& x, int& y, int& z) const;
    // 描き直す範囲: カーソル1マス / 置いた・壊したとき(そのブロックと、影が変わりうる面)
    void dirtyBlock(const Sink& sink, int x, int y, int z) const;
    void dirtyEdit(const Sink& sink, int x, int y, int z) const;
    // 読み込む範囲(表示範囲に、影をたどる分を左へ足したもの)
    void loadRange(int& umin, int& umax, int& smin, int& smax) const;

    // 影(テスト用に公開)。上面: 奥半分・手前半分 / 左面: 上半分・下半分がそれぞれ影なら true
    void topShadow(int x, int y, int z, int top, bool& far, bool& near) const;
    void leftShadow(int x, int y, int z, int top, bool& up, bool& low) const;
    // 読み込んだチャンクの中で、空気でない一番上の高さ(無ければ -1)
    int topAll() const;
    // 直前の render で描いた面の数(計測用)
    int lastFaces() const { return last_faces_; }

private:
    bool begin(const char* dir, uint8_t kind, int k, uint32_t seed);
    Chunk* findMut(int cx, int cz) { return const_cast<Chunk*>(find(cx, cz)); }
    Chunk* allocSlot(int cx, int cz);
    void unload(Chunk* c);
    bool writeChunk(Chunk* c);
    bool readChunkFile(int cx, int cz, uint8_t* out);
    static void Finish(Chunk* c);           // top と col を数え直す
    void chunkPath(int cx, int cz, FixedString<PICO_PATH_LEN>& out) const;
    bool isSaved(int n) const { return (saved_[n >> 3] >> (n & 7)) & 1; }
    void markSaved(int n) { saved_[n >> 3] |= (uint8_t)(1u << (n & 7)); }
    bool inWindow(int cx, int cz, int margin) const;

    Chunk* pool_ = nullptr;               // kMaxChunks 個。ワールドを開いている間だけ確保する
    uint8_t map_[32 * 32] = {};           // (cx & 31, cz & 31) → 置き場の番号 + 1(0 = 無し)
    uint8_t saved_[(kMaxK * kMaxK + 7) / 8] = {};
    FixedString<PICO_PATH_LEN> dir_;
    int K_ = 6, W_ = 48;
    uint8_t kind_ = EMPTY;
    uint32_t seed_ = 0;

    // 読み込む予定(近い順)
    static constexpr int kMaxQueue = 128;
    int32_t queue_[kMaxQueue];
    int32_t qdist_[kMaxQueue];                 // 並べ替えに使う距離(スタックに置かない)
    int qlen_ = 0, qpos_ = 0;
    int wa0_ = 1, wa1_ = 0, wb0_ = 1, wb1_ = 0;   // 今の範囲(チャンクの a = cx - cz、b = cx + cz)
    bool window_stale_ = true;
    bool warned_full_ = false;

    // 表示
    int vx_ = 0, vy_ = 20, vw_ = 240, vh_ = 204;
    int OX_ = 0, OY_ = 0;
    int cx_ = 0, cy_ = 0, cz_ = 0;
    bool show_cursor_ = true;
    bool cull_ = true;
    uint32_t occluders_ = DefaultOccluders();
    int last_faces_ = 0;

    static constexpr uint32_t DefaultOccluders() {
        return ((1u << (kBlockCount + 1)) - 1) & ~((1u << AIR) | (1u << WATER) | (1u << LEAVES));
    }
};

}  // namespace Iso
