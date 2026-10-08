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

void Flow::freeBack() {
    free(bdist_); bdist_ = nullptr;
    free(bpack_); bpack_ = nullptr;
    free(heap_); heap_ = nullptr;
    heap_n_ = heap_cap_ = 0;
}

void Flow::clear() {
    freeBack();
    free(fdist_); fdist_ = nullptr;
    free(fpack_); fpack_ = nullptr;
    W_ = 0;
    phase_ = Phase::Idle;
    revision_ = 0;
    ngoals_ = 0;
    failed_ = false;
}

bool Flow::begin(const World& w, const PathRules& r) {
    const int W = w.width();
    if (W < 1 || W > kMaxFlowWidth) return false;
    if (W != W_) clear();   // 大きさが変わったら前の結果は使えない
    freeBack();
    phase_ = Phase::Idle;
    failed_ = false;
    const size_t n = (size_t)W * W;
    bdist_ = static_cast<uint16_t*>(malloc(n * sizeof(uint16_t)));
    bpack_ = static_cast<uint8_t*>(malloc(n));
    heap_cap_ = 512;
    heap_ = static_cast<HeapEnt*>(malloc(sizeof(HeapEnt) * (size_t)heap_cap_));
    if (!bdist_ || !bpack_ || !heap_) {
        freeBack();
        failed_ = true;
        return false;
    }
    W_ = W;
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
    if (phase_ != Phase::Scan || ngoals_ >= kMaxFlowGoals) return false;
    if ((unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return false;
    goals_[ngoals_++] = (int16_t)(x * W_ + z);
    return true;
}

bool Flow::begin(const World& w, const PathRules& r, const int16_t* gx, const int16_t* gz, int n) {
    if (n < 0 || n > kMaxFlowGoals || !begin(w, r)) return false;
    for (int i = 0; i < n; i++) addGoal(gx[i], gz[i]);
    return true;
}

// 二分ヒープ(値段の小さい順)。値段を下げた柱はもう1件積み、古い件は取り出したときに飛ばす
bool Flow::pushHeap(uint16_t d, int c) {
    if (heap_n_ >= heap_cap_) {
        // 古くなった件を捨ててから、足りなければ広げる
        int k = 0;
        for (int i = 0; i < heap_n_; i++) {
            if (heap_[i].d == bdist_[heap_[i].c]) heap_[k++] = heap_[i];
        }
        if (k < heap_n_) {
            heap_n_ = k;
            for (int i = heap_n_ / 2 - 1; i >= 0; i--) {
                // 下へ沈めて組み直す
                int j = i;
                for (;;) {
                    const int l = j * 2 + 1, rr = l + 1;
                    int m = j;
                    if (l < heap_n_ && heap_[l].d < heap_[m].d) m = l;
                    if (rr < heap_n_ && heap_[rr].d < heap_[m].d) m = rr;
                    if (m == j) break;
                    const HeapEnt t = heap_[j]; heap_[j] = heap_[m]; heap_[m] = t;
                    j = m;
                }
            }
        }
        if (heap_n_ >= heap_cap_ - heap_cap_ / 8) {
            const int cap = heap_cap_ * 2;
            HeapEnt* p = static_cast<HeapEnt*>(realloc(heap_, sizeof(HeapEnt) * (size_t)cap));
            if (!p) { if (heap_n_ >= heap_cap_) return false; }
            else { heap_ = p; heap_cap_ = cap; }
        }
    }
    int i = heap_n_++;
    while (i > 0) {
        const int p = (i - 1) / 2;
        if (heap_[p].d <= d) break;
        heap_[i] = heap_[p];
        i = p;
    }
    heap_[i].d = d;
    heap_[i].c = (int16_t)c;
    return true;
}

Flow::HeapEnt Flow::popHeap() {
    const HeapEnt top = heap_[0];
    const HeapEnt last = heap_[--heap_n_];
    int i = 0;
    for (;;) {
        const int l = i * 2 + 1, r = l + 1;
        if (l >= heap_n_) break;
        const int m = (r < heap_n_ && heap_[r].d < heap_[l].d) ? r : l;
        if (heap_[m].d >= last.d) break;
        heap_[i] = heap_[m];
        i = m;
    }
    if (heap_n_ > 0) heap_[i] = last;
    return top;
}

bool Flow::step(const World& w, int budget) {
    if (phase_ == Phase::Idle || !bdist_ || w.width() != W_) return false;
    const int W = W_, n = W * W;
    const PathRules& r = rules_;
    while (budget > 0 && phase_ == Phase::Scan) {
        // 柱ごとの立つ高さを調べる
        const int x = scan_ / W, z = scan_ % W;
        const int y = StandAt(w, x, -1, z, r);
        bpack_[scan_] = y < 0 || y > 15 ? (uint8_t)(kPackNoStand << 4) : (uint8_t)((kPackUnreached << 4) | y);
        bdist_[scan_] = kUnreached;
        scan_++;
        budget--;
        if (scan_ >= n) {
            for (int i = 0; i < ngoals_; i++) {
                const int c = goals_[i];
                if ((bpack_[c] >> 4) == kPackNoStand || bdist_[c] == 0) continue;
                bdist_[c] = 0;
                bpack_[c] = (uint8_t)((kPackGoal << 4) | (bpack_[c] & 15));
                if (!pushHeap(0, c)) { phase_ = Phase::Idle; failed_ = true; freeBack(); return false; }
            }
            phase_ = Phase::Run;
        }
    }
    while (budget > 0 && phase_ == Phase::Run) {
        if (heap_n_ == 0) {
            // 出来上がり: 入れ替えて、前の結果と作業場所を返す
            free(fdist_);
            free(fpack_);
            fdist_ = bdist_; fpack_ = bpack_;
            bdist_ = nullptr; bpack_ = nullptr;
            freeBack();
            phase_ = Phase::Idle;
            revision_++;
            return true;
        }
        const HeapEnt e = popHeap();
        budget--;
        const int c = e.c;
        if (e.d != bdist_[c]) continue;   // 古い件(その後もっと安く来られた)
        const int cx = c / W, cz = c % W, cy = bpack_[c] & 15;
        const uint32_t dc = e.d;
        const float enter = EnterCost(w, cx, cy, cz, r);
        const int ndir = r.diagonal ? 8 : 4;
        for (int d = 0; d < ndir; d++) {
            // 隣の柱 m から、c へ1歩で来られるか(m から見た向きは -kDir[d])
            const int mx = cx + kDir[d][0], mz = cz + kDir[d][1];
            if ((unsigned)mx >= (unsigned)W || (unsigned)mz >= (unsigned)W) continue;
            const int m = mx * W + mz;
            if ((bpack_[m] >> 4) == kPackNoStand) continue;
            if (bdist_[m] <= dc) continue;    // もう確定している(値段は負にならない)
            const int my = bpack_[m] & 15;
            if (!CanStep(w, mx, my, mz, cx, cy, cz, r)) continue;
            if (d >= 4) {
                // 斜め: m から両脇の柱 (cx, mz) と (mx, cz) のどちらにも行けること(角をすり抜けない)
                const uint8_t sp = bpack_[cx * W + mz], tp = bpack_[mx * W + cz];
                if ((sp >> 4) == kPackNoStand || (tp >> 4) == kPackNoStand) continue;
                if (!CanStep(w, mx, my, mz, cx, sp & 15, mz, r) || !CanStep(w, mx, my, mz, mx, tp & 15, cz, r)) continue;
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
            bpack_[m] = (uint8_t)((kOpposite[d] << 4) | my);
            if (!pushHeap((uint16_t)nd, m)) { phase_ = Phase::Idle; failed_ = true; freeBack(); return false; }
        }
    }
    return false;
}

size_t Flow::memoryBytes() const {
    const size_t n = (size_t)W_ * W_;
    size_t b = 0;
    if (fdist_) b += n * 3;
    if (bdist_) b += n * 3;
    if (heap_) b += sizeof(HeapEnt) * (size_t)heap_cap_;
    return b;
}

float Flow::dist(int x, int z) const {
    if (!fdist_ || (unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return -1;
    const uint16_t d = fdist_[x * W_ + z];
    return d == kUnreached ? -1.0f : (float)d / kUnit;
}

int Flow::standY(int x, int z) const {
    if (!fpack_ || (unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return -1;
    const uint8_t p = fpack_[x * W_ + z];
    return (p >> 4) == kPackNoStand ? -1 : (p & 15);
}

bool Flow::next(int x, int z, int& nx, int& nz, bool* at_goal) const {
    if (at_goal) *at_goal = false;
    if (!fpack_ || (unsigned)x >= (unsigned)W_ || (unsigned)z >= (unsigned)W_) return false;
    const uint8_t d = fpack_[x * W_ + z] >> 4;
    if (d == kPackGoal) { if (at_goal) *at_goal = true; return false; }
    if (d >= 8) return false;
    nx = x + kDir[d][0];
    nz = z + kDir[d][1];
    return true;
}

}  // namespace Iso
