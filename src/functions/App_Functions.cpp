#include "functions/App_Functions.hpp"
#include <cstring>
#include "storage/SD_IO.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include <cstdio>

namespace {
    //icon_pathはargと違い、失敗してもアプリ自体は動く(既定アイコンへ落とすだけ)ので
    //登録ごとは拒否しない。覚えるのはargのディレクトリからの相対パスだけ(AppEntry::icon_file参照)
    void SetIconFile(AppEntry& entry, const char* icon_path){
        entry.icon_file.clear();
        if(!icon_path || icon_path[0] == '\0') return;
        //argの最後の"/"まで(IconPathOf()と同じ切り方)が icon_path の頭と一致すれば、その後ろを覚える
        const char* slash = entry.arg.empty() ? nullptr : strrchr(entry.arg.c_str(), '/');
        const size_t dir_len = slash ? (size_t)(slash - entry.arg.c_str()) + 1 : 0;
        const bool inside = slash && strncmp(icon_path, entry.arg.c_str(), dir_len) == 0 && icon_path[dir_len] != '\0';
        if(!inside || !entry.icon_file.assign(icon_path + dir_len)){
            LOG_SYS_WARN("App Register: アイコンはアプリのディレクトリの中の短いパスにしてください。既定アイコンにします (%s: %s)",
                entry.name.c_str(), icon_path);
            entry.icon_file.clear();
        }
    }
}

bool AppFunctions::Register(const char* name, IconID icon, Scene* (*create)(const AppEntry&),
                            const char* arg, const LuaPermissions& permissions,
                            const char* icon_path){
    if(!name || !create){
        LOG_SYS_WARN("App Register: 名前か生成関数が未指定です");
        return false;
    }
    if(app_count >= kMaxApps){
        LOG_SYS_WARN("App Register: 登録上限(%d)に達しているため \"%s\" を登録できません",
            kMaxApps, name);
        return false;
    }

    AppEntry& entry = apps[app_count];
    entry = AppEntry{};

    //名前は切り詰められても表示が縮むだけなので、警告を出した上で登録は通す
    if(!entry.name.assign(name)){
        LOG_SYS_WARN("App Register: 名前が長いため切り詰めました (%s)", name);
    }

    //argは大半がパスで、切り詰まると別のファイルを指してしまう。こちらは登録ごと拒否する
    if(arg && !entry.arg.assign(arg)){
        LOG_SYS_WARN("App Register: 引数が長すぎるため \"%s\" を登録できません (%s)",
            entry.name.c_str(), arg);
        entry = AppEntry{};
        return false;
    }

    SetIconFile(entry, icon_path);

    entry.icon = icon;
    entry.permissions = permissions;
    entry.create = create;
    //新しいアプリはランチャの並びの最後へ
    launcher_order[app_count] = (int8_t)app_count;
    launcher_hidden &= ~(1ull << app_count);
    app_count++;
    return true;
}

int AppFunctions::Count(){
    return app_count;
}

int AppFunctions::FindByArg(const char* arg){
    if(!arg || arg[0] == '\0') return -1;
    for(int i = 0; i < app_count; i++){
        if(apps[i].arg == arg) return i;
    }
    return -1;
}

bool AppFunctions::Refresh(int index, const char* name, const LuaPermissions& permissions,
                           const char* icon_path){
    if(index < 0 || index >= app_count || !name || !name[0]) return false;
    AppEntry& entry = apps[index];
    entry.name.assign(name);
    entry.permissions = permissions;
    SetIconFile(entry, icon_path);
    return true;
}

bool AppFunctions::Unregister(int index){
    if(index < 0 || index >= app_count) return false;
    for(int i = index; i + 1 < app_count; i++) apps[i] = apps[i + 1];
    apps[app_count - 1] = AppEntry{};

    //並びからindexを抜き、それより後ろのindexを1つずつ詰める
    int w = 0;
    for(int i = 0; i < app_count; i++){
        const int idx = launcher_order[i];
        if(idx == index) continue;
        launcher_order[w++] = (int8_t)(idx > index ? idx - 1 : idx);
    }
    //隠した印も同じく詰める(index より下のビットはそのまま、上のビットは1つ下へ)
    const uint64_t low = launcher_hidden & ((1ull << index) - 1ull);
    const uint64_t high = (index + 1 < 64) ? (launcher_hidden >> (index + 1)) << index : 0ull;
    launcher_hidden = low | high;

    app_count--;
    launcher_order[app_count] = (int8_t)app_count;
    return true;
}

const AppEntry* AppFunctions::Get(int index){
    if(index < 0 || index >= app_count) return nullptr;
    return &apps[index];
}

