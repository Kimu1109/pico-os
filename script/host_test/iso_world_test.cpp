// 2.5Dの箱庭のエンジン(src/iso/Iso_World・Iso_Blit)のテスト。Luaアプリ「ブロック」の view.lua / world.lua を
// C++へ移したもの:
//   地形の生成がLua版と同じ(Lua版で作ったチャンクのハッシュと比べる。既存のセーブの書き換えていないチャンクを
//     種から作り直すので、1バイトでも違うと地形が変わる)
//   ブロックの読み書き・チャンクの境目・世界の外、読み込みの範囲と手放し・置き場が足りないとき
//   保存と読み込みの往復・壊れたファイル・前の版(BLK1)からの移し替え
//   描画: 見えない面を描かない・影(三角形2つ)・水・描く範囲の絞り込み、Lua版と同じ描画の並び
//   隠れたブロックを描かない(省略しても画素が1つも変わらないこと。乱数のワールドで突き合わせる)
//   タップ位置の引き当て・描き直す範囲
//   面の写し方(FaceBlitter)が素朴な1画素ずつの写し方と同じになること(ディザも)
//   人や物(エンティティ): ブロックとの前後(セルに収まる人や物を画家の順の途中へ挟んだ答えと画素が同じ・乱数のワールド)、
//     水に沈む・地面の影(形・壁の裏で隠れる・高さで小さく)・描き直す範囲・引き当て・ハンドル・反転
//   松明の光: 広がり方・壁で遮られる・取ると消える・チャンクの境目と後から読み込んだチャンク・描く絵(明るさ・夜)・
//     描き直す範囲・影を落とさない・保存して開き直しても同じ
// 面の絵はリポジトリの pc/sdcard/lua/apps/ブロック/faces.pimg を使う(第1引数にリポジトリのルート)。
#include "iso/Iso_World.hpp"
#include "iso/Iso_Blit.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

void LogFunctions::Log(LogType, const char*, ...) {}

static int failures = 0;
static void check(bool c, const char* msg) {
    printf("%s %s\n", c ? "[ OK ]" : "[FAIL]", msg);
    if (!c) failures++;
}

using namespace Iso;

// ---- 面の絵 ----
static int SW = 0, SH = 0;
static std::vector<uint8_t> sheet;     // 1画素1バイト
static std::vector<uint8_t> sheet4;    // 4bpp
static int SheetPx(void*, int x, int y) { return sheet[(size_t)y * SW + x]; }

// ---- 描画の記録と、1画素1バイトの画面 ----
struct Rec {
    std::vector<std::string> lines;
    std::vector<std::string> dithers;
    bool raster = false;
    int cx0 = 0, cy0 = 0, cx1 = 240, cy1 = 320;
};
static uint8_t fb[320][240];
static std::vector<std::string> dirties;
static void Draw(void* p, int sx, int sy, int w, int h, int dx, int dy) {
    Rec* r = static_cast<Rec*>(p);
    char b[64];
    snprintf(b, sizeof b, "%d %d %d %d %d %d", sx, sy, w, h, dx, dy);
    r->lines.push_back(b);
    if (!r->raster) return;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int X = dx + x, Y = dy + y;
            if (X < r->cx0 || X >= r->cx1 || Y < r->cy0 || Y >= r->cy1) continue;
            const uint8_t c = sheet[(size_t)(sy + y) * SW + sx + x];
            if (c) fb[Y][X] = c;
        }
    }
}
static void Dither(void* p, int sx, int sy, int w, int h, int dx, int dy, int level) {
    Rec* r = static_cast<Rec*>(p);
    char b[64];
    snprintf(b, sizeof b, "%d %d %d %d %d %d %d", sx, sy, w, h, dx, dy, level);
    r->dithers.push_back(b);
    if (!r->raster) return;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int X = dx + x, Y = dy + y;
            if (X < r->cx0 || X >= r->cx1 || Y < r->cy0 || Y >= r->cy1 || !World::DitherOn(level, x, y)) continue;
            const uint8_t c = sheet[(size_t)(sy + y) * SW + sx + x];
            if (c) fb[Y][X] = c;
        }
    }
}
static void Dirty(void*, int x, int y, int w, int h) {
    char b[64];
    snprintf(b, sizeof b, "%d %d %d %d", x, y, w, h);
    dirties.push_back(b);
}

static uint32_t Fnv(const void* p, size_t n, uint32_t h = 2166136261u) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

// 描いた並びに (sx, sy, w, h, dx, dy) があるか(sx < 0 なら sx は問わない)
static bool Has(const std::vector<std::string>& lines, int sx, int sy, int w, int h, int dx, int dy) {
    for (auto& l : lines) {
        int a, b, c, d, e, f;
        if (sscanf(l.c_str(), "%d %d %d %d %d %d", &a, &b, &c, &d, &e, &f) != 6) continue;
        if ((sx < 0 || a == sx) && b == sy && c == w && d == h && e == dx && f == dy) return true;
    }
    return false;
}

static std::vector<std::string> RenderAll(World& w, bool cursor = false) {
    Rec r;
    w.setCursor(0, 0, 0, cursor);
    w.render(World::Sink{&r, Draw, Dirty, Dither}, 0, 20, 240, 224);
    return r.lines;
}

static Rec RenderRec(World& w) {
    Rec r;
    w.setCursor(0, 0, 0, false);
    w.render(World::Sink{&r, Draw, Dirty, Dither}, 0, 20, 240, 224);
    return r;
}

// 小さな空のワールド(K=6、48x48)を全部読み込む
static void EmptyWorld(World& w) {
    int x, y, z;
    w.create("", EMPTY, 0, 6, x, y, z);
    for (int cx = 0; cx < 6; cx++) for (int cz = 0; cz < 6; cz++) w.loadChunk(cx, cz);
    w.setView(0, 20, 240, 204);
    w.setOrigin(100, 150);
}

// ---- 人や物の描き先: fb へ1画素ずつ。人や物の絵は spr(1画素1バイト、0xFF = 透過) ----
static int SPW = 0, SPH = 0;
static std::vector<uint8_t> spr;
static int FacePx(void*, int x, int y) { return (x >= 0 && y >= 0 && x < SW && y < SH) ? sheet[(size_t)y * SW + x] : 0; }
static int ImagePx(void*, int32_t img, int x, int y) {
    if (img != 7 || x < 0 || y < 0 || x >= SPW || y >= SPH) return -1;
    const uint8_t c = spr[(size_t)y * SPW + x];
    return c == 0xFF ? -1 : c;
}
static void PutPx(void* p, int x, int y, int c) {
    Rec* r = static_cast<Rec*>(p);
    if (x < r->cx0 || x >= r->cx1 || y < r->cy0 || y >= r->cy1) return;
    fb[y][x] = (uint8_t)c;
}
static World::Sink EntSink(Rec& r) {
    World::Sink s{&r, Draw, Dirty, Dither};
    s.face_px = FacePx;
    s.image_px = ImagePx;
    s.put = PutPx;
    return s;
}
// w×h の絵(色 c、四隅は透過)
static void MakeSprite(int w, int h, uint8_t c) {
    SPW = w; SPH = h;
    spr.assign((size_t)w * h, c);
    spr[0] = spr[w - 1] = spr[(size_t)(h - 1) * w] = spr[(size_t)h * w - 1] = 0xFF;
}

