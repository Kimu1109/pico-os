#include "gui/core/Focus_Functions.hpp"
#include "apps/home/AppGrid.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/icons/icon_render.h"
#include "gui/core/Font_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "util/Utf8Byte.hpp"
#include "OS_Data.hpp"
#include "gui/ScopedClip.hpp"
#include "apps/home/Home_Widget.hpp"
#include "system/I18n_Functions.hpp"

#include <cstring>
#include <Arduino.h>

int AppGrid::tileW() const {
    const int avail = this->l_rect.w - kPadding * 2 - kGap * (kCols - 1);
    return (avail > kCols) ? (avail / kCols) : 1;
}

int AppGrid::rowsPerPage() const {
    //最終行の下にはgapが要らないので、その分を足してから割る
    const int avail = this->l_rect.h - kPadding * 2 + kGap;
    const int rows = avail / (kTileH + kGap);
    // 1ページのマスは32まで(HomeLayout の used がビットで持つ)
    constexpr int kMaxRows = 32 / kCols;
    return (rows > kMaxRows) ? kMaxRows : (rows > 0) ? rows : 1;
}

int AppGrid::tilesPerPage() const {
    return rowsPerPage() * kCols;
}

uint32_t AppGrid::layoutSignature() const {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v){ h = (h ^ v) * 16777619u; };
    mix((uint32_t)rowsPerPage());
    mix(HomeLayout::Revision());
    mix((uint32_t)AppFunctions::Count());
    mix(AppFunctions::layout_revision);
    mix((uint32_t)(folder_ + 1));
    mix((uint32_t)AppFunctions::launcher_hidden);
    mix((uint32_t)(AppFunctions::launcher_hidden >> 32));
    for (int i = 0; i < AppFunctions::Count(); i++) mix((uint8_t)AppFunctions::launcher_order[i]);
    return h | 1u; //0は「作っていない」の印
}

void AppGrid::ensureLayout() const {
    //開いているフォルダが消えた/中を全部隠した・出したらトップへ戻る
    if (folder_ >= 0 && AppFunctions::FolderAppCount(folder_) == 0) folder_ = -1;
    const uint32_t sig = layoutSignature();
    if (sig == layout_sig_) return;
    if (folder_ >= 0) {
        item_count_ = HomeLayout::ComputeFolder(folder_, rowsPerPage(), items_, HomeLayout::kMaxItems, &pages_);
    } else {
        item_count_ = HomeLayout::Compute(rowsPerPage(), items_, HomeLayout::kMaxItems, &pages_);
    }
    layout_sig_ = sig;
}

bool AppGrid::openFolder(int folder) {
    if (AppFunctions::FolderAppCount(folder) == 0) return false;
    folder_ = folder;
    page_ = 0;
    focus_item_ = 0;
    layoutChanged();
    return true;
}

void AppGrid::closeFolder() {
    ensureLayout();
    if (folder_ < 0) return;
    const int f = folder_;
    folder_ = -1;
    showItem(HomeLayout::ItemType::Folder, f);
}

int AppGrid::pageCount() const {
    ensureLayout();
    return pages_;
}

Rect AppGrid::cellRect(int col, int row, int w, int h) const {
    const int tw = tileW();
    return {
        (int16_t)(kPadding + col * (tw + kGap)),
        (int16_t)(kPadding + row * (kTileH + kGap)),
        (int16_t)(w * tw + (w - 1) * kGap),
        (int16_t)(h * kTileH + (h - 1) * kGap)
    };
}

Rect AppGrid::tileRect(int slot) const {
    return cellRect(slot % kCols, slot / kCols, 1, 1);
}

Rect AppGrid::itemRect(const HomeLayout::Item& it) const {
    return cellRect(it.col, it.row, it.w, it.h);
}

Rect AppGrid::itemScreenRect(const HomeLayout::Item& it) const {
    Rect r = itemRect(it);
    r.x = (int16_t)(r.x + this->getScreenX());
    r.y = (int16_t)(r.y + this->getScreenY());
    return r;
}

HomeWidget* AppGrid::widgetOf(const HomeLayout::Item& it) const {
    if (it.type != HomeLayout::ItemType::Widget) return nullptr;
    if (it.index < 0 || it.index >= HomeLayout::placed_count) return nullptr;
    return HomeLayout::Impl(HomeLayout::placed[it.index].kind);
}

