// 2.5Dの箱庭のエンジンに足した、タワーディフェンス向けの道具のテスト:
//   流れの場(Iso_Flow): 平らな所の値段と向き・壁の隙間を通る・通り抜けられるブロック(pass と body_cost)と、その上には立てないこと・
//     届かない所・少しずつ作る間は前の結果を答えること・乱数の地形で経路探索(FindPath)と同じ値段・次をたどると着くこと・斜め
//   視線(lineOfSight): 壁で遮られる・pass と水は通す・出発点と到着点のマスは見ない
//   近くの人や物(nearby): 範囲・tag・近い順・max
//   押し合い(crowdStep): 重なりが解ける・動かないもの・重さ・壁へ押し込まない・高さが違えば押さない・描き直しを頼む
//   弾(Shot): 狙った人や物へ必ず当たる・動く相手を追う・消えたら最後の位置で lost・点を狙う・山なり・満杯
//   HPバーと印: 壁の裏にいても一番上に描かれる・描き直す範囲に入る
//   世界全体を読み込んだままにする(setKeepAll)
//   ARENA の地形: ベースと出現位置が平ら・丸石と砂利・木が無い・多くの種で出現位置からベースへ道がある・種で決まる
// 面の絵はリポジトリの pc/sdcard/lua/apps/ブロック/faces.pimg を使う(第1引数にリポジトリのルート)。
#include "iso/Iso_World.hpp"
#include "iso/Iso_Path.hpp"
#include "iso/Iso_Flow.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cmath>
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

static int SW = 0, SH = 0;
static std::vector<uint8_t> sheet;
static uint8_t fb[320][240];
static int dirty_count = 0;
static int dx0, dy0, dx1, dy1;   // 描き直しを頼まれた範囲の外接矩形

static void Draw(void*, int sx, int sy, int w, int h, int dx, int dy) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int X = dx + x, Y = dy + y;
            if (X < 0 || X >= 240 || Y < 0 || Y >= 320) continue;
            const uint8_t c = sheet[(size_t)(sy + y) * SW + sx + x];
            if (c) fb[Y][X] = c;
        }
    }
}
static void Dither(void* p, int sx, int sy, int w, int h, int dx, int dy, int) { Draw(p, sx, sy, w, h, dx, dy); }
static void Dirty(void*, int x, int y, int w, int h) {
    if (dirty_count == 0) { dx0 = x; dy0 = y; dx1 = x + w; dy1 = y + h; }
    else { dx0 = std::min(dx0, x); dy0 = std::min(dy0, y); dx1 = std::max(dx1, x + w); dy1 = std::max(dy1, y + h); }
    dirty_count++;
}
static int FacePx(void*, int x, int y) { return (x >= 0 && y >= 0 && x < SW && y < SH) ? sheet[(size_t)y * SW + x] : 0; }
static int ImagePx(void*, int32_t img, int x, int y) {
    if (img != 7 || x < 0 || y < 0 || x >= 8 || y >= 16) return -1;
    return 12;
}
static void Put(void*, int x, int y, int c) {
    if (x >= 0 && x < 240 && y >= 0 && y < 320) fb[y][x] = (uint8_t)c;
}
static World::Sink MakeSink() {
    World::Sink s;
    s.draw = Draw;
    s.dirty = Dirty;
    s.dither = Dither;
    s.face_px = FacePx;
    s.image_px = ImagePx;
    s.put = Put;
    return s;
}

// 空の 48x48(K=6)を全部読み込み、y=0 に石の床
static void FlatWorld(World& w) {
    int x, y, z;
    w.create("", EMPTY, 0, 6, x, y, z);
    for (int cx = 0; cx < 6; cx++) for (int cz = 0; cz < 6; cz++) w.loadChunk(cx, cz);
    for (int i = 0; i < 48; i++) for (int k = 0; k < 48; k++) w.set(i, 0, k, STONE);
    w.setView(0, 20, 240, 204);
    w.setOrigin(120, 300);   // (10.5, 1, 10.5) が画面の真ん中あたり
}

static Entity Ent(float x, float y, float z) {
    Entity e;
    e.image = 7;
    e.x = x; e.y = y; e.z = z;
    e.sw = 8; e.sh = 16; e.ax = 4; e.ay = 15;
    e.r = 0.3f; e.h = 1.0f;
    e.shadow = false;
    return e;
}

