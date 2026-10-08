// 2.5Dの箱庭の上の流れの場(フローフィールド)。説明は Iso_Flow.hpp。
#include "iso/Iso_Flow.hpp"

#include <cstdlib>
#include <cstring>

namespace Iso {

namespace {
const int8_t kDir[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
const uint8_t kOpposite[8] = {1, 0, 3, 2, 7, 6, 5, 4};
}

Flow::~Flow() { clear(); }

void Flow::clear() {
    free(block_);
    block_ = nullptr;
    ycol_ = fy_ = fnext_ = bnext_ = nullptr;
    fdist_ = bdist_ = nullptr;
    hpos_ = heap_ = nullptr;
    goals_ = nullptr;
    W_ = 0;
    heap_n_ = 0;
    phase_ = Phase::Idle;
    revision_ = 0;
    ngoals_ = 0;
}

bool Flow::alloc(int W) {
    if (W == W_ && block_) return true;
    clear();
    const size_t n = (size_t)W * W;
    // y(1) + fy(1) + fdist(2) + fnext(1) + bdist(2) + bnext(1) + hpos(2) + heap(2)
    block_ = static_cast<uint8_t*>(malloc(n * 12 + sizeof(int16_t) * kMaxFlowGoals));
    if (!block_) return false;
    W_ = W;
    uint8_t* p = block_;
    fdist_ = reinterpret_cast<uint16_t*>(p); p += n * 2;
    bdist_ = reinterpret_cast<uint16_t*>(p); p += n * 2;
    hpos_ = reinterpret_cast<int16_t*>(p); p += n * 2;
    heap_ = reinterpret_cast<int16_t*>(p); p += n * 2;
    goals_ = reinterpret_cast<int16_t*>(p); p += sizeof(int16_t) * kMaxFlowGoals;
    ycol_ = p; p += n;
    fy_ = p; p += n;
    fnext_ = p; p += n;
    bnext_ = p;
    for (size_t i = 0; i < n; i++) { fdist_[i] = kUnreached; fnext_[i] = kNoNext; fy_[i] = 255; }
    return true;
}

bool Flow::begin(const World& w, const PathRules& r) {
    const int W = w.width();
    if (W < 1 || W > kMaxFlowWidth) return false;
    if (W != W_ && building()) phase_ = Phase::Idle;
    if (!alloc(W)) return false;
    rules_ = r;
    if (rules_.height < 1) rules_.height = 1;
    if (rules_.max_up < 0) rules_.max_up = 0;
    if (rules_.max_down < 0) rules_.max_down = 0;
    if (!(rules_.step > 0)) rules_.step = 1.0f;
    ngoals_ = 0;
    phase_ = Phase::Scan;
    scan_ = 0;
    heap_n_ = 0;
    return true;
}

bool Flow::addGoal(int x, int z) {
    if (!block_ || ngoals_ >= kMaxFlowGoals) return false;
    if ((unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return false;
    goals_[ngoals_++] = (int16_t)(x * W_ + z);
    return true;
}

bool Flow::begin(const World& w, const PathRules& r, const int16_t* gx, const int16_t* gz, int n) {
    if (n < 0 || n > kMaxFlowGoals || !begin(w, r)) return false;
    for (int i = 0; i < n; i++) addGoal(gx[i], gz[i]);
    return true;
}

void Flow::swapAt(int a, int b) {
    const int16_t t = heap_[a]; heap_[a] = heap_[b]; heap_[b] = t;
    hpos_[heap_[a]] = (int16_t)a;
    hpos_[heap_[b]] = (int16_t)b;
}
void Flow::up(int i) {
    while (i > 0) {
        const int p = (i - 1) / 2;
        if (!lessAt(i, p)) break;
        swapAt(i, p);
        i = p;
    }
}
void Flow::down(int i) {
    for (;;) {
        const int l = i * 2 + 1, r = l + 1;
        int m = i;
        if (l < heap_n_ && lessAt(l, m)) m = l;
        if (r < heap_n_ && lessAt(r, m)) m = r;
        if (m == i) break;
        swapAt(i, m);
        i = m;
    }
}
void Flow::push(int c) {
    heap_[heap_n_] = (int16_t)c;
    hpos_[c] = (int16_t)heap_n_;
    up(heap_n_++);
}
int Flow::pop() {
    const int c = heap_[0];
    heap_n_--;
    if (heap_n_ > 0) {
        heap_[0] = heap_[heap_n_];
        hpos_[heap_[0]] = 0;
        down(0);
    }
    hpos_[c] = -2;
    return c;
}

bool Flow::step(const World& w, int budget) {
    if (phase_ == Phase::Idle || !block_ || w.width() != W_) return false;
    const int W = W_, n = W * W;
    const PathRules& r = rules_;
    while (budget > 0 && phase_ == Phase::Scan) {
        // 柱ごとの立つ高さを調べる
        const int x = scan_ / W, z = scan_ % W;
        const int y = StandAt(w, x, -1, z, r);
        ycol_[scan_] = y < 0 ? 255 : (uint8_t)y;
        bdist_[scan_] = kUnreached;
        bnext_[scan_] = kNoNext;
        hpos_[scan_] = -1;
        scan_++;
        budget--;
        if (scan_ >= n) {
            for (int i = 0; i < ngoals_; i++) {
                const int c = goals_[i];
                if (ycol_[c] == 255 || bdist_[c] == 0) continue;
                bdist_[c] = 0;
                bnext_[c] = kAtGoal;
                push(c);
            }
            phase_ = Phase::Run;
        }
    }
    while (budget > 0 && phase_ == Phase::Run) {
        if (heap_n_ == 0) {
            // 出来上がり: 入れ替える
            uint16_t* td = fdist_; fdist_ = bdist_; bdist_ = td;
            uint8_t* tn = fnext_; fnext_ = bnext_; bnext_ = tn;
            memcpy(fy_, ycol_, (size_t)n);
            phase_ = Phase::Idle;
            revision_++;
            return true;
        }
        const int c = pop();
        budget--;
        const int cx = c / W, cz = c % W, cy = ycol_[c];
        const uint32_t dc = bdist_[c];
        const float enter = EnterCost(w, cx, cy, cz, r);
        const int ndir = r.diagonal ? 8 : 4;
        for (int d = 0; d < ndir; d++) {
            // 隣の柱 m から、c へ1歩で来られるか(m から見た向きは -kDir[d])
            const int mx = cx + kDir[d][0], mz = cz + kDir[d][1];
            if ((unsigned)mx >= (unsigned)W || (unsigned)mz >= (unsigned)W) continue;
            const int m = mx * W + mz;
            if (hpos_[m] == -2) continue;
            const int my = ycol_[m];
            if (my == 255) continue;
            if (!CanStep(w, mx, my, mz, cx, cy, cz, r)) continue;
            if (d >= 4) {
                // 斜め: m から両脇の柱 (cx, mz) と (mx, cz) のどちらにも行けること(角をすり抜けない)
                const int sy = ycol_[cx * W + mz], ty = ycol_[mx * W + cz];
                if (sy == 255 || ty == 255) continue;
                if (!CanStep(w, mx, my, mz, cx, sy, mz, r) || !CanStep(w, mx, my, mz, mx, ty, cz, r)) continue;
            }
            float cost = d >= 4 ? r.step * 1.41421356f : r.step;
            if (cy > my) cost += r.up_cost * (float)(cy - my);
            else if (cy < my) cost += r.down_cost * (float)(my - cy);
            cost += enter;
            if (!(cost >= 0)) cost = 0;
            uint32_t nd = dc + (uint32_t)(cost * kUnit + 0.5f);
            if (nd > 0xFFFE) nd = 0xFFFE;
            if (nd >= bdist_[m]) continue;
            bdist_[m] = (uint16_t)nd;
            // m から c への向き = kDir[d] の逆
            bnext_[m] = kOpposite[d];
            if (hpos_[m] >= 0) up(hpos_[m]);
            else push(m);
        }
    }
    return false;
}

float Flow::dist(int x, int z) const {
    if (!block_ || !revision_ || (unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return -1;
    const uint16_t d = fdist_[x * W_ + z];
    return d == kUnreached ? -1.0f : (float)d / kUnit;
}

int Flow::standY(int x, int z) const {
    if (!block_ || !revision_ || (unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return -1;
    const uint8_t y = fy_[x * W_ + z];
    return y == 255 ? -1 : y;
}

bool Flow::next(int x, int z, int& nx, int& nz, bool* at_goal) const {
    if (at_goal) *at_goal = false;
    if (!block_ || !revision_ || (unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return false;
    const uint8_t d = fnext_[x * W_ + z];
    if (d == kAtGoal) { if (at_goal) *at_goal = true; return false; }
    if (d >= 8) return false;
    nx = x + kDir[d][0];
    nz = z + kDir[d][1];
    return true;
}

}  // namespace Iso
