#pragma once
// 2.5Dの箱庭(Iso::World)の上の「流れの場」(フローフィールド)。Luaの pico.iso.flow_*。
//
// 目的地(柱の集まり。タワーディフェンスのベースのまわり等)から全部の柱へ、ダイクストラで「着くまでの値段」と
// 「次に進む隣の柱」を求めておく。大勢の人や物(ゾンビ)は、自分の柱の「次」をたどるだけで目的地へ着く
// (1体ずつ経路探索をしない)。
//
// 規則は経路探索(Iso_Path)と同じ PathRules(max_up / max_down / height / diagonal / swim / step / up_cost /
// down_cost / avoid / pass / block_cost / body_cost。max_nodes / partial / edge は使わない)。
// 1歩の向きは「歩く人から見た向き」で判定する(登りと降りは対称でないので、柱 n から目的地側の柱 c へ1歩で
// 行けるかを見る)。値段は行き先の柱 c へ入る値段(経路探索と同じ)。
//
// 簡単にするため、1つの柱に立てる高さは1つだけ(柱の一番上の立てる所。StandAt(.., -1, ..))。
// 橋の下のような「同じ柱の下の段」は扱わない。
//
// 作り直し(バリケードを建てた/壊された等)はフレームをまたいで少しずつ進められる(step の budget)。
// 作っている間は前の結果を引き続き答え、出来上がった瞬間に入れ替える(2面持ち)。
// 値段は 1/8 単位の 16bit で持つ(8191.75 で頭打ち)。
//
// メモリ(実機ではチャンクの置き場の後に取るので、大きな塊を1つ取らない):
//   出来上がった結果: 1柱あたり 3バイト(値段 2 + 向きと立つ高さを詰めた 1)。56x56 で約9.4KB。
//   作っている間だけ: もう1組(3バイト/柱)+ 出番待ちのヒープ(1件4バイト、512件から必要なだけ広げる)。
//   作り終えたら前の結果とヒープを返す。確保は配列ごとに分ける(一番大きいものでも 2*W*W バイト)。
// 幅は kMaxFlowWidth まで。

#include <cstddef>
#include <cstdint>

#include "iso/Iso_World.hpp"
#include "iso/Iso_Path.hpp"

namespace Iso {

constexpr int kMaxFlowWidth = 64;
constexpr int kMaxFlowGoals = 256;

class Flow {
public:
    static constexpr uint16_t kUnreached = 0xFFFF;
    static constexpr float kUnit = 8.0f;        // 値段の単位(1/8)

    Flow() = default;
    ~Flow();
    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;

    // 作り始める(前の結果は出来上がるまで答え続ける)。続けて addGoal() で目的地の柱を足す(最初の step() より前に)。
    // 世界が広すぎる・メモリが足りないなら false
    bool begin(const World& w, const PathRules& r);
    // 目的地の柱を足す(kMaxFlowGoals まで。世界の外・多すぎるなら false)
    bool addGoal(int x, int z);
    // まとめて(テスト用。gx/gz は n 個)
    bool begin(const World& w, const PathRules& r, const int16_t* gx, const int16_t* gz, int n);
    // 最大 budget 単位(柱を1つ調べる/1つ確定する = 1単位)進める。この呼び出しで出来上がったら true
    bool step(const World& w, int budget);
    bool building() const { return phase_ != Phase::Idle; }
    // 直前の作り直しがメモリ不足で止まったか(前の結果はそのまま残る。次の begin で下りる)
    bool failed() const { return failed_; }
    // 今持っているメモリ(バイト。目的地の表のぶんは Flow 自身の中なので含めない)
    size_t memoryBytes() const;
    bool ready() const { return revision_ > 0; }
    uint32_t revision() const { return revision_; }
    void clear();
    int width() const { return W_; }

    // 柱 (x, z) の値(出来上がった結果)。届かない・世界の外・まだ無いなら dist は負
    float dist(int x, int z) const;
    // 立つ高さ(柱の一番上の立てる所。立てない柱・まだ結果が無いなら -1)
    int standY(int x, int z) const;
    // 次に進む柱。目的地そのもの・届かないなら false(at_goal で区別)
    bool next(int x, int z, int& nx, int& nz, bool* at_goal = nullptr) const;

private:
    enum class Phase : uint8_t { Idle, Scan, Run };
    // 向き(上位4bit)と立つ高さ(下位4bit)を1バイトに詰める
    static constexpr uint8_t kPackNoStand = 14;   // 立てない柱(高さは意味が無い)
    static constexpr uint8_t kPackUnreached = 15; // 立てるが届かない
    static constexpr uint8_t kPackGoal = 8;
    struct HeapEnt { uint16_t d; int16_t c; };

    void freeBack();
    bool pushHeap(uint16_t d, int c);
    HeapEnt popHeap();

    int W_ = 0;
    uint16_t* fdist_ = nullptr;  // 出来上がった結果
    uint8_t* fpack_ = nullptr;
    uint16_t* bdist_ = nullptr;  // 作っている途中
    uint8_t* bpack_ = nullptr;
    HeapEnt* heap_ = nullptr;    // 出番待ち(古くなった件は取り出すときに飛ばす)
    int heap_n_ = 0;
    int heap_cap_ = 0;

    Phase phase_ = Phase::Idle;
    int scan_ = 0;
    PathRules rules_;
    int16_t goals_[kMaxFlowGoals];   // 目的地の柱の番号(x*W+z)
    int ngoals_ = 0;
    uint32_t revision_ = 0;
    bool failed_ = false;
};

}  // namespace Iso
