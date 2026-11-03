/*
** pico-os: Lua の命令を実行する本体(luaV_execute)を実機のRAMに置くための宣言。
** lib/lua/library.json がLuaのファイルすべてへ -include で読ませる(src/ のLua本体は書き換えない)。
** -DPICO_LUA_VM_IN_RAM=1 のときだけ効き、既定は何もしない(RAMを約10.5KB使うため。
** docs/design/performance.md「よく通る所をRAMに置く」)。
**
** 先に節の属性つきで宣言しておくと、lvm.h の宣言と lvm.c の定義がその属性を引き継ぐ。
** `.time_critical.*` は arduino-pico のリンカスクリプトが RAM(.data)へ集める。
** PC/Webのビルド・ホストテストは library.json を読まないので、ここは読まれない。
*/
#ifndef LUA_PICO_RAM_H
#define LUA_PICO_RAM_H

#if defined(PICO_LUA_VM_IN_RAM) && PICO_LUA_VM_IN_RAM
struct lua_State;
struct CallInfo;
__attribute__((section(".time_critical.luaV_execute")))
void luaV_execute (struct lua_State *L, struct CallInfo *ci);
#endif

#endif
