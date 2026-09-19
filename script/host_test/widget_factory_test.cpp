// WidgetFactory(WidgetType -> new Xxx)とWidgetRegistry::Resolve()を検証するテスト。
//
// 両方ともLua統合の受け皿として先行実装されていたが、呼び出し元が実コード中に
// 1つも無くテストも無かった(CLAUDE.md「Lua着手前の受け皿の状態」参照)。
// Resolve()が実際にgeneration不一致・type不一致・破棄済みIDを弾けるかは、
// 本物のWidgetを介さないと確かめられない。
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/Widget.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include <cstdio>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
// NumberInput/Textboxのデストラクタが呼ぶ。実体(KeyboardNum等)は本テストの対象外
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::HideAll(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// Create()が対応しているべき「汎用部品」の一覧。
// widgets/直下の汎用カタログからwidgets/apps・widgets/systems・widgets/dialogs
// 専用のものを除いたもの(CLAUDE.md「ウィジェットの置き場所」参照)。
static const WidgetType kCreatableTypes[] = {
    WidgetType::Button,
    WidgetType::Label,
    WidgetType::Textbox,
    WidgetType::NumberInput,
    WidgetType::Checkbox,
    WidgetType::Icon,
    WidgetType::Image,
    WidgetType::NumberSlider,
    WidgetType::ScrollContainer,
    WidgetType::ScrollList,
    WidgetType::CanvasRaster,
    WidgetType::LayoutContainer,
    WidgetType::GridContainer,
    WidgetType::TabBar,
    WidgetType::DropdownMenu,
};

// アプリ/OS専用で、Create()が対応しない側の代表例
static const WidgetType kNonCreatableTypes[] = {
    WidgetType::MarkdownView,
    WidgetType::FileExplorer,
    WidgetType::AnalogClock,
    WidgetType::DurationPicker,
    WidgetType::Statusbar,
    WidgetType::AppGrid,
    WidgetType::MsgDialog,
    WidgetType::Keyboard,
};

int main(){
    // ---- IsCreatable()とCreate()の対応が一致していること ----
    for(WidgetType type : kCreatableTypes){
        check(WidgetFactory::IsCreatable(type), "IsCreatable: 汎用部品はtrue");

        Widget* w = WidgetFactory::Create(type);
        check(w != nullptr, "Create: 汎用部品は生成できる");
        if(!w) continue;
        check(w->getWidgetType() == type, "Create: 生成物のgetWidgetType()が要求typeと一致");
        delete w;
    }

    for(WidgetType type : kNonCreatableTypes){
        check(!WidgetFactory::IsCreatable(type), "IsCreatable: アプリ/OS専用部品はfalse");
        check(WidgetFactory::Create(type) == nullptr, "Create: アプリ/OS専用部品はnullptrを返す");
    }

    // ---- WidgetRegistry::Resolve()の消費側検証 ----
    {
        Widget* button = WidgetFactory::Create(WidgetType::Button);
        Widget* label = WidgetFactory::Create(WidgetType::Label);
        check(button && label, "検証用ウィジェットを生成");

        const WidgetId button_id = button->getId();
        const WidgetId label_id = label->getId();
        check(WidgetIdTools::IsValid(button_id) && WidgetIdTools::IsValid(label_id),
              "getId(): 遅延発行された両IDが有効");
        check(button_id != label_id, "getId(): 異なるウィジェットには異なるIDが振られる");

        check(WidgetRegistry::Resolve(button_id) == button, "Resolve: 発行済みIDから元のWidget*が引ける");
        check(WidgetRegistry::Resolve(label_id) == label, "Resolve: 複数登録していても取り違えない");

        // 改ざん検出: 同じindex/generationのままtypeビットだけ書き換えたIDは弾かれる
        const WidgetId tampered = WidgetIdTools::Pack(
            WidgetType::Textbox, // 実際はButtonなので不一致になるよう別のtypeを指定
            WidgetIdTools::GetGeneration(button_id),
            WidgetIdTools::GetIndex(button_id));
        check(WidgetRegistry::Resolve(tampered) == nullptr, "Resolve: type不一致のIDはnullptr");

        // 破棄済みIDの誤参照(use-after-free)検出
        delete button;
        check(WidgetRegistry::Resolve(button_id) == nullptr,
              "Resolve: 破棄済みウィジェットのIDはnullptr(use-after-free検出)");
        check(WidgetRegistry::Resolve(label_id) == label,
              "Resolve: 他のウィジェットのIDは道連れにされない");

        // 同じスロットを再利用しても、古い世代のIDでは引けない
        Widget* reused = WidgetFactory::Create(WidgetType::Button);
        const WidgetId reused_id = reused->getId();
        check(WidgetIdTools::GetIndex(reused_id) == WidgetIdTools::GetIndex(button_id),
              "前提: スロットが再利用されている(free_indicesの構造上、直前に空いた枠が先に使われる)");
        check(reused_id != button_id, "スロット再利用時はgenerationが進み、IDそのものは変わる");
        check(WidgetRegistry::Resolve(button_id) == nullptr,
              "Resolve: スロット再利用後も古い世代のIDは弾かれる(generation不一致)");
        check(WidgetRegistry::Resolve(reused_id) == reused,
              "Resolve: 新しい世代のIDは新しいWidget*を指す");

        delete label;
        delete reused;
    }

    // ---- IDを一度も使わないウィジェットはスロットを消費しない ----
    {
        const size_t slots_before = WidgetRegistry::slots.size();
        Widget* w = WidgetFactory::Create(WidgetType::Checkbox);
        check(w != nullptr, "検証用ウィジェットを生成");
        // getId()を一度も呼ばずに破棄する
        delete w;
        check(WidgetRegistry::slots.size() == slots_before,
              "getId()未使用のウィジェットはスロットを消費しない");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