static int TopLen(const uint8_t* b) {
    int top = 0;
    for (int i = 0; i < kChunkBytes; i++) if (b[i]) top = i / kLayer + 1;
    return top;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root = argc > 1 ? argv[1] : ".";
    {
        std::ifstream f(root + "/pc/sdcard/lua/apps/ブロック/faces.pimg", std::ios::binary);
        const std::string s((std::istreambuf_iterator<char>(f)), {});
        if (s.size() < 5) { printf("[FAIL] faces.pimg を読めません\n"); return 1; }
        SW = (uint8_t)s[0] | ((uint8_t)s[1] << 8);
        SH = (uint8_t)s[2] | ((uint8_t)s[3] << 8);
        for (size_t i = 5; i + 1 < s.size(); i += 2) {
            for (int k = 0; k < (uint8_t)s[i]; k++) sheet.push_back((uint8_t)s[i + 1]);
        }
        sheet4.assign((size_t)((SW + 1) / 2) * SH, 0);
        for (int y = 0; y < SH; y++) {
            for (int x = 0; x < SW; x++) FaceBlitter::Put(&sheet4[(size_t)y * ((SW + 1) / 2)], x, sheet[(size_t)y * SW + x]);
        }
    }
    check(SW == kSheetW && SH == (kCursorRow + 1) * kRowH && (int)sheet.size() == SW * SH, "faces.pimg は 224x598");
    const uint32_t occ = World::ComputeOccluders(SheetPx, nullptr);
    {
        World w;
        check(occ == w.occluders(), "透けないブロック: 水・葉以外(絵から求めたものと既定が同じ)");
        check(!((occ >> LEAVES) & 1) && ((occ >> STONE) & 1) && ((occ >> GOLD) & 1), "透けないブロック: 葉は穴が開いている");
        check(!((occ >> TORCH) & 1), "透けないブロック: 松明は透ける(光も通す)");
    }

    // ================================================================ 生成: Lua版と同じ
    {
        World w;
        struct Want { int kind; uint32_t seed; uint32_t hash; };
        // 旧Lua版(world.lua)の generate(cx, cz) で作ったチャンク(上の空気の段を落とした文字列)のハッシュ。
        // 種ごとに cx = 0,9,..,126 × cz = 0,11,..,121、(64,64)、(0,0)、(127,127) の順
        const Want natural[] = {
            {0, 0u, 0x60d17d7cu}, {0, 1234u, 0x242b0162u}, {0, 99u, 0xd430bb1eu},
            {0, 0x7fffffffu, 0x09983f69u}, {0, 5u, 0xa7e18b84u}, {0, 424242u, 0x29300409u},
        };
        uint8_t b[kChunkBytes];
        auto add = [&](uint32_t h, int cx, int cz) {
            w.generate(cx, cz, b);
            const int top = TopLen(b);
            const uint8_t head[3] = {(uint8_t)cx, (uint8_t)cz, (uint8_t)top};
            return Fnv(b, (size_t)top * kLayer, Fnv(head, 3, h));
        };
        for (const Want& t : natural) {
            int x, y, z;
            w.create("", NATURAL, t.seed, 128, x, y, z);
            uint32_t h = 2166136261u;
            for (int cx = 0; cx <= 127; cx += 9) for (int cz = 0; cz <= 127; cz += 11) h = add(h, cx, cz);
            h = add(h, 64, 64); h = add(h, 0, 0); h = add(h, 127, 127);
            char msg[96];
            snprintf(msg, sizeof msg, "自然(種 %u): Lua版と同じ地形 (%08x)", (unsigned)t.seed, (unsigned)h);
            check(h == t.hash, msg);
        }
        int x, y, z;
        w.create("", DEMO, 0, 128, x, y, z);
        uint32_t h = 2166136261u;
        for (int cx = 0; cx < 8; cx++) for (int cz = 0; cz < 8; cz++) h = add(h, cx, cz);
        check(h == 0x0338064au, "デモ: Lua版と同じ家・東屋・ピラミッド・池・木");
        w.create("", FLAT, 0, 128, x, y, z);
        check(add(2166136261u, 3, 3) == 0x921e34c6u, "平ら: Lua版と同じ");
        w.create("", EMPTY, 0, 128, x, y, z);
        check(add(2166136261u, 3, 3) == 0x63953099u, "空: Lua版と同じ");
    }

    // ================================================================ 読み書き
    {
        World w;
        EmptyWorld(w);
        check(w.width() == 48 && w.get(5, 5, 5) == AIR, "空のワールドは空気");
        w.set(3, 9, 7, TNT);
        check(w.get(3, 9, 7) == TNT && w.get(3, 9, 6) == AIR && w.get(3, 8, 7) == AIR, "1マスだけ書き換わる");
        check(w.find(0, 0)->top == 10 && w.find(0, 0)->col[3 * 8 + 7] == 10, "チャンクの段の数と柱の高さ");
        w.set(3, 9, 7, AIR);
        check(w.find(0, 0)->top == 0, "空気に戻すと段の数も戻る");
        w.set(7, 2, 8, GOLD); w.set(8, 2, 7, SAND);
        check(w.get(7, 2, 8) == GOLD && w.get(8, 2, 7) == SAND && w.get(8, 2, 8) == AIR, "チャンクの境目をまたいで読み書きできる");
        check(w.get(-1, 0, 0) == 0 && w.get(0, H, 0) == 0 && w.get(0, 0, 48) == 0 && w.get(0, -1, 0) == 0, "世界の外は空気");
        w.set(-1, 0, 0, STONE);
        check(w.find(-1, 0) == nullptr, "世界の外への書き込みは無視");
    }

    // ================================================================ 生成の中身
    {
        World w;
        int px, py, pz;
        w.create("", NATURAL, 1234, 128, px, py, pz);
        check(w.width() == 1024 && w.k() == 128, "新しいワールドは 1024x1024 (128x128 チャンク)");
        check(px == 512 && pz == 512 && w.get(px, py, pz) == AIR && w.get(px, py - 1, pz) != AIR,
              "自然: 始まりは真ん中の柱の一番下の空気");
        bool smooth = true;
        for (int z = 448; z < 576; z++) if (std::abs(w.height(463, z) - w.height(464, z)) > 2) smooth = false;
        check(smooth, "自然: チャンクの境目で高さが飛ばない");
        uint8_t a[kChunkBytes], b[kChunkBytes];
        w.generate(40, 51, a); w.generate(40, 51, b);
        check(memcmp(a, b, sizeof a) == 0, "同じ種・同じ位置のチャンクはいつ作っても同じ");
        w.create("", NATURAL, 99, 128, px, py, pz);
        w.generate(40, 51, b);
        check(memcmp(a, b, sizeof a) != 0, "種が違えば違う地形");
        w.create("", DEMO, 5, 128, px, py, pz);
        check(px == 0 && py == 1 && pz == 0, "デモ: 始まりは (0, 1, 0)");
        for (int cx = 0; cx < 6; cx++) for (int cz = 0; cz < 6; cz++) w.loadChunk(cx, cz);
        check(w.get(47, 8, 47) == GOLD && w.get(39, 1, 39) == GOLD, "デモ: 金のピラミッド");
        check(w.get(18, 2, 3) == TNT && w.get(19, 1, 3) == BOOKS, "デモ: 家の中の TNT と本棚");
        check(w.get(6, 4, 5) == LEAVES && w.get(10, 4, 9) == LEAVES, "デモ: 隣のチャンクへはみ出した葉");
    }

    // ================================================================ 読み込みの範囲・保存
    {
        HostSd::files.clear();
        OSData::SD_usable = true;
        World w;
        int px, py, pz;
        w.create("/w/v", NATURAL, 7, 128, px, py, pz);
        w.setView(0, 20, 240, 204);
        auto look = [&](int x, int y, int z) {
            w.setOrigin(120 - 16 - 16 * (x - z), 122 - 16 + 8 * (x + z) + 16 * y);
            w.pump(1000, 0);
        };
        look(512, 7, 512);
        const int k1 = w.loadedCount();
        char msg[96];
        snprintf(msg, sizeof msg, "見える範囲のチャンクだけ読み込む (%d個)", k1);
        check(k1 > 20 && k1 <= kMaxChunks && w.pending() == 0, msg);
        check(w.find(64, 64) != nullptr && w.find(0, 0) == nullptr, "遠くのチャンクは読み込まない");
        const int ny = w.firstAir(515, 517);
        w.set(515, ny, 517, GOLD);
        look(100, 7, 100);
        check(w.find(64, 64) == nullptr, "範囲から外れたチャンクは手放す");
        check(HostSd::files.count("/w/v/c_64_64.dat") == 1, "書き換えたチャンクは手放すときに書き出す");
        check(HostSd::files.count("/w/v/c_63_63.dat") == 0, "書き換えていないチャンクは書き出さない");
        check(w.get(515, ny, 517) == AIR, "読み込んでいないチャンクは空気");
        // 動き回っても置き場が足りる
        int maxk = 0;
        bool full = false;
        std::mt19937 rng(3);
        int cx = 512, cy = 1, cz = 512;
        for (int i = 0; i < 500; i++) {
            cx += (int)(rng() % 13) - 6; cz += (int)(rng() % 13) - 6; cy = (int)(rng() % 16);
            w.setOrigin(120 - 16 - 16 * (cx - cz) + (int)(rng() % 121) - 60, 122 - 16 + 8 * (cx + cz) + 16 * cy + (int)(rng() % 121) - 60);
            w.pump(1000, 0);
            if (w.loadedCount() > maxk) maxk = w.loadedCount();
            if (w.pending() != 0) full = true;
        }
        snprintf(msg, sizeof msg, "動き回っても置き場(%d個)に収まる (最大 %d個)", kMaxChunks, maxk);
        check(!full && maxk <= kMaxChunks, msg);
        look(512, 7, 512);
        check(w.get(515, ny, 517) == GOLD, "戻ると書き換えた内容をファイルから読む");

        w.set(520, 9, 520, BRICKS);
        check(w.save(515, 4, 517, BRICKS), "保存できる");
        check(HostSd::files["/w/v/world.dat"].size() == 17 + 128 * 128 / 8, "見出し17バイト + チャンクの印");
        w.close();
        check(!w.isOpen() && w.get(515, ny, 517) == 0, "閉じると何も持たない");
        int x, y, z, cur;
        const char* err = nullptr;
        check(w.open("/w/v", x, y, z, cur, err) && x == 515 && y == 4 && z == 517 && cur == BRICKS && w.width() == 1024,
              "開くと同じ位置・ブロック・大きさ");
        w.loadChunk(515 >> 3, 517 >> 3); w.loadChunk(520 >> 3, 520 >> 3);
        check(w.get(515, ny, 517) == GOLD && w.get(520, 9, 520) == BRICKS, "書き換えた内容が残っている");
        uint8_t fresh[kChunkBytes];
        w.generate(10, 10, fresh);
        check(memcmp(w.loadChunk(10, 10)->b, fresh, kChunkBytes) == 0, "書き換えていないチャンクは種から作り直す");
        int kind, width;
        check(World::Info("/w/v/world.dat", kind, width) && kind == 0 && width == 1024, "見出しから種類と大きさを読める");
        // 壊れたチャンクのファイルは読まずに作り直す
        w.set(523, 1, 523, TNT);
        w.save(0, 0, 0, 2);
        HostSd::files["/w/v/c_65_65.dat"] = std::string("\xC8") + std::string(63, '\0');
        w.close();
        w.open("/w/v", x, y, z, cur, err);
        w.generate(65, 65, fresh);
        check(memcmp(w.loadChunk(65, 65)->b, fresh, kChunkBytes) == 0, "壊れたチャンクのファイルは読まずに作り直す");
        HostSd::files["/w/x/world.dat"] = "XXXX" + HostSd::files["/w/v/world.dat"].substr(4);
        check(!w.open("/w/x", x, y, z, cur, err), "形式の違うファイルは読まない");
        HostSd::files["/w/y/world.dat"] = HostSd::files["/w/v/world.dat"].substr(0, 100);
        check(!w.open("/w/y", x, y, z, cur, err), "途中で切れたファイルは読まない");
        check(!w.open("/w/none", x, y, z, cur, err), "無いワールドは開けない");
        std::string big = HostSd::files["/w/v/world.dat"];
        big[5] = 0x04; big[6] = 0x00;    // K = 1024
        HostSd::files["/w/z/world.dat"] = big;
        check(!w.open("/w/z", x, y, z, cur, err), "大きすぎるワールドは開かない");
    }

    // ================================================================ 前の版からの移し替え
    {
        World w;
        std::string layers;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < 48; x++) {
                for (int z = 0; z < 48; z++) {
                    layers += (char)((y == 0) ? BEDROCK : (((x * 7 + z * 3 + y) % 9 == 0 && y < 6) ? STONE : 0));
                }
            }
        }
        HostSd::files["/old.dat"] = std::string("BLK1") + std::string("\x30\x10\x03\x04\x05\x18", 6) + layers;
        const char* err = nullptr;
        check(w.migrate("/old.dat", "/w/m", err) && HostSd::files.count("/old.dat") == 0, "前の版の保存を移して元のファイルを消す");
        int x, y, z, cur;
        check(w.open("/w/m", x, y, z, cur, err) && x == 3 && y == 4 && z == 5 && cur == GOLD && w.width() == 48,
              "移したワールドを開ける");
        bool same = true;
        for (int cx = 0; cx < 6; cx++) for (int cz = 0; cz < 6; cz++) w.loadChunk(cx, cz);
        for (int xx = 0; xx < 48; xx++) for (int zz = 0; zz < 48; zz++) for (int yy = 0; yy < H; yy++) {
            if (w.get(xx, yy, zz) != (uint8_t)layers[(size_t)yy * 2304 + xx * 48 + zz]) same = false;
        }
        check(same, "移したワールドの中身が元と同じ");
        HostSd::files["/bad.dat"] = std::string("BLK1") + std::string("\x30\x10\x03\x04\x05\x18", 6) + layers.substr(0, 1000);
        check(!w.migrate("/bad.dat", "/w/n", err) && HostSd::files.count("/bad.dat") == 1, "途中で切れた古いファイルは移さない");
    }

    // ================================================================ 描画
    {
        World w;
        w.setOccluders(occ);
        EmptyWorld(w);
        w.set(0, 0, 0, STONE);
        std::vector<std::string> got = RenderAll(w);
        const int sy = (STONE - 1) * kRowH;
        char want[3][64];
        snprintf(want[0], 64, "0 %d 32 15 100 150", sy);
        snprintf(want[1], 64, "128 %d 16 23 100 158", sy);
        snprintf(want[2], 64, "192 %d 16 23 116 158", sy);
        check(got.size() == 3 && got[0] == want[0] && got[1] == want[1] && got[2] == want[2],
              "1個だけのブロックは3面(上面・左面は日なた、右面)を描く");

        EmptyWorld(w);
        for (int x = 0; x < 3; x++) for (int y = 0; y < 3; y++) for (int z = 0; z < 3; z++) w.set(x, y, z, DIRT);
        w.setCulling(false);
        check(RenderAll(w).size() == 27, "3x3x3 の塊は見える面(9+9+9)だけ描く");
        // (2,1,2) は (3,0,3) と画面のちょうど同じ所に重なり、後から描かれる
        EmptyWorld(w);
        w.set(3, 0, 3, STONE); w.set(2, 1, 2, STONE);
        check(RenderAll(w).size() == 6, "省略を切ると隠れたブロックも描く");
        w.setCulling(true);
        check(RenderAll(w).size() == 3, "透けないブロックに完全に隠れるブロックは描かない");
        w.set(2, 1, 2, LEAVES);
        check(RenderAll(w).size() == 6, "葉(穴がある)の後ろは描く");

        // 影: 太陽(-1,+1,+1)の方にブロックがあると、上面は影の絵
        auto shadow = [&](int ox, int oy, int oz, bool top, bool& a, bool& b) {
            EmptyWorld(w);
            w.set(5, 3, 5, STONE);
            w.set(ox, oy, oz, DIRT);
            if (top) w.topShadow(5, 3, 5, H - 1, a, b);
            else w.leftShadow(5, 3, 5, H - 1, a, b);
        };
        bool f, n;
        shadow(5, 4, 6, true, f, n);  check(f && !n, "上面: 奥 (x, y+1, z+1) は奥半分だけ影");
        shadow(4, 4, 5, true, f, n);  check(n && !f, "上面: 左 (x-1, y+1, z) は手前半分だけ影");
        shadow(4, 4, 6, true, f, n);  check(f && n, "上面: (x-1, y+1, z+1) は全部影");
        shadow(2, 7, 8, true, f, n);  check(f && n, "上面: 太陽の方へ3歩先のブロックも影を落とす");
        shadow(5, 2, 5, true, f, n);  check(!f && !n, "上面: 下のブロックは影を落とさない");
        shadow(4, 4, 5, false, f, n); check(f && !n, "左面: (x-1, y+1, z) は上半分だけ影");
        shadow(4, 3, 6, false, f, n); check(n && !f, "左面: (x-1, y, z+1) は下半分だけ影");
        shadow(3, 4, 6, false, f, n); check(f && n, "左面: (x-2, y+1, z+1) は全部影");
        EmptyWorld(w);
        w.set(5, 3, 5, STONE); w.set(4, 4, 6, WATER);
        w.topShadow(5, 3, 5, H - 1, f, n);
        check(!f && !n, "水は影を落とさない");

        // 奥半分だけ影の上面は x=64 の絵
        EmptyWorld(w);
        w.set(5, 3, 5, STONE); w.set(5, 4, 6, DIRT);
        got = RenderAll(w);
        int bx, by;
        w.blockPos(5, 3, 5, bx, by);
        check(Has(got, 64, sy, 32, 15, bx, by), "奥半分だけ影の上面は、奥半分が影の絵 (x=64)");

        // 水
        EmptyWorld(w);
        for (int x = 0; x < 2; x++) for (int z = 0; z < 2; z++) w.set(x, 0, z, WATER);
        got = RenderAll(w);
        bool water0 = got.size() == 8;
        for (auto& l : got) {
            int a, b2;
            if (sscanf(l.c_str(), "%d %d", &a, &b2) != 2 || b2 != 0) water0 = false;   // 水の段(sy=0)だけ
        }
        check(water0, "2x2の水はくっついた面を描かない(上4+左2+右2)");
        EmptyWorld(w);
        w.set(0, 0, 0, WATER);
        got = RenderAll(w);
        check(got.size() == 3 && got[0] == "0 0 32 15 100 152" && got[1] == "144 0 16 23 100 158" && got[2] == "160 0 16 23 116 158",
              "水面: 上面は2px下げ、横の面は水面用の絵");
        w.set(0, 1, 0, WATER);
        got = RenderAll(w);
        check(Has(got, 128, 0, 16, 23, 100, 158), "真上も水なら下の水は普通の横の面");

        // 葉の穴から後ろが見えるので、葉に面した面も描く
        EmptyWorld(w);
        w.set(5, 0, 5, STONE); w.set(5, 1, 5, LEAVES);
        got = RenderAll(w);
        w.blockPos(5, 0, 5, bx, by);
        check(Has(got, -1, sy, 32, 15, bx, by), "葉の下のブロックの上面も描く(葉の穴から見える)");

        // 描く範囲の外のブロックは描かない
        EmptyWorld(w);
        w.set(0, 0, 0, STONE);
        w.set(47, 0, 0, STONE);
        Rec r;
        w.setCursor(0, 0, 0, false);
        w.render(World::Sink{&r, Draw, Dirty}, 100, 150, 132, 181);
        check(r.lines.size() == 3, "矩形の外にあるブロックは描かない");

        // カーソル
        EmptyWorld(w);
        w.setCursor(1, 1, 1, true);
        Rec rc;
        w.render(World::Sink{&rc, Draw, Dirty}, 0, 20, 240, 224);
        check(rc.lines.size() == 3 && rc.lines[0].rfind("0 575 ", 0) == 0, "カーソルを最後に描く");
    }

    // ================================================================ Lua版と同じ描画の並び
    {
        // 旧Lua版(view.lua)で 自然・種1234・カーソル(512,7,512)を真ん中にした全体の描画の並びのハッシュ。
        // 葉に面した面の規則と隠れたブロックの省略は Lua版に無いので、葉も「透けない」として省略を切って比べる
        World w;
        int x, y, z;
        w.create("", NATURAL, 1234, 128, x, y, z);
        w.setView(0, 20, 240, 204);
        w.setOrigin(104, 8410);
        w.pump(1000, 0);
        w.setOccluders(~3u & ((1u << (kBlockCount + 1)) - 1));
        w.setCulling(false);
        Rec r;
        w.setCursor(512, 7, 512, true);
        w.render(World::Sink{&r, Draw, Dirty}, 0, 20, 240, 224);
        // Lua版のカーソルの段は 24(sy=552)。松明の段を足して 25(sy=575)になったので、比べるときだけ戻す
        std::string all;
        for (auto l : r.lines) {
            int a, b2, c, d, e, f2;
            if (sscanf(l.c_str(), "%d %d %d %d %d %d", &a, &b2, &c, &d, &e, &f2) == 6 && b2 == kCursorRow * kRowH) {
                char t[64];
                snprintf(t, sizeof t, "%d %d %d %d %d %d", a, 552, c, d, e, f2);
                l = t;
            }
            all += l;
            all += '\n';
        }
        char msg[96];
        snprintf(msg, sizeof msg, "Lua版と同じ面・影・順番で描く (%zu面、%08x)", r.lines.size(), (unsigned)Fnv(all.data(), all.size()));
        check(r.lines.size() == 396 && Fnv(all.data(), all.size()) == 0xf3de7f65u, msg);
    }

    // ================================================================ 隠れたブロックの省略で画素が変わらない
    {
        std::mt19937 rng(1);
        World w;
        w.setOccluders(occ);
        int bad = 0;
        long f0 = 0, f1 = 0;
        static uint8_t ref[320][240];
        for (int t = 0; t < 150; t++) {
            int x, y, z;
            w.create("", (t % 3 == 0) ? NATURAL : EMPTY, rng(), 6, x, y, z);
            for (int cx = 0; cx < 6; cx++) for (int cz = 0; cz < 6; cz++) w.loadChunk(cx, cz);
            const int n = 200 + (int)(rng() % 2000);
            for (int i = 0; i < n; i++) {
                const int bx = rng() % 48, byy = rng() % 16, bz = rng() % 48;
                const int r = rng() % 10;
                int b = r < 2 ? WATER : (r < 4 ? LEAVES : 2 + (int)(rng() % 23));
                if (rng() % 4 == 0) b = AIR;
                w.set(bx, byy, bz, (uint8_t)b);
            }
            // 松明を少し(置いた後に、まわりを少し書き換える)
            for (int i = 0, nt = (int)(rng() % 8); i < nt; i++) w.set(rng() % 48, rng() % 16, rng() % 48, TORCH);
            for (int i = 0; i < 40; i++) w.set(rng() % 48, rng() % 16, rng() % 48, (uint8_t)(rng() % 3 ? STONE : AIR));
            w.setView(0, 20, 240, 204);
            w.setSunlight(rng() % 3 != 0);
            w.setOrigin((int)(rng() % 200), 300 + (int)(rng() % 300));
            const int cx0 = (int)(rng() % 120), cy0 = 20 + (int)(rng() % 100);
            const int cx1 = (t & 1) ? 240 : cx0 + 1 + (int)(rng() % 120), cy1 = (t & 1) ? 224 : cy0 + 1 + (int)(rng() % 100);
            Rec r;
            r.raster = true; r.cx0 = cx0; r.cy0 = cy0; r.cx1 = cx1; r.cy1 = cy1;
            w.setCursor(0, 0, 0, false);
            memset(fb, 7, sizeof fb);
            w.setCulling(false);
            w.render(World::Sink{&r, Draw, Dirty, Dither}, cx0, cy0, cx1, cy1);
            f0 += w.lastFaces();
            memcpy(ref, fb, sizeof fb);
            memset(fb, 7, sizeof fb);
            w.setCulling(true);
            w.render(World::Sink{&r, Draw, Dirty, Dither}, cx0, cy0, cx1, cy1);
            f1 += w.lastFaces();
            if (memcmp(ref, fb, sizeof fb)) bad++;
        }
        char msg[128];
        snprintf(msg, sizeof msg, "隠れたブロックを省いても画素は1つも変わらない (150回・松明と夜を含む、面 %ld → %ld)", f0, f1);
        check(bad == 0 && f1 < f0, msg);
    }

    // ================================================================ 引き当て・描き直す範囲
    {
        World w;
        EmptyWorld(w);
        w.set(4, 2, 6, STONE);
        int bx, by, x, y, z;
        w.blockPos(4, 2, 6, bx, by);
        check(w.pick(bx + 16, by + 7, x, y, z) == Face::Top && x == 4 && y == 2 && z == 6, "上面のタップ");
        check(w.pick(bx + 4, by + 18, x, y, z) == Face::Left, "左面のタップ");
        check(w.pick(bx + 28, by + 18, x, y, z) == Face::Right, "右面のタップ");
        check(w.pick(bx - 30, by - 40, x, y, z) == Face::None, "何も無い所");
        w.set(4, 3, 6, DIRT);
        check(w.pick(bx + 16, by - 9, x, y, z) != Face::None && y == 3, "重なっていれば手前(上)のブロック");

        w.setOrigin(120, 200);
        dirties.clear();
        w.dirtyEdit(World::Sink{nullptr, nullptr, Dirty}, 10, 4, 10);
        bool small = dirties.size() > 1;
        for (auto& d : dirties) {
            int dx, dy, dw, dh;
            sscanf(d.c_str(), "%d %d %d %d", &dx, &dy, &dw, &dh);
            if (dw > 64 || dh > 63 || dy < 20 || dy + dh > 224) small = false;
        }
        check(small, "置いたときは表示範囲の中の小さな矩形だけ描き直す");
        dirties.clear();
        w.dirtyBlock(World::Sink{nullptr, nullptr, Dirty}, 2, 0, 2);
        check(dirties.size() == 1 && dirties[0] == "120 168 32 31", "カーソルの1マス");
        dirties.clear();
        w.dirtyBlock(World::Sink{nullptr, nullptr, Dirty}, 10, 4, 10);
        check(dirties.empty(), "表示範囲の外は描き直さない");
    }

    // ================================================================ 面の写し方
    {
        FaceBlitter b;
        b.setSource(sheet4.data(), SW, SH);
        int exact = 0;
        for (int r = 0; r < FaceBlitter::kRows; r++) for (int c = 0; c < FaceBlitter::kCols; c++) exact += b.exact(r, c);
        check(exact == 22 * 10, "透けないブロック22種の10枚の絵は形どおり(区間で写せる)");
        std::mt19937 rng(5);
        int bad = 0;
        std::vector<uint8_t> d1(120 * 320), d2;
        for (int t = 0; t < 20000; t++) {
            for (auto& v : d1) v = (uint8_t)rng();
            d2 = d1;
            const int row = rng() % FaceBlitter::kRows, c = rng() % FaceBlitter::kCols;
            int sx = FaceBlitter::kColX[c], sy = row * kRowH;
            const int shape = FaceBlitter::ShapeOf(c);
            const int w = shape == 0 ? 32 : 16, h = FaceBlitter::Height(shape);
            if (rng() % 10 == 0) { sx = rng() % (SW - 32); sy = rng() % (SH - h); }
            const int level = (int)(rng() % 5) - 1;   // -1 なら draw、0〜3 なら dither
            const int dx = (int)(rng() % 300) - 40, dy = (int)(rng() % 360) - 20;
            const int cx0 = rng() % 240, cx1 = cx0 + (int)(rng() % (241 - cx0));
            const int cy0 = rng() % 320, cy1 = cy0 + (int)(rng() % (321 - cy0));
            b.setTarget(d1.data(), 120, cx0, cy0, cx1, cy1);
            if (level < 0) b.draw(sx, sy, w, h, dx, dy);
            else b.dither(sx, sy, w, h, dx, dy, level);
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                const int X = dx + x, Y = dy + y;
                if (X < cx0 || X >= cx1 || Y < cy0 || Y >= cy1) continue;
                if (level >= 0 && (level == 0 || !World::DitherOn(level, x, y))) continue;
                const uint8_t v = sheet[(size_t)(sy + y) * SW + sx + x];
                if (v) FaceBlitter::Put(&d2[(size_t)Y * 120], X, v);
            }
            if (d1 != d2) bad++;
        }
        check(bad == 0, "面の写し方が1画素ずつの写し方と同じ(2万回、位置・クリップ・偶奇・ディザはばらばら)");
    }

    // ================================================================ 松明の光
    {
        World w;
        w.setOccluders(occ);
        EmptyWorld(w);
        for (int x = 0; x < 48; x++) for (int z = 0; z < 48; z++) w.set(x, 0, z, STONE);
        w.set(10, 1, 10, TORCH);
        // 明るさ 7(松明)から1マスごとに1減る: 距離0〜2 → 3、3〜4 → 2、5〜6 → 1、7〜 → 0
        check(w.light(10, 1, 10) == 3 && w.light(12, 1, 10) == 3 && w.light(10, 3, 10) == 3, "松明の近く(2マス)は明るさ3");
        check(w.light(13, 1, 10) == 2 && w.light(12, 1, 12) == 2, "3〜4マス先は明るさ2");
        check(w.light(15, 1, 10) == 1 && w.light(13, 1, 13) == 1, "5〜6マス先は明るさ1");
        check(w.light(17, 1, 10) == 0 && w.light(10, 1, 3) == 0, "7マス先には届かない");
        check(w.light(10, 0, 10) == 0, "石の中は暗い");
        check(w.light(30, 1, 30) == 0, "遠くは暗い");

        // 壁で遮られる: x=12 に高い壁を作ると、すぐ裏 (13,1,10) は回り込む道が長いので暗くなる
        for (int y = 1; y < H; y++) for (int z = 0; z < 48; z++) w.set(12, y, z, STONE);
        check(w.light(11, 1, 10) == 3, "壁の手前は明るい");
        check(w.light(13, 1, 10) == 0, "壁の裏は暗い(光は透けないブロックを通らない)");
        for (int y = 1; y < H; y++) for (int z = 0; z < 48; z++) w.set(12, y, z, AIR);
        check(w.light(13, 1, 10) == 2, "壁を取ると明るさが戻る");
        w.set(11, 1, 10, LEAVES);
        w.set(11, 1, 11, WATER);
        check(w.light(11, 1, 10) == 3 && w.light(11, 1, 11) == 3, "葉と水は光を通す");
        w.set(11, 1, 10, AIR);
        w.set(11, 1, 11, AIR);

        // 描き直す範囲: 松明を取ると明るさの変わった所を覆う矩形を描き直す
        w.setOrigin(120, 300);
        int x0, y0, z0, x1, y1, z1;
        w.dirtyEdit(World::Sink{nullptr, nullptr, Dirty}, 0, 0, 0);    // 溜まっていた分を捨てる
        check(!w.lightChanged(x0, y0, z0, x1, y1, z1), "描き直すと明るさの変わった範囲は空になる");
        w.set(10, 1, 10, AIR);
        check(w.light(10, 1, 10) == 0 && w.light(12, 1, 10) == 0, "松明を取ると暗くなる");
        check(w.lightChanged(x0, y0, z0, x1, y1, z1) && x0 == 4 && x1 == 16 && z0 == 4 && z1 == 16 && y0 == 1 && y1 == 7,
              "明るさが変わった範囲は松明のまわり6マス");
        dirties.clear();
        w.dirtyEdit(World::Sink{nullptr, nullptr, Dirty}, 10, 1, 10);
        bool big = false;
        for (auto& d : dirties) {
            int dx, dy, dw, dh;
            sscanf(d.c_str(), "%d %d %d %d", &dx, &dy, &dw, &dh);
            if (dw > 64 || dh > 63) big = true;
        }
        check(big, "明るさが変わったときは、影より広い範囲を描き直す");
        dirties.clear();
        w.set(30, 0, 30, DIRT);
        w.dirtyEdit(World::Sink{nullptr, nullptr, Dirty}, 30, 0, 30);
        bool small = true;
        for (auto& d : dirties) {
            int dx, dy, dw, dh;
            sscanf(d.c_str(), "%d %d %d %d", &dx, &dy, &dw, &dh);
            if (dw > 64 || dh > 63) small = false;
        }
        check(small, "松明の届かない所の置き換えは、明るさの範囲を描き直さない");

        // チャンクの境目: 後から読み込んだチャンクも照らす / 読み込んだチャンクの松明がまわりを照らす
        {
            World v;
            v.setOccluders(occ);
            int x, y, z;
            v.create("", EMPTY, 0, 6, x, y, z);
            v.loadChunk(0, 0);
            v.set(7, 1, 4, TORCH);
            check(v.light(8, 1, 4) == 0, "読み込んでいないチャンクは暗い");
            v.loadChunk(1, 0);
            check(v.light(8, 1, 4) == 3 && v.light(11, 1, 4) == 2, "後から読み込んだ隣のチャンクも照らす");
            v.set(16, 1, 4, TORCH);   // チャンク(2,0)はまだ無いので読み込まれる
            check(v.light(15, 1, 4) == 3 && v.light(9, 1, 4) == 3, "別のチャンクの松明の光が重なる");
        }

        // 松明は影を落とさない・後ろを隠さない
        EmptyWorld(w);
        bool f, n;
        w.set(5, 3, 5, STONE); w.set(4, 4, 6, TORCH);
        w.topShadow(5, 3, 5, H - 1, f, n);
        check(!f && !n, "松明は影を落とさない");
        EmptyWorld(w);
        w.set(3, 0, 3, STONE); w.set(2, 1, 2, TORCH);
        const int tsy = (TORCH - 1) * kRowH;
        std::vector<std::string> got = RenderAll(w);
        int bx, by;
        w.blockPos(2, 1, 2, bx, by);
        check(got.size() == 6 && Has(got, 0, tsy, 32, 15, bx, by) && Has(got, 128, tsy, 16, 23, bx, by + 8)
              && Has(got, 192, tsy, 16, 23, bx + 16, by + 8), "松明は松明の絵を描き、後ろのブロックも描く");

        // 描く絵: 松明のすぐ上の面は日なた、右面(いつも影)も日なたの絵 x=208、少し離れると影+ディザ
        EmptyWorld(w);
        const int ssy = (STONE - 1) * kRowH;
        w.set(5, 0, 5, STONE);      // 上面が松明に照らされる
        w.set(9, 1, 9, STONE);      // 右面(-z)の隣 (9,1,8) は松明から距離1
        w.set(9, 1, 8, TORCH);
        w.set(0, 0, 0, STONE);      // 照らされない
        w.set(5, 1, 5, AIR);
        w.set(5, 2, 5, AIR);
        w.set(6, 1, 5, TORCH);      // (5,1,5) は距離1
        w.setSunlight(false);
        Rec r = RenderRec(w);
        w.blockPos(5, 0, 5, bx, by);
        check(Has(r.lines, 0, ssy, 32, 15, bx, by), "夜: 松明に照らされた上面は日なたの絵");
        w.blockPos(0, 0, 0, bx, by);
        check(Has(r.lines, 32, ssy, 32, 15, bx, by) && Has(r.lines, 144, ssy, 16, 23, bx, by + 8),
              "夜: 照らされない面は影の絵");
        w.blockPos(9, 1, 9, bx, by);
        check(Has(r.lines, 208, ssy, 16, 23, bx + 16, by + 8), "松明に照らされた右面は日なたの右面 (x=208)");
        // 距離3〜6: 影の絵の上に、日なたの絵をディザで重ねる
        EmptyWorld(w);
        w.set(5, 0, 5, STONE);
        w.set(5, 1, 9, TORCH);      // (5,1,5) は距離4 → 明るさ2
        r = RenderRec(w);
        w.blockPos(5, 0, 5, bx, by);
        char want[64];
        snprintf(want, sizeof want, "0 %d 32 15 %d %d 2", ssy, bx, by);
        bool found = false;
        for (auto& d : r.dithers) if (d == want) found = true;
        check(Has(r.lines, 32, ssy, 32, 15, bx, by) && found, "夜: 少し離れた面は影の絵 + 日なたの絵のディザ(明るさ2)");
        w.setSunlight(true);
        r = RenderRec(w);
        found = false;
        for (auto& d : r.dithers) if (d.rfind(std::string("0 ") + std::to_string(ssy) + " 32 15 ", 0) == 0) found = true;
        check(!found && Has(r.lines, 0, ssy, 32, 15, bx, by), "昼: 日の当たる上面にはディザを重ねない");
        snprintf(want, sizeof want, "208 %d 16 23 %d %d 1", ssy, bx + 16, by + 8);
        found = false;
        for (auto& d : r.dithers) if (d == want) found = true;
        check(found, "昼でも右面(いつも影)は松明の光でディザがかかる(明るさ1)");

        // 保存して開き直しても同じ明るさ(明るさは保存せず、読み込むときに計算する)
        HostSd::files.clear();
        {
            World v;
            v.setOccluders(occ);
            int x, y, z, cur;
            v.create("/w/t", FLAT, 3, 6, x, y, z);
            v.loadChunk(2, 2);
            v.set(20, 1, 20, TORCH);
            const int l0 = v.light(22, 1, 20);
            check(v.save(1, 1, 1, TORCH), "松明を置いたワールドを保存できる");
            v.close();
            const char* err = nullptr;
            check(v.open("/w/t", x, y, z, cur, err) && cur == TORCH, "開き直せる(選んでいたブロックが松明)");
            v.pump(1000, 0);
            v.loadChunk(2, 2);
            check(v.get(20, 1, 20) == TORCH && v.light(22, 1, 20) == l0 && l0 == 3, "開き直しても同じ明るさ");
        }
    }

    // ================================================================ 人や物(エンティティ)
    {
        // セルの中に収まる人や物は、画家の順の途中(そのセルの所)へ挟んだのと同じ絵になる。
        // 答え: 「人や物より奥のブロックだけのワールド」を描き、人や物を描き、「手前のブロックだけのワールド」を重ねる
        // (手前 = s が小さいか、同じ s で y が大きい。日の光は切る: 影がブロックの有る無しで変わるので)
        // 絵は箱の画面の形の中に収まる大きさにする(はみ出す所は近似なので)
        std::mt19937 rng(7);
        MakeSprite(10, 14, 12);
        int mism = 0, total = 0, covered = 0, hidden_some = 0;
        for (int iter = 0; iter < 120; iter++) {
            World full, back, front;
            for (World* w : {&full, &back, &front}) { EmptyWorld(*w); w->setOccluders(occ); w->setSunlight(false); }
            // 人や物の居るセル(空気)と、まわりの乱数のブロック
            const int ex = 4 + (int)(rng() % 3), ey = 1 + (int)(rng() % 3), ez = 4 + (int)(rng() % 3);
            const int es = ex + ez;
            for (int n = 0; n < 60; n++) {
                const int x = 1 + (int)(rng() % 9), y = (int)(rng() % 6), z = 1 + (int)(rng() % 9);
                if (x == ex && z == ez && y == ey) continue;
                // 葉は使わない(葉どうしの面は描かない決まりなので、ワールドを分けた答えの方に余計な面が出る)
                uint8_t b = (uint8_t)(STONE + rng() % 20);
                if (b == LEAVES) b = BRICKS;
                full.set(x, y, z, b);
                const int s2 = x + z;
                ((s2 < es || (s2 == es && y > ey)) ? front : back).set(x, y, z, b);
            }
            Entity e;
            e.x = ex + 0.5f; e.y = (float)ey; e.z = ez + 0.5f; e.r = 0.3f; e.h = 0.95f;
            e.image = 7; e.sw = (int16_t)SPW; e.sh = (int16_t)SPH; e.ax = 5; e.ay = 13; e.shadow = false;
            // 答え
            memset(fb, 0, sizeof fb);
            Rec r1; r1.raster = true; r1.cy0 = 20; r1.cy1 = 224;
            back.setCursor(0, 0, 0, false);
            back.render(World::Sink{&r1, Draw, Dirty, Dither}, 0, 20, 240, 224);
            int dx, dy;
            {
                float fx, fy;
                back.project(e.x, e.y, e.z, fx, fy);
                dx = (int)floorf(fx + 0.5f) - e.ax; dy = (int)floorf(fy + 0.5f) - e.ay;
            }
            for (int y = 0; y < SPH; y++) for (int x = 0; x < SPW; x++) {
                const int c = ImagePx(nullptr, 7, x, y);
                if (c >= 0) PutPx(&r1, dx + x, dy + y, c);
            }
            front.setCursor(0, 0, 0, false);
            front.render(World::Sink{&r1, Draw, Dirty, Dither}, 0, 20, 240, 224);
            static uint8_t want[320][240];
            memcpy(want, fb, sizeof fb);
            // エンジン
            memset(fb, 0, sizeof fb);
            Rec r2; r2.raster = true; r2.cy0 = 20; r2.cy1 = 224;
            full.setCursor(0, 0, 0, false);
            full.entityAdd(World::Sink{}, e);
            full.render(EntSink(r2), 0, 20, 240, 224);
            int diff = 0, spr_px = 0;
            for (int y = 20; y < 224; y++) for (int x = 0; x < 240; x++) if (fb[y][x] != want[y][x]) diff++;
            for (int y = 0; y < SPH; y++) for (int x = 0; x < SPW; x++) {
                if (dy + y >= 20 && dy + y < 224 && ImagePx(nullptr, 7, x, y) >= 0) {
                    spr_px++;
                    if (fb[dy + y][dx + x] == 12) covered++;
                }
            }
            total += spr_px;
            if (covered < total) hidden_some++;
            if (diff) {
                mism++;
                if (mism <= 3) {
                    printf("  違い: 人や物 (%d,%d,%d) 画素 %d:", ex, ey, ez, diff);
                    for (int y = 20; y < 224; y++) for (int x = 0; x < 240; x++) if (fb[y][x] != want[y][x]) {
                        // その画素に見えているブロック
                        int bx, by, bz;
                        const Face f = full.pick(x, y, bx, by, bz);
                        printf(" (%d,%d %d->%d blk %d,%d,%d f%d)", x, y, want[y][x], fb[y][x], bx, by, bz, (int)f);
                        y = 999; break;
                    }
                    printf("\n");
                }
            }
        }
        char msg[128];
        snprintf(msg, sizeof msg, "人や物: 画家の順へ挟んだ答えと画素が同じ(乱数のワールド120個、違ったもの %d 個)", mism);
        check(mism == 0, msg);
        check(covered > 0 && covered < total && hidden_some > 0, "人や物: 見えている所も、手前のブロックに隠れた所もある");
    }
    {
        // 壁の裏/手前・柱の手前・水・影
        MakeSprite(12, 24, 12);
        auto setup = [&](World& w) {
            EmptyWorld(w);
            w.setOccluders(occ);
            w.setCursor(0, 0, 0, false);
            for (int x = 0; x < 12; x++) for (int z = 0; z < 12; z++) w.set(x, 0, z, STONE);
        };
        auto count = [&](World& w, int handle) {
            memset(fb, 0, sizeof fb);
            Rec r; r.raster = true; r.cy0 = 20; r.cy1 = 224;
            w.render(EntSink(r), 0, 20, 240, 224);
            const Entity* e = w.entity(handle);
            float fx, fy;
            w.project(e->x, e->y, e->z, fx, fy);
            const int dx = (int)floorf(fx + 0.5f) - e->ax, dy = (int)floorf(fy + 0.5f) - e->ay;
            int n = 0;
            for (int y = 0; y < SPH; y++) for (int x = 0; x < SPW; x++) {
                if (ImagePx(nullptr, 7, x, y) >= 0 && fb[dy + y][dx + x] == 12) n++;
            }
            return n;
        };
        Entity e;
        e.image = 7; e.sw = (int16_t)SPW; e.sh = (int16_t)SPH; e.ax = 6; e.ay = 23; e.r = 0.25f; e.h = 1.5f;
        e.shadow = false;
        const int all = SPW * SPH - 4;
        {
            World w;
            setup(w);
            e.x = 5.5f; e.y = 1; e.z = 5.5f;
            const int h = w.entityAdd(World::Sink{}, e);
            check(h != 0 && count(w, h) == all, "人や物: 何も無い所では絵が全部見える");
            // 奥(+x)の壁: 見えたまま
            for (int y = 1; y < 4; y++) for (int z = 3; z < 9; z++) w.set(6, y, z, STONE);
            check(count(w, h) == all, "人や物: 奥の壁の手前に立つと全部見える");
            // 手前(-x)の壁: 隠れる
            for (int y = 1; y < 4; y++) for (int z = 3; z < 9; z++) w.set(4, y, z, BRICKS);
            check(count(w, h) == 0, "人や物: 手前の壁の裏に回ると隠れる");
        }
        {
            World w;
            setup(w);
            e.x = 5.5f; e.y = 1; e.z = 5.5f;
            const int h = w.entityAdd(World::Sink{}, e);
            w.set(4, 1, 5, STONE);   // 手前の低いブロック(1段): 足元だけ隠れる
            const int n = count(w, h);
            check(n > 0 && n < all, "人や物: 手前の1段のブロックは足元だけを隠す");
            // 箱の上(高さ2)へ乗ると全部見える(足元のブロックは奥)
            w.set(5, 1, 5, STONE);
            w.entity(h)->y = 2;
            check(count(w, h) == all, "人や物: ブロックの上に乗ると全部見える");
            // 少しずれた所(セルをまたぐ)でも、奥のブロックに隠れない
            w.set(5, 1, 5, AIR);
            w.set(4, 1, 5, AIR);
            w.set(6, 1, 6, STONE); w.set(6, 2, 6, STONE); w.set(7, 1, 5, STONE); w.set(7, 2, 5, STONE);
            w.entity(h)->y = 1;
            int worst = all;
            for (int k = 0; k <= 10; k++) {
                w.entity(h)->x = 5.3f + 0.04f * k;
                w.entity(h)->z = 5.7f - 0.04f * k;
                worst = std::min(worst, count(w, h));
            }
            check(worst == all, "人や物: セルをまたいで動いても奥のブロックに隠れない");
        }
        {
            World w;
            setup(w);
            for (int x = 3; x < 9; x++) for (int z = 3; z < 9; z++) w.set(x, 1, z, WATER);
            e.x = 5.5f; e.y = 1; e.z = 5.5f;
            const int h = w.entityAdd(World::Sink{}, e);
            const int n = count(w, h);
            check(n > 0 && n < all, "人や物: 水の中では水面が手前に来て一部が透けて見える");
            // 水面より上(絵の上の方)は隠れない: 水面の人や物より奥の所は手前に来ない
            float fx, fy;
            w.project(e.x, e.y, e.z, fx, fy);
            const int dx = (int)floorf(fx + 0.5f) - e.ax, dy = (int)floorf(fy + 0.5f) - e.ay;
            int top_ok = 0, top_all = 0, low_hidden = 0;
            for (int y = 0; y < SPH; y++) for (int x = 0; x < SPW; x++) {
                if (ImagePx(nullptr, 7, x, y) < 0) continue;
                if (dy + y < (int)fy - 18) { top_all++; if (fb[dy + y][dx + x] == 12) top_ok++; }
                if (dy + y > (int)fy - 6 && fb[dy + y][dx + x] != 12) low_hidden++;
            }
            check(top_all > 0 && top_ok == top_all && low_hidden > 0, "人や物: 水から出ている所はそのまま、沈んだ所は水越し");
            check(w.ground(5.5f, 1.0f, 5.5f) == 1 && w.ground(5.5f, 1.5f, 5.5f) == 1 && w.ground(5.5f, 2.0f, 5.5f) == 2,
                  "ground: 水の中に立っていれば水の底、水面より上なら水面");
        }
        {
            // 影: 地面の上面に市松模様で。高く上がると小さく。壁の裏の地面の影は壁が隠す
            World w;
            setup(w);
            e.shadow = true; e.shadow_color = 3;
            e.x = 5.5f; e.y = 1; e.z = 5.5f;
            const int h = w.entityAdd(World::Sink{}, e);
            float cx, cy, a, b;
            int gy;
            check(w.shadowShape(*w.entity(h), cx, cy, a, b, gy) && gy == 1, "影: 地面の高さ");
            check(w.ground(5.5f, 1.0f, 5.5f) == 1 && w.ground(5.5f, 0.0f, 5.5f) == -1 && w.ground(10.5f, 3.f, 10.5f) == 1 && w.ground(20.f, 3.f, 20.f) == -1,
                  "影: ground は足の裏より下の一番上の地面");
            // 影の画素 = 影ありと影なしで違う画素
            auto shadowPx = [&]() {
                static uint8_t base[320][240];
                MakeSprite(12, 24, 12);
                std::fill(spr.begin(), spr.end(), 0xFF);   // 絵は全部透過(影だけ見る)
                Entity* p = w.entity(h);
                const bool on = p->shadow;
                memset(fb, 0, sizeof fb);
                Rec r; r.raster = true; r.cy0 = 20; r.cy1 = 224;
                p->shadow = false;
                w.render(EntSink(r), 0, 20, 240, 224);
                memcpy(base, fb, sizeof fb);
                p->shadow = on;
                memset(fb, 0, sizeof fb);
                w.render(EntSink(r), 0, 20, 240, 224);
                int n = 0;
                for (int y = 20; y < 224; y++) for (int x = 0; x < 240; x++) if (fb[y][x] != base[y][x]) n++;
                return n;
            };
            const int s0 = shadowPx();
            w.entity(h)->y = 4;
            const int s1 = shadowPx();
            check(s0 > 8 && s1 > 0 && s1 < s0, "影: 地面に落ち、高く上がると小さくなる");
            w.entity(h)->y = 1;
            for (int y = 1; y < 4; y++) for (int z = 3; z < 9; z++) w.set(4, y, z, BRICKS);
            check(shadowPx() == 0, "影: 手前の壁の裏の影は壁が隠す");
            w.entity(h)->shadow = false;
            check(shadowPx() == 0, "影: shadow = false なら落とさない");
            MakeSprite(12, 24, 12);
        }
        {
            // 描き直す範囲・引き当て・ハンドル・反転
            World w;
            setup(w);
            e.shadow = true;
            e.x = 5.5f; e.y = 1; e.z = 5.5f;
            dirties.clear();
            Rec r;
            const World::Sink sink = EntSink(r);
            const int h = w.entityAdd(sink, e);
            int x0, y0, x1, y1;
            check(w.entityRect(*w.entity(h), x0, y0, x1, y1) && dirties.size() == 1, "置くと絵と影の範囲を描き直す");
            float fx, fy;
            w.project(5.5f, 1, 5.5f, fx, fy);
            check(x0 <= (int)fx - 6 && x1 >= (int)fx + 6 && y0 <= (int)fy - 23 && y1 > (int)fy, "範囲は絵を含む");
            dirties.clear();
            w.entity(h)->x = 7.5f;
            w.entityChanged(sink, h);
            check(dirties.size() == 2, "動かすと前の所と今の所を描き直す");
            const int fx2 = (int)floorf(fx + 0.5f) + 32;   // x を 2 増やすと画面で右へ 32px、上へ 16px
            const int fy2 = (int)floorf(fy + 0.5f) - 16;
            check(w.entityAt(sink, fx2, fy2 - 10) == h, "引き当て: 絵の不透明な所");
            check(w.entityAt(sink, fx2 - 6, fy2 - 23) == 0, "引き当て: 絵の透過した所(角)は当たらない");
            // 手前のもう1つ
            Entity e2 = e;
            e2.x = 6.5f; e2.y = 1; e2.z = 5.5f;   // 少し手前…ではなく奥と手前を比べる: (7.5,5.5) より x が小さい = 手前
            const int h2 = w.entityAdd(sink, e2);
            w.project(6.5f, 1, 5.5f, fx, fy);
            check(w.entityAt(sink, (int)floorf(fx + 0.5f) + 5, (int)floorf(fy + 0.5f) - 10) == h2, "引き当て: 重なれば手前のもの");
            check(w.entityRemove(sink, h2) && !w.entity(h2) && w.entityCount() == 1, "取り除くとハンドルは無効");
            const int h3 = w.entityAdd(sink, e2);
            check(h3 != h2 && !w.entity(h2) && w.entity(h3), "取り除いた所を使い回してもハンドルは別物");
            int n = w.entityCount();
            while (w.entityAdd(sink, e2)) n++;
            check(n == kMaxEntities && w.entityCount() == kMaxEntities, "置けるのは kMaxEntities 個まで");
            w.entityClear(sink);
            check(w.entityCount() == 0 && !w.entity(h), "全部片付ける");
            // 反転: 左右で色の違う絵
            MakeSprite(12, 24, 12);
            for (int y = 0; y < 24; y++) for (int x = 0; x < 6; x++) if (spr[(size_t)y * 12 + x] != 0xFF) spr[(size_t)y * 12 + x] = 9;
            Entity e3 = e;
            e3.shadow = false; e3.flip = true;
            const int h4 = w.entityAdd(sink, e3);
            memset(fb, 0, sizeof fb);
            Rec rr; rr.raster = true; rr.cy0 = 20; rr.cy1 = 224;
            w.render(EntSink(rr), 0, 20, 240, 224);
            w.project(e3.x, e3.y, e3.z, fx, fy);
            const int dx = (int)floorf(fx + 0.5f) - 6, dy = (int)floorf(fy + 0.5f) - 23;
            check(w.entity(h4) && fb[dy + 10][dx + 1] == 12 && fb[dy + 10][dx + 10] == 9, "反転: 左右が入れ替わる");
            // ワールドを閉じると片付く
            w.close();
            check(w.entityCount() == 0, "ワールドを閉じると片付く");
            MakeSprite(12, 24, 12);
        }
        {
            // 前後の判定
            const Box b{5.2f, 1.0f, 5.2f, 5.8f, 2.5f, 5.8f};
            check(World::InFront(b, 4, 1, 5, STONE) && !World::InFront(b, 6, 1, 5, STONE), "前後: -x は手前、+x は奥");
            check(World::InFront(b, 5, 1, 4, STONE) && !World::InFront(b, 5, 1, 6, STONE), "前後: -z は手前、+z は奥");
            check(World::InFront(b, 5, 3, 5, STONE) && !World::InFront(b, 5, 0, 5, STONE), "前後: 上は手前、足元は奥");
            check(World::InFront(b, 5, 1, 5, WATER) && !World::InFront(b, 5, 1, 5, STONE), "前後: 重なった水は手前、ブロックは奥");
            const Box c{5.3f - 0.3f, 1.0f, 5.5f, 5.6f, 2.0f, 5.9f};   // x0 = 4.9999…(小数の誤差)
            check(World::InFront(c, 4, 1, 5, STONE), "前後: 面で接していれば小数の誤差があっても手前");
        }
    }

    {
        // 描き直す範囲: 人や物を動かす・跳ねる・反転・隠す・足す・取り除く、足元のブロックを置く/壊すたびに、
        // 頼まれた矩形だけを描き直した画面が、全体を描き直した画面と同じになる
        std::mt19937 rng(11);
        MakeSprite(12, 22, 12);
        World w;
        EmptyWorld(w);
        w.setOccluders(occ);
        w.setCursor(0, 0, 0, false);
        for (int x = 0; x < 14; x++) for (int z = 0; z < 14; z++) {
            w.set(x, 0, z, STONE);
            if (rng() % 4 == 0) w.set(x, 1, z, GRASS);
            if (rng() % 9 == 0) { w.set(x, 1, z, BRICKS); w.set(x, 2, z, BRICKS); }
        }
        for (int x = 9; x < 13; x++) for (int z = 2; z < 6; z++) w.set(x, 1, z, WATER);
        auto full = [&](uint8_t (*out)[240]) {
            memset(fb, 0, sizeof fb);
            Rec r; r.raster = true; r.cy0 = 20; r.cy1 = 224;
            w.render(EntSink(r), 0, 20, 240, 224);
            memcpy(out, fb, sizeof fb);
        };
        static uint8_t cur[320][240], want[320][240];
        std::vector<int> hs;
        Rec rr;
        const World::Sink dsink = EntSink(rr);
        for (int i = 0; i < 6; i++) {
            Entity e;
            e.image = 7; e.sw = 12; e.sh = 22; e.ax = 6; e.ay = 21; e.r = 0.2f; e.h = 1.4f; e.shadow_color = 4;
            e.x = 2.5f + i * 1.7f; e.z = 2.5f + (i % 3) * 3.1f;
            e.y = (float)w.ground(e.x, 16, e.z);
            hs.push_back(w.entityAdd(dsink, e));
        }
        full(cur);
        int bad = 0;
        for (int step = 0; step < 300; step++) {
            dirties.clear();
            const int h = hs[rng() % hs.size()];
            Entity* e = w.entity(h);
            const int op = (int)(rng() % 10);
            if (!e) {
                Entity n;
                n.image = 7; n.sw = 12; n.sh = 22; n.ax = 6; n.ay = 21; n.x = 6.5f; n.z = 6.5f;
                n.y = (float)std::max(0, w.ground(6.5f, 16, 6.5f));
                for (int& hh : hs) if (hh == h) hh = w.entityAdd(dsink, n);
            } else if (op < 5) {
                e->x = std::min(13.5f, std::max(0.5f, e->x + ((int)(rng() % 9) - 4) * 0.13f));
                e->z = std::min(13.5f, std::max(0.5f, e->z + ((int)(rng() % 9) - 4) * 0.13f));
                e->y = (float)std::max(0, w.ground(e->x, 16, e->z)) + ((rng() % 3 == 0) ? (rng() % 30) * 0.1f : 0.0f);
                w.entityChanged(dsink, h);
            } else if (op == 5) {
                e->flip = !e->flip; w.entityChanged(dsink, h);
            } else if (op == 6) {
                e->visible = !e->visible; w.entityChanged(dsink, h);
            } else if (op == 7) {
                w.entityRemove(dsink, h);
            } else {
                // 足元のまわりのブロックを置く/壊す
                const int bx = (int)floorf(e->x) + (int)(rng() % 3) - 1, bz = (int)floorf(e->z) + (int)(rng() % 3) - 1;
                const int by = 1 + (int)(rng() % 2);
                w.set(bx, by, bz, w.get(bx, by, bz) ? AIR : COBBLE);
                w.dirtyEdit(dsink, bx, by, bz);
            }
            // 頼まれた矩形だけ描き直す
            for (const std::string& d : dirties) {
                int x, y, ww, hh;
                sscanf(d.c_str(), "%d %d %d %d", &x, &y, &ww, &hh);
                memcpy(fb, cur, sizeof fb);
                Rec r; r.raster = true; r.cx0 = x; r.cy0 = y; r.cx1 = x + ww; r.cy1 = y + hh;
                for (int yy = y; yy < y + hh; yy++) for (int xx = x; xx < x + ww; xx++) fb[yy][xx] = 0;
                w.render(EntSink(r), x, y, x + ww, y + hh);
                memcpy(cur, fb, sizeof fb);
            }
            full(want);
            if (memcmp(cur, want, sizeof cur) != 0) { bad++; memcpy(cur, want, sizeof cur); }
        }
        char msg[128];
        snprintf(msg, sizeof msg, "人や物: 頼まれた矩形だけ描き直しても全体を描き直したのと同じ(300回、違ったもの %d 回)", bad);
        check(bad == 0, msg);
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
