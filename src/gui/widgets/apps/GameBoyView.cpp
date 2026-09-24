#include "gui/widgets/apps/GameBoyView.hpp"

#include "gb/Gb_Emu.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <cstring>

// DMGの濃さ(0=一番薄い〜3=一番濃い)→パレット番号
static constexpr uint8_t kShadeColors[4] = { PICO_WHITE, PICO_LIGHTGREY, PICO_DARKGREY, PICO_BLACK };

// OSData::frame の1行のバイト数(4bpp)
static constexpr int kFrameStride = SCREEN_WIDTH / 2;

static_assert(GameBoyView::kViewW == GbEmu::kWidth * 3 / 2, "横は1.5倍");
static_assert(GameBoyView::kViewH == GbEmu::kHeight * 3 / 2, "縦は1.5倍");
static_assert(GameBoyView::kViewW % 2 == 0, "1行を丸ごとバイト単位で扱う");

GameBoyView::GameBoyView(int x, int y, GbEmu* emu) : emu(emu) {
    this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)kViewW, (int16_t)kViewH };
    this->background_color = PICO_WHITE;
    this->buildLut();
}

void GameBoyView::buildLut(){
    for(int v = 0; v < 256; v++){
        const uint8_t c0 = kShadeColors[(v >> 6) & 3];
        const uint8_t c1 = kShadeColors[(v >> 4) & 3];
        const uint8_t c2 = kShadeColors[(v >> 2) & 3];
        const uint8_t c3 = kShadeColors[v & 3];
        // 4画素 p0 p1 p2 p3 → 6画素 p0 p0 p1 p2 p2 p3(4bppは左の画素が上位4bit)
        this->lut[v][0] = (uint8_t)((c0 << 4) | c0);
        this->lut[v][1] = (uint8_t)((c1 << 4) | c2);
        this->lut[v][2] = (uint8_t)((c2 << 4) | c3);
    }
}

void GameBoyView::buildLine(int dy, uint8_t* line) const {
    // 縦も2行→3行(dy*2/3)
    const uint8_t* src = this->emu->row(dy * 2 / 3);
    for(int i = 0; i < GbEmu::kBytesPerRow; i++){
        memcpy(line + i * 3, this->lut[src[i]], 3);
    }
}

void GameBoyView::setMessage(const char* text){
    FixedString<PICO_STR_L> next(text ? text : "");
    if(strcmp(next.c_str(), this->message.c_str()) == 0) return;
    this->message = next;
    this->needsRender();
}

void GameBoyView::onFrame(){
    if(!this->visible || !this->emu) return;

    int first = 0, last = 0;
    if(!this->emu->takeChangedRows(first, last)) return;

    // 元の行[first,last] を描く画面上の行の範囲(1行ぶん広めに取っても害は無い)
    const int y0 = first * 3 / 2;
    int y1 = ((last + 1) * 3 + 1) / 2;
    if(y1 > kViewH) y1 = kViewH;

    const Rect g = this->getScreenRect();
    this->markdirty({ g.x, (int16_t)(g.y + y0), g.w, (int16_t)(y1 - y0) });
}

void GameBoyView::drawMessage(const Rect& g){
    if(this->message.empty()) return;
    FontFn::SetSmall();
    OSData::frame->setTextColor(PICO_BLACK);
    const int tw = OSData::frame->textWidth(this->message.c_str());
    const int th = OSData::frame->fontHeight();
    OSData::frame->setCursor(g.x + (g.w - tw) / 2, g.y + (g.h - th) / 2);
    OSData::frame->print(this->message.c_str());
}

void GameBoyView::render(){
    if(!this->visible) return;

    // UpdateAll()から来た場合: 描かずにdirtyだけ積む(描くのはFlushDirty()の中)
    if(!PICO_GFX::isDirtyDeactivates){
        if(this->needs_redraw){
            if(this->prev_l_rect != this->l_rect) this->markdirty(this->getScreenPrevRect());
            this->markdirty(this->getScreenRect());
            this->prev_l_rect = this->l_rect;
            this->needs_redraw = false;
        }
        return;
    }
    this->needs_redraw = false;

    const Rect g = this->getScreenRect();

    if(!this->emu || !this->emu->loaded()){
        // 背景はOPAQUEなのでFlushDirty()が塗ってある
        this->drawMessage(g);
        return;
    }

    uint8_t* buf = static_cast<uint8_t*>(OSData::frame->getBuffer());
    if(!buf) return;

    // 今のクリップ(dirty矩形)と自分の重なりだけを書く
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
    const Rect area = g.intersection({ (int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch });
    if(area.w <= 0 || area.h <= 0) return;

    const int vx0 = area.x - g.x;          // 自分の中での横の範囲 [vx0, vx1)
    const int vx1 = vx0 + area.w;
    const bool byte_aligned = ((area.x & 1) == 0) && ((area.w & 1) == 0);

    uint8_t line[kViewW / 2];
    int built_src_row = -1;

    for(int sy = area.y; sy < area.y + area.h; sy++){
        const int dy = sy - g.y;
        const int src_row = dy * 2 / 3;
        if(src_row != built_src_row){
            this->buildLine(dy, line);
            built_src_row = src_row;
        }

        uint8_t* dst = buf + sy * kFrameStride;
        if(byte_aligned && (g.x & 1) == 0){
            memcpy(dst + area.x / 2, line + vx0 / 2, (size_t)area.w / 2);
            continue;
        }
        // 端が奇数の画素にかかる場合は1画素ずつ(ダイアログの縁などでまれに起きる)
        for(int vx = vx0; vx < vx1; vx++){
            const uint8_t c = (vx & 1) ? (line[vx / 2] & 0x0F) : (line[vx / 2] >> 4);
            const int ax = g.x + vx;
            uint8_t& b = dst[ax / 2];
            b = (ax & 1) ? (uint8_t)((b & 0xF0) | c) : (uint8_t)((b & 0x0F) | (c << 4));
        }
    }
}