void AppFunctions::Launch(int index){
    const AppEntry* entry = Get(index);
    if(!entry){
        LOG_SYS_WARN("App Launch: 範囲外のindexです (%d)", index);
        return;
    }

    //生成関数には登録簿の情報(特にarg)をそのまま渡す。
    //同じ生成関数でもargが違えば別の中身のシーンになる
    Scene* scene = entry->create(*entry);
    if(!scene){
        LOG_SYS_FAIL("App Launch: シーンを生成できませんでした (%s)", entry->name.c_str());
        return;
    }

    //Pushなので、アプリ側からPop()すればランチャへ戻れる。
    //SceneFunctionsは要求を登録するだけで、実際の遷移はフレーム境界で起きる
    SceneFunctions::Push(scene);
    LOG_SYS_MSG("アプリ起動: %s", entry->name.c_str());
}

bool AppFunctions::IconPathOf(const AppEntry& entry, FixedString<PICO_PATH_LEN>& out){
    //描画の途中から呼ばれるので、作業用のパスのバッファをスタックに置かず out の上で組み立てる
    //(Register()と同じく、argの最後の"/"までをディレクトリとする)
    out.clear();
    if(entry.icon_file.empty() || entry.arg.empty()) return false;
    const char* arg = entry.arg.c_str();
    const char* slash = strrchr(arg, '/');
    if(!slash) return false;
    if(!out.append(arg, (size_t)(slash - arg) + 1)) return false;
    return out.append(entry.icon_file.c_str());
}

bool AppFunctions::LaunchByName(const char* name){
    if(!name) return false;

    for(int i = 0; i < app_count; i++){
        if(apps[i].name == name){
            Launch(i);
            return true;
        }
    }

    LOG_SYS_WARN("App LaunchByName: 該当するアプリが見つかりません (%s)", name);
    return false;
}

bool AppFunctions::NameForDir(const char* dir, FixedString<PICO_STR_M>& out){
    if(!dir || dir[0] == '\0') return false;
    for(int i = 0; i < app_count; i++){
        if(apps[i].arg.empty()) continue;
        FixedString<PICO_PATH_LEN> parent, norm;
        if(!PICO_IO::parent(parent, apps[i].arg.c_str())) continue;
        if(!PICO_IO::normalize(norm, parent.c_str())) continue;
        if(norm == dir){
            out.assign(apps[i].name.c_str());
            return true;
        }
    }
    return false;
}

void AppFunctions::Clear(){
    for(int i = 0; i < kMaxApps; i++){
        apps[i] = AppEntry{};
    }
    app_count = 0;
    for(int i = 0; i < kMaxApps; i++) launcher_order[i] = (int8_t)i;
    launcher_hidden = 0;
}

// ---------------------------------------------------------------------------
// ランチャの並び・非表示
// ---------------------------------------------------------------------------

namespace {
    bool HiddenBit(int index){ return (AppFunctions::launcher_hidden >> index) & 1ull; }
    // 並びの中での位置(隠したものも数える)。無ければ-1
    int OrderSlot(int index){
        for(int i = 0; i < AppFunctions::app_count; i++){
            if(AppFunctions::launcher_order[i] == index) return i;
        }
        return -1;
    }
}

int AppFunctions::VisibleCount(){
    int n = 0;
    for(int i = 0; i < app_count; i++) if(!HiddenBit(i)) n++;
    return n;
}

int AppFunctions::VisibleAt(int pos){
    if(pos < 0) return -1;
    for(int i = 0; i < app_count; i++){
        const int idx = launcher_order[i];
        if(HiddenBit(idx)) continue;
        if(pos-- == 0) return idx;
    }
    return -1;
}

int AppFunctions::VisiblePosition(int index){
    if(index < 0 || index >= app_count || HiddenBit(index)) return -1;
    int pos = 0;
    for(int i = 0; i < app_count; i++){
        const int idx = launcher_order[i];
        if(idx == index) return pos;
        if(!HiddenBit(idx)) pos++;
    }
    return -1;
}

bool AppFunctions::IsHidden(int index){
    return index >= 0 && index < app_count && HiddenBit(index);
}

int AppFunctions::HiddenCount(){ return app_count - VisibleCount(); }

int AppFunctions::HiddenAt(int n){
    if(n < 0) return -1;
    for(int i = 0; i < app_count; i++){
        const int idx = launcher_order[i];
        if(!HiddenBit(idx)) continue;
        if(n-- == 0) return idx;
    }
    return -1;
}

