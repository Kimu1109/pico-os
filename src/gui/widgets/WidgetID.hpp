#pragma once

#include <cstdint>

// ウィジェットインスタンスの種類。
// Luaカスタムアプリ側でIDから型を判別するために使う(WidgetIdのtypeビットにそのまま埋め込む)。
// 6bit(最大64種)まで許容。増やす場合はWidgetID.hppのstatic_assertで検知される。
enum class WidgetType : uint8_t {
    AppGrid,
    Button,
    CanvasRaster,
    Checkbox,
    ColorDialog,
    DropdownMenu,
    FileExplorer,
    FileSaveDialog,
    FileSelectDialog,
    GridContainer,
    Icon,
    Image,
    InputDialog,
    Keyboard,
    KeyboardEng,
    KeyboardNum,
    Label,
    LayoutContainer,
    MarkdownView,
    MsgDialog,
    NumberInput,
    NumberSlider,
    ScrollContainer,
    ScrollList,
    Statusbar,
    Textbox,
    Count // 番兵。実際の種類としては使わない
};

// ウィジェットインスタンスを一意に識別する32bit ID(Luaスクリプトへはこの整数値を渡す)。
//
// ビット配分(上位→下位):
//   [31:26] type       6bit  ウィジェットの種類(WidgetType)
//   [25:10] generation 16bit スロット使い回し世代(削除済みIDの誤参照を検出する)
//   [ 9: 0] index      10bit スロットテーブルの添字
//
// generation == 0 は「未割り当て」を表す予約値。WidgetId 0 (オールゼロ)は常に無効値。
using WidgetId = uint32_t;

namespace WidgetIdTools {
    constexpr uint32_t TYPE_BITS = 6;
    constexpr uint32_t GENERATION_BITS = 16;
    constexpr uint32_t INDEX_BITS = 10;

    static_assert(TYPE_BITS + GENERATION_BITS + INDEX_BITS == 32, "WidgetId must fully occupy 32bit");
    static_assert(static_cast<uint32_t>(WidgetType::Count) <= (1u << TYPE_BITS), "WidgetType does not fit in TYPE_BITS");

    constexpr uint32_t INDEX_SHIFT = 0;
    constexpr uint32_t GENERATION_SHIFT = INDEX_BITS;
    constexpr uint32_t TYPE_SHIFT = INDEX_BITS + GENERATION_BITS;

    constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;
    constexpr uint32_t GENERATION_MASK = (1u << GENERATION_BITS) - 1;
    constexpr uint32_t TYPE_MASK = (1u << TYPE_BITS) - 1;

    // 同時に存在できるスロット(=ID発行済みウィジェット)の最大数
    constexpr uint32_t MAX_INDEX = INDEX_MASK;
    // 1スロットが使い回せる最大世代数
    constexpr uint32_t MAX_GENERATION = GENERATION_MASK;

    constexpr WidgetId Invalid() {
        return 0;
    }

    constexpr WidgetId Pack(WidgetType type, uint32_t generation, uint32_t index) {
        return (static_cast<uint32_t>(type) & TYPE_MASK) << TYPE_SHIFT
             | (generation & GENERATION_MASK) << GENERATION_SHIFT
             | (index & INDEX_MASK) << INDEX_SHIFT;
    }

    constexpr WidgetType GetType(WidgetId id) {
        return static_cast<WidgetType>((id >> TYPE_SHIFT) & TYPE_MASK);
    }

    constexpr uint32_t GetGeneration(WidgetId id) {
        return (id >> GENERATION_SHIFT) & GENERATION_MASK;
    }

    constexpr uint32_t GetIndex(WidgetId id) {
        return (id >> INDEX_SHIFT) & INDEX_MASK;
    }

    // generation 0 は未割り当てスロットの意味なので、実際に発行されたIDには現れない
    constexpr bool IsValid(WidgetId id) {
        return GetGeneration(id) != 0;
    }
};