int AppGrid::hitItem(int local_x, int local_y) const {
    ensureLayout();
    for (int i = 0; i < item_count_; i++) {
        if (items_[i].page != page_) continue;
        const Rect t = itemRect(items_[i]);
        if (local_x < t.x || local_x >= t.x + t.w) continue;
        if (local_y < t.y || local_y >= t.y + t.h) continue;
        return i;
    }
    return -1;
}

int AppGrid::hitTile(int local_x, int local_y) const {
    const int i = hitItem(local_x, local_y);
    if (i < 0 || items_[i].type != HomeLayout::ItemType::App) return -1;
    return items_[i].index;
}

int AppGrid::firstItemOnPage(int page) const {
    ensureLayout();
    for (int i = 0; i < item_count_; i++) if (items_[i].page == page) return i;
    return -1;
}

void AppGrid::clampFocus() {
    ensureLayout();
    if (focus_item_ >= 0 && focus_item_ < item_count_ && items_[focus_item_].page == page_) return;
    const int f = firstItemOnPage(page_);
    focus_item_ = f < 0 ? 0 : f;
}

bool AppGrid::takeLongPress(unsigned long now_ms, unsigned long long_ms, LongPress& out) {
    if (!this->is_pressing || long_pressed_) return false;
    if (now_ms - press_ms_ < long_ms) return false;
    long_pressed_ = true;
    out = LongPress{};
    if (pressed_item_ >= 0 && pressed_item_ < item_count_) {
        out.on_item = true;
        out.type = items_[pressed_item_].type;
        out.index = items_[pressed_item_].index;
    }
    //押し込みの表示を戻す(メニューが開くので、押したままの見た目にしない)
    pressed_item_ = -1;
    pressed_button_ = -1;
    this->needsRender();
    return true;
}

bool AppGrid::focusedItem(HomeLayout::ItemType& type, int& index) const {
    ensureLayout();
    int f = focus_item_;
    if (f < 0 || f >= item_count_ || items_[f].page != page_) f = firstItemOnPage(page_);
    if (f < 0) return false;
    type = items_[f].type;
    index = items_[f].index;
    return true;
}

int AppGrid::focusedApp() const {
    HomeLayout::ItemType type;
    int index;
    if (!focusedItem(type, index) || type != HomeLayout::ItemType::App) return -1;
    return index;
}

void AppGrid::showItem(HomeLayout::ItemType type, int index) {
    ensureLayout();
    for (int i = 0; i < item_count_; i++) {
        if (items_[i].type != type || items_[i].index != index) continue;
        page_ = items_[i].page;
        focus_item_ = i;
        pressed_item_ = -1;
        this->needsRender();
        return;
    }
    layoutChanged();
}

void AppGrid::layoutChanged() {
    layout_sig_ = 0;
    ensureLayout();
    if (page_ >= pages_) page_ = pages_ - 1;
    if (page_ < 0) page_ = 0;
    clampFocus();
    pressed_item_ = -1;
    pressed_button_ = -1;
    this->needsRender();
}

void AppGrid::setW(int w) {
    this->l_rect.w = (int16_t)w;
    setPage(page_); //1行あたりの列数は固定だが、念のためページ位置を丸め直す
    this->needsRender();
}

