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
// 光源(松明): 松明のマスを明るさ kTorchLight とし、光を通すマス(空気・水・葉・松明)を1マスごとに1ずつ暗くしながら
// 広げる(マインクラフトと同じ考え方。透けないブロックは光を通さないので、壁の裏は暗い)。明るさは4段階
// (0=届かない / 1・2=ディザで半分ほど / 3=日なた と同じ)に丸めて、チャンクごとに1マス2bitで持つ(256バイト)。
// 面の明るさは、その面が向いている隣のマス(上面なら真上)の明るさ。保存はしない(読み込むときに計算する)。
// 夜(setSunlight(false))は日の当たる面も影の絵で描き、松明の光が届く所だけ明るく見える。
//
// 人や物(エンティティ): 箱庭の中の好きな位置(小数でよい)に、画像を立てて置ける。足元の点と「当たりの箱」
// (足元を中心にした半径 r の正方形 × 高さ h)を持ち、ブロックとの前後は箱どうしで決める:
//   ブロックを全部描いた後、エンティティを奥から順に描き、そのたびに「その箱より手前のブロック」だけを、
//   エンティティの絵の不透明な画素の上にだけ描き直す(画家のアルゴリズムの途中へ挟んだのと同じ結果になる。
//   壁の裏に回ると隠れ、柱の手前に出ると柱を隠す。水の中では水面が手前に来て沈んで見える)。
//   足元の下の地面には丸い影(市松模様)を落とす。影も地面の高さで同じように前後を決める。高く上がるほど小さくなる。
// 前後の決め方: 箱とブロックが x・z・y のどれかで離れていれば、見る人の側(-x・-z・+y)にある方が手前。
// 絵が箱からはみ出す所(箱より幅の広い絵)は近似になる。
//
// タワーディフェンス向けの道具(2026-10-08):
//   ・人や物の上の HPバー(bar)と選択の印(mark)は、ブロックにも他の人や物にも隠れず一番上に描く
//   ・弾(Shot): エンティティではない軽い点。狙った人や物(または点)へ必ず当たるまで飛び、当たったら知らせる
//   ・押し合い(crowdStep): crowd を持つ人や物どうしが重ならないよう水平に押し合う(fixed は動かない)
//   ・近くの人や物(nearby)、ブロックに遮られない視線(lineOfSight)、世界全体を読み込んだままにする(setKeepAll)
//
// 描画・dirty の積み方は Sink(関数ポインタ)越しにするので、このファイルは LovyanGFX を知らない(ホストテストで中身を見られる)。

#include <cstdint>
#include <cstddef>
#include <cmath>

#include "util/FixedString.hpp"
#include "consts.hpp"

