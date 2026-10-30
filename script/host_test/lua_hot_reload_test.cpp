// 実機でのLuaアプリのホットリロード(lua/LuaHotReload)のうち、画面に依らない所を確かめる。run.sh から呼ぶ。
//
//   - AppDirOf: /lua/apps/<dir>/ の下の .lua(下の段も)と app.cfg だけを拾う
//   - OnFileChanged/TakeDue: 静かになる(kQuietMs)まで待つ・続けて届いたら待ち直す・切ってあれば何もしない
//
// 開き直し(シーンの入れ替え)と Wi-Fi転送を通した一通りは PCビルドで確かめた(docs/design/devtools.md)。
#include "lua/LuaHotReload.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "services/Display_Functions.hpp"
#include "hal/Network_Functions.hpp"

#include <cstdio>
#include <cstring>

void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
// 開き直しの仕事が Wi-Fi転送(と電源)を引くので、液晶・Wi-Fiのスタブに無い口を埋める
void DisplayFunctions::SetSleeping(bool){}
bool DisplayFunctions::IsDimmed(){ return false; }
void NetworkFunctions::SetLowPower(bool){}
bool NetworkFunctions::StartSetupAp(const char*, const char*){ return false; }
void NetworkFunctions::StopSetupAp(){}
int NetworkFunctions::SetupApClients(){ return 0; }

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static bool g_enabled = true;
static bool Enabled(){ return g_enabled; }

static bool DirOf(const char* path, const char* expect){
    FixedString<PICO_PATH_LEN> out;
    const bool ok = LuaHotReload::AppDirOf(path, out);
    return expect ? (ok && out == expect) : !ok;
}

int main(){
    LuaHotReload::enabled_query = &Enabled;

    check(DirOf("/lua/apps/テトリス/main.lua", "/lua/apps/テトリス"), "AppDirOf: main.lua");
    check(DirOf("/lua/apps/テトリス/lib/board.LUA", "/lua/apps/テトリス"), "AppDirOf: 下の段の .lua(大小を問わない)");
    check(DirOf("/lua/apps/テトリス/app.cfg", "/lua/apps/テトリス"), "AppDirOf: app.cfg");
    check(DirOf("/lua/apps/テトリス/store.json", nullptr), "AppDirOf: セーブ等は拾わない");
    check(DirOf("/lua/apps/テトリス/data/app.cfg", nullptr), "AppDirOf: 下の段の app.cfg は拾わない");
    check(DirOf("/lua/apps/テトリス", nullptr) && DirOf("/lua/apps//main.lua", nullptr), "AppDirOf: アプリのフォルダそのもの・空の名前");
    check(DirOf("/lua/main.lua", nullptr) && DirOf("/notes/a.lua", nullptr), "AppDirOf: /lua/apps/ の外");

    FixedString<PICO_PATH_LEN> dir;
    const uint32_t q = LuaHotReload::kQuietMs;
    LuaHotReload::OnFileChanged("/lua/apps/A/main.lua", 1000);
    check(!LuaHotReload::TakeDue(1000 + q - 1, dir), "待つ: 静かになるまでは開き直さない");
    LuaHotReload::OnFileChanged("/lua/apps/A/lib.lua", 1000 + q - 1);
    check(!LuaHotReload::TakeDue(1000 + q, dir), "待つ: 続けて届いたら待ち直す");
    check(LuaHotReload::TakeDue(1000 + 2 * q, dir) && dir == "/lua/apps/A", "待つ: 静かになったら1回だけ");
    check(!LuaHotReload::TakeDue(1000 + 3 * q, dir), "待つ: 済んだら忘れる");

    LuaHotReload::OnFileChanged("/lua/apps/A/icon.pimg", 5000);
    check(!LuaHotReload::TakeDue(5000 + q, dir), "見ない種類のファイルでは開き直さない");

    g_enabled = false;
    LuaHotReload::OnFileChanged("/lua/apps/A/main.lua", 9000);
    check(!LuaHotReload::TakeDue(9000 + q, dir), "切ってあれば何もしない(debug.cfg の lua-hot-reload)");

    // millis() が一周しても待てる
    g_enabled = true;
    LuaHotReload::Test::Reset();
    LuaHotReload::OnFileChanged("/lua/apps/B/main.lua", 0xFFFFFF00u);
    check(!LuaHotReload::TakeDue(0xFFFFFF10u, dir), "一周: まだ");
    check(LuaHotReload::TakeDue(0xFFFFFF00u + q, dir) && dir == "/lua/apps/B", "一周: 越えても静かになったら");

    printf("%s (%d件の失敗)\n", failures == 0 ? "全部通った" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
