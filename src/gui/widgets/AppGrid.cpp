#include "gui/widgets/AppGrid.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/icons/icon_render.h"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

int AppGrid::tileW() const {
    const int avail = this->l_rect.w - kPadding * 2 - kGap * (kCols - 1);
    return (avail > kCols) ? (avail / kCols) : 1;
}

int AppGrid::rowsPerPage() const {
    //最終行の下にはgapが要らないので、その分を足してから割る
    const int avail = this->l_rect.h - kPadding * 2 + kGap;
    const int rows = avail / (kTileH + kGap);
    return (rows > 0) ? rows : 1;
}

int AppGrid::tilesPerPage() const {
    return rowsPerPage() * kCols;
}

int AppGrid::pageCount() const {
    const int per_page = tilesPerPage();
    const int count = AppFunctions::Count();
    if (count <= 0) return 1;
    return (count + per_page - 1) / per_page;
}

Rect AppGrid::tileRect(int slot) const {
    const int col = slot % kCols;
    const int row = slot / kCols;
    const int w = tileW();
    return {
        (int16_t)(kPadding + col * (w + kGap)),
        (int16_t)(kPadding + row * (kTileH + kGap)),
        (int16_t)w,
        (int16_t)kTileH
    };
}

int AppGrid::hitTile(int local_x, int local_y) const {
    const int per_page = tilesPerPage();

    for (int slot = 0; slot < per_page; slot++) {
        const Rect t = tileRect(slot);
        if (local_x < t.x || local_x >= t.x + t.w) continue;
        if (local_y < t.y || local_y >= t.y + t.h) continue;

        const int app_index = page_ * per_page + slot;
        return (app_index < AppFunctions::Count()) ? app_index : -1;
    }
    return -1;
}

void AppGrid::setW(int w) {
    this->l_rect.w = (int16_t)w;
    setPage(page_); //1行あたりの列数は固定だが、念のためページ位置を丸め直す
    this->needsRender();
}

void AppGrid::setH(int h) {
    this->l_rect.h = (int16_t)h;
    //高さが変わると1ページのタイル数が変わるので、範囲外になったページを戻す
    if (page_ >= pageCount()) {
        page_ = pageCount() - 1;
    }
    this->needsRender();
}

void AppGrid::setPage(int page) {
    const int count = pageCount();
    if (page < 0) page = 0;
    if (page >= count) page = count - 1;
    if (page == page_) return;

    page_ = page;
    pressed_index_ = -1;
    this->needsRender();
}

bool AppGrid::nextPage() {
    if (page_ + 1 >= pageCount()) return false;
    setPage(page_ + 1);
    return true;
}

bool AppGrid::prevPage() {
    if (page_ <= 0) return false;
    setPage(page_ - 1);
    return true;
}

void AppGrid::render() {
    if (!this->visible) return;
    if (!this->needs_redraw) return;

    const Rect g = this->getScreenRect();
    OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    const int per_page = tilesPerPage();
    const int first = page_ * per_page;

    for (int slot = 0; slot < per_page; slot++) {
        const AppEntry* entry = AppFunctions::Get(first + slot);
        if (!entry) break; // 以降は空きスロット

        const Rect t = tileRect(slot);
        const int tx = g.x + t.x;
        const int ty = g.y + t.y;

        //押下中のタイルは背景を敷いて押し込みを表す
        if (first + slot == pressed_index_) {
            OSData::frame->fillRect(tx, ty, t.w, t.h, PICO_LIGHTGREY);
        }

        IconRender::DrawIcon(entry->icon, kIconSize,
                             tx + (t.w - kIconPx) / 2, ty + kPadding, PICO_BLACK);

        //名前はアイコンの下へ中央揃え。幅の測定にはフォントの適用が要る
        FontFn::SetFontSize(FontFn::Small);
        const int text_w = OSData::frame->textWidth(entry->name);
        FontFn::SetDefault();

        int text_x = tx + (t.w - text_w) / 2;
        if (text_x < tx) text_x = tx; //名前がタイルより広い場合は左寄せ+DrawPlain側で切り詰め

        Label<PICO_STR_M>::DrawPlain(FontFn::Small, PICO_BLACK,
                                      text_x, ty + kPadding + kIconPx + kLabelGap,
                                      t.w, entry->name);
    }

    markdirty(g);
    this->needs_redraw = false;
}

void AppGrid::causeOnPressStart() {
    Widget::causeOnPressStart();

    pressed_index_ = hitTile(OSData::touchX - this->getScreenX(),
                             OSData::touchY - this->getScreenY());
    if (pressed_index_ >= 0) this->needsRender();
}

void AppGrid::causeOnPressEnd() {
    Widget::causeOnPressEnd();

    const int index = pressed_index_;
    pressed_index_ = -1;
    this->needsRender();

    //起動するとシーンの入れ替えが要求されるので、自分の状態を戻してから呼ぶ
    if (index >= 0 && on_launch_) on_launch_(index);
}
