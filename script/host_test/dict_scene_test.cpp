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
// 「戻る」はテキストの代わりにアイコン(IconID::ArrowLeft)を持つボタンに
// なったので、テキストではなくアイコンで探す
static Button* findButtonByIcon(IconID icon){
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() != WidgetType::Button) continue;
        Button* b = static_cast<Button*>(w);
        if(b->getHasIcon() && b->getIconId() == icon) return b;
    }
    return nullptr;
}
static int countItems(ScrollList* list){
    int n = 0;
    while(list->itemAt(n)) n++;
    return n;
}

// WidgetFunctions::Add()はvisitAll()で子孫も含めて全部widgetsへ積むため、
// 素朴に「widgetsを1つずつdelete」すると、detail_scroll(親)のデストラクタが
// 子のdetail_labelを道連れに解放した後、そのdetail_labelを別枠でもう一度
// deleteする二重解放になる。本物のWidgetFunctions::ClearSceneWidgets()は
// 親を持たないルートしかdeleteしない(子は親のデストラクタに任せる)ので、
// ここでも同じ流儀に揃える
static void deleteSceneWidgets(){
    // 先に「親を持たないもの」を全部集めてから delete する(2パスに分ける)。
    // 1パスで回しながらdeleteすると、あるルートの delete が子孫を道連れに
    // 解放した直後、その子孫をこの同じループが(まだ生きているつもりで)
    // 次の要素として読みに行ってしまい使用済みメモリの参照になる
    std::vector<Widget*> roots;
    for(Widget* w : WidgetFunctions::widgets){
        if(!w->getParent()) roots.push_back(w);
    }
    for(Widget* w : roots){
        delete w;
    }
    WidgetFunctions::widgets.clear();
}

// status_label/detail_title(どちらもLabel<PICO_STR_L>)とdetail_label
// (Label<PICO_STR_2KiB>)は、どれもWidgetType::Labelを名乗るため型だけでは
// 区別できない(異なるNへstatic_castするのはUB)。レイアウト上、上から
// status_label→detail_title→detail_labelの順に並ぶことを使い、
// Y座標でソートして位置で見分ける(calculator_test.cppのfindDisplays()と同じ発想)
static std::vector<Widget*> findLabelsSortedByY(){
    std::vector<Widget*> labels;
    for(Widget* w : WidgetFunctions::widgets){
        if(w->getWidgetType() == WidgetType::Label) labels.push_back(w);
    }
    std::sort(labels.begin(), labels.end(), [](Widget* a, Widget* b){
        return a->getScreenRect().y < b->getScreenRect().y;
    });
    return labels;
}
// 一番下(=Y座標最大)が必ずdetail_label
static Label<PICO_STR_2KiB>* findDetailLabel(){
    std::vector<Widget*> labels = findLabelsSortedByY();
    if(labels.empty()) return nullptr;
    return static_cast<Label<PICO_STR_2KiB>*>(labels.back());
}
// 下から2番目が必ずdetail_title(status_label, detail_title, detail_labelの3つが
// 揃っている前提。onEnter()直後は常にこの3つが存在する)
static Label<PICO_STR_L>* findDetailTitle(){
    std::vector<Widget*> labels = findLabelsSortedByY();
    if(labels.size() < 2) return nullptr;
    return static_cast<Label<PICO_STR_L>*>(labels[labels.size() - 2]);
}

