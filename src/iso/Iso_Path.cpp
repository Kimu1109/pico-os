// 2.5Dの箱庭の上を歩く経路を探す(A*)。規則は Iso_Path.hpp のコメント参照。
#include "iso/Iso_Path.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Iso {

namespace {

// 体のマスに居てよいか(世界より上は空気)
bool BodyOk(const World& w, int x, int y, int z, const PathRules& r) {
    if (y >= H) return true;
    const uint8_t b = w.get(x, y, z);
    if ((r.avoid >> b) & 1) return false;
    return b == AIR || b == TORCH || (b == WATER && r.swim) || ((r.pass >> b) & 1);
}

// 頭の上 y0 .. y1-1 が空いているか
bool ClearRange(const World& w, int x, int y0, int y1, int z, const PathRules& r) {
    for (int y = y0; y < y1 && y < H; y++) if (!BodyOk(w, x, y, z, r)) return false;
    return true;
}

bool Standable(const World& w, int x, int y, int z, const PathRules& r) {
    if (y < 1 || y > H) return false;
    const uint8_t f = w.get(x, y - 1, z);
    if (f == AIR || f == TORCH || ((r.avoid >> f) & 1) || ((r.pass >> f) & 1)) return false;
    return ClearRange(w, x, y, y + r.height, z, r);
}

// (x,y,z) から隣の柱 (nx,nz) の高さ ny へ1歩で行けるか(高低差と頭の上だけ。値段は見ない)
bool CanStepImpl(const World& w, int x, int y, int z, int nx, int ny, int nz, const PathRules& r) {
    if (ny > y) {
        if (ny - y > r.max_up) return false;
        // 今の柱で ny+height まで頭の上が空いている
        if (!ClearRange(w, x, y + r.height, ny + r.height, z, r)) return false;
    } else if (ny < y) {
        if (y - ny > r.max_down) return false;
        // 隣の柱で y+height まで空いている(縁を越えてから降りる)
        if (!ClearRange(w, nx, ny + r.height, y + r.height, nz, r)) return false;
    }
    return Standable(w, nx, ny, nz, r);
}

bool InWorld(const World& w, int x, int z) {
    return (unsigned)x < (unsigned)w.width() && (unsigned)z < (unsigned)w.width();
}

// 隣の柱で行ける高さを上から順に out へ(最大 H+1 個)
int StepTargets(const World& w, int x, int y, int z, int nx, int nz, const PathRules& r, uint8_t* out) {
    if (!InWorld(w, nx, nz)) return 0;
    int n = 0;
    int hi = y + r.max_up, lo = y - r.max_down;
    if (hi > H) hi = H;
    if (lo < 1) lo = 1;
    for (int ny = hi; ny >= lo; ny--) {
        if (CanStepImpl(w, x, y, z, nx, ny, nz, r)) out[n++] = (uint8_t)ny;
    }
    return n;
}

struct Node {
    int16_t x, z;
    uint8_t y;
    uint8_t closed;
    int16_t parent;
    int16_t hpos;     // ヒープの中の位置(-1 = 入っていない)
    float g, f;
};

struct Search {
    Node* nodes = nullptr;
    int16_t* table = nullptr;   // 点の番号 + 1(0 = 空き)
    int16_t* heap = nullptr;
    int count = 0, cap = 0, heap_n = 0;
    uint32_t mask = 0;

    ~Search() { free(nodes); free(table); free(heap); }

    bool init(int n) {
        cap = n;
        int t = 1;
        while (t < n * 2) t <<= 1;
        mask = (uint32_t)t - 1;
        nodes = (Node*)malloc(sizeof(Node) * (size_t)n);
        table = (int16_t*)calloc((size_t)t, sizeof(int16_t));
        heap = (int16_t*)malloc(sizeof(int16_t) * (size_t)n);
        return nodes && table && heap;
    }

    static uint32_t Key(int x, int y, int z) { return (uint32_t)x | ((uint32_t)z << 10) | ((uint32_t)y << 20); }
    uint32_t slot(uint32_t key) const { return (key * 2654435761u) >> 7 & mask; }

