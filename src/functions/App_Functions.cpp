#include "functions/App_Functions.hpp"
#include <cstring>
#include "storage/SD_IO.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"

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

    //icon_pathはargと違い、失敗してもアプリ自体は動く(既定アイコンへ落とすだけ)ので
    //登録ごとは拒否しない。覚えるのはargのディレクトリからの相対パスだけ(AppEntry::icon_file参照)
    if(icon_path && icon_path[0] != '\0'){
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

    entry.icon = icon;
    entry.permissions = permissions;
    entry.create = create;
    app_count++;
    return true;
}

int AppFunctions::Count(){
    return app_count;
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
}