namespace Iso {

constexpr int H = 16;                 // 高さ
constexpr int kLayer = 64;            // 1段(8x8)のバイト数
constexpr int kChunkBytes = H * kLayer;
constexpr int kMaxK = 128;            // 1辺のチャンク数の上限(新しく作るワールドは 128 = 1024マス)
constexpr int kNewK = 128;
constexpr int kMaxChunks = 56;        // 同時に読み込めるチャンクの数(240x204 の表示で最大約50個。1個 約1.1KB)
constexpr int kBlockCount = 25;       // ブロックの種類(水・松明を含む。0=空気)
constexpr int kRowH = 23;             // faces.pimg の1種類ぶんの段の高さ
constexpr int kCursorRow = 25;        // カーソルの段
constexpr int kSheetW = 224;          // faces.pimg の幅(段の中に 上4・左4・右2 の10枚)
constexpr int kTorchLight = 7;        // 松明のマスの明るさ(1マスごとに1減る。6マス先まで届く)
constexpr int kLightReach = kTorchLight - 1;
constexpr int kMaxEntities = 96;      // 同時に置ける人や物の数(ハンドルの下位8bitが番号+1なので255まで)
constexpr int kEntityChunk = 16;      // 人や物の置き場は、置いた数に合わせてこの数ずつ広げる(使わないアプリに RAM を負担させない)
constexpr int kMaxShots = 64;         // 同時に飛ばせる弾の数(置き場は最初に弾を撃ったときに確保する)

enum Block : uint8_t {
    AIR = 0, WATER = 1, STONE = 2, GRASS = 3, DIRT = 4, COBBLE = 5, PLANKS = 6, BRICKS = 7,
    SLABS = 8, WOOD = 9, LEAVES = 10, SAND = 11, BOOKS = 12, TNT = 13, CRAFTING = 14,
    FURNACE = 15, JUKEBOX = 16, SPONGE = 17, GRAVEL = 18, MOSS = 19, COAL_ORE = 20,
    IRON_ORE = 21, BEDROCK = 22, IRON = 23, GOLD = 24, TORCH = 25,
};

enum Kind : uint8_t { NATURAL = 0, FLAT = 1, DEMO = 2, EMPTY = 3 };

// タップした面
enum class Face : uint8_t { None, Top, Left, Right };

struct Chunk {
    int16_t cx = -1, cz = -1;   // cx < 0 なら空き
    uint8_t top = 0;            // 空気でない段の数(一番上の空気でない段 + 1)
    bool dirty = false;         // 書き換えて、まだ書き出していない
    uint8_t torches = 0;        // 松明の数(255で頭打ち。0 なら光の計算を飛ばせる)
    uint8_t col[kLayer];        // 柱ごとの「空気でない一番上の y + 1」(0 = 空気だけ)
    uint8_t b[kChunkBytes];     // ブロック
    uint8_t light[kChunkBytes / 4];   // 松明の明るさ(1マス2bit、0〜3)。i = y*64 + lx*8 + lz の (i&3)*2 bit目から
};

// 人や物。位置はブロックの単位(小数)で、(x, y, z) は足元の中心(y は足の裏の高さ)。
// 点 (X, Y, Z) は画面の (OX + 16 + 16*(X - Z), OY + 32 - 8*(X + Z) - 16*Y) に写る(ブロック (x,y,z) の上面の中心は
// (x+0.5, y+1, z+0.5))。絵は「足元の点 (ax, ay)」がそこへ来るように置く
struct Entity {
    bool used = false;
    bool visible = true;
    bool shadow = true;          // 地面に影を落とす
    bool flip = false;           // 左右反転
    uint8_t shadow_color = 0;
    uint16_t gen = 0;
    float x = 0, y = 0, z = 0;
    float r = 0.25f, h = 1.5f;   // 当たりの箱(前後を決める): 足元を中心にした半径 r の正方形 × 高さ h
    int32_t image = 0;           // 絵(呼び出し側の画像のハンドル。World は中身を知らず Sink::image_px で読む)
    int16_t sx = 0, sy = 0, sw = 0, sh = 0;   // 画像の中の範囲
    int16_t ax = 0, ay = 0;      // 足元の点(範囲の左上から)
    int16_t lx = 0, ly = 0, lw = 0, lh = 0;   // 最後に描き直しを頼んだ画面の範囲(動いたときに消す所)
    // 一番上に描く飾り(ブロックに隠れない)
    int8_t bar = -1;             // HPバー(0〜100%。負なら出さない)
    uint8_t bar_color = 11;
    int8_t mark = -1;            // 選択の印(頭の上の小さな三角)の色。負なら出さない
    // 分類と押し合い
    uint8_t tag = 0;             // 分類(0〜31。nearby で絞る)
    uint8_t crowd = 0;           // 押し合い: 0 = しない / 1 = 押し合って動く / 2 = 動かない(押し返すだけ)
    float mass = 1.0f;           // 押し合いの重さ(重いほど動かない)
};

// 弾。位置はブロックの単位。狙った人や物(target)か点(tx,ty,tz)へ、出発点からの割合 p で進む(必ず当たる)。
// 狙った人や物が消えたら、最後に見た位置へ飛んで終わる
struct Shot {
    bool used = false;
    uint16_t gen = 0;
    float x = 0, y = 0, z = 0;        // 今の位置
    float ox = 0, oy = 0, oz = 0;     // 出発点
    float tx = 0, ty = 0, tz = 0;     // 狙う点(人や物なら毎回の位置の中心へ更新)
    int32_t target = 0;               // 狙う人や物のハンドル(0 = 点)
    float speed = 8.0f;               // ブロック/秒
    float arc = 0.0f;                 // 山なりの高さ(距離に対する割合)
    float p = 0.0f;                   // 進んだ割合(0〜1)
    uint8_t color = 0, size = 2;
    int32_t tag = 0;                  // 呼び出し側の印(当たったときに返す)
    int16_t lx = 0, ly = 0, lw = 0, lh = 0;
};

struct ShotHit {
    int32_t shot;      // 弾のハンドル(当たった時点で消えている)
    int32_t target;    // 狙った人や物(0 = 点、または当たる前に消えていた)
    int32_t tag;
    bool lost;         // 狙った人や物が途中で消えていた
    float x, y, z;
};

// 箱(前後の判定に使う)。[x0, x1] x [y0, y1] x [z0, z1]
struct Box {
    float x0, y0, z0, x1, y1, z1;
};

inline int LightOf(const Chunk* c, int i) { return c ? (c->light[i >> 2] >> ((i & 3) * 2)) & 3 : 0; }

class World {
public:
    // 描き先。draw は faces.pimg の (sx, sy, w, h) を画面の (dx, dy) へ透過つきで写す。
    // dirty は画面の矩形を描き直す予定に入れる(表示範囲で切ってから呼ぶ)。
    // dither は draw と同じだが、絵の左上からの (x, y) が DitherOn(level, x, y) の画素だけを写す(松明の光の
    // 中間の明るさを、影の絵の上に日なたの絵を重ねて見せる)。nullptr なら level 2 以上を draw で描く
    struct Sink {
        void* ctx = nullptr;
        void (*draw)(void* ctx, int sx, int sy, int w, int h, int dx, int dy) = nullptr;
        void (*dirty)(void* ctx, int x, int y, int w, int h) = nullptr;
        void (*dither)(void* ctx, int sx, int sy, int w, int h, int dx, int dy, int level) = nullptr;
        // 人や物を描くのに使う(どれかが nullptr なら人や物は描かない):
        // face_px は faces.pimg の画素(0 = 透過)、image_px は人や物の絵 image の画素(透過・範囲外は -1)、
        // put は画面の1画素(クリップは World が済ませる)
        int (*face_px)(void* ctx, int sx, int sy) = nullptr;
        int (*image_px)(void* ctx, int32_t image, int x, int y) = nullptr;
        void (*put)(void* ctx, int x, int y, int c) = nullptr;
    };
    // ディザの模様: level 1 は4画素に1つ、level 2 は市松模様、3 以上は全部
    static bool DitherOn(int level, int x, int y) {
        return level >= 3 || (level == 2 ? ((x + y) & 1) == 0 : ((x | y) & 1) == 0);
    }

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
    // ワールドを開いている間に確保するバイト数(チャンクの置き場 + 明るさの計算の作業場所)
    static constexpr size_t PoolBytes() { return sizeof(Chunk) * kMaxChunks + kScratchBytes; }

