// src/util/SizeClassPool.hpp
//
// 決まった大きさ(8・16・24・32B…)の小さな物を、大きさごとの塊(チャンク)へ詰めて持つ確保器。
// Luaの確保(LuaEngine::Alloc)の小さい物の置き場。docs/design/performance.md「Luaの小さな確保を専用のプールへ」。
//
// なぜ:
//   Luaの確保は8〜32Bが8割を超え(実機と同じ32bitで、Luaのアプリのテスト8本を計測)、1回ごとに
//   newlib の malloc(1個に見出し4Bと8Bへの切り上げ、最小16B)と arduino-pico の割り込み止め+ロックを通っていた。
//   ここでは見出しを置かずに大きさごとに詰め、malloc を呼ぶのは塊を借りる/返すときだけにする。
//
// しくみ(よくある slab と同じ):
//   - 塊は ChunkBytes(1KB 等)。先頭に見出し(この塊の大きさの区分・使っている数・空きの列・つなぎ)を置き、
//     残りを区分の大きさの枠に切る。塊はどれか1つの区分だけに使う。
//   - 区分ごとに「空きのある塊」の列を持ち、確保はその先頭の塊から枠を1つ取る(空きの列 → まだ使っていない枠)。
//     空きのある塊が無ければ、呼び出し側の関数(source)で新しい塊を借りる。借りられなければ nullptr
//     (呼び出し側がふつうの malloc へ回す)。
//   - 解放はどの塊の物かを、塊の番地の昇順の表を二分探索して引く(物の前に見出しを置かないため)。
//     塊が全部空いたら、区分ごとに1つだけ空のまま持ち(借りて返すのを繰り返さないため)、2つ目からはすぐ返す。
//   - releaseEmpty() で空の塊を残らず返す(Lua を閉じた後・C++ 側がメモリに困ってごみを集めた後)。
//   - 整列は8(Lua の LUAI_MAXALIGN が求めるのは double・long long・ポインタの整列。32bit/64bit とも8)。
//
// ASan 付きのホストテストでは、空いた枠と使っていない枠に毒を塗る(解放後の読み書きを捕まえるため)。
// 1つのスレッドからだけ使う(Lua は1コア目だけで動く)。
#pragma once
#include <cstddef>
#include <cstring>
#include <stdint.h>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define PICO_POOL_POISON(p, n)   ASAN_POISON_MEMORY_REGION((p), (n))
#define PICO_POOL_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#define PICO_POOL_POISON(p, n)   ((void)(p), (void)(n))
#define PICO_POOL_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

// ChunkBytes: 塊の大きさ / MaxChunks: 借りる塊の上限(超えたら nullptr) / ClassCount: 区分の数(8の倍数を8から順に)
template<size_t ChunkBytes, size_t MaxChunks, size_t ClassCount>
class SizeClassPool {
public:
    static constexpr size_t kAlign = 8;
    static constexpr size_t kMaxObject = ClassCount * kAlign;   // これより大きい物は扱わない

    // 塊を借りる/返す関数(呼び出し側がヒープの残りを守る判断をする)。借りられなければ nullptr を返す
    using AcquireFn = void* (*)(void* ctx, size_t bytes);
    using ReleaseFn = void (*)(void* ctx, void* chunk);

    struct Stats {
        uint32_t chunks = 0;        // いま借りている塊
        uint32_t peak_chunks = 0;   // 借りた塊の最大
        uint32_t live = 0;          // いま生きている物の数
        uint32_t live_bytes = 0;    // いま生きている物の枠の合計(区分の大きさで数える)
        uint32_t served = 0;        // プールから渡した回数(累計)
        uint32_t borrowed = 0;      // 塊を借りた回数(累計)
        uint32_t returned = 0;      // 塊を返した回数(累計)
        uint32_t refused = 0;       // 塊を借りられず nullptr を返した回数(累計。上限・source の拒否)
    };

