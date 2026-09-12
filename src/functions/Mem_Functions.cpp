#include "functions/Mem_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <malloc.h>
#include <cstdlib>

#if PICO_MEM_PROFILE

// RP2350(ARM/RISC-V)ではヒープ末尾を sbrk(0) で取得できる。
// ホストテスト(glibc/x86)のmallocはmmapも併用するため、この計測は行わない
#if defined(__arm__) || defined(__riscv)
    #define PICO_MEM_HAS_SBRK 1
    extern "C" void* sbrk(int incr);
#else
    #define PICO_MEM_HAS_SBRK 0
#endif

namespace {

    // newlibは mallinfo()、glibc 2.33以降は mallinfo2()。
    // 中身は同じなので必要なフィールドだけ uint32_t に寄せて扱う
    struct MallocInfo {
        uint32_t arena;
        uint32_t used;
        uint32_t free_total;
        uint32_t free_blocks;
        uint32_t keepcost;
    };

    MallocInfo ReadMallocInfo(){
    #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
        const struct mallinfo2 mi = mallinfo2();
    #else
        const struct mallinfo mi = mallinfo();
    #endif
        MallocInfo out;
        out.arena       = (uint32_t)mi.arena;
        out.used        = (uint32_t)mi.uordblks;
        out.free_total  = (uint32_t)mi.fordblks;
        out.free_blocks = (uint32_t)mi.ordblks;
        out.keepcost    = (uint32_t)mi.keepcost;
        return out;
    }

    // nバイトが「既存の空きブロック」から確保できるかを試す。
    //
    // malloc の成否だけを見るのでは不十分で、mallocはヒープ末尾を伸ばして(sbrk)成功する
    // ことがある。それは既存の空きが繋がっていた証拠にはならないので、arenaが増えた場合は
    // 失敗扱いにする。これをやらないと断片化を見落とす
    bool FitsInExistingFree(uint32_t n, bool& out_grew){
        if(n == 0) return true;

        const uint32_t arena_before = ReadMallocInfo().arena;
        void* p = malloc(n);
        if(!p) return false;

        const bool grew = (ReadMallocInfo().arena != arena_before);
        free(p);

        if(grew) out_grew = true;
        return !grew;
    }

    // 既存の空きブロックから確保できる最大サイズを二分探索で求める。
    // 上限は free_total(空き合計を超える連続領域は原理的に存在しない)と
    // kMaxProbeBytes の小さいほう。
    //
    // 注意: 探索中に一時的にヒープが伸びることがある。伸びた確保は失敗扱いにするので
    // 戻り値自体は正しいが、arenaは伸びたまま戻らない場合がある(out_grew で通知する)
    uint32_t ProbeLargestFree(uint32_t free_total, bool& out_grew){
        uint32_t lo = 0;
        uint32_t hi = (free_total < MemFunctions::kMaxProbeBytes)
            ? free_total : MemFunctions::kMaxProbeBytes;

        while(lo < hi){
            const uint32_t mid = lo + (hi - lo + 1) / 2;
            if(FitsInExistingFree(mid, out_grew)){
                lo = mid;
            }else{
                hi = mid - 1;
            }
        }
        return lo;
    }

    uint32_t ReadStackHeadroom(){
    #if PICO_MEM_HAS_SBRK
        char stack_marker = 0;
        const char* heap_end = (const char*)sbrk(0);
        const char* sp = &stack_marker;
        //スタックはSRAM上位から下へ、ヒープは下位から上へ伸びる
        if(sp <= heap_end) return 0;
        return (uint32_t)(sp - heap_end);
    #else
        (void)0;
        return 0;
    #endif
    }

    // --- 現シーン滞在中の追跡状態 ---
    int current_stat_index = -1;
    uint32_t scene_baseline_used = 0; //onEnter()直前(ウィジェット生成前)のused
    bool scene_active = false;
    bool baseline_valid = false;

    // シーン名に対応する統計スロットを引く。無ければ作る。
    // 枠が尽きたら最後のスロットを "(overflow)" として共用する
    MemFunctions::SceneStat* FindOrCreateStat(const char* name){
        using namespace MemFunctions;

        for(int i = 0; i < scene_stat_count; i++){
            if(scene_stats[i].name == name) return &scene_stats[i];
        }

        if(scene_stat_count < kMaxTrackedScenes){
            SceneStat* stat = &scene_stats[scene_stat_count++];
            stat->name.assign(name);
            return stat;
        }

        SceneStat* overflow = &scene_stats[kMaxTrackedScenes - 1];
        overflow->name.assign("(overflow)");
        return overflow;
    }
}