int main(){
    HostSd::files.clear();
    writeDict({
        {"book",        "book",        "a written work"},
        {"cat",         "cat",         "a small domesticated animal"},
        {"category",    "category",    "a class or division"},
        {"concatenate", "concatenate", "to link together"},
        // 辞書の説明文は"~"や"**"をマークアップ記号ではなく生の文字として
        // 普通に使う(実データで836行該当)。ここでは意図的にそれらを含む
        // 説明文を用意し、Labelのマークアップとして解釈されないことを確かめる
        {"tildetest",   "tildetest",   "abc~def **not bold** _not underline_"},
    }, /*block_size=*/2);

    WidgetFunctions::widgets.clear();
    WidgetFunctions::dialog_roots.clear();

    DictScene* scene = new DictScene();
    scene->onEnter();

    Button* back_button    = findButtonByIcon(IconID::ArrowLeft);
    Button* search_button  = findButtonByText("検索");
    Textbox<PICO_STR_LL>* search_box = findByType<Textbox<PICO_STR_LL>>(WidgetType::Textbox);
    ScrollList* result_list = findByType<ScrollList>(WidgetType::ScrollList);
    Label<PICO_STR_L>* detail_title = findDetailTitle();
    Label<PICO_STR_2KiB>* detail_label = findDetailLabel();

    check(back_button && search_button && search_box && result_list && detail_title && detail_label,
          "onEnter(): 想定した6種のウィジェットが揃っている");
    if(!back_button || !search_button || !search_box || !result_list || !detail_title || !detail_label) return 1;

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
    const char* title = detail_title->getText()->c_str();
    const char* detail = detail_label->getText()->c_str();
    check(strstr(title, "**") != nullptr, "一覧タップ: 見出し(表示用語句)が太字マークアップで始まる");
    check(strlen(detail) > 0, "一覧タップ: 詳細欄(説明文)が空でなくなる");
    check(detail_label->getDisableAutoTextDecoration(),
          "一覧タップ: 詳細欄はマークアップ解釈を無効化している(説明文の\"~\"等をそのまま出すため)");

    // ---- 説明文に"~"や"**"が入っていてもマークアップとして解釈されず、
    //      生の文字としてそのまま保持される ----
    search_box->setText("tildetest");
    search_button->causeOnPressEnd();
    eq_int(countItems(result_list), 1, "'tildetest'検索: 1件見つかる");
    result_list->setSelectedIndex(0);
    result_list->causeOnSelectItem(true);
    const char* tilde_detail = detail_label->getText()->c_str();
    check(strstr(tilde_detail, "abc~def **not bold** _not underline_") != nullptr,
          "説明文の\"~\"/\"**\"/\"_\"が変換・除去されず生のまま詳細欄に残る");

    // 以降のPop→再onEnter()の確認は検索語"cat"の復元を見るので、
    // ここで書き戻しておく(直前のtildetest検索で上書きされているため)
    search_box->setText("cat");

    // ---- onExit()→再onEnter(): 検索語が復元される ----
    // onExit()自体はウィジェットを破棄しない(フレームワーク側=ClearSceneWidgetsの責務)ので、
    // 実機と同じ順序(onExit→ウィジェット破棄→次のonEnter)をここで模す
    scene->onExit();
    deleteSceneWidgets();
    WidgetFunctions::dialog_roots.clear();

    scene->onEnter();
    Textbox<PICO_STR_LL>* restored_box = findByType<Textbox<PICO_STR_LL>>(WidgetType::Textbox);
    check(restored_box && strcmp(restored_box->getText()->c_str(), "cat") == 0,
          "Pop→再onEnter(): 検索語(cat)が復元される");

    // ---- 後片付け ----
    scene->onExit();
    deleteSceneWidgets();
    WidgetFunctions::dialog_roots.clear();
    delete scene;

    // ---- 説明文が長くても切り詰めずスクロールで全文へたどり着ける ----
    // (以前はdetail_labelにsetMaxHeight()で固定高さを持たせて溢れた分を
    // 切り詰めていたが、それだと長い説明の途中で見えなくなっていた。
    // ScrollContainerへ包んだことで、labelの高さは箱に収まる必要が無くなった
    // はず — それをここで確かめる)
    {
        HostSd::files.clear();
        std::vector<Entry> entries = {
            {"book", "book", "a written work"},
            {"longword", "longword", std::string(2000, 'x')}, // 器の高さを大きく超える説明
        };
        writeDict(entries, /*block_size=*/2);

        WidgetFunctions::widgets.clear();
        WidgetFunctions::dialog_roots.clear();

        DictScene* scene2 = new DictScene();
        scene2->onEnter();

        Button* search_button2 = findButtonByText("検索");
        Textbox<PICO_STR_LL>* search_box2 = findByType<Textbox<PICO_STR_LL>>(WidgetType::Textbox);
        ScrollList* result_list2 = findByType<ScrollList>(WidgetType::ScrollList);
        ScrollContainer* detail_scroll2 = findByType<ScrollContainer>(WidgetType::ScrollContainer);

        check(search_button2 && search_box2 && result_list2 && detail_scroll2,
              "長文説明テスト: 想定したウィジェットが揃っている");
        if(search_button2 && search_box2 && result_list2 && detail_scroll2){
            search_box2->setText("longword");
            search_button2->causeOnPressEnd();
            eq_int(countItems(result_list2), 1, "長文説明テスト: 前方一致1件(longword)");

            result_list2->setSelectedIndex(0);
            result_list2->causeOnSelectItem(true);

            Label<PICO_STR_2KiB>* detail_label2 = findDetailLabel();
            check(detail_label2 != nullptr, "長文説明テスト: 詳細欄が見つかる");
            if(detail_label2){
                const int container_h = detail_scroll2->getH();
                const int label_h = detail_label2->getH();
                check(label_h > container_h,
                      "長文説明テスト: 詳細欄の実際の高さが箱の高さを超えている"
                      "(=setMaxHeightで切り詰めなくなった。値は下記参照)");
                printf("       container_h=%d label_h=%d\n", container_h, label_h);
            }
        }

        scene2->onExit();
        deleteSceneWidgets();
        WidgetFunctions::dialog_roots.clear();
        delete scene2;
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
