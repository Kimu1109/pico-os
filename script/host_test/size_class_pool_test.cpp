// 小さな物の確保器(src/util/SizeClassPool.hpp)のテスト。ASan 付きで動かす。
//
// 確かめること:
//   - 1〜kMaxObject の大きさで渡した物が重ならず、8に整列し、書いた中身が壊れない(乱数で確保と解放を混ぜ、中身を突き合わせる)
//   - kMaxObject を超える・0 は扱わない(nullptr)。owns()/free() はよその番地(malloc の物・塊の見出し)を自分の物と言わない
//   - 塊は区分ごとに使い、全部空いたら区分ごとに1つだけ持って、2つ目からはすぐ返す。releaseEmpty() で残らず返す
//   - 塊の上限・借りる関数の拒否で nullptr(refused が増える)。空きができればまた渡せる
//   - sameClass(): 同じ枠のまま大きさを変えられるか
// LuaEngine での使い方(予算・ヒープの残り・閉じた後の返却)は lua_engine_test 側で見る。
#include "util/SizeClassPool.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

namespace {
    constexpr size_t kChunk = 1024;
    struct Source {
        int borrowed = 0;
        int returned = 0;
        bool refuse = false;
        std::set<void*> live;
    };
    void* Acquire(void* ctx, size_t bytes){
        Source* s = static_cast<Source*>(ctx);
        if(s->refuse) return nullptr;
        void* p = malloc(bytes);
        s->borrowed++;
        s->live.insert(p);
        return p;
    }
    void Release(void* ctx, void* p){
        Source* s = static_cast<Source*>(ctx);
        s->returned++;
        s->live.erase(p);
        free(p);
    }
    using Pool = SizeClassPool<kChunk, 64, 4>;   // 8/16/24/32B

    uint8_t Pattern(size_t id, size_t i){ return (uint8_t)(id * 131 + i * 7 + 1); }
}

int main(){
    // ---- 乱数で確保と解放を混ぜる ----
    {
        Source src;
        Pool pool(Acquire, Release, &src);
        std::mt19937 rng(12345);
        struct Obj { size_t size; size_t id; };
        std::map<uint8_t*, Obj> live;
        size_t next_id = 1;
        bool ok_align = true, ok_overlap = true, ok_content = true, ok_owns = true;
        for(int step = 0; step < 200000; step++){
            const bool do_alloc = live.empty() || (rng() % 100) < (step < 100000 ? 55 : 45);
            if(do_alloc){
                const size_t n = 1 + rng() % Pool::kMaxObject;
                uint8_t* p = static_cast<uint8_t*>(pool.alloc(n));
                if(!p) continue;   // 上限(64塊)に当たったら渡さないだけ
                if(reinterpret_cast<uintptr_t>(p) % 8 != 0) ok_align = false;
                auto it = live.lower_bound(p);
                if(it != live.end() && it->first < p + n) ok_overlap = false;
                if(it != live.begin()){ auto pr = std::prev(it); if(pr->first + pr->second.size > p) ok_overlap = false; }
                if(!pool.owns(p)) ok_owns = false;
                const size_t id = next_id++;
                for(size_t i = 0; i < n; i++) p[i] = Pattern(id, i);
                live[p] = Obj{n, id};
            }else{
                auto it = live.begin();
                std::advance(it, rng() % live.size());
                for(size_t i = 0; i < it->second.size; i++){
                    if(it->first[i] != Pattern(it->second.id, i)){ ok_content = false; break; }
                }
                if(!pool.free(it->first)) ok_owns = false;
                live.erase(it);
            }
        }
        check(ok_align, "8に整列している");
        check(ok_overlap, "生きている物が重ならない");
        check(ok_content, "書いた中身が解放まで壊れない");
        check(ok_owns, "渡した物は owns()/free() が自分の物と分かる");
        check(pool.stats().live == live.size(), "生きている数が合う");
        for(auto& kv : live) pool.free(kv.first);
        live.clear();
        check(pool.stats().live == 0 && pool.stats().live_bytes == 0, "全部解放すると生きている数・バイトが0");
        check(pool.stats().chunks <= 4, "全部空くと区分ごとに1つまでしか持たない");
        pool.releaseEmpty();
        check(pool.stats().chunks == 0 && src.live.empty(), "releaseEmpty() で塊を残らず返す");
        check(src.borrowed == src.returned, "借りた数と返した数が合う");
    }

    // ---- 扱わない大きさ・よその番地 ----
    {
        Source src;
        Pool pool(Acquire, Release, &src);
        check(pool.alloc(0) == nullptr, "0Bは扱わない");
        check(pool.alloc(Pool::kMaxObject + 1) == nullptr, "kMaxObjectを超えると扱わない");
        void* a = pool.alloc(16);
        void* m = malloc(16);
        check(!pool.owns(m) && !pool.free(m), "mallocの物は自分の物と言わない");
        free(m);
        // 塊の見出し(塊の先頭)は枠ではない
        void* head = *src.live.begin();
        check(!pool.owns(head), "塊の見出しは自分の物と言わない");
        check(pool.free(a), "渡した物は解放できる");
        check(Pool::sameClass(9, 16) && !Pool::sameClass(16, 17) && Pool::sameClass(1, 8), "sameClass(): 同じ枠の大きさを見分ける");
    }

    // ---- 空の塊の持ち方 ----
    {
        Source src;
        Pool pool(Acquire, Release, &src);
        // 32Bの区分で2塊ぶん埋める
        std::vector<void*> v;
        while(src.borrowed < 2) v.push_back(pool.alloc(32));
        // 1塊目の物だけを返すと1塊目が空になり、空のまま持つ
        for(void* p : v) pool.free(p);
        check(pool.stats().chunks == 1 && src.returned == 1, "2つ空いたら1つ返して1つ持つ");
        void* again = pool.alloc(32);
        check(again && src.borrowed == 2, "持っていた空の塊から渡す(借り直さない)");
        pool.free(again);
        void* other = pool.alloc(8);
        check(other && pool.stats().chunks == 2, "区分が違えば別の塊を借りる");
        pool.free(other);
        pool.releaseEmpty();
        check(pool.stats().chunks == 0 && src.live.empty(), "releaseEmpty() で空の塊を全部返す");
    }

    // ---- 借りられないとき ----
    {
        Source src;
        Pool pool(Acquire, Release, &src);
        src.refuse = true;
        check(pool.alloc(8) == nullptr && pool.stats().refused == 1, "借りる関数が断ると nullptr");
        src.refuse = false;
        std::vector<void*> v;
        for(;;){ void* p = pool.alloc(32); if(!p) break; v.push_back(p); }
        check(pool.stats().chunks == 64, "塊の上限まで借りる");
        check(pool.stats().refused == 2, "上限に当たると nullptr");
        pool.free(v.back()); v.pop_back();
        void* again = pool.alloc(32);
        check(again != nullptr, "空きができればまた渡せる");
        pool.free(again);
        for(void* p : v) pool.free(p);
    }

    if(failures){
        printf("%d 件失敗\n", failures);
        return 1;
    }
    printf("全部成功\n");
    return 0;
}
