#pragma once

#include "gui/widgets/WidgetID.hpp"

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
    // typeが非対応、またはWidget::operator newの確保失敗時はnullptrを返す。
    // 呼び出し側は必ずnullptrを確認すること(Widget.cpp内のoperator newのコメント参照)。
    Widget* Create(WidgetType type);

    // typeがCreate()で生成できる汎用部品かどうか(生成前の事前チェック用)。
    bool IsCreatable(WidgetType type);
};
