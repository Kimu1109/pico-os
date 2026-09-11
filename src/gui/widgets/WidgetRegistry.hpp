#pragma once

#include <vector>
#include "gui/widgets/WidgetID.hpp"

class Widget;

// WidgetId <-> Widget* の対応を管理するスロットテーブル。
// generational index方式: スロットを再利用するたびにgenerationを進め、
// 削除済みIDでの誤参照(use-after-free)をResolve()で検出できるようにする。
//
// スロットの確保はWidget::getId()が初回呼び出し時に遅延で行う(Lua側から
// 参照されないウィジェットはスロットを消費しない)。
namespace WidgetRegistry {
    struct Slot {
        uint32_t generation = 0; // 0 = 未使用スロット
        WidgetType type = WidgetType::Count;
        Widget* widget = nullptr;
    };

    inline std::vector<Slot> slots;
    inline std::vector<uint32_t> free_indices;

    // widgetにIDを新規発行する。プールが上限(WidgetIdTools::MAX_INDEX)に達した場合はInvalid()を返す。
    WidgetId Register(WidgetType type, Widget* widget);

    // idに対応するスロットを解放し、generationを進める。
    // 既に無効化済み/他世代のidを渡した場合は何もしない。
    void Unregister(WidgetId id);

    // idからWidget*を解決する。index範囲外・generation不一致・type不一致(改ざん検出)なら nullptr。
    Widget* Resolve(WidgetId id);
};
