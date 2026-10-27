// src/util/ChunkArena.hpp
//
// 小さなオブジェクトを「塊(チャンク)」へ詰めて持つ確保器。シーンアリーナ(Widget::operator new)の中身。
//
// なぜ固定の大きな領域にしないか:
//   以前の計測(docs/design/performance.md「メモリ計測の結論」)で、64KBを常時取る形は
//   「断片化もリークも観測されていないのに、Lua・TLSが使えるヒープを64KB削る」ので見送った。
//   ここではChunkBytesの塊を要るぶんだけヒープから借り、空いたら返す。シーンにウィジェットが
//   少なければ塊も少なく、シーンを離れれば全部ヒープへ戻る。
//
// しくみ:
//   - 確保: ①同じ大きさの空き(使い回し)→ ②塊の末尾から切り出す → ③新しい塊を借りる、の順。
//     kMaxObject(塊の半分)より大きい物と、塊がMaxChunks個に達した後は nullptr を返す
//     (呼び出し側がふつうのmallocへ回す。アリーナが溢れても動き続ける)。
//   - 解放: 塊の末尾なら末尾を戻し、そうでなければ大きさごとの空きとして覚える。
//     シーンの途中で開いて閉じるダイアログは、次に開いたとき同じ場所をそのまま使う。
//     塊の中が全部空いたら、その塊をヒープへ返す(いま切り出し中の塊だけは空にして持っておく)。
//   - releaseEmpty(): シーンの終わりに呼ぶ。空いた塊を残らずヒープへ返す。
//   - 解放には確保したときの大きさが要る(sized delete から受け取る)。塊の中に見出しは置かない。
//
// ASan付きのホストテストでは、空きと塊の未使用部分に毒を塗る(解放後の読み書きを捕まえるため)。
#pragma once
#include <cstddef>
#include <stdint.h>
#include <stdlib.h>

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define PICO_ARENA_POISON(p, n)   ASAN_POISON_MEMORY_REGION((p), (n))
#define PICO_ARENA_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#define PICO_ARENA_POISON(p, n)   ((void)(p), (void)(n))
#define PICO_ARENA_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

template<size_t ChunkBytes, size_t MaxChunks>
class ChunkArena {
public:
    // mallocと同じ整列(32bitで8、64bitで16)
    static constexpr size_t kAlign = alignof(std::max_align_t);
    static constexpr size_t kMaxObject = ChunkBytes / 2;
    static_assert(ChunkBytes % kAlign == 0, "ChunkArena: 塊の大きさは整列の倍数にする");

    struct Stats {
        uint32_t chunks = 0;        // いま借りている塊
        uint32_t peak_chunks = 0;   // 借りた塊の最大
        uint32_t live = 0;          // いま生きている物の数
        uint32_t served = 0;        // アリーナから渡した回数(累計)
        uint32_t reused = 0;        // うち空きの使い回し
        uint32_t fallback = 0;      // 大きすぎる・塊が足りない等でmallocへ回した回数(累計)
        uint32_t borrowed = 0;      // 塊をヒープから借りた回数(累計)
    };

    ChunkArena() = default;
    // 空の塊だけ返す。生きている物が残る塊は持ったまま(プログラムの終わりに後から消される物があっても壊さない)
    ~ChunkArena() { releaseEmpty(); }
    ChunkArena(const ChunkArena&) = delete;
    ChunkArena& operator=(const ChunkArena&) = delete;

    // bytesぶんを渡す。渡せなければnullptr(呼び出し側がmallocする)
    void* alloc(size_t bytes) {
        const size_t sz = roundUp(bytes);
        if (sz > kMaxObject) { stats_.fallback++; return nullptr; }

        // ①同じ大きさの空き
        for (FreeBlock** link = &free_; *link; link = &(*link)->next) {
            FreeBlock* b = *link;
            if (b->size != sz) continue;
            *link = b->next;
            Chunk* c = find(b);
            c->live++;
            PICO_ARENA_UNPOISON(b, bytes);
            stats_.live++;
            stats_.served++;
            stats_.reused++;
            return b;
        }

        // ②塊の末尾(新しい塊から順に)
        for (size_t i = count_; i-- > 0; ) {
            if (void* p = carve(chunks_[i], sz, bytes)) return p;
        }

        // ③新しい塊
        if (count_ >= MaxChunks) { stats_.fallback++; return nullptr; }
        unsigned char* base = static_cast<unsigned char*>(malloc(ChunkBytes));
        if (!base) { stats_.fallback++; return nullptr; }
        PICO_ARENA_POISON(base, ChunkBytes);
        Chunk& c = chunks_[count_++];
        c.base = base;
        c.top = 0;
        c.live = 0;
        stats_.borrowed++;
        stats_.chunks = (uint32_t)count_;
        if (stats_.chunks > stats_.peak_chunks) stats_.peak_chunks = stats_.chunks;
        return carve(c, sz, bytes);
    }

