// 辞書アプリ(DictScene)のGUI配線を検証するテスト。
//
// 検索エンジン自体(前方一致/全体走査の正しさ)はdict_testが受け持つので、
// ここでは「入力欄→検索ボタン→一覧への反映→一覧タップで詳細欄」という
// 実際のウィジェット経路が繋がっていることと、全体走査ぶん(語の途中の一致)が
// onUpdate()を回すことで一覧へ追記されていくことを確認する。
#include "gui/scenes/DictScene.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// ---- モック(calculator_test.cppと同じ方針: 実機描画/シーン遷移本体は使わない) ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int pop_calls = 0;
void SceneFunctions::Pop(){ pop_calls++; }

// Textboxはタップ/破棄のたびにキーボード常駐機構へ登録・解除するが、
// このテストではオンスクリーンキーボードそのものは使わない(setText()で
// 直接値を入れる)ので、実装の代わりに空実装を置く(calculator_test.cppの
// GFX/Log同様、フレームワーク境界のモック)
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::HideAll(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_int(int actual, int expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %-56s 実測=%d 期待=%d\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

// ---- テスト用辞書データ(dict_test.cppと同じ組み立て方) ----
struct Entry { std::string term, disp, desc; };

static void writeDict(std::vector<Entry> entries, int block_size){
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b){ return a.term < b.term; });

    std::string body, index;
    uint32_t offset = 0;
    for(size_t i = 0; i < entries.size(); i++){
        if(block_size > 0 && (i % (size_t)block_size) == 0){
            index += entries[i].term + "\t" + std::to_string(offset) + "\n";
        }
        std::string line = entries[i].term + "\t" + entries[i].disp + "\t" + entries[i].desc + "\n";
        body += line;
        offset += (uint32_t)line.size();
    }
    HostSd::files[PICO_Path::FILE::DICT::DICT_BODY] = body;
    HostSd::files[PICO_Path::FILE::DICT::DICT_INDEX] = index;
}

// ---- ウィジェットを見つける補助(calculator_test.cppと同じ発想) ----
template<typename T>
static T* findByType(WidgetType type){
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() == type) return static_cast<T*>(w);
    }
    return nullptr;
}
static Button* findButtonByText(const char* text){
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() != WidgetType::Button) continue;
        Button* b = static_cast<Button*>(w);
        if(strcmp(b->getText().c_str(), text) == 0) return b;
    }
    return nullptr;
}
static int countItems(ScrollList* list){
    int n = 0;
    while(list->itemAt(n)) n++;
    return n;
}

// status_label(Label<PICO_STR_L>)とdetail_label(Label<PICO_STR_2KiB>)は
// どちらもWidgetType::Labelを名乗るため型だけでは区別できない
// (異なるNへstatic_castするのはUB)。レイアウト上detail_labelが必ず
// 一番下にあることを使ってY座標最大のものを選ぶ
static Label<PICO_STR_2KiB>* findDetailLabel(){
    Widget* bottom = nullptr;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() != WidgetType::Label) continue;
        if(!bottom || w->getScreenRect().y > bottom->getScreenRect().y) bottom = w;
    }
    return static_cast<Label<PICO_STR_2KiB>*>(bottom);
}

int main(){
    HostSd::files.clear();
    writeDict({
        {"book",        "book",        "a written work"},
        {"cat",         "cat",         "a small domesticated animal"},
        {"category",    "category",    "a class or division"},
        {"concatenate", "concatenate", "to link together"},
    }, /*block_size=*/2);

    WidgetFunctions::widgets.clear();
    WidgetFunctions::dialog_roots.clear();

    DictScene* scene = new DictScene();
    scene->onEnter();

    Button* back_button    = findButtonByText("戻る");
    Button* search_button  = findButtonByText("検索");
    Textbox<PICO_STR_LL>* search_box = findByType<Textbox<PICO_STR_LL>>(WidgetType::Textbox);
    ScrollList* result_list = findByType<ScrollList>(WidgetType::ScrollList);
    Label<PICO_STR_2KiB>* detail_label = findDetailLabel();

    check(back_button && search_button && search_box && result_list && detail_label,
          "onEnter(): 想定した5種のウィジェットが揃っている");
    if(!back_button || !search_button || !search_box || !result_list || !detail_label) return 1;

    // ---- 検索語を入れずに検索: 案内文が出るだけで一覧は空のまま ----
    search_button->causeOnPressEnd();
    eq_int(countItems(result_list), 0, "空の検索語: 一覧は増えない");

    // ---- "cat"で検索: 前方一致ぶん(cat, category)はボタンを押した直後から出る ----
    search_box->setText("cat");
    search_button->causeOnPressEnd();
    eq_int(countItems(result_list), 2, "'cat'検索直後: 前方一致2件(cat, category)がすぐ一覧に出る");

    // ---- onUpdate()を回すと、語の途中の一致(concatenate)が追記される ----
    int guard = 0;
    while(countItems(result_list) < 3 && guard++ < 10000){
        scene->onUpdate();
    }
    eq_int(countItems(result_list), 3, "onUpdate()を回すとconcatenate(語の途中の一致)が一覧へ追記される");

    // 一覧の中身(順序は問わない)
    bool foundCat = false, foundCategory = false, foundConcat = false;
    for(int i = 0; i < countItems(result_list); i++){
        const char* text = result_list->itemAt(i)->text.c_str();
        if(strncmp(text, "cat ", 4) == 0) foundCat = true;
        if(strncmp(text, "category", 8) == 0) foundCategory = true;
        if(strncmp(text, "concatenate", 11) == 0) foundConcat = true;
    }
    check(foundCat && foundCategory && foundConcat,
          "一覧の中身: cat/category/concatenateがそれぞれ表示用語句から始まる行として出る");

    // ---- 一覧をタップすると詳細欄に全文が出る ----
    // ScrollListの当たり判定(座標→行番号)は別のテスト(app_test/calculator_test)で
    // 検証済みの仕組みなので、ここでは「選ばれた」ことそのものを直接起こして
    // DictScene側の配線(on_selectitem→詳細欄への反映)だけを確かめる
    result_list->setSelectedIndex(0);
    result_list->causeOnSelectItem(true);
    const char* detail = detail_label->getText()->c_str();
    check(strstr(detail, "**") != nullptr, "一覧タップ: 詳細欄の表示用語句が太字マークアップで始まる");
    check(strlen(detail) > 0, "一覧タップ: 詳細欄が空でなくなる");

    // ---- onExit()→再onEnter(): 検索語が復元される ----
    // onExit()自体はウィジェットを破棄しない(フレームワーク側=ClearSceneWidgetsの責務)ので、
    // 実機と同じ順序(onExit→ウィジェット破棄→次のonEnter)をここで模す
    scene->onExit();
    for(Widget* w : WidgetFunctions::widgets) delete w;
    WidgetFunctions::widgets.clear();
    WidgetFunctions::dialog_roots.clear();

    scene->onEnter();
    Textbox<PICO_STR_LL>* restored_box = findByType<Textbox<PICO_STR_LL>>(WidgetType::Textbox);
    check(restored_box && strcmp(restored_box->getText()->c_str(), "cat") == 0,
          "Pop→再onEnter(): 検索語(cat)が復元される");

    // ---- 後片付け ----
    scene->onExit();
    for(Widget* w : WidgetFunctions::widgets) delete w;
    WidgetFunctions::widgets.clear();
    WidgetFunctions::dialog_roots.clear();
    delete scene;

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