void AppGrid::setH(int h) {
    this->l_rect.h = (int16_t)h;
    //高さが変わると1ページの行数が変わるので、範囲外になったページを戻す
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
    pressed_item_ = -1;
    pressed_button_ = -1;
    clampFocus();
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

void AppGrid::drawIcon(const AppEntry& entry, int x, int y, int color) {
    FixedString<PICO_PATH_LEN> icon_path;
    if (OSData::SD_usable && AppFunctions::IconPathOf(entry, icon_path)) {
        FsFile f = OSData::SD.open(icon_path.c_str(), O_RDONLY);
        if (f) {
            IconRender::PimgHeader header;
            if (IconRender::ReadPimgHeader(f, header) && header.width > 0 && header.height > 0) {
                // kIconPx四方の枠内で中央に置く。読み込みと同じFsFileを続けて渡す
                // ことでヘッダの再読み込みを避ける(DrawImageRLE4bppは自分でseekし直す)
                const int ix = x + (kIconPx - (int)header.width) / 2;
                const int iy = y + (kIconPx - (int)header.height) / 2;
                //枠をはみ出すサイズの.pimgが置かれても隣のタイルを侵さないための安全策
                //(IconRender::DrawIconRawが組み込みアイコンで同じことをしているのと同じ理由)
                {
                    ScopedClip clip(x, y, kIconPx, kIconPx);
                    IconRender::DrawImageRLE4bpp(f, ix, iy);
                }
                f.close();
                return;
            }
            f.close();
        }
        // ファイルが開けない/ヘッダが壊れている場合は下の既定アイコンへフォールバックする
    }

    IconRender::DrawIcon(entry.icon, kIconSize, x, y, color);
}

void AppGrid::drawSmallIcon(const AppEntry& entry, int x, int y, int box, int color) {
    FixedString<PICO_PATH_LEN> icon_path;
    if (OSData::SD_usable && AppFunctions::IconPathOf(entry, icon_path)) {
        FsFile f = OSData::SD.open(icon_path.c_str(), O_RDONLY);
        if (f) {
            ScopedClip clip(x, y, box, box);
            const bool ok = IconRender::DrawImageRLE4bppScaled(f, x, y, box);
            f.close();
            if (ok) return;
        }
    }
    IconRender::DrawIcon(entry.icon, IconSize::Px16, x + (box - 16) / 2, y + (box - 16) / 2, color);
}

void AppGrid::drawFolderIcon(int folder, int x, int y, bool pressed) {
    //枠の色は押し込みの背景(黒)の上でも見えるように変える
    const int frame = pressed ? PICO_DARKGREY : PICO_LIGHTGREY;
    const int fore = pressed ? PICO_WHITE : PICO_BLACK;
    OSData::frame->fillRoundRect(x, y, kIconPx, kIconPx, 8, frame);
    //16pxを2x2に、外側5px・間6px(5+16+6+16+5 = 48)
    constexpr int kMini = 16, kEdge = 5, kStep = kMini + 6;
    const int n = AppFunctions::FolderAppCount(folder);
    for (int i = 0; i < n && i < 4; i++) {
        const AppEntry* e = AppFunctions::Get(AppFunctions::FolderAppAt(folder, i));
        if (!e) continue;
        drawSmallIcon(*e, x + kEdge + (i % 2) * kStep, y + kEdge + (i / 2) * kStep, kMini, fore);
    }
}

bool AppGrid::wantsFrameUpdate() const {
    ensureLayout();
    for (int i = 0; i < item_count_; i++) {
        if (items_[i].page == page_ && items_[i].type == HomeLayout::ItemType::Widget) return true;
    }
    return false;
}

void AppGrid::render() {
    if (!this->visible) return;
    ensureLayout();
    if (page_ >= pages_) page_ = pages_ - 1;

    const Rect g = this->getScreenRect();

    //合成の外の回: ウィジェットの見た目が変わっていたら、その矩形だけを描き直してもらう
    //(合成の中ではMarkDirty()が効かないので、変化はここでだけ見る)
    if (!PICO_GFX::isDirtyDeactivates) {
        const uint32_t page_sig = layout_sig_ ^ ((uint32_t)page_ * 2654435761u);
        const bool fresh = (page_sig != stamps_page_sig_);
        stamps_page_sig_ = page_sig;
        for (int i = 0; i < item_count_; i++) {
            if (items_[i].page != page_) continue;
            HomeWidget* w = widgetOf(items_[i]);
            if (!w) continue;
            const uint32_t st = w->stamp(items_[i].w, items_[i].h);
            if (st == stamps_[i] && !fresh) continue;
            stamps_[i] = st;
            if (!fresh) PICO_GFX::MarkDirty(itemScreenRect(items_[i]));
        }
        if (fresh) this->needsRender();
    }

    if (!this->needs_redraw) return;
    //合成の外の回は描いても捨てられる(dirtyはneedsRender()で積んである)
    if (PICO_GFX::render_suppressed) {
        markdirty(g);
        this->needs_redraw = false;
        return;
    }

    //今のクリップ(合成ではdirty矩形)にかからない項目は描かない
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
    const Rect clip = (cw > 0 && ch > 0) ? Rect{(int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch} : g;

    OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);

    for (int i = 0; i < item_count_; i++) {
        const HomeLayout::Item& it = items_[i];
        if (it.page != page_) continue;
        const Rect r = itemScreenRect(it);
        if (!r.intersects(clip)) continue;

        if (it.type == HomeLayout::ItemType::Folder) {
            const bool is_pressed = (i == pressed_item_);
            if (is_pressed) OSData::frame->fillRect(r.x, r.y, r.w, r.h, PICO_BLACK);
            drawFolderIcon(it.index, r.x + (r.w - kIconPx) / 2, r.y + kPadding, is_pressed);
            drawName(AppFunctions::FolderName(it.index), r.x, r.y + kPadding + kIconPx + kLabelGap, r.w,
                     is_pressed ? PICO_WHITE : PICO_BLACK);
        } else if (it.type == HomeLayout::ItemType::App) {
            const AppEntry* entry = AppFunctions::Get(it.index);
            if (!entry) continue;
            const bool is_pressed = (i == pressed_item_);
            const int fore_color = is_pressed ? PICO_WHITE : PICO_BLACK;
            //押下中のタイルは背景を敷いて押し込みを表す
            if (is_pressed) OSData::frame->fillRect(r.x, r.y, r.w, r.h, PICO_BLACK);
            drawIcon(*entry, r.x + (r.w - kIconPx) / 2, r.y + kPadding, fore_color);
            drawName(I18nFunctions::Tr(entry->name.c_str()), r.x, r.y + kPadding + kIconPx + kLabelGap, r.w, fore_color);
        } else {
            //ウィジェット: 角の丸い枠の中を、種類ごとの描き方に任せる。ボタンの外を押している間は枠を太く
            const bool body_pressed = (i == pressed_item_ && pressed_button_ < 0);
            OSData::frame->drawRoundRect(r.x, r.y, r.w, r.h, 6, body_pressed ? PICO_BLACK : PICO_LIGHTGREY);
            if (body_pressed) OSData::frame->drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 5, PICO_BLACK);
            HomeWidget* w = widgetOf(it);
            ScopedClip sc(r.x + 2, r.y + 2, r.w - 4, r.h - 4);
            if (w) {
                w->draw(r, it.w, it.h, (i == pressed_item_) ? pressed_button_ : -1);
            } else if (it.index >= 0 && it.index < HomeLayout::placed_count) {
                const char* name = I18nFunctions::Tr(HomeLayout::Info(HomeLayout::placed[it.index].kind).name);
                drawName(name, r.x, r.y + r.h / 2 - 8, r.w, PICO_DARKGREY);
            }
        }

        //キー/コントローラーで選んでいる項目
        if (i == focus_item_ && FocusFunctions::ring_visible && FocusFunctions::focused == this) {
            for (int k = 0; k < FocusFunctions::kRingWidth; k++) {
                OSData::frame->drawRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, FocusFunctions::kRingColor);
            }
        }
    }

    markdirty(g);
    this->needs_redraw = false;
}