    // pを返す。アリーナの物でなければfalse(呼び出し側がfreeする)
    bool release(void* p, size_t bytes) {
        if (!p) return true;
        Chunk* c = find(p);
        if (!c) return false;
        const size_t sz = roundUp(bytes);
        c->live--;
        stats_.live--;

        if (c->live == 0) {
            // 塊が丸ごと空いた: 空きの一覧からこの塊の分を外す
            dropFreeBlocksIn(*c);
            if (c == &chunks_[count_ - 1]) {
                // いま切り出している塊は空にして持っておく(閉じてすぐ開くダイアログで借り直さない)
                c->top = 0;
                PICO_ARENA_POISON(c->base, ChunkBytes);
            } else {
                removeChunk(c);
            }
            return true;
        }

        unsigned char* q = static_cast<unsigned char*>(p);
        if (q + sz == c->base + c->top) {
            // 末尾: そのまま戻す。戻した先が空きなら続けて戻す
            c->top -= (uint32_t)sz;
            PICO_ARENA_POISON(q, sz);
            shrinkTop(*c);
            return true;
        }

        PICO_ARENA_POISON(q, sz);
        PICO_ARENA_UNPOISON(q, sizeof(FreeBlock));
        FreeBlock* b = reinterpret_cast<FreeBlock*>(q);
        b->size = sz;
        b->next = free_;
        free_ = b;
        return true;
    }

    // 中身が空の塊を全部ヒープへ返す(シーンの終わり)。生きている物が残る塊はそのまま
    void releaseEmpty() {
        for (size_t i = count_; i-- > 0; ) {
            if (chunks_[i].live == 0) removeChunk(&chunks_[i]);
        }
    }

    bool owns(const void* p) const {
        return const_cast<ChunkArena*>(this)->find(p) != nullptr;
    }

    const Stats& stats() const { return stats_; }

private:
    struct Chunk {
        unsigned char* base = nullptr;
        uint32_t top = 0;   // 切り出した末尾(base からのバイト数)
        uint32_t live = 0;  // この塊で生きている物の数
    };
    struct FreeBlock {
        FreeBlock* next;
        size_t size;
    };

    Chunk chunks_[MaxChunks];
    size_t count_ = 0;
    FreeBlock* free_ = nullptr;
    Stats stats_;

    static size_t roundUp(size_t bytes) {
        size_t sz = bytes < sizeof(FreeBlock) ? sizeof(FreeBlock) : bytes;
        return (sz + kAlign - 1) & ~(kAlign - 1);
    }

    void* carve(Chunk& c, size_t sz, size_t bytes) {
        if (c.top + sz > ChunkBytes) return nullptr;
        unsigned char* p = c.base + c.top;
        c.top += (uint32_t)sz;
        c.live++;
        PICO_ARENA_UNPOISON(p, bytes);
        stats_.live++;
        stats_.served++;
        return p;
    }

    Chunk* find(const void* p) {
        const unsigned char* q = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < count_; i++) {
            if (q >= chunks_[i].base && q < chunks_[i].base + ChunkBytes) return &chunks_[i];
        }
        return nullptr;
    }

    // 末尾のすぐ手前が空きなら、空きの一覧から外して末尾を戻す(続く限り)
    void shrinkTop(Chunk& c) {
        bool moved = true;
        while (moved && c.top > 0) {
            moved = false;
            for (FreeBlock** link = &free_; *link; link = &(*link)->next) {
                FreeBlock* b = *link;
                unsigned char* q = reinterpret_cast<unsigned char*>(b);
                if (q + b->size != c.base + c.top) continue;
                *link = b->next;
                c.top -= (uint32_t)b->size;
                PICO_ARENA_POISON(q, b->size);
                moved = true;
                break;
            }
        }
    }

    void dropFreeBlocksIn(const Chunk& c) {
        for (FreeBlock** link = &free_; *link; ) {
            unsigned char* q = reinterpret_cast<unsigned char*>(*link);
            if (q >= c.base && q < c.base + ChunkBytes) *link = (*link)->next;
            else link = &(*link)->next;
        }
    }

    void freeChunk(Chunk& c) {
        PICO_ARENA_UNPOISON(c.base, ChunkBytes);
        free(c.base);
        c.base = nullptr;
    }

    // 空の塊をヒープへ返し、配列を詰める(新しい塊ほど後ろ、の並びは保つ)
    void removeChunk(Chunk* c) {
        dropFreeBlocksIn(*c);
        freeChunk(*c);
        const size_t i = (size_t)(c - chunks_);
        for (size_t j = i; j + 1 < count_; j++) chunks_[j] = chunks_[j + 1];
        count_--;
        stats_.chunks = (uint32_t)count_;
    }
};
