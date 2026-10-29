// 文字の大きさの設定(FontFn::Text / SetLargeText)のテスト。
//
// - FontFn::Text は設定に従って16px/20px相当(16pxの1.25倍)になる(Small等は変わらない)
// - Lua等から来た整数(FontFn::FromInt)は Small〜Bigger だけを通し、Text にはならない
//   (Luaのウィジェットのプロパティ font_size も同じ)
// - TextView・ScrollList は Text を選ぶと設定に従って行の高さが変わる
//   (ScrollList の一覧の高さは公称値ではなく測った文字の高さで見積もる)
// フォントはスタブ(高さ=名前のpx)なので、幅の折り返しまでは見ない。
#include "gui/core/Font_Functions.hpp"
#include "gui/widgets/TextView.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <cstring>

void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
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

static int fontHeightOf(FontFn::FontSize size){
    FontFn::SetFontSize(size);
    const int h = OSData::frame->fontHeight();
    FontFn::SetDefault();
    return h;
}

// 一番下までスクロールしたときの位置(=中身の高さ-表示の高さ)。行の高さに比例する
static int maxScrollOf(TextView& v){
    v.setScrollY(1 << 20);
    return v.getScrollY();
}

static void testFontFn(){
    FontFn::SetLargeText(false);
    check(!FontFn::IsLargeText(), "既定は標準の大きさ");
    check(FontFn::GetFontSize(FontFn::Text) == 16, "標準: Text は16px");
    check(fontHeightOf(FontFn::Text) == 16, "標準: Text の実際の高さは16px");
    check(FontFn::GetTextScale() == 1.0f, "標準: Text は拡大しない");

    FontFn::SetLargeText(true);
    check(FontFn::GetFontSize(FontFn::Text) == 20, "大: Text は20px");
    check(FontFn::GetTextScale() == 1.25f, "大: Text は16pxを1.25倍にする");
    check(fontHeightOf(FontFn::Text) == 20, "大: Text の実際の高さは20px");
    check(FontFn::GetFontSize(FontFn::Small) == 16, "大: Small は16pxのまま");
    check(fontHeightOf(FontFn::Small) == 16, "大: Small の実際の高さは16pxのまま");
    check(fontHeightOf(FontFn::Normal) == 24, "大: Normal は24pxのまま");
    FontFn::SetLargeText(false);
}

static void testFromInt(){
    check(FontFn::FromInt(0) == FontFn::Small, "FromInt: 0=Small");
    check(FontFn::FromInt(1) == FontFn::Normal, "FromInt: 1=Normal");
    check(FontFn::FromInt(3) == FontFn::Bigger, "FromInt: 3=Bigger");
    check(FontFn::FromInt((int)FontFn::Text) == FontFn::Small, "FromInt: Text の値は Small になる(Luaは本文の設定に従わない)");
    check(FontFn::FromInt(-1) == FontFn::Small, "FromInt: 負は Small");
    check(FontFn::FromInt(99) == FontFn::Small, "FromInt: 範囲外は Small");

    // Luaのウィジェットのプロパティ font_size も同じ口を通る
    Widget* l = WidgetFactory::Create(WidgetType::Label);
    namespace WP = WidgetProperty;
    check(WP::Set(l, WP::Id::FontSize, WP::Value::MakeInt((int)FontFn::Text)), "プロパティ: font_size に Text の値を入れられる");
    WP::Value v;
    check(WP::Get(l, WP::Id::FontSize, v) && v.i == (int)FontFn::Small, "プロパティ: Text の値は Small として入る");
    delete l;
}

static void testTextView(){
    static char doc[2048];
    doc[0] = '\0';
    for(int i = 0; i < 40; i++) strcat(doc, "line\n");

    FontFn::SetLargeText(true);
    TextView small_view(0, 0, 200, 100);
    small_view.setDocument(doc, (int)strlen(doc));
    TextView text_view(0, 0, 200, 100);
    text_view.setFontSize(FontFn::Text);
    text_view.setDocument(doc, (int)strlen(doc));
    const int small_max = maxScrollOf(small_view);
    const int large_max = maxScrollOf(text_view);
    // 測った後も、測る前の拡大率に戻っている(他の部品の描画へ漏らさない)
    check(OSData::frame->getTextSizeX() == 1.0f, "TextView: 測った後は拡大率を元に戻す");
    check(small_view.getRowCount() == text_view.getRowCount(), "TextView: 改行だけなら行数は同じ");
    check(large_max > small_max, "TextView: 大のとき Text の行は Small より高い");

    // 標準へ戻すと Small と同じ高さ(作り直したときから効く)
    FontFn::SetLargeText(false);
    TextView normal_view(0, 0, 200, 100);
    normal_view.setFontSize(FontFn::Text);
    normal_view.setDocument(doc, (int)strlen(doc));
    check(maxScrollOf(normal_view) == small_max, "TextView: 標準のとき Text は Small と同じ高さ");

    // Small/Text 以外は Small として扱う
    TextView other(0, 0, 200, 100);
    other.setFontSize(FontFn::Bigger);
    other.setDocument(doc, (int)strlen(doc));
    check(maxScrollOf(other) == small_max, "TextView: Small/Text 以外は Small");
}

// ScrollList::MARGIN(非公開)と同じ値
static constexpr int kListMargin = 2;

static void testScrollList(){
    FontFn::SetLargeText(true);
    ScrollList list(0, 0, 100, 10);
    list.setFontSize(FontFn::Text);
    for(int i = 0; i < 3; i++){
        ScrollListTools::Item item;
        item.text.assign("項目");
        list.add(item);
    }
    // 描く前でも、測った高さ(20px)で見積もる
    check(list.getFittingHeight() == 3 * (20 + kListMargin), "ScrollList: 一覧の高さは測った文字の高さで見積もる");

    // 文字を変えたら測り直す
    list.setFontSize(FontFn::Normal);
    check(list.getFittingHeight() == 3 * (24 + kListMargin), "ScrollList: 文字を変えたら測り直す");
    FontFn::SetLargeText(false);
}

int main(){
    testFontFn();
    testFromInt();
    testTextView();
    testScrollList();
    if(failures){
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
