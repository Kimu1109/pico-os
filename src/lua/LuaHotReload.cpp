#include "lua/LuaHotReload.hpp"

#include "lua/LuaAppScanner.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "services/App_Functions.hpp"
#include "services/FileShare_Functions.hpp"
#include "devtools/DevTools_Functions.hpp"
#include "system/Background_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/I18n_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "Arduino.h"

#include <cstring>
#include <strings.h>

namespace {
    constexpr const char* kAppsRoot = "/lua/apps/";

    bool pending = false;
    uint32_t last_change_ms = 0;
    FixedString<PICO_PATH_LEN> pending_dir;
    FixedString<PICO_STR_M> last_reloaded;

    // 開き直しの途中(同じアプリの push_scene したページを1フレームに1つずつ閉じている)
    bool reloading = false;
    FixedString<PICO_PATH_LEN> reload_dir;

    bool EndsWithNoCase(const char* s, const char* suffix){
        const size_t n = strlen(s), m = strlen(suffix);
        return n >= m && strcasecmp(s + n - m, suffix) == 0;
    }

    // scene が dir のアプリのLuaの画面か
    bool IsAppScene(Scene* scene, const char* dir){
        if(!scene || strcmp(scene->getName(), "Lua") != 0) return false;
        return PICO_IO::isSameOrInside(static_cast<LuaScene*>(scene)->scriptPath(), dir);
    }

    // 1フレームに1歩。開き直し終えたら(続ける必要が無くなったら)false
    bool StepReload(){
        FixedString<PICO_PATH_LEN> main_lua;
        if(!PICO_IO::join(main_lua, reload_dir, "main.lua")) return false;
        Scene* cur = SceneFunctions::Current();
        if(!IsAppScene(cur, reload_dir.c_str())) return false; //もう別の画面にいる
        //下にも同じアプリのページがあれば、上から閉じる(古い状態のページへ戻らないように)
        const int depth = SceneFunctions::stack_depth;
        if(depth > 0 && IsAppScene(SceneFunctions::stack[depth - 1], reload_dir.c_str())){
            SceneFunctions::Pop();
            return true;
        }
        const int index = AppFunctions::FindByArg(main_lua.c_str());
        const AppEntry* e = index >= 0 ? AppFunctions::Get(index) : nullptr;
        if(!e || !e->create){
            LOG_APP_WARN("ホットリロード: %s は登録簿にありません", main_lua.c_str());
            return false;
        }
        SceneFunctions::Change(e->create(*e));
        last_reloaded.assign(e->name.c_str());
        LOG_APP_OK("ホットリロード: %s を開き直しました", e->name.c_str());
        return false;
    }

    void Reload(const char* dir){
        //名前・権限・アイコンが変わっていれば登録簿へ(無ければ足す。main.lua が消えていれば何もしない)
        LuaAppScanner::RegisterOrRefresh(dir);
        if(!IsAppScene(SceneFunctions::Current(), dir)){
            LOG_APP_MSG("ホットリロード: %s を読み直しました(開いていないので開き直しはしません)", dir);
            last_reloaded.assign(PICO_IO::filename(dir));
            return;
        }
        reload_dir.assign(dir);
        reloading = StepReload();
    }

    class Service : public BackgroundService {
        public:
            const char* key() const override { return "lua-hot-reload"; }
            const char* name() const override { return TR("Luaのホットリロード"); }
            IconID icon() const override { return IconID::Share; }
            const char* appName() const override { return FileShareFunctions::kAppName; }
            // 画面を閉じたWi-Fi転送を続けている間だけ一覧に「動いている」と出す
            bool active() const override {
                return FileShareFunctions::InBackground() && FileShareFunctions::GetState() == FileShareFunctions::State::Running;
            }
            void stop() override { FileShareFunctions::Stop(); }
            void status(FixedString<PICO_STR_LL>& out) const override {
                out.clear();
                if(!active()){ out.assign(TR("Wi-Fi転送の画面を閉じると待ち受けを続けます")); return; }
                out.appendFormat(TR("キー %s"), FileShareFunctions::Key());
                if(!last_reloaded.empty()) out.appendFormat(TR(" / 最後: %s"), last_reloaded.c_str());
            }
            void update(uint32_t now) override {
                if(!LuaHotReload::Enabled()) return;
                if(active()) FileShareFunctions::Update();
                if(reloading){
                    //前の Pop/Change がフレームの境目で済んでから次の1歩
                    if(SceneFunctions::pending_type == SceneFunctions::RequestType::None) reloading = StepReload();
                    return;
                }
                FixedString<PICO_PATH_LEN> dir;
                if(LuaHotReload::TakeDue(now, dir)) Reload(dir.c_str());
            }
    };

    Service service;
}

bool LuaHotReload::Enabled(){
    return enabled_query ? enabled_query() : DevToolsFunctions::LuaHotReload();
}

bool LuaHotReload::AppDirOf(const char* path, FixedString<PICO_PATH_LEN>& out){
    out.clear();
    if(!path) return false;
    const size_t root_len = strlen(kAppsRoot);
    if(strncmp(path, kAppsRoot, root_len) != 0) return false;
    const char* name = path + root_len;
    const char* slash = strchr(name, '/');
    if(!slash || slash == name) return false; // /lua/apps/<dir> そのもの・空の名前
    const char* rel = slash + 1;
    const bool reload = strcasecmp(rel, "app.cfg") == 0 || EndsWithNoCase(rel, ".lua");
    if(!reload) return false;
    return out.assign(path, (size_t)(slash - path));
}

void LuaHotReload::OnFileChanged(const char* path, uint32_t now_ms){
    if(!Enabled()) return;
    FixedString<PICO_PATH_LEN> dir;
    if(!AppDirOf(path, dir)) return;
    //別のアプリが混ざったら、先のものはここで済ませる(静かになるのを待たない)
    if(pending && !(pending_dir == dir.c_str())) Reload(pending_dir.c_str());
    pending = true;
    pending_dir.assign(dir);
    last_change_ms = now_ms;
}

bool LuaHotReload::TakeDue(uint32_t now_ms, FixedString<PICO_PATH_LEN>& out){
    if(!pending || now_ms - last_change_ms < kQuietMs) return false;
    pending = false;
    out.assign(pending_dir);
    return true;
}

const char* LuaHotReload::LastReloaded(){ return last_reloaded.c_str(); }

BackgroundService* LuaHotReload::Service(){ return &service; }

void LuaHotReload::Test::Reset(){
    pending = false;
    reloading = false;
    last_change_ms = 0;
    pending_dir.clear();
    reload_dir.clear();
    last_reloaded.clear();
}
