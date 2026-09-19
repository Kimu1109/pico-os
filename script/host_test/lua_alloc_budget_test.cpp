// lua_newstate()にカスタムアロケータを渡し、確保量に上限を課しても安全に
// 動作することを確認するテスト。
//
// CLAUDE.md「Lua着手前の受け皿の状態」の「RAM/Flash予算」が挙げている
// 「lua_newstateのカスタムallocでLuaに上限枠(暫定200KB)を切る」方式が
// 実際に安全に機能するか(予算超過でクラッシュせず、リークも残さないか)を
// Lua本体に手を入れずに検証する。まだ本番の200KB版アロケータそのものではない
// (置き場所や実際の枠はLuaバインディング本体を書く回で決める)。
//
// 実測(このテストと同じ構成で事前に確認済み): 空のstate+全標準ライブラリで
// ピーク約19.5KB、lua_close後は常にused=0(リーク無し)。
#include "lua.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
    struct Budget {
        size_t used = 0;
        size_t cap;
        size_t peak = 0;
    };

    // Luaが要求する確保/再確保/解放を1つの関数窓口に集約する形(lua_Alloc)。
    // nsize==0は解放、ptr==nullptrは新規確保、それ以外は再確保として扱う
    // (lua.hのlua_Allocのドキュメント通り)。
    void* BudgetAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
        Budget* b = static_cast<Budget*>(ud);
        const size_t old = ptr ? osize : 0;

        if (nsize == 0) {
            if (ptr) {
                b->used -= old;
                free(ptr);
            }
            return nullptr;
        }

        // 予算を超える確保は失敗させる。Lua側はこれを「メモリ不足」として
        // 扱い(LUA_ERRMEM)、abort()はしない(下のテストで確認する)
        if (b->used - old + nsize > b->cap) {
            return nullptr;
        }

        void* np = realloc(ptr, nsize);
        if (!np) return nullptr;

        b->used = b->used - old + nsize;
        if (b->used > b->peak) b->peak = b->used;
        return np;
    }

    int OpenLibsWrapper(lua_State* L) {
        luaL_openlibs(L);
        return 0;
    }
}

static int failures = 0;
static void check(bool cond, const char* label) {
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if (!cond) failures++;
}

int main(){
    // ---- 予算内なら普通に動く ----
    {
        Budget budget{0, 64 * 1024};
        lua_State* L = lua_newstate(BudgetAlloc, &budget);
        check(L != nullptr, "予算64KiB: lua_newstateが成功する");
        if (L) {
            luaL_openlibs(L);
            check(luaL_dostring(L, "return 1+1") == LUA_OK, "予算内では通常通りスクリプトが動く");
            check(budget.used > 0, "カスタムallocが実際に確保量を追跡している");
            lua_close(L);
            check(budget.used == 0, "lua_close後は確保量が0に戻る(リーク無し)");
        }
    }

    // ---- state本体すら確保できないほど小さい予算 ----
    // lua_newstate自身が内部でluaD_rawrunprotected(保護された呼び出し)を
    // 使っているため、この時点でのOOMはabortせずnullptrとして返る
    {
        Budget budget{0, 100};
        lua_State* L = lua_newstate(BudgetAlloc, &budget);
        check(L == nullptr, "予算100B: state本体も確保できずlua_newstateがnullptrを返す(クラッシュしない)");
        check(budget.used == 0, "失敗時も確保量は0のまま(部分確保が残らない)");
    }

    // ---- state本体は作れるが、標準ライブラリ一式までは入らない予算 ----
    // luaL_openlibs()を直接呼ぶとpcallで保護されておらず、内部でOOMが起きた際に
    // 保護フレームが無いままlongjmpしてabort()してしまう(Lua本体の仕様)。
    // 実際にLuaコードを実行する際は必ずpcall越しにする、という運用がそのまま
    // ここでの安全策にもなることを確認する
    {
        Budget budget{0, 12 * 1000};
        lua_State* L = lua_newstate(BudgetAlloc, &budget);
        check(L != nullptr, "予算12000B: state本体は確保できる");
        if (L) {
            lua_pushcfunction(L, OpenLibsWrapper);
            const int status = lua_pcall(L, 0, 0, 0);
            check(status == LUA_ERRMEM,
                  "予算12000B: pcall越しのopenlibsは(abortせず)LUA_ERRMEMで安全に失敗する");
            lua_close(L);
            check(budget.used == 0, "OOM発生後にlua_closeしても確保量は0に戻る(リーク無し)");
        }
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
