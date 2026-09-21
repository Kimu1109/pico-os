// LuaAppScanner(SD上の"/lua/apps/"を走査してランチャの登録簿へ自動登録する)の
// ホストテスト。
//
// **完全な走査そのものはここでは確認できない。** script/host_test/stubs/SdFat.h は
// パス->内容のフラットなmapで、ディレクトリの実体もopenNext()による走査も無い
// (openNext()は常にfalseを返す)ため、"/lua/apps/"配下に何個ディレクトリを置いても
// この環境では1件も見つからない。この制約はCLAUDE.mdの「Doc_Cache::Clear()」
// 「pico.sd_list」と同じもので、実際の走査結果はPCビルド(pc/compat/SdFat.hは
// 実ファイルシステム)の--shotで確認した(pc/sdcard/lua/apps/スキャン確認/main.lua
// が実際にランチャへ現れ、タップで起動できることを確認済み)。
//
// ここで確認するのは、ASanで再現できる範囲の「何もしない」経路が本当に安全に
// 何もしないこと: SD無しの間はScan()が0を返しクラッシュしないこと、
// SD使用可能でもディレクトリ自体が無ければ同じく0を返すこと。
#include "lua/LuaAppScanner.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <cstdarg>

// ---- モック(lua_scene_test.cppと同じ方針。LuaAppScannerはLuaSceneを構築する
// 生成関数を保持するため、実際に走査で見つからなくても、LuaScene.cppとその依存先を
// リンクする必要がある) ----
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void KeyboardFunctions::HideAll(){}
void KeyboardFunctions::Setup(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    AppFunctions::Clear();

    // ---- SD無し ----
    OSData::SD_usable = false;
    const int found_no_sd = LuaAppScanner::Scan();
    check(found_no_sd == 0, "Scan(): SD無しの間は0件(クラッシュしない)");
    check(AppFunctions::Count() == 0, "Scan(): SD無しの間は登録簿も増えない");

    // ---- SDは使えるが"/lua/apps/"自体が無い ----
    OSData::SD_usable = true;
    const int found_no_dir = LuaAppScanner::Scan();
    check(found_no_dir == 0, "Scan(): \"/lua/apps/\"が無ければ0件(クラッシュしない)");
    check(AppFunctions::Count() == 0, "Scan(): ディレクトリが無い間は登録簿も増えない");

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
