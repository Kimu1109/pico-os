#include "functions/App_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"

bool AppFunctions::Register(const char* name, IconID icon, Scene* (*create)()){
    if(!name || !create){
        LOG_SYS_WARN("App Register: 名前か生成関数が未指定です");
        return false;
    }
    if(app_count >= kMaxApps){
        LOG_SYS_WARN("App Register: 登録上限(%d)に達しているため \"%s\" を登録できません",
            kMaxApps, name);
        return false;
    }

    apps[app_count].name = name;
    apps[app_count].icon = icon;
    apps[app_count].create = create;
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

    //Pushなので、アプリ側からPop()すればランチャへ戻れる。
    //SceneFunctionsは要求を登録するだけで、実際の遷移はフレーム境界で起きる
    SceneFunctions::Push(entry->create());
    LOG_SYS_MSG("アプリ起動: %s", entry->name);
}

void AppFunctions::Clear(){
    for(int i = 0; i < kMaxApps; i++){
        apps[i] = AppEntry{};
    }
    app_count = 0;
}
