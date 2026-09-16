#include "gui/widgets/systems/AppGrid.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/icons/icon_render.h"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "util/Utf8Byte.hpp"
#include "OS_Data.hpp"

#include <cstring>

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

void AppGrid::drawName(const char* name, int x, int y, int w, int color) {
    if (!name || name[0] == '\0' || w <= 0) return;

    const int line_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Small);

    // 行の切れ目を先に全部決めてから描く。
    // DrawPlain()は内部でフォントを既定へ戻してしまうので、
    // 幅の測定(Smallを適用した状態が要る)と描画を混ぜられない
    size_t line_start[kNameLines] = {0};
    size_t line_end[kNameLines]   = {0};
    int    line_w[kNameLines]     = {0};
    int    line_count = 0;

    {
        FontFn::SetFontSize(FontFn::Small);

        const size_t total = strlen(name);
        size_t pos = 0;

        for (int i = 0; i < kNameLines && pos < total; i++) {
            const bool is_last = (i == kNameLines - 1);

            size_t end = pos;
            int acc = 0;
            while (end < total) {
                int clen = Utf8CharBytesFromLeadByte((uint8_t)name[end]);
                if (end + (size_t)clen > total) clen = (int)(total - end);

                char ch[5];
                memcpy(ch, name + end, (size_t)clen);
                ch[clen] = '\0';

                const int cw = OSData::frame->textWidth(ch);
                //1文字も入らない幅でも、最低1文字は進めないと無限ループになる
                if (acc + cw > w && end > pos) break;

                acc += cw;
                end += (size_t)clen;
            }

            //最終行は残り全部を渡し、はみ出した分はDrawPlain()のクリップに任せる
            line_start[line_count] = pos;
            line_end[line_count]   = is_last ? total : end;
            line_w[line_count]     = is_last ? 0 : acc; //最終行の幅は下で測り直す
            line_count++;

            pos = end;
        }

        //最終行だけは範囲が変わり得るので、確定した範囲で測り直す
        if (line_count > 0) {
            const int last = line_count - 1;
            char buf[kMaxNameBytes];
            size_t len = line_end[last] - line_start[last];
            if (len >= sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, name + line_start[last], len);
            buf[len] = '\0';
            line_w[last] = OSData::frame->textWidth(buf);
        }

        FontFn::SetDefault();
    }

    for (int i = 0; i < line_count; i++) {
        char buf[kMaxNameBytes];
        size_t len = line_end[i] - line_start[i];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, name + line_start[i], len);
        buf[len] = '\0';

        int tx = x + (w - line_w[i]) / 2;
        if (tx < x) tx = x; //行がタイルより広い場合は左寄せ+DrawPlain側で切り詰め

        Label<PICO_STR_M>::DrawPlain(FontFn::Small, color, tx, y + i * line_h, w, buf);
    }
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

        const bool is_pressed = (first + slot == pressed_index_);
        const int fore_color = is_pressed ? PICO_WHITE : PICO_BLACK;

        //押下中のタイルは背景を敷いて押し込みを表す
        if (is_pressed) {
            OSData::frame->fillRect(tx, ty, t.w, t.h, PICO_BLACK);
        }

        IconRender::DrawIcon(entry->icon, kIconSize,
                             tx + (t.w - kIconPx) / 2, ty + kPadding, fore_color);

        drawName(entry->name.c_str(), tx, ty + kPadding + kIconPx + kLabelGap, t.w, fore_color);
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
