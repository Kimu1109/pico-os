#include "gui/widgets/WidgetRegistry.hpp"
#include "functions/Log_Functions.hpp"

WidgetId WidgetRegistry::Register(WidgetType type, Widget* widget) {
    uint32_t index;

    if (!free_indices.empty()) {
        index = free_indices.back();
        free_indices.pop_back();
    } else {
        if (slots.size() > WidgetIdTools::MAX_INDEX) {
            LOG_SYS_WARN("WidgetRegistry: slot pool exhausted (max %u), cannot assign WidgetId", WidgetIdTools::MAX_INDEX + 1);
            return WidgetIdTools::Invalid();
        }
        index = static_cast<uint32_t>(slots.size());
        slots.push_back(Slot{});
    }

    Slot& slot = slots[index];
    // generation 0 は「未割り当て」の予約値なので、初回使用時は1から始める
    if (slot.generation == 0) {
        slot.generation = 1;
    }
    slot.type = type;
    slot.widget = widget;

    return WidgetIdTools::Pack(type, slot.generation, index);
}

void WidgetRegistry::Unregister(WidgetId id) {
    if (!WidgetIdTools::IsValid(id)) return;

    const uint32_t index = WidgetIdTools::GetIndex(id);
    if (index >= slots.size()) return;

    Slot& slot = slots[index];
    if (slot.generation != WidgetIdTools::GetGeneration(id)) return; // 既に古い世代のid

    slot.widget = nullptr;
    slot.generation++;
    if (slot.generation == 0) {
        // wrapして予約値0に戻った場合は1に飛ばす
        slot.generation = 1;
    }

    free_indices.push_back(index);
}

Widget* WidgetRegistry::Resolve(WidgetId id) {
    if (!WidgetIdTools::IsValid(id)) return nullptr;

    const uint32_t index = WidgetIdTools::GetIndex(id);
    if (index >= slots.size()) return nullptr;

    const Slot& slot = slots[index];
    if (slot.generation != WidgetIdTools::GetGeneration(id)) return nullptr;
    if (slot.type != WidgetIdTools::GetType(id)) return nullptr; // 改ざん・不整合の検出

    return slot.widget;
}
