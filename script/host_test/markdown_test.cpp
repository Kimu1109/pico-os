// MarkdownViewのブロックレイアウトを検証するテスト。
//
// MarkdownViewは measure_label で各ブロックの高さを測って b.height に積み、
// 実際の表示は別のLabel(labelPool)が行う。この2つは別インスタンスで設定手順も
// 違うため、片方だけ挙動が変わるとブロック同士が重なる/隙間が空くという形で壊れる。
// Label単体のテストでは見えない層なので、ここで押さえる。
//
// 実際にこの層でしか捕まえられなかった例:
//   段落をコピーせずraw_textの参照で渡すようにした際、parseMarkup()の
//   自動装飾オフ分岐が長さではなくNUL終端まで読んでいたため、コードブロックが
//   以降の段落まで取り込んで高さが倍近くになった(自動装飾オフはコードブロック専用の
//   経路なので、他のブロック種別では再現しない)。
#include "gui/widgets/MarkdownView.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <string>
#include <vector>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void KeyboardFunctions::HideAll(){}
void KeyboardFunctions::Setup(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq(int actual, int expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %-46s 実測=%d 期待=%d\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

// 表示中の子ウィジェットを、縦位置の順に並べて返す
static std::vector<Widget*> visibleInOrder(MarkdownView& v){
    std::vector<Widget*> out;
    for(Widget* c : v.getChildren()){
        if(c->getVisible()) out.push_back(c);
    }
    for(size_t i = 0; i + 1 < out.size(); i++){
        for(size_t j = i + 1; j < out.size(); j++){
            if(out[j]->getY() < out[i]->getY()){ Widget* t = out[i]; out[i] = out[j]; out[j] = t; }
        }
    }
    return out;
}

// 縦に並んだ要素同士が重なっていないことを確認する。
// ブロックに割り当てた高さより実際のLabelが高いと、ここで重なりとして現れる
static void checkNoOverlap(MarkdownView& v, const char* label){
    std::vector<Widget*> items = visibleInOrder(v);
    int worst_overlap = 0;
    for(size_t i = 0; i + 1 < items.size(); i++){
        const int bottom = items[i]->getY() + items[i]->getH();
        const int next_top = items[i + 1]->getY();
        const int overlap = bottom - next_top;
        if(overlap > worst_overlap) worst_overlap = overlap;
    }
    if(worst_overlap > 0){
        printf("[FAIL] %s (最大%dpxの重なり、表示中%d件)\n", label, worst_overlap, (int)items.size());
        for(Widget* w : items) printf("         y=%d h=%d\n", w->getY(), w->getH());
        failures++;
    }else{
        printf("[ OK ] %s (表示中%d件)\n", label, (int)items.size());
    }
}

static void load(MarkdownView& v, const std::string& doc){
    HostSd::files["t.md"] = doc;
    v.load("t.md");
}

int main(){
    FontFn::SetDefault();

    // ---- コードブロック ----
    {
        MarkdownView v(0, 0, 240, 260);
        load(v, "段落1です。\n\n```\ncode line one\ncode line two\ncode line three\n```\n\n段落2です。\n");
        checkNoOverlap(v, "コードブロックを挟んでも重ならない");

        std::vector<Widget*> items = visibleInOrder(v);
        check(items.size() == 3, "コードブロック文書の表示要素は3件");
        if(items.size() == 3){
            //コードブロックは3行(Small=16px) + 装飾余白2px
            eq(items[1]->getH(), 3 * 16 + 2, "コードブロックの高さは3行ぶん");
            eq(items[1]->getY(), 28, "コードブロックのY位置");
            eq(items[2]->getY(), 92, "コードブロック直後の段落のY位置");
        }
    }

    // ---- コードブロックの中にマークアップ記号がある場合 ----
    // 自動装飾オフなので、記号はそのまま1文字として扱われ装飾にはならない
    {
        MarkdownView v(0, 0, 240, 260);
        load(v, "```\na = **b**\nc = _d_\n```\n\nあと\n");
        checkNoOverlap(v, "記号入りコードブロックでも重ならない");
        std::vector<Widget*> items = visibleInOrder(v);
        if(items.size() >= 1) eq(items[0]->getH(), 2 * 16 + 2, "記号入りコードブロックの高さは2行ぶん");
    }

    // ---- 見出し・リスト・引用の混在 ----
    {
        MarkdownView v(0, 0, 240, 300);
        load(v, "# 見出し\n\n段落です。\n\n- 項目1\n- 項目2\n\n> 引用文\n\n最後。\n");
        checkNoOverlap(v, "見出し/リスト/引用の混在でも重ならない");
    }

    // ---- 折り返しが起きる長い段落 ----
    {
        MarkdownView v(0, 0, 240, 300);
        load(v, "短い段落。\n\n"
                "これは折り返しが発生する長さの日本語の段落です。"
                "複数行に分かれた場合でも、次のブロックと重ならないことを確認します。\n\n"
                "最後の段落。\n");
        checkNoOverlap(v, "折り返す段落があっても重ならない");
    }

    // ---- 空文書 ----
    {
        MarkdownView v(0, 0, 240, 260);
        load(v, "");
        checkNoOverlap(v, "空文書でも破綻しない");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