    int find(int x, int y, int z) const {
        const uint32_t key = Key(x, y, z);
        for (uint32_t s = slot(key);; s = (s + 1) & mask) {
            const int i = table[s] - 1;
            if (i < 0) return -1;
            if (Key(nodes[i].x, nodes[i].y, nodes[i].z) == key) return i;
        }
    }
    // 増やせなければ -1
    int add(int x, int y, int z) {
        if (count >= cap) return -1;
        const uint32_t key = Key(x, y, z);
        uint32_t s = slot(key);
        while (table[s]) s = (s + 1) & mask;
        const int i = count++;
        table[s] = (int16_t)(i + 1);
        Node& n = nodes[i];
        n.x = (int16_t)x; n.z = (int16_t)z; n.y = (uint8_t)y;
        n.closed = 0; n.parent = -1; n.hpos = -1;
        return i;
    }

    bool less(int a, int b) const {
        const Node& na = nodes[heap[a]];
        const Node& nb = nodes[heap[b]];
        return na.f < nb.f || (na.f == nb.f && na.g > nb.g);   // 同じなら進んでいる方(g が大きい)を先に
    }
    void swap(int a, int b) {
        const int16_t t = heap[a]; heap[a] = heap[b]; heap[b] = t;
        nodes[heap[a]].hpos = (int16_t)a;
        nodes[heap[b]].hpos = (int16_t)b;
    }
    void up(int i) {
        while (i > 0) {
            const int p = (i - 1) / 2;
            if (!less(i, p)) break;
            swap(i, p);
            i = p;
        }
    }
    void down(int i) {
        for (;;) {
            const int l = i * 2 + 1, r = l + 1;
            int m = i;
            if (l < heap_n && less(l, m)) m = l;
            if (r < heap_n && less(r, m)) m = r;
            if (m == i) break;
            swap(i, m);
            i = m;
        }
    }
    void push(int n) {
        heap[heap_n] = (int16_t)n;
        nodes[n].hpos = (int16_t)heap_n;
        up(heap_n++);
    }
    int pop() {
        const int n = heap[0];
        heap_n--;
        if (heap_n > 0) {
            heap[0] = heap[heap_n];
            nodes[heap[0]].hpos = 0;
            down(0);
        }
        nodes[n].hpos = -1;
        return n;
    }
};

float Heuristic(int x, int z, int gx, int gz, const PathRules& r) {
    const int dx = std::abs(x - gx), dz = std::abs(z - gz);
    if (!r.diagonal) return r.step * (float)(dx + dz);
    const int lo = dx < dz ? dx : dz, hi = dx < dz ? dz : dx;
    return r.step * ((float)(hi - lo) + 1.41421356f * (float)lo);
}

}  // namespace

bool CanStep(const World& w, int x, int y, int z, int nx, int ny, int nz, const PathRules& r) {
    return InWorld(w, nx, nz) && CanStepImpl(w, x, y, z, nx, ny, nz, r);
}

float EnterCost(const World& w, int x, int y, int z, const PathRules& r) {
    const uint8_t floor = w.get(x, y - 1, z);
    float c = r.block_cost[floor <= kBlockCount ? floor : 0];
    for (int yy = y; yy < y + r.height && yy < H; yy++) {
        const uint8_t b = w.get(x, yy, z);
        if (b && b <= kBlockCount) c += r.body_cost[b];
    }
    return c;
}

int StandAt(const World& w, int x, int y, int z, const PathRules& r) {
    if (!InWorld(w, x, z)) return -1;
    int yy = (y < 0 || y > H) ? H : y;
    for (; yy >= 1; yy--) if (Standable(w, x, yy, z, r)) return yy;
    return -1;
}

