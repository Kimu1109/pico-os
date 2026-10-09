#pragma once
// 2.5Dの箱庭(Iso::World)の上を歩く経路を探す(A*)。Luaの pico.iso.path。
//
// 「立てる場所」は (x, y, z) のマス: 足元 (x, y-1, z) が空気・松明・pass のブロックでないブロックで、体のマス
// (x, y .. y+height-1, z) が通り抜けられる(空気・松明・pass のブロック。swim なら水も)こと。
// pass は「中を通り抜けられるブロック」(バリケード等。乗れはしない)。y は足の裏の高さで、
// pico.iso.ground() / エンティティの y と同じ数え方。水も足元になれる(水面を歩く/泳ぐ。嫌なら avoid に水を入れる)。
//
// 1歩は東西南北(diagonal なら斜めも)の隣の柱へ。隣の柱で立てる高さのうち、登りが max_up 段以内・降りが
// max_down 段以内のものへ行ける。登るときは今の柱の頭の上、降りるときは隣の柱の頭の上が空いている必要がある
// (天井を突き抜けない)。斜めは、両脇の柱のどちらにも(同じ規則で)行けるときだけ(角をすり抜けない)。
//
// 1歩の値段 = step(斜めは×√2) + up_cost×登った段数 + down_cost×降りた段数 + block_cost[足元のブロック]
//            + 行き先の体のマスごとの body_cost[ブロック] + edge コールバックの返す値(負なら通れない)。
// 一番安い道を返す(どの値段も 0 以上なら。見積もりは step×距離なので、1歩の値段が step を下回る規則は
// 最短を保証しない)。
//
// 読み込んでいないチャンクは空気に見えるので、そこには立てない(探すのは読み込んでいる所だけ)。
// 作業場所(1点 約26バイト)は探す間だけ malloc して返す。256点から始めて足りなければ倍にし(max_nodes まで)、
// 広げられなければ max_nodes を使い切ったのと同じ扱い(Limit / partial なら途中までの道)。

#include <cstdint>
#include <cstddef>

#include "iso/Iso_World.hpp"

namespace Iso {

struct PathRules {
    int max_up = 1;            // 1歩で登れる段数
    int max_down = 2;          // 1歩で降りられる段数
    int height = 2;            // 体の高さ(マス)
    bool diagonal = false;     // 斜めにも進む
    bool swim = false;         // 体のマスに水があってもよい(水の中を歩く)
    float step = 1.0f;         // 1歩の値段(斜めは×√2)
    float up_cost = 0.0f;      // 1段登るごとに足す値段
    float down_cost = 0.0f;    // 1段降りるごとに足す値段
    uint32_t avoid = 0;        // このブロック(ビット)が足元か体のマスにある所へは行かない
    uint32_t pass = 0;         // 体のマスにあってよいブロック(ビット。中を通り抜ける。足元にはならない)
    float block_cost[kBlockCount + 1] = {};   // 足元のブロックごとに足す値段
    float body_cost[kBlockCount + 1] = {};    // 行き先の体のマスのブロックごとに足す値段(pass のブロックの中を通る値段)
    int max_nodes = 1024;      // 調べる点の上限(kMaxPathNodes まで)
    bool partial = false;      // 着けなければ、目的地に一番近づける所までの道を返す
    // 1歩ごとの追加の判定(無くてよい)。戻り値: 負なら通れない、0以上なら足す値段。
    // abort を true にすると探すのをやめる(Lua のエラー等)
    float (*edge)(void* ctx, int x, int y, int z, int nx, int ny, int nz, uint8_t floor, bool& abort) = nullptr;
    void* ctx = nullptr;
};

constexpr int kMaxPathNodes = 4096;

struct PathPoint { int16_t x, z; uint8_t y; };

enum class PathStatus : uint8_t {
    Found,      // 着いた
    Partial,    // 着けなかったので一番近い所まで(partial)
    NoPath,     // 行ける所を全部調べても着けない
    Limit,      // max_nodes を使い切った
    BadStart,   // 出発点に立てる場所が無い
    BadGoal,    // 目的地の柱に立てる場所が無い
    NoMemory,
    Aborted,    // edge が abort した
};

struct PathResult {
    PathStatus status = PathStatus::NoPath;
    float cost = 0;
    int nodes = 0;     // 調べた点の数
    int length = 0;    // 道の点の数(出発点と到着点を含む)。out に入りきらなかった分も数える
};

// 立てる高さ。y < 0 なら柱の一番上、そうでなければ y 以下で一番上(無ければ -1)
int StandAt(const World& w, int x, int y, int z, const PathRules& r);
// (x,y,z) から隣の柱 (nx,nz) の高さ ny へ1歩で行けるか(高低差・頭の上・立てるか。値段は見ない。斜めの角は見ない)
bool CanStep(const World& w, int x, int y, int z, int nx, int ny, int nz, const PathRules& r);
// (x,y,z) へ入る値段のうち、足元と体のマスの分(block_cost + body_cost)
float EnterCost(const World& w, int x, int y, int z, const PathRules& r);

// (sx,sy,sz) から (gx,gy,gz) への道。sy/gy は負なら柱の一番上、そうでなければ その高さ以下で一番上の立てる所。
// gy < 0 なら目的地の柱のどの高さでもよい。道は out[0] = 出発点 … out[length-1] = 到着点(max_out まで)
PathResult FindPath(const World& w, int sx, int sy, int sz, int gx, int gy, int gz, const PathRules& r,
                    PathPoint* out, int max_out);
// 道の長さぶんだけ malloc した配列を *out_alloc へ返す版(呼び出し側が free する)。確保できなければ NoMemory
PathResult FindPathAlloc(const World& w, int sx, int sy, int sz, int gx, int gy, int gz, const PathRules& r,
                         PathPoint** out_alloc);

}  // namespace Iso
