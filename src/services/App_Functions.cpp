#include "services/App_Functions.hpp"
#include <cstring>
#include "storage/SD_IO.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include <cstdio>

namespace {
    // 並びを登録順へ戻し、全部見せ、フォルダを無くす(ファイルには触らない)
    void ResetOrder(){
        using namespace AppFunctions;
        for(int i = 0; i < kMaxApps; i++){
            launcher_order[i] = (int8_t)i;
            launcher_folder[i] = -1;
        }
        launcher_hidden = 0;
        for(auto& n : folder_names) n.clear();
        folder_used = 0;
        layout_revision++;
    }

    // LaunchByName(name, scene)で預けた画面の名前と、預けた先(登録簿のindex。-1=無し)
    int launch_scene_app = -1;
    FixedString<PICO_STR_S> launch_scene;

    // 中にアプリが1つも無いフォルダを無くす
    void DropEmptyFolders(){
        using namespace AppFunctions;
        uint8_t has = 0;
        for(int i = 0; i < app_count; i++){
            if(launcher_folder[i] >= 0) has |= (uint8_t)(1u << launcher_folder[i]);
        }
        for(int f = 0; f < kMaxFolders; f++){
            if(((folder_used >> f) & 1u) && !((has >> f) & 1u)){
                folder_used &= (uint8_t)~(1u << f);
                folder_names[f].clear();
            }
        }
    }

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
    launcher_folder[app_count] = -1;
    app_count++;
    layout_revision++;
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

const char* AppFunctions::DisplayName(const AppEntry& entry){
    const int lang = (int)I18nFunctions::Current();
    if(lang > 0 && lang - 1 < kAppLocalNames && !entry.local_names[lang - 1].empty()){
        return entry.local_names[lang - 1].c_str();
    }
    return I18nFunctions::Tr(entry.name.c_str());
}

bool AppFunctions::SetLocalName(int index, I18nFunctions::Lang lang, const char* name){
    const int li = (int)lang - 1;
    if(index < 0 || index >= app_count || li < 0 || li >= kAppLocalNames) return false;
    //表示だけなので切り詰めても登録は通す
    if(name) apps[index].local_names[li].assign(name);
    else apps[index].local_names[li].clear();
    return true;
}

bool AppFunctions::Unregister(int index){
    if(index < 0 || index >= app_count) return false;
    launch_scene_app = -1;
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
    for(int i = index; i + 1 < app_count; i++) launcher_folder[i] = launcher_folder[i + 1];

    app_count--;
    launcher_order[app_count] = (int8_t)app_count;
    launcher_folder[app_count] = -1;
    //最後の1つが消えたフォルダは無くす
    DropEmptyFolders();
    layout_revision++;
    return true;
}

const AppEntry* AppFunctions::Get(int index){
    if(index < 0 || index >= app_count) return nullptr;
    return &apps[index];
}

void AppFunctions::Launch(int index){
    //前の起動で預けた画面は、受け取られなかったら別の起動へ持ち越さない
    launch_scene_app = -1;
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

bool AppFunctions::LaunchByName(const char* name, const char* scene){
    if(!LaunchByName(name)) return false;
    if(!scene || scene[0] == '\0') return true;
    for(int i = 0; i < app_count; i++){
        if(apps[i].name != name) continue;
        if(!launch_scene.assign(scene)){
            LOG_SYS_WARN("App LaunchByName: 画面の名前が長すぎます (%s)", scene);
            return true;
        }
        launch_scene_app = i;
        break;
    }
    return true;
}

bool AppFunctions::TakeLaunchScene(const char* key, FixedString<PICO_STR_S>& out){
    if(!key || key[0] == '\0') return false;
    const AppEntry* entry = Get(launch_scene_app);
    if(!entry) return false;
    const bool match = entry->name == key || (!entry->arg.empty() && entry->arg == key);
    if(!match) return false;
    out.assign(launch_scene.c_str());
    launch_scene_app = -1;
    return true;
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
    launch_scene_app = -1;
    ResetOrder();
}

// ---------------------------------------------------------------------------
// ランチャの並び・非表示・フォルダ
// ---------------------------------------------------------------------------
//
// 並び(launcher_order)は「塊」の列として扱う。塊はフォルダの外のアプリ1つか、1つのフォルダの中のアプリ全部
// (フォルダの中のアプリは続けて並ぶ。崩れたら Normalize() が直す)。トップに並ぶのは見えている塊
// (隠していないアプリか、隠していないアプリを1つ以上持つフォルダ)。

namespace {
    using namespace AppFunctions;

    bool HiddenBit(int index){ return (launcher_hidden >> index) & 1ull; }
    int FolderAtSlot(int slot){ return launcher_folder[launcher_order[slot]]; }
    // 並びの中での位置(隠したものも数える)。無ければ-1
    int OrderSlot(int index){
        for(int i = 0; i < app_count; i++){
            if(launcher_order[i] == index) return i;
        }
        return -1;
    }

    // slot を含む塊の範囲 [start, end)
    int BlockStart(int slot){
        const int f = FolderAtSlot(slot);
        if(f < 0) return slot;
        while(slot > 0 && FolderAtSlot(slot - 1) == f) slot--;
        return slot;
    }
    int BlockEnd(int slot){
        const int f = FolderAtSlot(slot);
        if(f < 0) return slot + 1;
        while(slot < app_count && FolderAtSlot(slot) == f) slot++;
        return slot;
    }
    bool BlockVisible(int start, int end){
        for(int i = start; i < end; i++) if(!HiddenBit(launcher_order[i])) return true;
        return false;
    }
    // フォルダの塊の始まり(無ければ-1)
    int FolderStart(int folder){
        for(int i = 0; i < app_count; i++) if(FolderAtSlot(i) == folder) return i;
        return -1;
    }

    // [a, b) を [b, c) の後ろへ回す(std::rotate と同じ。確保なし)
    void Rotate(int a, int b, int c){
        auto rev = [](int i, int j){
            for(j--; i < j; i++, j--){
                const int8_t t = launcher_order[i];
                launcher_order[i] = launcher_order[j];
                launcher_order[j] = t;
            }
        };
        rev(a, b); rev(b, c); rev(a, c);
    }

    // フォルダの中のアプリを、そのフォルダの先頭のアプリの位置へ続けて並べ直す(他の並びは保つ)
    void Normalize(){
        int8_t out[kMaxApps];
        uint8_t done = 0;
        int n = 0;
        for(int i = 0; i < app_count; i++){
            const int idx = launcher_order[i];
            const int f = launcher_folder[idx];
            if(f < 0){ out[n++] = (int8_t)idx; continue; }
            if((done >> f) & 1u) continue;
            done |= (uint8_t)(1u << f);
            for(int j = i; j < app_count; j++){
                if(launcher_folder[launcher_order[j]] == f) out[n++] = launcher_order[j];
            }
        }
        for(int i = 0; i < n; i++) launcher_order[i] = out[i];
    }

    // 塊 [start, end) を、範囲 [lo, hi) の中の見えている塊を |delta| 個飛び越すまで動かす
    bool MoveBlock(int start, int end, int delta, int lo, int hi, bool single_slots){
        if(delta == 0) return false;
        auto bstart = [&](int slot){ return single_slots ? slot : BlockStart(slot); };
        auto bend   = [&](int slot){ return single_slots ? slot + 1 : BlockEnd(slot); };
        bool moved = false;
        for(int remaining = delta < 0 ? -delta : delta; remaining > 0; remaining--){
            if(delta < 0){
                //前の見えている塊を探す(隠した塊は一緒に越える)
                int ts = start;
                bool found = false;
                while(ts > lo){
                    ts = bstart(ts - 1);
                    if(BlockVisible(ts, bend(ts))){ found = true; break; }
                }
                if(!found) break;
                Rotate(ts, start, end);
                end = ts + (end - start);
                start = ts;
            }else{
                int te = end;
                bool found = false;
                while(te < hi){
                    const int ts = te;
                    te = bend(ts);
                    if(BlockVisible(ts, te)){ found = true; break; }
                }
                if(!found) break;
                Rotate(start, end, te);
                start = te - (end - start);
                end = te;
            }
            moved = true;
        }
        return moved;
    }

    bool Changed(){
        layout_revision++;
        SaveLayout();
        return true;
    }

    // 空いているフォルダの番号(無ければ-1)
    int FreeFolder(){
        for(int f = 0; f < kMaxFolders; f++) if(!((folder_used >> f) & 1u)) return f;
        return -1;
    }

    void SetFolderName(int folder, const char* name){
        //前後の空白は落とす。空なら既定の名前
        if(!name) name = "";
        while(*name == ' ' || *name == '\t') name++;
        size_t len = strlen(name);
        while(len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t')) len--;
        if(len == 0){ name = "フォルダ"; len = strlen(name); }
        folder_names[folder].assign(name, len);
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

int AppFunctions::TopCount(){
    int n = 0;
    for(int s = 0; s < app_count; s = BlockEnd(s)) if(BlockVisible(s, BlockEnd(s))) n++;
    return n;
}

bool AppFunctions::TopAt(int pos, TopItem& out){
    if(pos < 0) return false;
    for(int s = 0; s < app_count; s = BlockEnd(s)){
        if(!BlockVisible(s, BlockEnd(s))) continue;
        if(pos-- > 0) continue;
        const int f = FolderAtSlot(s);
        out.is_folder = f >= 0;
        out.index = (int8_t)(f >= 0 ? f : launcher_order[s]);
        return true;
    }
    return false;
}

int AppFunctions::TopPosition(bool is_folder, int index){
    int pos = 0;
    for(int s = 0; s < app_count; s = BlockEnd(s)){
        if(!BlockVisible(s, BlockEnd(s))) continue;
        const int f = FolderAtSlot(s);
        if(is_folder ? (f == index) : (f < 0 && launcher_order[s] == index)) return pos;
        pos++;
    }
    return -1;
}

bool AppFunctions::FolderExists(int folder){
    return folder >= 0 && folder < kMaxFolders && ((folder_used >> folder) & 1u);
}

int AppFunctions::FolderCount(){
    int n = 0;
    for(int f = 0; f < kMaxFolders; f++) if(FolderExists(f)) n++;
    return n;
}

const char* AppFunctions::FolderName(int folder){
    return FolderExists(folder) ? folder_names[folder].c_str() : "";
}

int AppFunctions::FolderOf(int index){
    if(index < 0 || index >= app_count) return -1;
    return launcher_folder[index];
}

int AppFunctions::FolderAppCount(int folder){
    if(!FolderExists(folder)) return 0;
    int n = 0;
    for(int i = 0; i < app_count; i++) if(launcher_folder[i] == folder && !HiddenBit(i)) n++;
    return n;
}

int AppFunctions::FolderAppAt(int folder, int pos){
    if(!FolderExists(folder) || pos < 0) return -1;
    for(int i = 0; i < app_count; i++){
        const int idx = launcher_order[i];
        if(launcher_folder[idx] != folder || HiddenBit(idx)) continue;
        if(pos-- == 0) return idx;
    }
    return -1;
}

int AppFunctions::FolderAppPosition(int index){
    const int f = FolderOf(index);
    if(f < 0 || HiddenBit(index)) return -1;
    int pos = 0;
    for(int i = 0; i < app_count; i++){
        const int idx = launcher_order[i];
        if(idx == index) return pos;
        if(launcher_folder[idx] == f && !HiddenBit(idx)) pos++;
    }
    return -1;
}

bool AppFunctions::MoveApp(int index, int delta){
    if(delta == 0 || IsHidden(index)) return false;
    const int slot = OrderSlot(index);
    if(slot < 0) return false;
    bool moved;
    if(launcher_folder[index] >= 0){
        //フォルダの中: そのフォルダの塊の中だけで1つずつ
        moved = MoveBlock(slot, slot + 1, delta, BlockStart(slot), BlockEnd(slot), true);
    }else{
        moved = MoveBlock(slot, slot + 1, delta, 0, app_count, false);
    }
    return moved && Changed();
}

bool AppFunctions::MoveAppToFront(int index){
    const int pos = FolderOf(index) >= 0 ? FolderAppPosition(index) : TopPosition(false, index);
    if(pos <= 0) return false;
    return MoveApp(index, -pos);
}

bool AppFunctions::MoveFolder(int folder, int delta){
    const int s = FolderStart(folder);
    if(s < 0 || !BlockVisible(s, BlockEnd(s))) return false;
    return MoveBlock(s, BlockEnd(s), delta, 0, app_count, false) && Changed();
}

bool AppFunctions::MoveFolderToFront(int folder){
    const int pos = TopPosition(true, folder);
    if(pos <= 0) return false;
    return MoveFolder(folder, -pos);
}

bool AppFunctions::SetHidden(int index, bool hidden){
    if(index < 0 || index >= app_count || HiddenBit(index) == hidden) return false;
    if(hidden) launcher_hidden |= (1ull << index);
    else       launcher_hidden &= ~(1ull << index);
    return Changed();
}

int AppFunctions::CreateFolder(const char* name, int index){
    if(index < 0 || index >= app_count) return -1;
    const int f = FreeFolder();
    if(f < 0) return -1;
    if(launcher_folder[index] >= 0){
        //別のフォルダから出してから(保存は下でまとめて)
        const int slot = OrderSlot(index);
        Rotate(slot, slot + 1, BlockEnd(slot));
        launcher_folder[index] = -1;
        DropEmptyFolders();
    }
    folder_used |= (uint8_t)(1u << f);
    SetFolderName(f, name);
    launcher_folder[index] = (int8_t)f;
    Changed();
    return f;
}

bool AppFunctions::MoveToFolder(int index, int folder){
    if(index < 0 || index >= app_count || !FolderExists(folder) || launcher_folder[index] == folder) return false;
    //いったん並びから抜き、行き先のフォルダの最後へ入れる
    const int slot = OrderSlot(index);
    Rotate(slot, slot + 1, app_count);
    launcher_folder[index] = -1;
    const int fs = FolderStart(folder);
    const int fe = fs < 0 ? app_count - 1 : BlockEnd(fs);
    Rotate(fe, app_count - 1, app_count);
    launcher_folder[index] = (int8_t)folder;
    DropEmptyFolders();
    return Changed();
}

bool AppFunctions::RemoveFromFolder(int index){
    if(index < 0 || index >= app_count || launcher_folder[index] < 0) return false;
    const int slot = OrderSlot(index);
    Rotate(slot, slot + 1, BlockEnd(slot));
    launcher_folder[index] = -1;
    DropEmptyFolders();
    return Changed();
}

bool AppFunctions::RenameFolder(int folder, const char* name){
    if(!FolderExists(folder)) return false;
    FixedString<PICO_STR_M> before = folder_names[folder];
    SetFolderName(folder, name);
    if(folder_names[folder] == before.c_str()) return false;
    return Changed();
}

bool AppFunctions::DissolveFolder(int folder){
    if(!FolderExists(folder)) return false;
    //中のアプリは続けて並んでいるので、印を外すだけでその場に出る
    for(int i = 0; i < app_count; i++) if(launcher_folder[i] == folder) launcher_folder[i] = -1;
    DropEmptyFolders();
    return Changed();
}

void AppFunctions::ResetLayout(){
    ResetOrder();
    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_LAUNCHER_CFG)){
        OSData::SD.remove(PICO_Path::FILE::CFG::SYS_LAUNCHER_CFG);
    }
}

void AppFunctions::LoadLayout(){
    ResetOrder();
    if(!OSData::SD_usable) return;
    const char* path = PICO_Path::FILE::CFG::SYS_LAUNCHER_CFG;
    if(!OSData::SD.exists(path)) return; //無いのが普通(登録順のまま)

    int8_t order[kMaxApps];
    int placed_count = 0;
    uint64_t placed = 0;
    uint64_t hidden = 0;
    int cur_folder = -1;  // 今読んでいるフォルダ(folder の行の後の in の行が入る)
    PICO_Config::ParseFile(path, [&](const char* key, const char* value){
        const bool is_app = strcmp(key, "app") == 0;
        const bool is_hidden = strcmp(key, "hidden") == 0;
        const bool is_in = strcmp(key, "in") == 0;
        const bool is_in_hidden = strcmp(key, "in-hidden") == 0;
        if(strcmp(key, "folder") == 0){
            cur_folder = FreeFolder();
            if(cur_folder < 0){
                LOG_SYS_WARN("launcher.cfg: フォルダが多すぎます(%d個まで)。中のアプリは外に並べます", kMaxFolders);
                return;
            }
            folder_used |= (uint8_t)(1u << cur_folder);
            SetFolderName(cur_folder, value);
            return;
        }
        if(!is_app && !is_hidden && !is_in && !is_in_hidden){
            LOG_SYS_WARN("launcher.cfg: 知らないキーです: %s", key);
            return;
        }
        if(is_app || is_hidden) cur_folder = -1;
        //同じ名前が複数あれば、まだ並べていない若いものから当てる。見つからない(消したアプリ)は捨てる
        for(int i = 0; i < app_count; i++){
            if(((placed >> i) & 1ull) || !(apps[i].name == value)) continue;
            placed |= (1ull << i);
            order[placed_count++] = (int8_t)i;
            if(is_hidden || is_in_hidden) hidden |= (1ull << i);
            if(is_in || is_in_hidden) launcher_folder[i] = (int8_t)cur_folder;
            return;
        }
    });
    //ファイルに無いアプリ(新しく入れたもの)は後ろへ登録順に
    for(int i = 0; i < app_count; i++){
        if(!((placed >> i) & 1ull)) order[placed_count++] = (int8_t)i;
    }
    for(int i = 0; i < app_count; i++) launcher_order[i] = order[i];
    launcher_hidden = hidden;
    //中身の無いフォルダ(アプリが消えた)を落とし、手で書き換えて離れたフォルダの中身を寄せる
    DropEmptyFolders();
    Normalize();
    layout_revision++;
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
    put("# ランチャの並び(上から順)。hidden = の行は隠したアプリ。\n");
    put("# folder = の行はフォルダで、続く in = / in-hidden = の行がその中身。ランチャの長押し/設定アプリが書き換える\n");
    char line[PICO_Config::kConfigMaxLineLen];
    int prev_folder = -1;
    for(int i = 0; i < app_count && ok; i++){
        const int idx = launcher_order[i];
        const int fo = launcher_folder[idx];
        if(fo >= 0 && fo != prev_folder){
            snprintf(line, sizeof(line), "folder = %s\n", folder_names[fo].c_str());
            put(line);
        }
        prev_folder = fo;
        const char* key = fo >= 0 ? (HiddenBit(idx) ? "in-hidden" : "in") : (HiddenBit(idx) ? "hidden" : "app");
        snprintf(line, sizeof(line), "%s = %s\n", key, apps[idx].name.c_str());
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
