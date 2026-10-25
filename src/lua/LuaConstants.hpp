#pragma once
// Luaから pico.COLOR_RED のように名前で使える定数。数値を直接書かずに済ませるための表。
// 値は C++ 側の定義(consts.hpp・各 enum・アイコン表)をそのまま引くので、食い違わない。
// テーブルへは丸ごと入れず、LuaEngine::l_api_index が初めて引かれた名前だけを写す(関数と同じ遅延)。
// 名前を足したら lua-api-doc/content/reference/constants.md も直すこと
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "consts.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/icons/icons_data.h"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/QRCodeView.hpp"
#include "iso/Iso_World.hpp"

namespace LuaConstants {
    struct Entry { const char* name; int32_t value; };

    // pico.<名前>
    inline constexpr Entry kPico[] = {
        // パレット(consts.hpp)
        {"COLOR_BLACK", PICO_BLACK},         {"COLOR_NAVY", PICO_NAVY},
        {"COLOR_DARKGREEN", PICO_DARKGREEN}, {"COLOR_DARKCYAN", PICO_DARKCYAN},
        {"COLOR_MAROON", PICO_MAROON},       {"COLOR_PURPLE", PICO_PURPLE},
        {"COLOR_OLIVE", PICO_OLIVE},         {"COLOR_LIGHTGREY", PICO_LIGHTGREY},
        {"COLOR_DARKGREY", PICO_DARKGREY},   {"COLOR_BLUE", PICO_BLUE},
        {"COLOR_GREEN", PICO_GREEN},         {"COLOR_CYAN", PICO_CYAN},
        {"COLOR_RED", PICO_RED},             {"COLOR_MAGENTA", PICO_MAGENTA},
        {"COLOR_YELLOW", PICO_YELLOW},       {"COLOR_WHITE", PICO_WHITE},
        {"COLOR_BACKGROUND", PICO_BACKGROUND}, {"COLOR_FORECOLOR", PICO_FORECOLOR},
        // 画面
        {"SCREEN_WIDTH", SCREEN_WIDTH},      {"SCREEN_HEIGHT", SCREEN_HEIGHT},
        {"STATUSBAR_HEIGHT", STATUSBAR_HEIGHT},
        // font_size
        {"FONT_SMALL", FontFn::Small},       {"FONT_NORMAL", FontFn::Normal},
        {"FONT_BIG", FontFn::Big},           {"FONT_BIGGER", FontFn::Bigger},
        // icon_size
        {"ICON_SIZE_16", (int)IconSize::Px16}, {"ICON_SIZE_24", (int)IconSize::Px24},
        {"ICON_SIZE_32", (int)IconSize::Px32}, {"ICON_SIZE_48", (int)IconSize::Px48},
        {"ICON_SIZE_64", (int)IconSize::Px64},
        // text_align(Label / Textbox)
        {"TEXT_ALIGN_LEFT", (int)TextAlign::Left},     {"TEXT_ALIGN_CENTER", (int)TextAlign::Center},
        {"TEXT_ALIGN_RIGHT", (int)TextAlign::Right},
        // direction(LayoutContainer)
        {"DIRECTION_VERTICAL", LayoutContainerTools::VERTICAL},
        {"DIRECTION_HORIZONTAL", LayoutContainerTools::HORIZONTAL},
        // cross_align(LayoutContainer) / h_align・v_align(GridContainer)。3つとも同じ値
        {"ALIGN_START", LayoutContainerTools::START},   {"ALIGN_CENTER", LayoutContainerTools::CENTER},
        {"ALIGN_END", LayoutContainerTools::END},
        // canvas_mode(CanvasRaster)
        {"CANVAS_LINE", Canvas::Line},       {"CANVAS_RECT", Canvas::Rect},
        {"CANVAS_ELLIPSE", Canvas::Ellipse}, {"CANVAS_ARROW", Canvas::Arrow},
        {"CANVAS_STRAIGHT", Canvas::Straight}, {"CANVAS_FILL", Canvas::Fill},
        // ecc(QRCode)
        {"QR_ECC_LOW", (int)QRCodeView::Ecc::Low},           {"QR_ECC_MEDIUM", (int)QRCodeView::Ecc::Medium},
        {"QR_ECC_QUARTILE", (int)QRCodeView::Ecc::Quartile}, {"QR_ECC_HIGH", (int)QRCodeView::Ecc::High},
        // pico.get_time() の wday
        {"WDAY_SUNDAY", 0}, {"WDAY_MONDAY", 1}, {"WDAY_TUESDAY", 2}, {"WDAY_WEDNESDAY", 3},
        {"WDAY_THURSDAY", 4}, {"WDAY_FRIDAY", 5}, {"WDAY_SATURDAY", 6},
    };

    // pico.iso.<名前>
    inline constexpr Entry kIso[] = {
        // iso.create の kind
        {"KIND_NATURAL", Iso::NATURAL}, {"KIND_FLAT", Iso::FLAT}, {"KIND_DEMO", Iso::DEMO},
        {"KIND_EMPTY", Iso::EMPTY},     {"KIND_ARENA", Iso::ARENA},
        // ブロックの番号
        {"BLOCK_AIR", Iso::AIR}, {"BLOCK_WATER", Iso::WATER}, {"BLOCK_STONE", Iso::STONE},
        {"BLOCK_GRASS", Iso::GRASS}, {"BLOCK_DIRT", Iso::DIRT}, {"BLOCK_COBBLE", Iso::COBBLE},
        {"BLOCK_PLANKS", Iso::PLANKS}, {"BLOCK_BRICKS", Iso::BRICKS}, {"BLOCK_SLABS", Iso::SLABS},
        {"BLOCK_WOOD", Iso::WOOD}, {"BLOCK_LEAVES", Iso::LEAVES}, {"BLOCK_SAND", Iso::SAND},
        {"BLOCK_BOOKS", Iso::BOOKS}, {"BLOCK_TNT", Iso::TNT}, {"BLOCK_CRAFTING", Iso::CRAFTING},
        {"BLOCK_FURNACE", Iso::FURNACE}, {"BLOCK_JUKEBOX", Iso::JUKEBOX}, {"BLOCK_SPONGE", Iso::SPONGE},
        {"BLOCK_GRAVEL", Iso::GRAVEL}, {"BLOCK_MOSS", Iso::MOSS}, {"BLOCK_COAL_ORE", Iso::COAL_ORE},
        {"BLOCK_IRON_ORE", Iso::IRON_ORE}, {"BLOCK_BEDROCK", Iso::BEDROCK}, {"BLOCK_IRON", Iso::IRON},
        {"BLOCK_GOLD", Iso::GOLD}, {"BLOCK_TORCH", Iso::TORCH},
    };

    // 名前から値を引く。見つかれば true。pico では ICON_<IconID名> も引ける(アイコン表から)
    template <size_t N>
    inline bool Find(const Entry (&table)[N], const char* key, int32_t& out) {
        for (const Entry& e : table) {
            if (strcmp(e.name, key) == 0) { out = e.value; return true; }
        }
        return false;
    }

    inline bool FindPico(const char* key, int32_t& out) {
        if (strncmp(key, "ICON_", 5) == 0 && strncmp(key, "ICON_SIZE_", 10) != 0) {
            for (size_t i = 0; i < sizeof(kIconNames) / sizeof(kIconNames[0]); i++) {
                if (strcmp(key + 5, kIconNames[i]) == 0) { out = (int32_t)i; return true; }
            }
            return false;
        }
        return Find(kPico, key, out);
    }

    inline bool FindIso(const char* key, int32_t& out) { return Find(kIso, key, out); }
}
