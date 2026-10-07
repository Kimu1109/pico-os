// 2.5Dの箱庭のエンジン(src/iso/Iso_World・Iso_Blit)のテスト。Luaアプリ「ブロック」の view.lua / world.lua を
// C++へ移したもの:
//   地形の生成がLua版と同じ(Lua版で作ったチャンクのハッシュと比べる。既存のセーブの書き換えていないチャンクを
//     種から作り直すので、1バイトでも違うと地形が変わる)
//   ブロックの読み書き・チャンクの境目・世界の外、読み込みの範囲と手放し・置き場が足りないとき
//   保存と読み込みの往復・壊れたファイル・前の版(BLK1)からの移し替え
//   描画: 見えない面を描かない・影(三角形2つ)・水・描く範囲の絞り込み、Lua版と同じ描画の並び
//   隠れたブロックを描かない(省略しても画素が1つも変わらないこと。乱数のワールドで突き合わせる)
//   タップ位置の引き当て・描き直す範囲
//   面の写し方(FaceBlitter)が素朴な1画素ずつの写し方と同じになること
// 面の絵はリポジトリの pc/sdcard/lua/apps/ブロック/faces.pimg を使う(第1引数にリポジトリのルート)。
#include "iso/Iso_World.hpp"
#include "iso/Iso_Blit.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

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
    w.render(World::Sink{&r, Draw, Dirty}, 0, 20, 240, 224);
    return r.lines;
}

// 小さな空のワールド(K=6、48x48)を全部読み込む
static void EmptyWorld(World& w) {
    int x, y, z;
    w.create("", EMPTY, 0, 6, x, y, z);
    for (int cx = 0; cx < 6; cx++) for (int cz = 0; cz < 6; cz++) w.loadChunk(cx, cz);
    w.setView(0, 20, 240, 204);
    w.setOrigin(100, 150);
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
    check(SW == 208 && SH == 575 && (int)sheet.size() == SW * SH, "faces.pimg は 208x575");
    const uint32_t occ = World::ComputeOccluders(SheetPx, nullptr);
    {
        World w;
        check(occ == w.occluders(), "透けないブロック: 水・葉以外(絵から求めたものと既定が同じ)");
        check(!((occ >> LEAVES) & 1) && ((occ >> STONE) & 1) && ((occ >> GOLD) & 1), "透けないブロック: 葉は穴が開いている");
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
        check(rc.lines.size() == 3 && rc.lines[0].rfind("0 552 ", 0) == 0, "カーソルを最後に描く");
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
        std::string all;
        for (auto& l : r.lines) { all += l; all += '\n'; }
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
            w.setView(0, 20, 240, 204);
            w.setOrigin((int)(rng() % 200), 300 + (int)(rng() % 300));
            const int cx0 = (int)(rng() % 120), cy0 = 20 + (int)(rng() % 100);
            const int cx1 = (t & 1) ? 240 : cx0 + 1 + (int)(rng() % 120), cy1 = (t & 1) ? 224 : cy0 + 1 + (int)(rng() % 100);
            Rec r;
            r.raster = true; r.cx0 = cx0; r.cy0 = cy0; r.cx1 = cx1; r.cy1 = cy1;
            w.setCursor(0, 0, 0, false);
            memset(fb, 7, sizeof fb);
            w.setCulling(false);
            w.render(World::Sink{&r, Draw, Dirty}, cx0, cy0, cx1, cy1);
            f0 += w.lastFaces();
            memcpy(ref, fb, sizeof fb);
            memset(fb, 7, sizeof fb);
            w.setCulling(true);
            w.render(World::Sink{&r, Draw, Dirty}, cx0, cy0, cx1, cy1);
            f1 += w.lastFaces();
            if (memcmp(ref, fb, sizeof fb)) bad++;
        }
        char msg[128];
        snprintf(msg, sizeof msg, "隠れたブロックを省いても画素は1つも変わらない (150回、面 %ld → %ld)", f0, f1);
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
        check(exact == 22 * 9, "透けないブロック22種の9枚の絵は形どおり(区間で写せる)");
        std::mt19937 rng(5);
        int bad = 0;
        std::vector<uint8_t> d1(120 * 320), d2;
        for (int t = 0; t < 20000; t++) {
            for (auto& v : d1) v = (uint8_t)rng();
            d2 = d1;
            const int row = rng() % 25, c = rng() % 9;
            int sx = FaceBlitter::kColX[c], sy = row * kRowH;
            const int shape = FaceBlitter::ShapeOf(c);
            const int w = shape == 0 ? 32 : 16, h = FaceBlitter::Height(shape);
            if (rng() % 10 == 0) { sx = rng() % 176; sy = rng() % (SH - h); }
            const int dx = (int)(rng() % 300) - 40, dy = (int)(rng() % 360) - 20;
            const int cx0 = rng() % 240, cx1 = cx0 + (int)(rng() % (241 - cx0));
            const int cy0 = rng() % 320, cy1 = cy0 + (int)(rng() % (321 - cy0));
            b.setTarget(d1.data(), 120, cx0, cy0, cx1, cy1);
            b.draw(sx, sy, w, h, dx, dy);
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                const int X = dx + x, Y = dy + y;
                if (X < cx0 || X >= cx1 || Y < cy0 || Y >= cy1) continue;
                const uint8_t v = sheet[(size_t)(sy + y) * SW + sx + x];
                if (v) FaceBlitter::Put(&d2[(size_t)Y * 120], X, v);
            }
            if (d1 != d2) bad++;
        }
        check(bad == 0, "面の写し方が1画素ずつの写し方と同じ(2万回、位置・クリップ・偶奇はばらばら)");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