MemFunctions::Snapshot MemFunctions::Take(bool probe_largest){
    const MallocInfo mi = ReadMallocInfo();

    Snapshot s;
    s.arena          = mi.arena;
    s.used           = mi.used;
    s.free_total     = mi.free_total;
    s.free_blocks    = mi.free_blocks;
    s.keepcost       = mi.keepcost;
    s.stack_headroom = ReadStackHeadroom();

    if(probe_largest){
        bool grew = false;
        s.largest_free = ProbeLargestFree(mi.free_total, grew);
        s.probe_grew_arena = grew;
        s.largest_free_capped = (s.largest_free >= kMaxProbeBytes);
    }

    return s;
}

uint16_t MemFunctions::FragmentationPermil(const Snapshot& s){
    if(s.free_total == 0) return 0;
    if(s.largest_free_capped) return 0; //探索上限まで連続で取れている = 実害なし
    if(s.largest_free >= s.free_total) return 0;

    const uint32_t connected = (uint32_t)((uint64_t)s.largest_free * 1000u / s.free_total);
    return (uint16_t)(1000u - connected);
}

void MemFunctions::Log(const char* label, bool probe_largest){
    const Snapshot s = Take(probe_largest);

    if(!probe_largest){
        LOG_SYS_DEBUG("[MEM] %s: used=%luB free=%luB(%lu塊)",
            label,
            (unsigned long)s.used,
            (unsigned long)s.free_total,
            (unsigned long)s.free_blocks);
        return;
    }

    LOG_SYS_DEBUG("[MEM] %s: used=%luB free=%luB(%lu塊) max_alloc=%lu%sB frag=%u%% stack余裕=%luB",
        label,
        (unsigned long)s.used,
        (unsigned long)s.free_total,
        (unsigned long)s.free_blocks,
        (unsigned long)s.largest_free,
        s.largest_free_capped ? "+" : "",
        (unsigned)(FragmentationPermil(s) / 10),
        (unsigned long)s.stack_headroom);
}

void MemFunctions::LogDelta(const char* label, const Snapshot& base, bool probe_largest){
    const Snapshot s = Take(probe_largest);

    //usedは減ることもあるので符号付きで出す
    LOG_SYS_DEBUG("[MEM] %s: used%+ldB (計%luB) 空き塊%+ld (計%lu) max_alloc=%luB",
        label,
        (long)s.used - (long)base.used,
        (unsigned long)s.used,
        (long)s.free_blocks - (long)base.free_blocks,
        (unsigned long)s.free_blocks,
        (unsigned long)s.largest_free);
}

void MemFunctions::Setup(){
    boot_snapshot = Take(true);
    Log("起動直後");
}

void MemFunctions::SealPermanentBaseline(){
    permanent_snapshot = Take(true);
    permanent_sealed = true;

    const uint32_t permanent_bytes = (permanent_snapshot.used > boot_snapshot.used)
        ? (permanent_snapshot.used - boot_snapshot.used) : 0;

    Log("常駐層の確保完了");
    LOG_SYS_DEBUG("[MEM] -> 常駐ウィジェット等が%luB。アリーナを分割する場合これが永続領域の目安",
        (unsigned long)permanent_bytes);
}

void MemFunctions::Update(){
    if(!scene_active || current_stat_index < 0) return;

    //毎フレーム呼ばれるのでmallinfoのみ。max_allocの実測(malloc試行)はここではやらない
    const uint32_t used = ReadMallocInfo().used;
    if(used <= scene_baseline_used) return;

    const uint32_t delta = used - scene_baseline_used;
    SceneStat& stat = scene_stats[current_stat_index];
    if(delta > stat.peak_bytes) stat.peak_bytes = delta;
}

void MemFunctions::OnSceneExit(){
    const uint32_t used = ReadMallocInfo().used;

    //シーンのウィジェットが1つも生きていない瞬間。リーク判定の一次情報になる
    if(floor_samples == 0){
        floor_first_used = used;
        floor_max_used = used;
    }
    floor_last_used = used;
    if(used > floor_max_used) floor_max_used = used;
    floor_samples++;

    if(scene_active && current_stat_index >= 0 && baseline_valid){
        SceneStat& stat = scene_stats[current_stat_index];

        //全部破棄したのにシーン開始前より増えていれば、その差分は解放漏れの候補
        if(used > scene_baseline_used){
            const uint32_t residue = used - scene_baseline_used;
            stat.residue_bytes += residue;

            //閾値以下は背景処理の一時確保に埋もれるので即時ログには出さない
            if(residue >= kResidueLogThreshold){
                LOG_SYS_DEBUG("[MEM] シーン破棄: %s 解放後も%luB残留 (累計%luB)",
                    stat.name.c_str(),
                    (unsigned long)residue,
                    (unsigned long)stat.residue_bytes);
            }
        }
    }

    scene_active = false;
    current_stat_index = -1;
}

void MemFunctions::BeforeSceneEnter(){
    scene_baseline_used = ReadMallocInfo().used;
    baseline_valid = true;
}