    SizeClassPool(AcquireFn acquire, ReleaseFn release, void* ctx)
        : acquire_(acquire), release_(release), ctx_(ctx) {}
    // 生きている物が残っていても、塊は返さない(返すのは呼び出し側が全部解放した後の releaseEmpty())
    ~SizeClassPool() { releaseEmpty(); }
    SizeClassPool(const SizeClassPool&) = delete;
    SizeClassPool& operator=(const SizeClassPool&) = delete;

    static constexpr bool fits(size_t bytes) { return bytes > 0 && bytes <= kMaxObject; }
    // 同じ区分(=同じ枠のまま大きさを変えられる)か
    static constexpr bool sameClass(size_t a, size_t b) { return classOf(a) == classOf(b); }

    // bytes(1〜kMaxObject)ぶんを渡す。渡せなければ nullptr
    void* alloc(size_t bytes) {
        if (!fits(bytes)) return nullptr;
        const size_t ci = classOf(bytes);
        Chunk* c = partial_[ci];
        if (!c) {
            c = newChunk(ci);
            if (!c) { stats_.refused++; return nullptr; }
        }
        void* p;
        if (c->free) {
            Slot* s = c->free;
            PICO_POOL_UNPOISON(s, slotSize(ci));
            c->free = s->next;
            p = s;
        } else {
            p = slotAt(c, c->bump++);
            PICO_POOL_UNPOISON(p, slotSize(ci));
        }
        c->used++;
        if (!c->free && c->bump == capacity(ci)) unlinkPartial(c);   // 満杯になった
        if (c == empty_[ci]) empty_[ci] = nullptr;
        stats_.live++;
        stats_.live_bytes += (uint32_t)slotSize(ci);
        stats_.served++;
        return p;
    }

    // p がこのプールの物か(塊の表を二分探索)
    bool owns(const void* p) const { return findChunk(p) != nullptr; }

    // このプールの物なら解放して true。違えば何もしないで false(呼び出し側が free する)
    bool free(void* p) {
        Chunk* c = findChunk(p);
        if (!c) return false;
        const size_t ci = c->cls;
        const bool was_full = !c->free && c->bump == capacity(ci);
        Slot* s = static_cast<Slot*>(p);
        s->next = c->free;
        c->free = s;
        PICO_POOL_POISON(s, slotSize(ci));
        c->used--;
        stats_.live--;
        stats_.live_bytes -= (uint32_t)slotSize(ci);
        if (was_full) linkPartial(c);
        if (c->used == 0) {
            if (!empty_[ci]) {
                empty_[ci] = c;       // 区分ごとに1つは空のまま持つ
            } else {
                dropChunk(c);
            }
        }
        return true;
    }

    // 空の塊を残らず返す
    void releaseEmpty() {
        for (size_t ci = 0; ci < ClassCount; ci++) {
            if (Chunk* c = empty_[ci]) {
                empty_[ci] = nullptr;
                dropChunk(c);
            }
        }
    }

    const Stats& stats() const { return stats_; }
    size_t chunkBytesInUse() const { return (size_t)count_ * ChunkBytes; }

private:
    struct Slot { Slot* next; };
    struct Chunk {
        Chunk* next;      // 空きのある塊の列(区分ごと)
        Chunk* prev;
        Slot* free;       // 一度使って空いた枠の列
        uint16_t used;    // 使っている枠の数
        uint16_t bump;    // まだ一度も使っていない枠の先頭(この番号から後ろは未使用)
        uint8_t cls;
        bool in_partial;
    };
    static constexpr size_t kHeader = (sizeof(Chunk) + kAlign - 1) / kAlign * kAlign;
    static_assert(ChunkBytes > kHeader + kMaxObject, "SizeClassPool: 塊が小さすぎる");
    static_assert(ChunkBytes % kAlign == 0, "SizeClassPool: 塊の大きさは8の倍数にする");
    static_assert(ClassCount >= 1, "SizeClassPool: 区分が要る");