    // ---------------- ブロック ----------------
    // 松明の明るさ(0〜3)。世界の外と読み込んでいないチャンクは 0
    int light(int x, int y, int z) const {
        if ((unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_ || (unsigned)y >= (unsigned)H) return 0;
        return LightOf(find(x >> 3, z >> 3), y * kLayer + (x & 7) * 8 + (z & 7));
    }
    // 光を通さないブロック(透けない絵のブロック。水・葉・松明は通す)
    bool blocksLight(uint8_t b) const { return (occluders_ >> b) & 1; }
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
    // 日の光(false = 夜。どの面も影の絵で描き、松明の光だけで明るくなる)
    void setSunlight(bool on) { sun_ = on; }
    bool sunlight() const { return sun_; }
    // 絵に透けた所が無い(手前に置くと後ろを完全に隠す)ブロックの印(ビット = ブロック番号)。
    // 既定は水と葉と松明(穴の開いた絵)以外。ComputeOccluders() で faces.pimg から求め直せる。
    // 光の通り方もこれで決まるので、変わったら読み込んでいるチャンクの明るさを計算し直す
    void setOccluders(uint32_t mask);
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
    // 1個のブロックをまるごと(日なたの3面。松明は松明の絵)描く
    static void DrawIcon(const Sink& sink, int id, int px, int py);
    // 画面の点に見えている一番手前のブロックと面
    Face pick(int px, int py, int& x, int& y, int& z) const;
    // 描き直す範囲: カーソル1マス / 置いた・壊したとき(そのブロックと、影が変わりうる面)
    void dirtyBlock(const Sink& sink, int x, int y, int z) const;
    // dirtyEdit は、直前の set() で松明の明るさが変わった範囲もまとめて描き直す
    void dirtyEdit(const Sink& sink, int x, int y, int z);
    // 読み込む範囲(表示範囲に、影をたどる分を左へ足したもの)
    void loadRange(int& umin, int& umax, int& smin, int& smax) const;

    // 影(テスト用に公開)。上面: 奥半分・手前半分 / 左面: 上半分・下半分がそれぞれ影なら true
    void topShadow(int x, int y, int z, int top, bool& far, bool& near) const;
    void leftShadow(int x, int y, int z, int top, bool& up, bool& low) const;
    // 読み込んだチャンクの中で、空気でない一番上の高さ(無ければ -1)
    int topAll() const;

    // ---------------- 人や物(エンティティ) ----------------
    // 置く。e の used/gen/lx.. は無視する。ハンドル(0 = 置き場が満杯)を返す。描き直しは sink.dirty で頼む
    int entityAdd(const Sink& sink, const Entity& e);
    // ハンドルの指すもの(無効なら nullptr)。書き換えたら entityChanged() を呼ぶ
    Entity* entity(int handle);
    const Entity* entity(int handle) const { return const_cast<World*>(this)->entity(handle); }
    // 位置や絵を変えた後に呼ぶ: 前に描いた所と今の所を描き直す
    void entityChanged(const Sink& sink, int handle);
    bool entityRemove(const Sink& sink, int handle);
    void entityClear(const Sink& sink);
    int entityCount() const;
    int entityCapacity() const { return ent_cap_; }   // 今確保している置き場の数(kMaxEntities まで広がる)
    // 画面の点に見えている一番手前の人や物(絵の不透明な画素で判定。ブロックに隠れているかは見ない)。無ければ 0
    int entityAt(const Sink& sink, int px, int py) const;
    // 画面の範囲(絵と影を合わせたもの)。見えなければ false
    bool entityRect(const Entity& e, int& x0, int& y0, int& x1, int& y1) const;
    // (x, z) の柱で、上面が高さ y(足の裏)以下にある一番上のブロックの上面の y(= 地面の高さ)。無ければ -1
    // (水も地面。松明は地面にならない。水の中に立っているなら水の底)
    int ground(float x, float y, float z) const;
    // 点 (x, z) の柱のチャンクを読み込んでいるか(世界の外は false)。読み込んでいない所は空気に見えるので、
    // 人や物を動かすときはここで止める(落ちて消えないように)
    bool loadedAt(float x, float z) const {
        const int ix = (int)floorf(x), iz = (int)floorf(z);
        if (ix < 0 || iz < 0 || ix >= W_ || iz >= W_ || !pool_) return false;
        return find(ix >> 3, iz >> 3) != nullptr;
    }
    // 点 (X, Y, Z) の画面の位置
    void project(float x, float y, float z, float& sx, float& sy) const {
        sx = (float)OX_ + 16.0f + 16.0f * (x - z);
        sy = (float)OY_ + 32.0f - 8.0f * (x + z) - 16.0f * y;
    }
    // ブロック (bx, by, bz)(中身 b)が箱 e より手前か(テスト用に公開)
    static bool InFront(const Box& e, int bx, int by, int bz, uint8_t b);
    // ブロックと箱が離れているか(重なっていなければ true)
    static bool Apart(const Box& e, int bx, int by, int bz);
    // 地面の影の形(中心と横・縦の半径)。影が無ければ false
    bool shadowShape(const Entity& e, float& cx, float& cy, float& a, float& b, int& gy) const;
    // 直前の render で描いた面の数(計測用)
    int lastFaces() const { return last_faces_; }

    // ---------------- タワーディフェンス向けの道具 ----------------
    // 中心 (x, z) から水平距離 range 以内の人や物(tagmask のビットの tag のもの。0 なら全部)を近い順に out へ。数を返す
    int nearby(float x, float z, float range, uint32_t tagmask, int32_t* out, int max) const;
    // 押し合い。crowd を持つ人や物どうしを、水平に重ならないよう押し離す(高さが height 以上違えば押さない)。
    // 押された先の体のマス(足の裏から height マス)に、空気・水・松明・pass 以外のブロックがあれば、その軸は動かさない。
    // 動いたものは描き直しを頼む。動いた数を返す
    int crowdStep(const Sink& sink, int iterations, int height, uint32_t pass);
    // (x0,y0,z0) から (x1,y1,z1) への線分がブロックに遮られないか。空気・水・松明・pass のブロックは通す。
    // 出発点と到着点のマスは見ない(弓兵の立っているタワー・狙う相手の足元)
    bool lineOfSight(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t pass) const;
    // 弾。target(0 なら点 tx,ty,tz)へ。ハンドル(0 = 満杯)
    int shotAdd(const Sink& sink, const Shot& s);
    Shot* shot(int handle);
    bool shotRemove(const Sink& sink, int handle);
    void shotClear(const Sink& sink);
    int shotCount() const;
    // dt 秒進め、着いた弾を消して out へ(最大 max 個。溢れた分は次の呼び出しで返す)。数を返す
    int shotsStep(const Sink& sink, float dt, ShotHit* out, int max);
    // 世界全体のチャンクを読み込んだままにする(視点が変わっても手放さない)。K*K が kMaxChunks を超えれば false
    bool setKeepAll(bool on);
    bool keepAll() const { return keep_all_; }
    // (cx, cz) のチャンクの明るさを計算し直す(まわり kLightReach マスの松明から)。変わった範囲を覚えるなら track
    void relight(int cx, int cz, bool track);
    // 読み込んでいるチャンクを全部計算し直す
    void relightAll();
    // 前の dirtyEdit() から、set() で明るさが変わった範囲(マス)。無ければ false
    bool lightChanged(int& x0, int& y0, int& z0, int& x1, int& y1, int& z1) const {
        if (!ld_valid_) return false;
        x0 = ld_[0]; y0 = ld_[1]; z0 = ld_[2]; x1 = ld_[3]; y1 = ld_[4]; z1 = ld_[5];
        return true;
    }

private:
    bool begin(const char* dir, uint8_t kind, int k, uint32_t seed);
    Chunk* findMut(int cx, int cz) { return const_cast<Chunk*>(find(cx, cz)); }
    Chunk* allocSlot(int cx, int cz);
    void unload(Chunk* c);
    bool writeChunk(Chunk* c);
    bool readChunkFile(int cx, int cz, uint8_t* out);
    static void Finish(Chunk* c);           // top と col と松明の数を数え直す
    static void DrawLit(const Sink& sink, int sx, int lit, int q, int sy, int w, int h, int dx, int dy);
    void chunkPath(int cx, int cz, FixedString<PICO_PATH_LEN>& out) const;
    bool isSaved(int n) const { return (saved_[n >> 3] >> (n & 7)) & 1; }
    void markSaved(int n) { saved_[n >> 3] |= (uint8_t)(1u << (n & 7)); }
    bool inWindow(int cx, int cz, int margin) const;
    static void CountTorches(Chunk* c);
    void relightAround(int cx, int cz, bool track);   // そのチャンクと、まわり8つの読み込んでいるもの
    // ブロックを描く(front があれば、その箱より手前のブロックだけ)。cut があれば、箱と重なった水の上面を描く間だけ
    // そこへ「この行より下(手前)だけ描く」画面の y を入れる(水面のうち人や物より奥の所は人や物を隠さない)
    void renderBlocks(const Sink& sink, int x0, int y0, int x1, int y1, const Box* front, int* cut = nullptr);
    void renderEntities(const Sink& sink, int x0, int y0, int x1, int y1);
    void renderShots(const Sink& sink, int x0, int y0, int x1, int y1) const;
    void renderOverlays(const Sink& sink, int x0, int y0, int x1, int y1) const;
    bool overlayRect(const Entity& e, int& x0, int& y0, int& x1, int& y1) const;
    bool shotRect(const Shot& s, int& x0, int& y0, int& x1, int& y1) const;
    void shotAim(Shot& s) const;           // 狙う点を今の人や物の位置へ
    void shotPlace(Shot& s) const;         // p から今の位置を決める
    void shotChanged(const Sink& sink, Shot& s);
    bool bodyFree(float x, float y, float z, int height, uint32_t pass) const;
    void drawCursor(const Sink& sink, int x0, int y0, int x1, int y1) const;
    void entityScreen(const Entity& e, int& dx, int& dy) const;
    void markRect(const Sink& sink, int x0, int y0, int x1, int y1) const;
    void refreshEntityRects();   // 視点・表示範囲が変わったとき(呼び出し側が全体を描き直す)
    bool torchNear(int x, int y, int z) const;          // kLightReach 歩(マンハッタン距離)以内に松明があるか

    Chunk* pool_ = nullptr;               // kMaxChunks 個。ワールドを開いている間だけ確保する
    // 明るさの計算の作業場所(チャンクのまわり kLightReach マスまで: 20x20x16)。pool_ と一緒に確保する
    static constexpr int kLightSpan = 8 + 2 * kLightReach;
    static constexpr int kScratchBytes = kLightSpan * kLightSpan * H;
    uint8_t* scratch_ = nullptr;
    int ld_[6] = {};                      // 明るさが変わった範囲(lightChanged)
    bool ld_valid_ = false;
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
    bool keep_all_ = false;

    // 表示
    int vx_ = 0, vy_ = 20, vw_ = 240, vh_ = 204;
    int OX_ = 0, OY_ = 0;
    int cx_ = 0, cy_ = 0, cz_ = 0;
    bool show_cursor_ = true;
    bool cull_ = true;
    bool sun_ = true;
    uint32_t occluders_ = DefaultOccluders();
    int last_faces_ = 0;
    // 人や物と弾の置き場はヒープ(固定長で持つと World が約15KBになり、実機でチャンクの置き場を確保できなかった)。
    // 人や物は置いた数に合わせて kEntityChunk ずつ広げ、閉じる(close)まで縮めない。弾は最初の shotAdd で kMaxShots 個。
    // 番号(ハンドルの下位8bit)は広げても変わらない。Entity* は広げたとき(entityAdd)に無効になる
    Entity* ents_ = nullptr;
    int ent_cap_ = 0;
    // 並べ替え・近い順・押し合いの作業場所(人や物と同じ数。スタックに置かない)
    struct EntScratch { float key, ox, oz; int16_t order, idx; };
    mutable EntScratch* escr_ = nullptr;
    Shot* shots_ = nullptr;
    bool growEntities();
    bool ensureShots();
    void freeEntities();
    int sortEntities() const;   // 奥から順に escr_[k].order へ。数を返す

    static constexpr uint32_t DefaultOccluders() {
        return ((1u << (kBlockCount + 1)) - 1) & ~((1u << AIR) | (1u << WATER) | (1u << LEAVES) | (1u << TORCH));
    }
};

}  // namespace Iso
