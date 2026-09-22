#pragma once

#include <cstddef>
#include "gui/widgets/WidgetID.hpp"
#include "consts.hpp"

class Widget;

// WidgetType -> Widget* の対応表。Luaなど外部からウィジェットを生成するための唯一の口
// (WidgetRegistry::Resolve()と対になる「消費側」の片割れ)。
//
// 生成できるのは widgets/ 直下の汎用部品だけ。widgets/apps(MarkdownView等)・
// widgets/systems(Statusbar/AppGrid)・widgets/dialogsの専用ウィジェットは、
// 生成にSD走査やシーン固有の状態を必要とするためここでは作らない
// (CLAUDE.md「ウィジェットの置き場所」参照)。
//
// 生成直後は仮の位置・大きさで置かれるだけなので、呼び出し側がsetX/setY/setW/setH等で
// 実際の配置へ整えること。
namespace WidgetFactory {
    // Create()が生成するLabel/Textboxの文字容量(テンプレート引数N)。
    // WidgetProperty側がstatic_castで実体型(Label<kLabelTextCapacity>等)へ
    // 戻す際にも同じ値を使う必要があるため、ここへ出して1箇所にまとめてある
    // (Create()の実装と食い違うと不正なテンプレート特殊化へキャストしてしまう)。
    constexpr size_t kLabelTextCapacity = PICO_STR_L;
    constexpr size_t kTextboxCapacity = PICO_STR_LL;

    // typeが非対応、またはWidget::operator newの確保失敗時はnullptrを返す。
    // 呼び出し側は必ずnullptrを確認すること(Widget.cpp内のoperator newのコメント参照)。
    Widget* Create(WidgetType type);

    // typeがCreate()で生成できる汎用部品かどうか(生成前の事前チェック用)。
    bool IsCreatable(WidgetType type);

    // Lua等、種別を文字列で指定したい呼び出し元向け。WidgetType列挙子と
    // 同じ表記(例: "Button")。IsCreatable()がtrueを返す20種のみ対応し、
    // 一致しなければfalseを返す(outは書き換えない)。
    bool TypeFromName(const char* name, WidgetType& out);
};