    static constexpr size_t classOf(size_t bytes) { return (bytes + kAlign - 1) / kAlign - 1; }
    static constexpr size_t slotSize(size_t ci) { return (ci + 1) * kAlign; }
    static constexpr size_t capacity(size_t ci) { return (ChunkBytes - kHeader) / slotSize(ci); }
    static void* slotAt(Chunk* c, size_t i) {
        return reinterpret_cast<uint8_t*>(c) + kHeader + i * slotSize(c->cls);
    }

    Chunk* newChunk(size_t ci) {
        if (count_ >= MaxChunks) return nullptr;
        void* mem = acquire_(ctx_, ChunkBytes);
        if (!mem) return nullptr;
        Chunk* c = static_cast<Chunk*>(mem);
        c->next = c->prev = nullptr;
        c->free = nullptr;
        c->used = 0;
        c->bump = 0;
        c->cls = (uint8_t)ci;
        c->in_partial = false;
        PICO_POOL_POISON(reinterpret_cast<uint8_t*>(c) + kHeader, ChunkBytes - kHeader);
        // 塊の表へ番地の昇順で入れる
        size_t pos = lowerBound(c);
        memmove(&chunks_[pos + 1], &chunks_[pos], (count_ - pos) * sizeof(Chunk*));
        chunks_[pos] = c;
        count_++;
        linkPartial(c);
        stats_.chunks = (uint32_t)count_;
        if (stats_.chunks > stats_.peak_chunks) stats_.peak_chunks = stats_.chunks;
        stats_.borrowed++;
        return c;
    }

    void dropChunk(Chunk* c) {
        if (c->in_partial) unlinkPartial(c);
        size_t pos = lowerBound(c);
        memmove(&chunks_[pos], &chunks_[pos + 1], (count_ - pos - 1) * sizeof(Chunk*));
        count_--;
        stats_.chunks = (uint32_t)count_;
        stats_.returned++;
        PICO_POOL_UNPOISON(c, ChunkBytes);
        release_(ctx_, c);
    }

    void linkPartial(Chunk* c) {
        Chunk*& head = partial_[c->cls];
        c->prev = nullptr;
        c->next = head;
        if (head) head->prev = c;
        head = c;
        c->in_partial = true;
    }
    void unlinkPartial(Chunk* c) {
        if (c->prev) c->prev->next = c->next;
        else partial_[c->cls] = c->next;
        if (c->next) c->next->prev = c->prev;
        c->next = c->prev = nullptr;
        c->in_partial = false;
    }

    // c 以上の最初の位置
    size_t lowerBound(const void* p) const {
        size_t lo = 0, hi = count_;
        const uintptr_t key = reinterpret_cast<uintptr_t>(p);
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (reinterpret_cast<uintptr_t>(chunks_[mid]) < key) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }
    // p を含む塊(枠の中の番地なら)。無ければ nullptr
    Chunk* findChunk(const void* p) const {
        const uintptr_t key = reinterpret_cast<uintptr_t>(p);
        size_t lo = 0, hi = count_;
        while (lo < hi) {   // key 以下の最後の塊
            const size_t mid = (lo + hi) / 2;
            if (reinterpret_cast<uintptr_t>(chunks_[mid]) <= key) lo = mid + 1;
            else hi = mid;
        }
        if (lo == 0) return nullptr;
        Chunk* c = chunks_[lo - 1];
        const uintptr_t base = reinterpret_cast<uintptr_t>(c);
        if (key < base + kHeader || key >= base + ChunkBytes) return nullptr;
        return c;
    }

    AcquireFn acquire_;
    ReleaseFn release_;
    void* ctx_;
    Chunk* chunks_[MaxChunks] = {};       // 番地の昇順
    size_t count_ = 0;
    Chunk* partial_[ClassCount] = {};     // 区分ごとの空きのある塊の列
    Chunk* empty_[ClassCount] = {};       // 区分ごとに1つだけ持っておく空の塊
    Stats stats_;
};
