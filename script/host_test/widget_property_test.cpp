// WidgetProperty(WidgetType非依存のget/set共通口)を検証するテスト。
//
// WidgetFactory::Create()で生成した各汎用ウィジェットに対し、代表的なプロパティが
// 正しく読み書きできること、型不一致・非対応idではfalseを返すことを確認する
// (CLAUDE.md「Lua着手前の受け皿の状態」の「プロパティのget/set共通口」に対応)。
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/Widget.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/NumberInput.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include <cstdio>

// ---- モック(widget_factory_test.cppと同じ) ----
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
void KeyboardFunctions::HideAll(){}

namespace WP = WidgetProperty;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    // ---- 共通プロパティ(X/Y/Visible/BackgroundColor) ----
    {
        Widget* b = WidgetFactory::Create(WidgetType::Button);
        check(WP::Set(b, WP::Id::X, WP::Value::MakeInt(10)), "共通: X set");
        check(WP::Set(b, WP::Id::Y, WP::Value::MakeInt(20)), "共通: Y set");
        WP::Value v;
        check(WP::Get(b, WP::Id::X, v) && v.i == 10, "共通: X get");
        check(WP::Get(b, WP::Id::Y, v) && v.i == 20, "共通: Y get");

        check(WP::Set(b, WP::Id::Visible, WP::Value::MakeBool(false)), "共通: Visible set");
        check(WP::Get(b, WP::Id::Visible, v) && v.b == false, "共通: Visible get");

        check(WP::Set(b, WP::Id::BackgroundColor, WP::Value::MakeInt(5)), "共通: BackgroundColor set");
        check(WP::Get(b, WP::Id::BackgroundColor, v) && v.i == 5, "共通: BackgroundColor get");

        // 型不一致は弾く
        check(!WP::Set(b, WP::Id::X, WP::Value::MakeBool(true)), "共通: 型不一致のsetはfalse");
        check(!WP::Set(b, WP::Id::X, WP::Value::MakeStr("x")), "共通: 型不一致のsetはfalse(文字列)");

        delete b;
    }

    // ---- nullptr / 非対応idの安全側動作 ----
    {
        WP::Value v;
        check(!WP::Get(nullptr, WP::Id::X, v), "nullptrのgetはfalse");
        check(!WP::Set(nullptr, WP::Id::X, WP::Value::MakeInt(1)), "nullptrのsetはfalse");

        Widget* icon = WidgetFactory::Create(WidgetType::Icon);
        check(!WP::Get(icon, WP::Id::Text, v), "Iconに無いプロパティ(Text)のgetはfalse");
        delete icon;
    }

    // ---- Button ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::Button);
        check(WP::Set(w, WP::Id::Text, WP::Value::MakeStr("押す")), "Button: Text set");
        WP::Value v;
        check(WP::Get(w, WP::Id::Text, v) && v.s == "押す", "Button: Text get");

        check(WP::Set(w, WP::Id::W, WP::Value::MakeInt(80)), "Button: W set");
        check(WP::Set(w, WP::Id::H, WP::Value::MakeInt(30)), "Button: H set");
        check(WP::Get(w, WP::Id::W, v) && v.i == 80, "Button: W get");

        check(WP::Set(w, WP::Id::TextColor, WP::Value::MakeInt(3)), "Button: TextColor set");
        check(WP::Get(w, WP::Id::TextColor, v) && v.i == 3, "Button: TextColor get");
        delete w;
    }

    // ---- Label ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::Label);
        check(WP::Set(w, WP::Id::Text, WP::Value::MakeStr("hello")), "Label: Text set");
        check(WP::Set(w, WP::Id::MaxWidth, WP::Value::MakeInt(120)), "Label: MaxWidth set");
        check(WP::Set(w, WP::Id::TextAlign, WP::Value::MakeInt((int)TextAlign::Center)), "Label: TextAlign set");
        WP::Value v;
        check(WP::Get(w, WP::Id::Text, v) && v.s == "hello", "Label: Text get");
        check(WP::Get(w, WP::Id::MaxWidth, v) && v.i == 120, "Label: MaxWidth get");
        check(WP::Get(w, WP::Id::TextAlign, v) && v.i == (int)TextAlign::Center, "Label: TextAlign get");
        delete w;
    }

    // ---- Checkbox ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::Checkbox);
        check(WP::Set(w, WP::Id::Checked, WP::Value::MakeBool(true)), "Checkbox: Checked set");
        WP::Value v;
        check(WP::Get(w, WP::Id::Checked, v) && v.b == true, "Checkbox: Checked get");
        delete w;
    }

    // ---- NumberSlider ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::NumberSlider);
        check(WP::Set(w, WP::Id::MinValue, WP::Value::MakeFloat(0)), "NumberSlider: MinValue set");
        check(WP::Set(w, WP::Id::MaxValue, WP::Value::MakeFloat(10)), "NumberSlider: MaxValue set");
        check(WP::Set(w, WP::Id::Value, WP::Value::MakeFloat(7.5f)), "NumberSlider: Value set");
        WP::Value v;
        check(WP::Get(w, WP::Id::Value, v) && v.f == 7.5f, "NumberSlider: Value get");
        // 範囲外はmin/maxへclampされる(NumberSlider::setValueの既存挙動)
        check(WP::Set(w, WP::Id::Value, WP::Value::MakeFloat(999)), "NumberSlider: Value set(範囲外)");
        check(WP::Get(w, WP::Id::Value, v) && v.f == 10, "NumberSlider: Valueがmaxへclampされる");
        delete w;
    }

    // ---- Icon ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::Icon);
        check(WP::Set(w, WP::Id::IconId, WP::Value::MakeInt((int)IconID::File)), "Icon: IconId set");
        WP::Value v;
        check(WP::Get(w, WP::Id::IconId, v) && v.i == (int)IconID::File, "Icon: IconId get");
        check(WP::Set(w, WP::Id::IconOpaque, WP::Value::MakeBool(true)), "Icon: IconOpaque set");
        check(WP::Get(w, WP::Id::IconOpaque, v) && v.b == true, "Icon: IconOpaque get(getOpaque()追加で解消)");
        delete w;
    }

    // ---- NumberInput ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::NumberInput);
        check(WP::Set(w, WP::Id::Text, WP::Value::MakeStr("42")), "NumberInput: Text set(setNum()追加で解消)");
        WP::Value v;
        check(WP::Get(w, WP::Id::Text, v) && v.s == "42", "NumberInput: Text get(getNum()追加で解消)");
        delete w;
    }

    // ---- Image ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::Image);
        check(WP::Set(w, WP::Id::Path, WP::Value::MakeStr("/sd/a.pimg")), "Image: Path set");
        WP::Value v;
        check(WP::Get(w, WP::Id::Path, v) && v.s == "/sd/a.pimg", "Image: Path get");
        delete w;
    }

    // ---- ScrollList ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::ScrollList);
        ScrollList* sl = static_cast<ScrollList*>(w);
        check(WP::Set(w, WP::Id::SelectedIndex, WP::Value::MakeInt(2)), "ScrollList: SelectedIndex set");
        WP::Value v;
        check(WP::Get(w, WP::Id::SelectedIndex, v) && v.i == 2, "ScrollList: SelectedIndex get");
        check(WP::Set(w, WP::Id::EnableIcon, WP::Value::MakeBool(true)), "ScrollList: EnableIcon set");
        check(WP::Get(w, WP::Id::EnableIcon, v) && v.b == true, "ScrollList: EnableIcon get");

        // pico.list_add(LuaEngine)が実際に叩くadd()/clear()と、新設のItemCountプロパティ
        ScrollListTools::Item item;
        item.text.assign("one");
        sl->add(item);
        sl->add(item);
        check(WP::Get(w, WP::Id::ItemCount, v) && v.i == 2, "ScrollList: ItemCount get(add()2回で2)");
        sl->clear();
        check(WP::Get(w, WP::Id::ItemCount, v) && v.i == 0, "ScrollList: ItemCount get(clear()後は0)");
        delete w;
    }

    // ---- LayoutContainer / GridContainer ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::LayoutContainer);
        check(WP::Set(w, WP::Id::Gap, WP::Value::MakeInt(4)), "LayoutContainer: Gap set");
        WP::Value v;
        check(WP::Get(w, WP::Id::Gap, v) && v.i == 4, "LayoutContainer: Gap get");
        delete w;

        Widget* g = WidgetFactory::Create(WidgetType::GridContainer);
        check(WP::Set(g, WP::Id::Cols, WP::Value::MakeInt(3)), "GridContainer: Cols set");
        check(WP::Get(g, WP::Id::Cols, v) && v.i == 3, "GridContainer: Cols get");
        check(WP::Set(g, WP::Id::HAlign, WP::Value::MakeInt(1)), "GridContainer: HAlign set");
        check(WP::Get(g, WP::Id::HAlign, v) && v.i == 1, "GridContainer: HAlign get(getHAlign()追加で解消)");
        check(WP::Set(g, WP::Id::VAlign, WP::Value::MakeInt(2)), "GridContainer: VAlign set");
        check(WP::Get(g, WP::Id::VAlign, v) && v.i == 2, "GridContainer: VAlign get(getVAlign()追加で解消)");
        delete g;
    }

    // ---- TabBar ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::TabBar);
        TabBar* tb = static_cast<TabBar*>(w);
        tb->addTab("A");
        tb->addTab("B");
        check(WP::Set(w, WP::Id::TabSelected, WP::Value::MakeInt(1)), "TabBar: TabSelected set");
        WP::Value v;
        check(WP::Get(w, WP::Id::TabSelected, v) && v.i == 1, "TabBar: TabSelected get");
        check(WP::Get(w, WP::Id::TabCount, v) && v.i == 2, "TabBar: TabCount get");
        delete w;
    }

    // ---- DropdownMenu ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::DropdownMenu);
        DropdownMenu* dm = static_cast<DropdownMenu*>(w);
        dm->add("A");
        dm->add("B");
        check(WP::Set(w, WP::Id::SelectedIndex, WP::Value::MakeInt(1)), "DropdownMenu: SelectedIndex set");
        WP::Value v;
        check(WP::Get(w, WP::Id::SelectedIndex, v) && v.i == 1, "DropdownMenu: SelectedIndex get");
        check(WP::Get(w, WP::Id::ItemCount, v) && v.i == 2, "DropdownMenu: ItemCount get(add()2回で2)");
        dm->clear();
        check(WP::Get(w, WP::Id::ItemCount, v) && v.i == 0, "DropdownMenu: ItemCount get(clear()新設、0に戻る)");
        delete w;
    }

    // ---- RectShape ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::RectShape);
        check(WP::Set(w, WP::Id::W, WP::Value::MakeInt(60)), "RectShape: W set");
        check(WP::Set(w, WP::Id::H, WP::Value::MakeInt(30)), "RectShape: H set");
        check(WP::Set(w, WP::Id::Color, WP::Value::MakeInt(3)), "RectShape: Color set");
        check(WP::Set(w, WP::Id::Filled, WP::Value::MakeBool(false)), "RectShape: Filled set");
        check(WP::Set(w, WP::Id::Thickness, WP::Value::MakeInt(2)), "RectShape: Thickness set");
        WP::Value v;
        check(WP::Get(w, WP::Id::W, v) && v.i == 60, "RectShape: W get");
        check(WP::Get(w, WP::Id::Color, v) && v.i == 3, "RectShape: Color get");
        check(WP::Get(w, WP::Id::Filled, v) && v.b == false, "RectShape: Filled get");
        check(WP::Get(w, WP::Id::Thickness, v) && v.i == 2, "RectShape: Thickness get");
        delete w;
    }

    // ---- EllipseShape ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::EllipseShape);
        check(WP::Set(w, WP::Id::W, WP::Value::MakeInt(50)), "EllipseShape: W set");
        check(WP::Set(w, WP::Id::Filled, WP::Value::MakeBool(true)), "EllipseShape: Filled set");
        WP::Value v;
        check(WP::Get(w, WP::Id::W, v) && v.i == 50, "EllipseShape: W get");
        check(WP::Get(w, WP::Id::Filled, v) && v.b == true, "EllipseShape: Filled get");
        delete w;
    }

    // ---- LineShape ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::LineShape);
        check(WP::Set(w, WP::Id::X1, WP::Value::MakeInt(0)), "LineShape: X1 set");
        check(WP::Set(w, WP::Id::Y1, WP::Value::MakeInt(0)), "LineShape: Y1 set");
        check(WP::Set(w, WP::Id::X2, WP::Value::MakeInt(30)), "LineShape: X2 set");
        check(WP::Set(w, WP::Id::Y2, WP::Value::MakeInt(40)), "LineShape: Y2 set");
        check(WP::Set(w, WP::Id::Thickness, WP::Value::MakeInt(3)), "LineShape: Thickness set");
        WP::Value v;
        check(WP::Get(w, WP::Id::X2, v) && v.i == 30, "LineShape: X2 get");
        check(WP::Get(w, WP::Id::Y2, v) && v.i == 40, "LineShape: Y2 get");
        check(WP::Get(w, WP::Id::Thickness, v) && v.i == 3, "LineShape: Thickness get");
        // 外接矩形はx1/y1/x2/y2+余白から自動計算される(setW/setHは非対応)
        check(!WP::Set(w, WP::Id::W, WP::Value::MakeInt(10)), "LineShape: Wの直接設定は非対応");
        // x/yは2点をまとめて平行移動する(点の個別位置はx1/y1/x2/y2で変える)
        check(WP::Get(w, WP::Id::X, v), "LineShape: X get(外接矩形の左上)");
        const int before_x = v.i;
        check(WP::Set(w, WP::Id::X, WP::Value::MakeInt(before_x + 5)), "LineShape: X set(平行移動)");
        check(WP::Get(w, WP::Id::X1, v) && v.i == 5, "LineShape: X1が平行移動で追従する");
        check(WP::Get(w, WP::Id::X2, v) && v.i == 35, "LineShape: X2も平行移動で追従する");
        delete w;
    }

    // ---- TriangleShape ----
    {
        Widget* w = WidgetFactory::Create(WidgetType::TriangleShape);
        check(WP::Set(w, WP::Id::X1, WP::Value::MakeInt(0)), "TriangleShape: X1 set");
        check(WP::Set(w, WP::Id::Y1, WP::Value::MakeInt(20)), "TriangleShape: Y1 set");
        check(WP::Set(w, WP::Id::X2, WP::Value::MakeInt(20)), "TriangleShape: X2 set");
        check(WP::Set(w, WP::Id::Y2, WP::Value::MakeInt(0)), "TriangleShape: Y2 set");
        check(WP::Set(w, WP::Id::X3, WP::Value::MakeInt(40)), "TriangleShape: X3 set");
        check(WP::Set(w, WP::Id::Y3, WP::Value::MakeInt(20)), "TriangleShape: Y3 set");
        check(WP::Set(w, WP::Id::Filled, WP::Value::MakeBool(false)), "TriangleShape: Filled set");
        WP::Value v;
        check(WP::Get(w, WP::Id::X3, v) && v.i == 40, "TriangleShape: X3 get");
        check(WP::Get(w, WP::Id::Y3, v) && v.i == 20, "TriangleShape: Y3 get");
        check(WP::Get(w, WP::Id::Filled, v) && v.b == false, "TriangleShape: Filled get");
        delete w;
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
