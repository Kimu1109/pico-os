// LuaAppScanner(SD上の"/lua/apps/"を走査してランチャの登録簿へ自動登録する)の
// ホストテスト。
//
// **完全な走査そのものはここでは確認できない。** script/host_test/stubs/SdFat.h は
// パス->内容のフラットなmapで、ディレクトリの実体もopenNext()による走査も無い
// (openNext()は常にfalseを返す)ため、"/lua/apps/"配下に何個ディレクトリを置いても
// この環境では1件も見つからない。この制約はdocs/design/browser-net.mdの「文書キャッシュ」(Doc_Cache::Clear())
// とdocs/design/lua-api.mdの「SDカードアクセス」(pico.sd_list)と同じもので、実際の走査結果はPCビルド(pc/compat/SdFat.hは
// 実ファイルシステム)の--shotで確認した(pc/sdcard/lua/apps/ の各アプリ
// が実際にランチャへ現れ、タップで起動できることを確認済み)。
//
// ここで確認するのは、ASanで再現できる範囲の「何もしない」経路が本当に安全に
// 何もしないこと: SD無しの間はScan()が0を返しクラッシュしないこと、
// SD使用可能でもディレクトリ自体が無ければ同じく0を返すこと。
#include "lua/LuaAppScanner.hpp"
#include "system/I18n_Functions.hpp"
#include <cstring>
#include "services/App_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
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
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::OnPanelShown(KeyboardPanel*){}
void KeyboardFunctions::OnPanelHidden(KeyboardPanel*){}
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel*, bool){}

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

    // app.cfg の name_en: 英語のときだけランチャ等で見せる名前。登録名(name)は言語で変えない
    HostSd::files["/lua/apps/tet/main.lua"] = "";
    HostSd::files["/lua/apps/tet/app.cfg"] = "name = テトリス\nname_en = Tetris\nname_xx = ignored\n";
    check(LuaAppScanner::RegisterOrRefresh("/lua/apps/tet"), "name_en: 登録できる");
    const int ti = AppFunctions::FindByArg("/lua/apps/tet/main.lua");
    const AppEntry* te = AppFunctions::Get(ti);
    check(te && te->name == "テトリス", "name_en: 登録名は name のまま");
    I18nFunctions::current_lang = I18nFunctions::Lang::Ja;
    check(te && strcmp(AppFunctions::DisplayName(*te), "テトリス") == 0, "name_en: 日本語では name");
    I18nFunctions::current_lang = I18nFunctions::Lang::En;
    check(te && strcmp(AppFunctions::DisplayName(*te), "Tetris") == 0, "name_en: 英語では name_en");
    //name_en を消して読み直すと、英語でも name に戻る
    HostSd::files["/lua/apps/tet/app.cfg"] = "name = テトリス\n";
    check(LuaAppScanner::RegisterOrRefresh("/lua/apps/tet"), "name_en: 読み直せる");
    check(te && strcmp(AppFunctions::DisplayName(*te), "テトリス") == 0, "name_en: 消したら name に戻る");
    I18nFunctions::current_lang = I18nFunctions::Lang::Ja;

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
