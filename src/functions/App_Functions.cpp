#include "functions/App_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"

bool AppFunctions::Register(const char* name, IconID icon, Scene* (*create)(const AppEntry&),
                            const char* arg){
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

    entry.icon = icon;
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

void AppFunctions::Clear(){
    for(int i = 0; i < kMaxApps; i++){
        apps[i] = AppEntry{};
    }
    app_count = 0;
}