void AppGrid::causeOnPressStart() {
    Widget::causeOnPressStart();

    pressed_item_ = hitItem(OSData::touchX - this->getScreenX(),
                            OSData::touchY - this->getScreenY());
    pressed_button_ = -1;
    press_ms_ = millis();
    long_pressed_ = false;
    if (pressed_item_ >= 0) {
        const HomeLayout::Item& it = items_[pressed_item_];
        if (HomeWidget* w = widgetOf(it)) {
            pressed_button_ = w->buttonAt(itemScreenRect(it), it.w, it.h, OSData::touchX, OSData::touchY);
        }
        focus_item_ = pressed_item_;
        this->needsRender();
    }
}

void AppGrid::causeOnPressEnd() {
    Widget::causeOnPressEnd();

    const int item = pressed_item_;
    const int button = pressed_button_;
    pressed_item_ = -1;
    pressed_button_ = -1;
    this->needsRender();
    //長押しでメニューを出した項目は起動しない
    if (long_pressed_ || item < 0 || item >= item_count_) return;

    const HomeLayout::Item& it = items_[item];
    if (it.type == HomeLayout::ItemType::App) {
        //起動するとシーンの入れ替えが要求されるので、自分の状態を戻してから呼ぶ
        if (on_launch_) on_launch_(it.index);
        return;
    }
    if (it.type == HomeLayout::ItemType::Folder) {
        openFolder(it.index);
        return;
    }
    HomeWidget* w = widgetOf(it);
    if (!w) return;
    const Rect r = itemScreenRect(it);
    if (button >= 0) {
        //押したボタンの上で離したときだけ(指をずらしてやめられるように)
        if (w->buttonAt(r, it.w, it.h, OSData::touchX, OSData::touchY) == button) w->press(button);
        return;
    }
    if (OSData::touchX >= r.x && OSData::touchX < r.x + r.w && OSData::touchY >= r.y && OSData::touchY < r.y + r.h) {
        activate(item);
    }
}