// 次をたどって目的地へ着くまでの歩数(着かなければ -1)
static int Follow(const Flow& f, int x, int z, int limit) {
    for (int i = 0; i < limit; i++) {
        int nx, nz;
        bool goal;
        if (!f.next(x, z, nx, nz, &goal)) return goal ? i : -1;
        x = nx; z = nz;
    }
    return -1;
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
    }
    const World::Sink sink = MakeSink();

    // ================================================================ 流れの場
    {
        static World w;
        FlatWorld(w);
        Flow f;
        PathRules r;
        const int16_t gx[1] = {40}, gz[1] = {40};
        check(f.begin(w, r, gx, gz, 1), "流れの場: 作り始められる");
        check(!f.ready() && f.dist(0, 0) < 0, "流れの場: 出来上がる前は答えない");
        int steps = 0;
        while (!f.step(w, 100)) steps++;
        check(f.ready() && f.revision() == 1 && steps > 10, "流れの場: 少しずつ作って出来上がる");
        {
            const size_t n = (size_t)w.width() * w.width();
            check(f.memoryBytes() == n * 3, "流れの場: 出来上がったら結果の 3バイト/柱 だけ持つ(作業場所は返す)");
            Flow g;
            g.begin(w, r, gx, gz, 1);
            g.step(w, 1 << 20);
            const size_t peak_first = g.memoryBytes();
            g.begin(w, r, gx, gz, 1);
            check(g.memoryBytes() > n * 6 && g.memoryBytes() < n * 6 + 8192, "流れの場: 作り直しの間は結果2組と出番待ちの列だけ");
            check(g.dist(10, 40) == 30 && !g.failed(), "流れの場: 作り直しの間も前の結果を答える");
            while (!g.step(w, 500)) {}
            check(g.memoryBytes() == peak_first && g.revision() == 2, "流れの場: 作り直した後も 3バイト/柱 に戻る");
        }
        check(f.dist(40, 40) == 0 && f.standY(40, 40) == 1, "流れの場: 目的地は値段0、立つ高さ1");
        check(f.dist(10, 40) == 30 && f.dist(0, 0) == 80, "流れの場: 平らな所の値段は歩数(斜め無し)");
        int nx, nz;
        bool goal;
        check(f.next(10, 40, nx, nz) && nx == 11 && nz == 40, "流れの場: 次は目的地へ近づく柱");
        check(!f.next(40, 40, nx, nz, &goal) && goal, "流れの場: 目的地では次が無い(at_goal)");
        check(Follow(f, 0, 0, 200) == 80, "流れの場: 次をたどると歩数どおりに着く");

        // 壁(高さ3)で仕切り、z=5 にだけ隙間
        for (int z = 0; z < 48; z++) {
            if (z == 5) continue;
            for (int y = 1; y <= 3; y++) w.set(20, y, z, STONE);
        }
        f.begin(w, r, gx, gz, 1);
        // 作っている間は前の結果を答える
        f.step(w, 50);
        check(f.building() && f.dist(10, 40) == 30, "流れの場: 作り直している間は前の結果");
        while (!f.step(w, 1000)) {}
        check(f.revision() == 2, "流れの場: 作り直すと版が進む");
        // (10,40) → 隙間(20,5) → (40,40): |10-20|+|40-5| = 45、そこから 20+35 = 55 → 100
        check(f.dist(10, 40) == 100, "流れの場: 壁の隙間を回る");
        check(Follow(f, 10, 40, 300) == 100, "流れの場: 隙間を通ってたどり着く");
        check(f.dist(20, 10) < 0 || f.standY(20, 10) == 4, "流れの場: 壁の上(高さ4)には登れない(届かない)");

        // 隙間を通り抜けられるブロック(板)で塞ぐ: pass にすると通れて、body_cost だけ高くなる
        w.set(20, 1, 5, PLANKS);
        w.set(20, 2, 5, PLANKS);
        f.begin(w, r, gx, gz, 1);
        while (!f.step(w, 1000)) {}
        check(f.dist(10, 40) < 0, "流れの場: pass でなければ塞がる");
        r.pass = 1u << PLANKS;
        r.body_cost[PLANKS] = 2.5f;
        f.begin(w, r, gx, gz, 1);
        while (!f.step(w, 1000)) {}
        check(f.dist(10, 40) == 100 + 5, "流れの場: pass のブロックの中を通れる(体の2マスぶん body_cost)");
        check(f.standY(20, 5) == 1, "流れの場: pass のブロックの上には立たず、中(足元の床の上)に立つ");

        // 閉じ込められた柱は届かない
        for (int y = 1; y <= 3; y++) {
            w.set(2, y, 30, STONE); w.set(4, y, 30, STONE); w.set(3, y, 29, STONE); w.set(3, y, 31, STONE);
        }
        f.begin(w, r, gx, gz, 1);
        while (!f.step(w, 1000)) {}
        check(f.dist(3, 30) < 0 && !f.next(3, 30, nx, nz, &goal) && !goal, "流れの場: 囲まれた柱には届かない");

        // 斜め
        PathRules rd;
        rd.diagonal = true;
        World w2;
        FlatWorld(w2);
        f.begin(w2, rd, gx, gz, 1);
        while (!f.step(w2, 4096)) {}
        const float d = f.dist(30, 30);
        check(std::fabs(d - 10 * 1.375f) < 1e-4f, "流れの場: 斜めの値段(√2 を 1/8 単位へ丸めた 1.375)");
        check(f.next(30, 30, nx, nz) && nx == 31 && nz == 31, "流れの場: 斜めに進む");
    }
    // ---- 乱数の地形で経路探索と同じ値段 ----
    {
        static World w;
        std::mt19937 rng(12345);
        bool same = true, follow_ok = true;
        int compared = 0;
        for (int trial = 0; trial < 20 && same; trial++) {
            int x, y, z;
            w.create("", EMPTY, 0, 3, x, y, z);   // 24x24
            for (int cx = 0; cx < 3; cx++) for (int cz = 0; cz < 3; cz++) w.loadChunk(cx, cz);
            for (int i = 0; i < 24; i++) {
                for (int k = 0; k < 24; k++) {
                    const int hgt = 1 + (int)(rng() % 4);
                    for (int yy = 0; yy < hgt; yy++) w.set(i, yy, k, (rng() % 7 == 0 && yy == hgt - 1) ? SAND : STONE);
                }
            }
            PathRules r;
            r.max_up = 1; r.max_down = 2;
            r.up_cost = 0.5f; r.down_cost = 0.25f;
            r.block_cost[SAND] = 1.5f;
            r.max_nodes = 4096;
            const int16_t gx[2] = {(int16_t)(rng() % 24), 0}, gz[2] = {(int16_t)(rng() % 24), 0};
            Flow f;
            f.begin(w, r, gx, gz, 1);
            while (!f.step(w, 100000)) {}
            std::vector<PathPoint> out(kMaxPathNodes);
            for (int k = 0; k < 15; k++) {
                const int sx = (int)(rng() % 24), sz = (int)(rng() % 24);
                const PathResult res = FindPath(w, sx, -1, sz, gx[0], -1, gz[0], r, out.data(), (int)out.size());
                const float d = f.dist(sx, sz);
                if (res.status == PathStatus::Found) {
                    if (std::fabs(res.cost - d) > 1e-3f) {
                        printf("  不一致: (%d,%d)→(%d,%d) path=%f flow=%f\n", sx, sz, gx[0], gz[0], res.cost, d);
                        same = false;
                        break;
                    }
                    if (Follow(f, sx, sz, 2000) < 0) follow_ok = false;
                    compared++;
                } else if (res.status == PathStatus::NoPath && d >= 0) {
                    printf("  経路探索は届かないのに流れの場は %f\n", d);
                    same = false;
                    break;
                }
            }
        }
        check(same && compared > 100, "流れの場: 乱数の地形20個で経路探索と同じ値段");
        check(follow_ok, "流れの場: 次をたどると必ず目的地に着く");
    }
    {
        // 広すぎる世界は断る
        static World w;
        int x, y, z;
        w.create("", EMPTY, 0, 9, x, y, z);   // 72x72
        Flow f;
        PathRules r;
        const int16_t g[1] = {0};
        check(!f.begin(w, r, g, g, 1), "流れの場: 幅64を超える世界は断る");
    }

    // ================================================================ 視線
    {
        static World w;
        FlatWorld(w);
        check(w.lineOfSight(5.5f, 1.5f, 5.5f, 15.5f, 1.5f, 5.5f, 0), "視線: 何も無ければ通る");
        w.set(10, 1, 5, STONE);
        check(!w.lineOfSight(5.5f, 1.5f, 5.5f, 15.5f, 1.5f, 5.5f, 0), "視線: 石で遮られる");
        check(w.lineOfSight(5.5f, 3.5f, 5.5f, 15.5f, 3.5f, 5.5f, 0), "視線: 高い所からは越えて見える");
        w.set(10, 1, 5, PLANKS);
        check(!w.lineOfSight(5.5f, 1.5f, 5.5f, 15.5f, 1.5f, 5.5f, 0) &&
              w.lineOfSight(5.5f, 1.5f, 5.5f, 15.5f, 1.5f, 5.5f, 1u << PLANKS), "視線: pass のブロックは通す");
        w.set(10, 1, 5, WATER);
        check(w.lineOfSight(5.5f, 1.5f, 5.5f, 15.5f, 1.5f, 5.5f, 0), "視線: 水は通す");
        w.set(5, 1, 5, STONE);
        w.set(15, 1, 5, STONE);
        check(w.lineOfSight(5.5f, 1.5f, 5.5f, 15.5f, 1.5f, 5.5f, 0), "視線: 出発点と到着点のマスは見ない");
        check(!w.lineOfSight(15.5f, 1.5f, 15.5f, 15.5f, 1.5f, 2.5f, 0), "視線: 逆向き・別の軸でも遮られる");
        check(w.lineOfSight(14.5f, 2.5f, 14.5f, 14.5f, 0.5f, 14.5f, 0), "視線: 真下の足元(到着点)は見ない");
        check(w.lineOfSight(14.5f, 3.5f, 14.5f, 14.5f, 0.2f, 14.5f, 0), "視線: 縦の線");
        w.set(14, 1, 14, STONE);
        check(!w.lineOfSight(14.5f, 3.5f, 14.5f, 14.5f, 0.2f, 14.5f, 0), "視線: 縦の線も間のブロックで遮られる");
    }

    // ================================================================ 近くの人や物
    {
        static World w;
        FlatWorld(w);
        Entity a = Ent(10.5f, 1, 10.5f); a.tag = 1;
        Entity b = Ent(12.5f, 1, 10.5f); b.tag = 2;
        Entity c = Ent(11.0f, 1, 10.5f); c.tag = 2;
        Entity d = Ent(30.5f, 1, 10.5f); d.tag = 2;
        const int ha = w.entityAdd(sink, a), hb = w.entityAdd(sink, b), hc = w.entityAdd(sink, c), hd = w.entityAdd(sink, d);
        int32_t out[8];
        int n = w.nearby(10.5f, 10.5f, 5, 0, out, 8);
        check(n == 3 && out[0] == ha && out[1] == hc && out[2] == hb, "近く: 範囲の中を近い順に");
        n = w.nearby(10.5f, 10.5f, 5, 1u << 2, out, 8);
        check(n == 2 && out[0] == hc && out[1] == hb, "近く: tag で絞る");
        n = w.nearby(10.5f, 10.5f, 100, 1u << 2, out, 2);
        check(n == 2 && out[0] == hc && out[1] == hb, "近く: max で近いものだけ");
        n = w.nearby(29.0f, 10.5f, 2, 0, out, 8);
        check(n == 1 && out[0] == hd, "近く: 遠くの1つ");
        // 置き場は置いた数に合わせて広がり、96個まで置ける(World 自体は小さいまま)
        check(sizeof(World) < 6 * 1024, "人や物: World に置き場を持たない(実機でチャンクの置き場を確保できるように)");
        check(w.entityCapacity() == kEntityChunk, "人や物: 4個なら置き場は16個ぶんだけ");
        w.entityClear(sink);
        int ok = 0;
        std::vector<int> hs;
        for (int i = 0; i < kMaxEntities + 2; i++) {
            const int h = w.entityAdd(sink, Ent(1.5f + (i % 40), 1, 1.5f + i / 40));
            if (h) { ok++; hs.push_back(h); }
        }
        check(ok == kMaxEntities && kMaxEntities == 96 && w.entityCapacity() == kMaxEntities, "人や物: 96個まで置ける");
        bool same = true;
        for (size_t i = 0; i < hs.size(); i++) {
            const Entity* e = w.entity(hs[i]);
            if (!e || e->x != 1.5f + (float)(i % 40)) same = false;
        }
        check(same, "人や物: 置き場を広げても前のハンドルがそのまま使える");
        w.entityClear(sink);
    }

    // ================================================================ 押し合い
    {
        static World w;
        FlatWorld(w);
        Entity a = Ent(10.5f, 1, 10.5f); a.crowd = 1;
        Entity b = Ent(10.7f, 1, 10.5f); b.crowd = 1;
        const int ha = w.entityAdd(sink, a), hb = w.entityAdd(sink, b);
        dirty_count = 0;
        const int moved = w.crowdStep(sink, 2, 2, 0);
        const Entity* ea = w.entity(ha);
        const Entity* eb = w.entity(hb);
        float d = std::hypot(eb->x - ea->x, eb->z - ea->z);
        check(moved == 2 && std::fabs(d - 0.6f) < 1e-3f, "押し合い: 重なりが解ける(半径の和まで)");
        check(std::fabs((ea->x + eb->x) * 0.5f - 10.6f) < 1e-3f, "押し合い: 同じ重さなら半分ずつ");
        check(dirty_count > 0, "押し合い: 動いたら描き直しを頼む");
        check(w.crowdStep(sink, 2, 2, 0) == 0, "押し合い: 重なっていなければ動かない");

        // 動かないもの(タワー)は押されない
        w.entityClear(sink);
        Entity t = Ent(20.5f, 1, 20.5f); t.crowd = 2;
        Entity z = Ent(20.6f, 1, 20.5f); z.crowd = 1;
        const int ht = w.entityAdd(sink, t), hz = w.entityAdd(sink, z);
        w.crowdStep(sink, 2, 2, 0);
        check(w.entity(ht)->x == 20.5f && std::fabs(w.entity(hz)->x - 21.1f) < 1e-3f, "押し合い: fixed は動かず、相手だけ押し出される");

        // 重さ
        w.entityClear(sink);
        Entity h1 = Ent(5.5f, 1, 30.5f); h1.crowd = 1; h1.mass = 3;
        Entity h2 = Ent(5.9f, 1, 30.5f); h2.crowd = 1; h2.mass = 1;
        const int hh1 = w.entityAdd(sink, h1), hh2 = w.entityAdd(sink, h2);
        w.crowdStep(sink, 1, 2, 0);
        check(std::fabs((5.5f - w.entity(hh1)->x) - 0.05f) < 1e-3f && std::fabs((w.entity(hh2)->x - 5.9f) - 0.15f) < 1e-3f,
              "押し合い: 重いほど動かない(重さの逆数の比)");

        // 壁へは押し込まない
        w.entityClear(sink);
        for (int y = 1; y <= 2; y++) w.set(31, y, 30, STONE);
        Entity p = Ent(30.85f, 1, 30.5f); p.crowd = 1;
        Entity q = Ent(30.55f, 1, 30.5f); q.crowd = 1;
        const int hp = w.entityAdd(sink, p), hq = w.entityAdd(sink, q);
        w.crowdStep(sink, 1, 2, 0);
        check(w.entity(hp)->x == 30.85f, "押し合い: 壁の中へは押し込まない");
        check(w.entity(hq)->x < 30.55f, "押し合い: 壁際の相手はこちらが下がる側は動ける");
        // 高さが違えば押さない
        w.entityClear(sink);
        Entity u1 = Ent(8.5f, 1, 8.5f); u1.crowd = 1;
        Entity u2 = Ent(8.6f, 4, 8.5f); u2.crowd = 1;
        w.entityAdd(sink, u1); w.entityAdd(sink, u2);
        check(w.crowdStep(sink, 2, 2, 0) == 0, "押し合い: 高さが height 以上違えば押さない");
        // 押し合いをしないものは関係ない
        w.entityClear(sink);
        Entity n1 = Ent(8.5f, 1, 8.5f);
        Entity n2 = Ent(8.6f, 1, 8.5f); n2.crowd = 1;
        w.entityAdd(sink, n1); w.entityAdd(sink, n2);
        check(w.crowdStep(sink, 2, 2, 0) == 0, "押し合い: crowd の無いものは押さない");
        // ちょうど重なっていても離れる
        w.entityClear(sink);
        Entity s1 = Ent(15.5f, 1, 15.5f); s1.crowd = 1;
        const int hs1 = w.entityAdd(sink, s1), hs2 = w.entityAdd(sink, s1);
        w.crowdStep(sink, 2, 2, 0);
        check(std::hypot(w.entity(hs1)->x - w.entity(hs2)->x, w.entity(hs1)->z - w.entity(hs2)->z) > 0.59f,
              "押し合い: ちょうど同じ位置でも離れる");
        w.entityClear(sink);
    }

    // ================================================================ 弾
    {
        static World w;
        FlatWorld(w);
        const int target = w.entityAdd(sink, Ent(20.5f, 1, 10.5f));
        Shot s;
        s.x = 10.5f; s.y = 2; s.z = 10.5f;
        s.target = target;
        s.speed = 10;
        s.color = 8; s.size = 2; s.tag = 77;
        dirty_count = 0;
        const int h = w.shotAdd(sink, s);
        check(h && w.shotCount() == 1 && dirty_count == 1, "弾: 置けて描き直しを頼む");
        ShotHit hits[4];
        int n = 0;
        float t = 0;
        bool moved = false;
        while (n == 0 && t < 3) {
            n = w.shotsStep(sink, 0.1f, hits, 4);
            t += 0.1f;
            if (n == 0 && w.shot(h) && w.shot(h)->x > 11) moved = true;
            if (t > 0.45f && t < 0.55f) w.entity(target)->x = 22.5f;   // 途中で相手が動く
        }
        check(moved && n == 1 && hits[0].target == target && hits[0].tag == 77 && !hits[0].lost, "弾: 狙った人や物に当たる");
        check(std::fabs(hits[0].x - 22.5f) < 1e-3f && std::fabs(hits[0].y - 1.5f) < 1e-3f, "弾: 動いた相手を追い、体の中心に当たる");
        check(w.shotCount() == 0 && !w.shot(h), "弾: 当たったら消える");

        // 相手が消えたら最後の位置で lost
        const int target2 = w.entityAdd(sink, Ent(30.5f, 1, 10.5f));
        s.target = target2;
        w.shotAdd(sink, s);
        w.shotsStep(sink, 0.1f, hits, 4);
        w.entityRemove(sink, target2);
        n = 0;
        for (int i = 0; i < 50 && n == 0; i++) n = w.shotsStep(sink, 0.1f, hits, 4);
        check(n == 1 && hits[0].lost && hits[0].target == 0 && std::fabs(hits[0].x - 30.5f) < 1e-3f, "弾: 相手が消えたら最後の位置で lost");

        // 点を狙う・山なり
        Shot p;
        p.x = 5.5f; p.y = 1; p.z = 5.5f;
        p.tx = 15.5f; p.ty = 1; p.tz = 5.5f;
        p.arc = 0.5f; p.speed = 5;
        const int hp = w.shotAdd(sink, p);
        w.shotsStep(sink, 1.0f, hits, 4);
        const Shot* sp = w.shot(hp);
        check(sp && std::fabs(sp->x - 10.5f) < 1e-3f && std::fabs(sp->y - (1 + 0.5f * 10)) < 1e-3f, "弾: 山なり(真ん中で一番高い)");
        n = w.shotsStep(sink, 1.0f, hits, 4);
        check(n == 1 && hits[0].target == 0 && !hits[0].lost && std::fabs(hits[0].x - 15.5f) < 1e-3f, "弾: 点に着く");
        // 満杯
        int ok = 0;
        for (int i = 0; i < kMaxShots + 3; i++) if (w.shotAdd(sink, p)) ok++;
        check(ok == kMaxShots, "弾: 64個まで");
        // 返しきれない分は次の呼び出しへ
        int total = 0, calls = 0;
        while (w.shotCount() > 0 && calls < 20) { total += w.shotsStep(sink, 5.0f, hits, 4); calls++; }
        check(total == kMaxShots && calls == kMaxShots / 4, "弾: 返しきれない分は次の呼び出しで返す");
        w.shotAdd(sink, p);
        w.shotClear(sink);
        check(w.shotCount() == 0, "弾: shotClear");
        {
            static World w2;
            FlatWorld(w2);
            check(w2.shotCount() == 0 && !w2.shot(1 << 8 | 1), "弾: 撃つまで置き場は無い");
            ShotHit none[1];
            check(w2.shotsStep(sink, 0.1f, none, 1) == 0, "弾: 置き場が無くても進められる");
        }
        // 描く: 置いた所に色の点
        memset(fb, 15, sizeof fb);
        Shot q;
        q.x = 10.5f; q.y = 1; q.z = 10.5f; q.tx = 40; q.ty = 1; q.tz = 40; q.color = 8; q.size = 3;
        w.shotAdd(sink, q);
        float fx, fy;
        w.project(10.5f, 1, 10.5f, fx, fy);
        w.render(sink, 0, 0, 240, 320);
        check(fb[(int)floorf(fy + 0.5f)][(int)floorf(fx + 0.5f)] == 8, "弾: 描かれる");
        w.shotClear(sink);
        w.entityClear(sink);
    }

    // ================================================================ HPバーと印
    {
        static World w;
        FlatWorld(w);
        // 人や物の手前(-x 側)に高い壁 → 体は隠れるが、HPバーと印は見える
        for (int y = 1; y <= 4; y++) for (int z = 8; z <= 12; z++) w.set(9, y, z, STONE);
        Entity e = Ent(10.5f, 1, 10.5f);
        e.bar = 50; e.bar_color = 11; e.mark = 9;
        dirty_count = 0;
        const int h = w.entityAdd(sink, e);
        float fx, fy;
        w.project(10.5f, 1, 10.5f, fx, fy);
        const int cx = (int)floorf(fx + 0.5f), top = (int)floorf(fy + 0.5f) - 15;
        check(dirty_count == 1 && dy0 <= top - 9, "HPバー: 描き直す範囲に入る");
        memset(fb, 15, sizeof fb);
        w.render(sink, 0, 0, 240, 320);
        check(fb[top + 4][cx] != 12, "HPバー: 体は壁に隠れている");
        check(fb[top - 4][cx - 6] == 0 && fb[top - 3][cx - 5] == 11 && fb[top - 3][cx - 1] == 11 && fb[top - 3][cx + 1] == 0,
              "HPバー: 壁の手前に、黒い枠と半分の緑で描かれる");
        check(fb[top - 9][cx] == 9 && fb[top - 7][cx] == 9 && fb[top - 9][cx - 2] == 9, "印: 頭の上の三角");
        w.entity(h)->bar = -1;
        w.entity(h)->mark = -1;
        dirty_count = 0;
        w.entityChanged(sink, h);
        check(dirty_count == 2 && dy0 <= top - 9, "HPバー: 消すと前の範囲も描き直す");
    }

    // ================================================================ 世界全体を読み込む
    {
        static World w;
        int x, y, z;
        w.create("", NATURAL, 7, 7, x, y, z);   // 56x56 = 49 チャンク
        check(w.setKeepAll(true), "全体: 7x7 なら読み込んだままにできる");
        w.setView(0, 20, 240, 204);
        w.setOrigin(0, 0);
        while (w.pump(64, 0)) {}
        check(w.loadedCount() == 49, "全体: 49チャンクを全部読み込む");
        w.setOrigin(-500, 900);
        while (w.pump(64, 0)) {}
        check(w.loadedCount() == 49, "全体: 視点を動かしても手放さない");
        w.setKeepAll(false);
        w.pump(64, 0);
        check(w.loadedCount() < 49, "全体: やめると視点の外を手放す");
        w.create("", NATURAL, 7, 8, x, y, z);
        check(!w.setKeepAll(true), "全体: 8x8(64チャンク)は置き場に入らない");
    }

    // ================================================================ ARENA(タワーディフェンスの地形)
    {
        static World w;
        PathRules r;
        r.max_up = 1; r.max_down = 2; r.height = 2; r.up_cost = 0.5f; r.diagonal = true;
        int reachable = 0, flat_ok = 0, blocks_ok = 0, created = 0, n = 0;
        uint32_t sum = 0;
        for (uint32_t seed = 1; seed <= 24; seed++, n++) {
            int x, y, z;
            created += w.create("", ARENA, seed, 7, x, y, z);
            w.setKeepAll(true);
            while (w.pump(64, 0)) {}
            int bx, bz, sx[kArenaSpawns], sz[kArenaSpawns];
            w.arenaLayout(bx, bz, sx, sz);
            if (seed == 1) {
                check(x == bx && z == bz && y == w.arenaHeight(bx, bz) + 1, "ARENA: create はベースの中心を返す");
                check(bx == 28 && bz == 4 && sz[0] == 53 && sx[0] == 14 && sx[2] == 42, "ARENA: ベースは手前の端の真ん中、出現位置は奥の端に3つ");
                // 明るさを持たない(1チャンクあたり256バイトと作業場所を確保しない)。松明を置いても光らず、落ちない
                check(!w.lighting() && w.loadedCount() == 49
                      && w.poolBytes() == 49 * (offsetof(Chunk, b) + kCompactBytes), "ARENA: 柱ごとの形で持つ(明るさも持たない)");
                // 柱ごとの形でも、作った地形とブロックが1つも違わない
                {
                    static uint8_t full[kChunkBytes];
                    bool same = true;
                    for (int cx = 0; cx < 7 && same; cx++) for (int cz = 0; cz < 7 && same; cz++) {
                        w.generate(cx, cz, full);
                        for (int y = 0; y < H && same; y++) for (int i = 0; i < kLayer; i++) {
                            if (w.get(cx * 8 + i / 8, y, cz * 8 + i % 8) != full[y * kLayer + i]) { same = false; break; }
                        }
                    }
                    check(same, "ARENA: 柱ごとの形でも地形は同じ");
                    // 地面の上の1段は置ける/消せる。それより上や地面の中は書き換えない
                    const int fx = bx + 5, fz = bz + 5, fy = w.arenaHeight(fx, fz) + 1;
                    const uint8_t was = w.get(fx, fy, fz);
                    w.set(fx, fy, fz, 12);
                    const bool put = w.get(fx, fy, fz) == 12 && w.get(fx, fy + 1, fz) == AIR;
                    w.set(fx, fy + 1, fz, 12);
                    w.set(fx, fy - 2, fz, AIR);
                    const bool kept = w.get(fx, fy + 1, fz) == AIR && w.get(fx, fy - 2, fz) != AIR;
                    w.set(fx, fy, fz, AIR);
                    check(put && kept && w.get(fx, fy, fz) == was, "ARENA: 地面の上の1段だけ書き換えられる");
                }
                const int ty = w.arenaHeight(bx, bz) + 1;
                w.set(bx, ty, bz, TORCH);
                check(w.get(bx, ty, bz) == TORCH && w.light(bx, ty, bz) == 0 && w.light(bx + 1, ty, bz) == 0,
                      "ARENA: 松明は置けるが光らない");
                w.set(bx, ty, bz, AIR);
            }
            // ベースのまわり(7x7)と出現位置(3x3)は平らで、地面のブロックで見分けられる。水の上ではない
            bool flat = true, blk = true;
            const int hb = w.arenaHeight(bx, bz);
            for (int dx = -3; dx <= 3; dx++) for (int dz = -3; dz <= 3; dz++) {
                if (w.arenaHeight(bx + dx, bz + dz) != hb) flat = false;
                if (w.get(bx + dx, hb, bz + dz) != COBBLE || w.get(bx + dx, hb + 1, bz + dz) != AIR) blk = false;
            }
            if (hb < 6) flat = false;   // 水面(5)より1段上以上
            for (int i = 0; i < kArenaSpawns; i++) {
                const int hs = w.arenaHeight(sx[i], sz[i]);
                for (int dx = -1; dx <= 1; dx++) for (int dz = -1; dz <= 1; dz++) {
                    if (w.arenaHeight(sx[i] + dx, sz[i] + dz) != hs) flat = false;
                    if (w.get(sx[i] + dx, hs, sz[i] + dz) != GRAVEL) blk = false;
                }
                if (hs < 6) flat = false;
            }
            // 木(幹・葉)は生えない
            for (int i = 0; i < 56 && blk; i++) for (int k = 0; k < 56; k++) for (int yy = 0; yy < H; yy++) {
                const uint8_t b = w.get(i, yy, k);
                if (b == WOOD || b == LEAVES) { blk = false; break; }
            }
            flat_ok += flat; blocks_ok += blk;
            // 流れの場: ベースのまわりの輪から、出現位置へ届くか
            Flow f;
            f.begin(w, r);
            for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
                if (abs(dx) == 2 || abs(dz) == 2) f.addGoal(bx + dx, bz + dz);
            }
            while (!f.step(w, 1 << 20)) {}
            bool ok = true;
            for (int i = 0; i < kArenaSpawns; i++) {
                if (f.dist(sx[i], sz[i]) < 0) ok = false;
                else if (Follow(f, sx[i], sz[i], 4000) < 0) ok = false;
            }
            reachable += ok;
            for (int i = 0; i < 56; i++) for (int k = 0; k < 56; k++) sum = sum * 31 + (uint32_t)w.arenaHeight(i, k);
        }
        check(created == n, "ARENA: 作れる");
        check(flat_ok == n, "ARENA: ベースのまわりと出現位置は平らで、水面より上");
        check(blocks_ok == n, "ARENA: ベースは丸石・出現位置は砂利・木は無い");
        printf("  ARENA: %d/%d の種で出現位置からベースへ道がある\n", reachable, n);
        check(reachable * 2 >= n, "ARENA: 半分以上の種で出現位置からベースへ道がある");
        // 同じ種なら同じ地形
        uint32_t again = 0;
        for (uint32_t seed = 1; seed <= 24; seed++) {
            int x, y, z;
            w.create("", ARENA, seed, 7, x, y, z);
            for (int i = 0; i < 56; i++) for (int k = 0; k < 56; k++) again = again * 31 + (uint32_t)w.arenaHeight(i, k);
        }
        check(again == sum, "ARENA: 種で決まる");
        w.close();
    }

    printf("\n%s (%d 件失敗)\n", failures ? "失敗" : "全部成功", failures);
    return failures ? 1 : 0;
}
