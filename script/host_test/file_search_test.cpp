// ファイル検索(FileSearchScene)・容量(StorageScene)と、その下のSDを1件ずつたどる部品(SdWalker)。
// SdFatのスタブの HostSd::dirs_enabled でファイルのパスからフォルダを作って列挙させる。
#include "gui/scenes/FileSearchScene.hpp"
#include "gui/scenes/StorageScene.hpp"
#include "gui/scenes/FileViewerScene.hpp"
#include "storage/Sd_Walker.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <set>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int pop_calls = 0, push_calls = 0;
void SceneFunctions::Pop(){ pop_calls++; }
void SceneFunctions::Push(Scene* s){ push_calls++; delete s; }

// ファイルビューワーは開くだけ(中身はこのテストの対象外)
void FileViewerScene::onEnter(){}
void FileViewerScene::onExit(){}
void FileViewerScene::onUpdate(){}
bool FileViewerScene::onKey(const KeyInputFunctions::Event&){ return false; }
uint32_t FileViewerScene::preferredSampleRate() const { return 0; }

void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::OnPanelShown(KeyboardPanel*){}
void KeyboardFunctions::OnPanelHidden(KeyboardPanel*){}
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel*, bool){}
void KeyboardFunctions::HideAll(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static void setupFiles(){
    HostSd::files.clear();
    HostSd::dirs_enabled = true;
    OSData::SD_usable = true;
    HostSd::files["/README.md"] = std::string(100, 'a');
    HostSd::files["/music/a.WAV"] = std::string(1000, 'b');
    HostSd::files["/music/サンプル/demo.mml"] = std::string(50, 'c');
    HostSd::files["/screenshots/shot_0001.bmp"] = std::string(3000, 'd');
    HostSd::files["/screenshots/shot_0002.bmp"] = std::string(3000, 'd');
    HostSd::files["/deep/1/2/3/4/5/6/7/8/9/x.bmp"] = "z";
}

static void testWalker(){
    setupFiles();
    SdWalker w;
    check(w.begin("/"), "walker: ルートを開ける");
    std::set<std::string> seen;
    uint64_t total = 0;
    bool too_deep = false;
    SdWalker::Entry e;
    while(w.next(e)){
        seen.insert(e.path);
        total += e.size;
        if(e.too_deep) too_deep = true;
    }
    check(seen.count("/music/サンプル/demo.mml") == 1, "walker: 2段下のファイル");
    check(seen.count("/music/サンプル") == 1, "walker: フォルダも返す");
    check(seen.count("/README.md") == 1, "walker: ルート直下");
    check(too_deep, "walker: 深すぎるフォルダは中を見ない印");
    check(seen.count("/deep/1/2/3/4/5/6/7/8/9/x.bmp") == 0, "walker: 深すぎる所のファイルは返さない");
    check(total == 100 + 1000 + 50 + 6000, "walker: 大きさの合計");
    check(!w.active(), "walker: 終わったら止まる");

    SdWalker w2;
    check(w2.begin("/music/"), "walker: 末尾の/を許す");
    int n = 0;
    while(w2.next(e)){ n++; check(strncmp(e.path, "/music/", 7) == 0 && e.path[7] != '/', "walker: パスに//が入らない"); }
    check(n == 3, "walker: /musicの下は3件");
    SdWalker w3;
    check(!w3.begin("/nothing"), "walker: 無いフォルダは開けない");
}

static void testSearch(){
    setupFiles();
    check(FileSearchScene::NameMatches("Shot_0001.BMP", "bmp"), "検索: 大文字/小文字を区別しない");
    check(!FileSearchScene::NameMatches("a.wav", "mml"), "検索: 含まなければ外れ");
    check(FileSearchScene::NameMatches("サンプル", "ンプ"), "検索: 日本語の部分一致");

    FileSearchScene s("/", "BMP");
    s.onEnter();
    for(int i = 0; i < 100 && !s.finished(); i++) s.onUpdate();
    check(s.finished(), "検索: 終わる");
    check(s.resultCount() == 2, "検索: 2件(深すぎる所は見ない)");
    check(strcmp(s.resultAt(0), "/screenshots/shot_0001.bmp") == 0, "検索: 絶対パスで持つ");

    // フォルダを選ぶとファイルアプリへ戻ってそこを開く
    FileSearchScene s2("/", "サンプ");
    s2.onEnter();
    for(int i = 0; i < 100 && !s2.finished(); i++) s2.onUpdate();
    check(s2.resultCount() == 1, "検索: フォルダも見つかる");
    s2.onExit();
    WidgetFunctions::ClearSceneWidgets();
    s.onExit();
    WidgetFunctions::ClearSceneWidgets();
}

static void testStorage(){
    setupFiles();
    char buf[16];
    StorageScene::FormatBytes(512, buf, sizeof(buf));
    check(strcmp(buf, "512B") == 0, "容量: 512B");
    StorageScene::FormatBytes(1536, buf, sizeof(buf));
    check(strcmp(buf, "1.50KB") == 0, "容量: 1.50KB");
    StorageScene::FormatBytes(3ull * 1024 * 1024 * 1024, buf, sizeof(buf));
    check(strcmp(buf, "3.00GB") == 0, "容量: 3.00GB");

    StorageScene st("/");
    st.onEnter();
    for(int i = 0; i < 200 && !st.finished(); i++) st.onUpdate();
    check(st.finished(), "容量: 数え終わる");
    check(st.entryCount() == 4, "容量: 直下は4件");
    check(strcmp(st.entryAt(0).name.c_str(), "screenshots") == 0 && st.entryAt(0).size == 6000, "容量: 大きい順(フォルダは中身の合計)");
    check(strcmp(st.entryAt(1).name.c_str(), "music") == 0 && st.entryAt(1).size == 1050, "容量: 2段下まで足す");
    check(st.totalBytes() == 100 + 1050 + 6000, "容量: 合計(深すぎる所は数えない)");

    // SD全体の使用量: freeClusterCount()(FAT32で数秒止まる)を使わず、たどった大きさをクラスタへ切り上げて足す
    check(StorageScene::ClusterRound(0, false, 32768) == 0, "容量: 空のファイルは0クラスタ");
    check(StorageScene::ClusterRound(1, false, 32768) == 32768, "容量: 1バイトでも1クラスタ");
    check(StorageScene::ClusterRound(32769, false, 32768) == 65536, "容量: 切り上げ");
    check(StorageScene::ClusterRound(0, true, 32768) == 32768, "容量: フォルダは1クラスタ");
    int frames = 0;
    while(!st.capacityKnown() && frames < 500){ st.onUpdate(); frames++; }
    check(st.capacityKnown(), "容量: SD全体を数え終わる");
    uint64_t expect = 32768;   // ルート
    {
        SdWalker w;
        w.begin("/");
        SdWalker::Entry e;
        while(w.next(e)) expect += StorageScene::ClusterRound(e.size, e.is_dir, 32768);
    }
    check(StorageScene::CapacityUsed() == expect, "容量: 使用量はクラスタへ切り上げた合計");
    check(StorageScene::CapacityTotal() == 1000ull * 32768, "容量: 全体はクラスタ数×クラスタの大きさ");
    st.onExit();
    WidgetFunctions::ClearSceneWidgets();
    HostSd::dirs_enabled = false;
}

int main(){
    testWalker();
    testSearch();
    testStorage();
    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