void AppGrid::activate(int item) {
    if (item < 0 || item >= item_count_) return;
    const HomeLayout::Item& it = items_[item];
    if (it.type == HomeLayout::ItemType::App) {
        if (on_launch_) on_launch_(it.index);
        return;
    }
    if (it.type == HomeLayout::ItemType::Folder) {
        openFolder(it.index);
        return;
    }
    HomeWidget* w = widgetOf(it);
    const char* app = w ? w->appName() : nullptr;
    if (!app || !on_launch_) return;
    for (int i = 0; i < AppFunctions::Count(); i++) {
        const AppEntry* e = AppFunctions::Get(i);
        if (e && e->name == app) { on_launch_(i); return; }
    }
}

Rect AppGrid::focusRect() const {
    ensureLayout();
    int f = focus_item_;
    if (f < 0 || f >= item_count_ || items_[f].page != page_) f = firstItemOnPage(page_);
    if (f < 0) return this->getScreenRect();
    return itemScreenRect(items_[f]);
}

bool AppGrid::onFocusKey(FocusKey key) {
    ensureLayout();
    //戻る: 開いているフォルダを閉じる
    if (key == FocusKey::Back) {
        if (folder_ < 0) return false;
        closeFolder();
        return true;
    }
    clampFocus();
    if (item_count_ == 0 || focus_item_ < 0 || focus_item_ >= item_count_ || items_[focus_item_].page != page_) return false;

    int dx = 0, dy = 0;
    switch (key) {
        case FocusKey::Activate: activate(focus_item_); return true;
        case FocusKey::Up:    dy = -1; break;
        case FocusKey::Down:  dy = 1; break;
        case FocusKey::Left:  dx = -1; break;
        case FocusKey::Right: dx = 1; break;
        default: return false;
    }

    const Rect cur = itemRect(items_[focus_item_]);
    const int ccx = cur.x + cur.w / 2, ccy = cur.y + cur.h / 2;

    //その向きにある項目のうち「向きの距離 + 横ずれ×3」が一番小さいもの(範囲が重なれば横ずれは0)
    int best = -1;
    long best_score = 0;
    for (int i = 0; i < item_count_; i++) {
        if (i == focus_item_ || items_[i].page != page_) continue;
        const Rect r = itemRect(items_[i]);
        const int ox = r.x + r.w / 2 - ccx;
        const int oy = r.y + r.h / 2 - ccy;
        const int along = dx ? ox * dx : oy * dy;
        if (along <= 0) continue;
        int perp;
        if (dx) perp = (r.y < cur.y + cur.h && cur.y < r.y + r.h) ? 0 : (oy < 0 ? -oy : oy);
        else    perp = (r.x < cur.x + cur.w && cur.x < r.x + r.w) ? 0 : (ox < 0 ? -ox : ox);
        const long score = along + 3L * perp;
        if (best < 0 || score < best_score) { best = i; best_score = score; }
    }
    if (best >= 0) {
        focus_item_ = best;
        this->needsRender();
        return true;
    }
    if (dx == 0) return false;

    //左右の端ではページを送り、同じ高さに近い項目(右へなら左端、左へなら右端)を選ぶ
    if (dx > 0 ? !nextPage() : !prevPage()) return false;
    best = -1;
    for (int i = 0; i < item_count_; i++) {
        if (items_[i].page != page_) continue;
        const Rect r = itemRect(items_[i]);
        const int oy = r.y + r.h / 2 - ccy;
        const int side = dx > 0 ? r.x : -(r.x + r.w);
        const long score = 1000L * (oy < 0 ? -oy : oy) + side;
        if (best < 0 || score < best_score) { best = i; best_score = score; }
    }
    if (best >= 0) focus_item_ = best;
    this->needsRender();
    return true;
}