PathResult FindPath(const World& w, int sx, int sy, int sz, int gx, int gy, int gz, const PathRules& rules,
                    PathPoint* out, int max_out) {
    PathResult res;
    PathRules r = rules;
    if (r.max_nodes < 1) r.max_nodes = 1;
    if (r.max_nodes > kMaxPathNodes) r.max_nodes = kMaxPathNodes;
    if (r.height < 1) r.height = 1;
    if (r.max_up < 0) r.max_up = 0;
    if (r.max_down < 0) r.max_down = 0;

    const int s_y = StandAt(w, sx, sy, sz, r);
    if (s_y < 0) { res.status = PathStatus::BadStart; return res; }
    // 目的地の柱に立てる所が無ければ探さない(行ける所を全部調べることになるため)
    const int g_top = StandAt(w, gx, gy, gz, r);
    if (g_top < 0) { res.status = PathStatus::BadGoal; return res; }
    const int g_y = gy < 0 ? -1 : g_top;

    Search s;
    if (!s.init(r.max_nodes)) { res.status = PathStatus::NoMemory; return res; }

    const int start = s.add(sx, s_y, sz);
    s.nodes[start].g = 0;
    s.nodes[start].f = Heuristic(sx, sz, gx, gz, r);
    s.push(start);

    static const int8_t kDir[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const int ndir = r.diagonal ? 8 : 4;
    int best = start;               // 目的地に一番近い点(partial のため)
    float best_h = s.nodes[start].f;
    int goal = -1;
    bool full = false;
    uint8_t targets[H + 1];
    uint8_t side[H + 1];

    while (s.heap_n > 0) {
        const int cur = s.pop();
        Node c = s.nodes[cur];
        s.nodes[cur].closed = 1;
        if (c.x == gx && c.z == gz && (g_y < 0 || c.y == g_y)) { goal = cur; break; }
        const float hc = c.f - c.g;
        if (hc < best_h || (hc == best_h && c.g < s.nodes[best].g)) { best = cur; best_h = hc; }

        for (int d = 0; d < ndir; d++) {
            const int nx = c.x + kDir[d][0], nz = c.z + kDir[d][1];
            const int nt = StepTargets(w, c.x, c.y, c.z, nx, nz, r, targets);
            if (!nt) continue;
            if (d >= 4) {
                // 斜め: 両脇の柱のどちらにも行けること
                if (!StepTargets(w, c.x, c.y, c.z, c.x + kDir[d][0], c.z, r, side)) continue;
                if (!StepTargets(w, c.x, c.y, c.z, c.x, c.z + kDir[d][1], r, side)) continue;
            }
            for (int t = 0; t < nt; t++) {
                const int ny = targets[t];
                int ni = s.find(nx, ny, nz);
                if (ni >= 0 && s.nodes[ni].closed) continue;
                const uint8_t floor = w.get(nx, ny - 1, nz);
                float cost = (d >= 4 ? r.step * 1.41421356f : r.step);
                if (ny > c.y) cost += r.up_cost * (float)(ny - c.y);
                else if (ny < c.y) cost += r.down_cost * (float)(c.y - ny);
                cost += EnterCost(w, nx, ny, nz, r);
                if (r.edge) {
                    bool abort = false;
                    const float e = r.edge(r.ctx, c.x, c.y, c.z, nx, ny, nz, floor, abort);
                    if (abort) { res.status = PathStatus::Aborted; res.nodes = s.count; return res; }
                    if (!(e >= 0)) continue;   // 負・NaN は通れない
                    cost += e;
                }
                if (!(cost >= 0)) cost = 0;
                const float g = c.g + cost;
                if (ni < 0) {
                    ni = s.add(nx, ny, nz);
                    if (ni < 0) { full = true; continue; }
                    s.nodes[ni].parent = (int16_t)cur;
                    s.nodes[ni].g = g;
                    s.nodes[ni].f = g + Heuristic(nx, nz, gx, gz, r);
                    s.push(ni);
                } else if (g < s.nodes[ni].g) {
                    const float h = s.nodes[ni].f - s.nodes[ni].g;
                    s.nodes[ni].parent = (int16_t)cur;
                    s.nodes[ni].g = g;
                    s.nodes[ni].f = g + h;
                    s.up(s.nodes[ni].hpos);
                }
            }
        }
    }

    res.nodes = s.count;
    int end = goal;
    if (goal >= 0) res.status = PathStatus::Found;
    else {
        res.status = full ? PathStatus::Limit : PathStatus::NoPath;
        if (!r.partial) return res;
        end = best;
        res.status = PathStatus::Partial;
    }
    res.cost = s.nodes[end].g;
    int len = 0;
    for (int i = end; i >= 0; i = s.nodes[i].parent) len++;
    res.length = len;
    if (out && max_out > 0) {
        int k = len - 1;
        for (int i = end; i >= 0; i = s.nodes[i].parent, k--) {
            if (k < max_out) out[k] = PathPoint{s.nodes[i].x, s.nodes[i].z, s.nodes[i].y};
        }
    }
    return res;
}

}  // namespace Iso