void MemFunctions::AfterSceneEnter(const char* scene_name){
    if(!baseline_valid){
        //BeforeSceneEnter()を通らずに来た場合は、この時点を基準にして次回以降に備える
        scene_baseline_used = ReadMallocInfo().used;
        baseline_valid = true;
    }

    const uint32_t used = ReadMallocInfo().used;
    const uint32_t enter_delta = (used > scene_baseline_used) ? (used - scene_baseline_used) : 0;

    SceneStat* stat = FindOrCreateStat(scene_name);
    stat->visits++;
    if(enter_delta > stat->enter_bytes) stat->enter_bytes = enter_delta;
    if(enter_delta > stat->peak_bytes)  stat->peak_bytes = enter_delta;

    current_stat_index = (int)(stat - &scene_stats[0]);
    scene_active = true;

    transition_count++;

    //遷移のたびに1行だけ出す(mallinfoのみ。max_allocの実測はレポート側に任せる)。
    //これが無いと最初のレポートが出る10回目まで、シーンの数字が一切見えない
    LOG_SYS_DEBUG("[MEM] シーン生成: %s +%luB (used=%luB 空き塊=%lu)",
        stat->name.c_str(),
        (unsigned long)enter_delta,
        (unsigned long)used,
        (unsigned long)ReadMallocInfo().free_blocks);

    //1回目は表の書式を確認できるようレポートも出しておく
    if(transition_count == 1){
        LogReport();
        return;
    }
    if(kAutoReportInterval > 0 && (transition_count % kAutoReportInterval) == 0){
        LogReport();
    }
}

void MemFunctions::LogReport(){
    const Snapshot s = Take(true);

    LOG_SYS_DEBUG("[MEM] ===== シーン別メモリレポート (遷移%lu回目) =====",
        (unsigned long)transition_count);
    //列がずれないよう見出しはASCIIで揃える
    LOG_SYS_DEBUG("[MEM] %-12s %9s %9s %9s %7s", "scene", "enter", "peak", "residue", "visits");

    uint32_t worst_peak = 0;
    for(int i = 0; i < scene_stat_count; i++){
        const SceneStat& stat = scene_stats[i];
        LOG_SYS_DEBUG("[MEM] %-12s %8luB %8luB %8luB %7lu",
            stat.name.c_str(),
            (unsigned long)stat.enter_bytes,
            (unsigned long)stat.peak_bytes,
            (unsigned long)stat.residue_bytes,
            (unsigned long)stat.visits);
        if(stat.peak_bytes > worst_peak) worst_peak = stat.peak_bytes;
    }

    LOG_SYS_DEBUG("[MEM] enter=onEnter()での増分 / peak=滞在中の最大増分 / residue=破棄後も戻らなかった累計");
    LOG_SYS_DEBUG("[MEM] ※residueは滞在中ずっとを窓にするため、背景処理の一時確保も拾う。");
    LOG_SYS_DEBUG("[MEM] 　リークの有無は下の「ヒープ下限」で判断すること");

    Log("現在");
    if(s.probe_grew_arena){
        LOG_SYS_DEBUG("[MEM] ※max_allocの実測中にヒープが伸びたため、実際の空き連続域はこれより大きい可能性あり");
    }

    //アリーナ枠の見積もり。実測の最大ピークに5割の余裕を持たせる
    LOG_SYS_DEBUG("[MEM] -> シーン領域の最大ピーク=%luB / 推奨アリーナ枠=%luB (x1.5)",
        (unsigned long)worst_peak,
        (unsigned long)(worst_peak + worst_peak / 2));

    //リーク判定の本命。シーンのウィジェットが無い瞬間のusedを毎回同じ条件で比べる
    if(floor_samples > 0){
        LOG_SYS_DEBUG("[MEM] ヒープ下限(シーン破棄直後/%lu回): 初回=%luB 最新=%luB 最大=%luB 差%+ldB",
            (unsigned long)floor_samples,
            (unsigned long)floor_first_used,
            (unsigned long)floor_last_used,
            (unsigned long)floor_max_used,
            (long)floor_last_used - (long)floor_first_used);
        LOG_SYS_DEBUG("[MEM] -> 差が回数に比例して増えるならリーク。横ばいならリーク無し");
    }
}

#else // PICO_MEM_PROFILE == 0

MemFunctions::Snapshot MemFunctions::Take(bool){ return Snapshot(); }
uint16_t MemFunctions::FragmentationPermil(const Snapshot&){ return 0; }
void MemFunctions::Log(const char*, bool){}
void MemFunctions::LogDelta(const char*, const Snapshot&, bool){}
void MemFunctions::Setup(){}
void MemFunctions::SealPermanentBaseline(){}
void MemFunctions::Update(){}
void MemFunctions::OnSceneExit(){}
void MemFunctions::BeforeSceneEnter(){}
void MemFunctions::AfterSceneEnter(const char*){}
void MemFunctions::LogReport(){}

#endif