bool AppFunctions::MoveApp(int index, int delta){
    if(delta == 0 || IsHidden(index)) return false;
    int slot = OrderSlot(index);
    if(slot < 0) return false;
    const int step = delta < 0 ? -1 : 1;
    int remaining = delta < 0 ? -delta : delta;
    bool moved = false;
    //隣の「見えている」アプリを飛び越すまで、1つずつ入れ替えて進める(隠したものとの相対位置は保つ)
    while(remaining > 0){
        int target = slot + step;
        while(target >= 0 && target < app_count && HiddenBit(launcher_order[target])) target += step;
        if(target < 0 || target >= app_count) break;
        while(slot != target){
            const int next = slot + step;
            const int8_t tmp = launcher_order[slot];
            launcher_order[slot] = launcher_order[next];
            launcher_order[next] = tmp;
            slot = next;
        }
        moved = true;
        remaining--;
    }
    if(moved) SaveLayout();
    return moved;
}

bool AppFunctions::MoveAppToFront(int index){
    const int pos = VisiblePosition(index);
    if(pos <= 0) return false;
    return MoveApp(index, -pos);
}

bool AppFunctions::SetHidden(int index, bool hidden){
    if(index < 0 || index >= app_count || HiddenBit(index) == hidden) return false;
    if(hidden) launcher_hidden |= (1ull << index);
    else       launcher_hidden &= ~(1ull << index);
    SaveLayout();
    return true;
}

void AppFunctions::ResetLayout(){
    for(int i = 0; i < kMaxApps; i++) launcher_order[i] = (int8_t)i;
    launcher_hidden = 0;
    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_LAUNCHER_CFG)){
        OSData::SD.remove(PICO_Path::FILE::CFG::SYS_LAUNCHER_CFG);
    }
}

void AppFunctions::LoadLayout(){
    for(int i = 0; i < kMaxApps; i++) launcher_order[i] = (int8_t)i;
    launcher_hidden = 0;
    if(!OSData::SD_usable) return;
    const char* path = PICO_Path::FILE::CFG::SYS_LAUNCHER_CFG;
    if(!OSData::SD.exists(path)) return; //無いのが普通(登録順のまま)

    int8_t order[kMaxApps];
    int placed_count = 0;
    uint64_t placed = 0;
    uint64_t hidden = 0;
    PICO_Config::ParseFile(path, [&](const char* key, const char* value){
        const bool is_app = strcmp(key, "app") == 0;
        const bool is_hidden = strcmp(key, "hidden") == 0;
        if(!is_app && !is_hidden){
            LOG_SYS_WARN("launcher.cfg: 知らないキーです: %s", key);
            return;
        }
        //同じ名前が複数あれば、まだ並べていない若いものから当てる。見つからない(消したアプリ)は捨てる
        for(int i = 0; i < app_count; i++){
            if(((placed >> i) & 1ull) || !(apps[i].name == value)) continue;
            placed |= (1ull << i);
            order[placed_count++] = (int8_t)i;
            if(is_hidden) hidden |= (1ull << i);
            return;
        }
    });
    //ファイルに無いアプリ(新しく入れたもの)は後ろへ登録順に
    for(int i = 0; i < app_count; i++){
        if(!((placed >> i) & 1ull)) order[placed_count++] = (int8_t)i;
    }
    for(int i = 0; i < app_count; i++) launcher_order[i] = order[i];
    launcher_hidden = hidden;
}

bool AppFunctions::SaveLayout(){
    if(!OSData::SD_usable) return false;
    const char* path = PICO_Path::FILE::CFG::SYS_LAUNCHER_CFG;
    char tmp_path[PICO_PATH_LEN];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FsFile f = OSData::SD.open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f){
        LOG_SYS_FAIL("ランチャ: %s を作れませんでした", tmp_path);
        return false;
    }
    bool ok = true;
    auto put = [&](const char* text){
        const size_t len = strlen(text);
        if(ok && f.write(text, len) != len) ok = false;
    };
    put("# ランチャの並び(上から順)。hidden = の行は隠したアプリ。ランチャの長押し/設定アプリが書き換える\n");
    char line[PICO_Config::kConfigMaxLineLen];
    for(int i = 0; i < app_count && ok; i++){
        const int idx = launcher_order[i];
        snprintf(line, sizeof(line), "%s = %s\n", HiddenBit(idx) ? "hidden" : "app", apps[idx].name.c_str());
        put(line);
    }
    f.close();

    if(!ok){
        LOG_SYS_FAIL("ランチャ: launcher.cfgの書き込みに失敗しました");
        OSData::SD.remove(tmp_path);
        return false;
    }
    if(OSData::SD.exists(path) && !OSData::SD.remove(path)){
        LOG_SYS_FAIL("ランチャ: 古いlauncher.cfgを消せませんでした");
        OSData::SD.remove(tmp_path);
        return false;
    }
    if(!OSData::SD.rename(tmp_path, path)){
        LOG_SYS_FAIL("ランチャ: launcher.cfgへ差し替えられませんでした");
        return false;
    }
    return true;
}
