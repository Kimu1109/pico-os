#include "gui/widgets/apps/VideoView.hpp"

#include "video/Pvid_Player.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <cstring>

// OSData::frame の1行のバイト数(4bpp)
static constexpr int kFrameStride = SCREEN_WIDTH / 2;

VideoView::VideoView(int x, int y, int w, int h){
    this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
    this->background_color = PICO_BLACK;
}

void VideoView::setPlayer(PvidPlayer* p){
    this->player = (p && p->isOpen()) ? p : nullptr;
    this->vw = this->vh = 0;
    if(this->player){
        int w = this->player->width(), h = this->player->height();
        if(w > this->l_rect.w) w = this->l_rect.w;
        if(h > this->l_rect.h) h = this->l_rect.h;
        this->vw = (int16_t)w;
        this->vh = (int16_t)h;
        // 横は偶数の画素から(1バイトに2画素なので、そろえると行を丸ごと写せる)
        this->vx = (int16_t)(((this->l_rect.w - w) / 2) & ~1);
        this->vy = (int16_t)((this->l_rect.h - h) / 2);
    }
    this->needsRender();
}

Rect VideoView::videoRect() const {
    const Rect g = this->getScreenRect();
    return { (int16_t)(g.x + this->vx), (int16_t)(g.y + this->vy), this->vw, this->vh };
}

void VideoView::setMessage(const char* text){
    FixedString<PICO_STR_L> next(text ? text : "");
    if(strcmp(next.c_str(), this->message.c_str()) == 0) return;
    this->message = next;
    this->needsRender();
}

void VideoView::onFrame(){
    if(!this->player || !this->visible) return;
    const Rect v = this->videoRect();
    const int t = PvidPlayer::kTile;
    this->player->takeDirty([&](int row, int c0, int c1){
        Rect r = { (int16_t)(v.x + c0 * t), (int16_t)(v.y + row * t),
                   (int16_t)((c1 - c0 + 1) * t), (int16_t)t };
        r = r.intersection(v);
        if(r.w > 0 && r.h > 0) this->markdirty(r);
    });
}

void VideoView::drawMessage(const Rect& g){
    if(this->message.empty()) return;
    FontFn::SetSmall();
    OSData::frame->setTextColor(PICO_WHITE);
    const int tw = OSData::frame->textWidth(this->message.c_str());
    const int th = OSData::frame->fontHeight();
    OSData::frame->setCursor(g.x + (g.w - tw) / 2, g.y + (g.h - th) / 2);
    OSData::frame->print(this->message.c_str());
}

void VideoView::render(){
    if(!this->visible) return;

    //UpdateAll()から来た場合: 描かずにdirtyだけ積む(描くのはFlushDirty()の中)
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
    if(!this->player || !this->player->screen()){
        //背景(黒)はOPAQUEなのでFlushDirty()が塗ってある
        this->drawMessage(g);
        return;
    }

    uint8_t* buf = static_cast<uint8_t*>(OSData::frame->getBuffer());
    if(!buf) return;

    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
    const Rect v = this->videoRect();
    const Rect area = v.intersection({ (int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch });
    if(area.w <= 0 || area.h <= 0) return;

    const uint8_t* src = this->player->screen();
    const int sstride = this->player->stride();
    const int sx0 = area.x - v.x;
    const bool aligned = ((area.x & 1) == 0) && ((area.w & 1) == 0) && ((v.x & 1) == 0);
    for(int sy = area.y; sy < area.y + area.h; sy++){
        const uint8_t* line = src + (size_t)(sy - v.y) * sstride;
        uint8_t* dst = buf + sy * kFrameStride;
        if(aligned){
            memcpy(dst + area.x / 2, line + sx0 / 2, (size_t)area.w / 2);
            continue;
        }
        // 端が奇数の画素にかかる場合は1画素ずつ(ダイアログの縁などでまれに起きる)
        for(int x = 0; x < area.w; x++){
            const int s = sx0 + x;
            const uint8_t c = (s & 1) ? (line[s / 2] & 0x0F) : (line[s / 2] >> 4);
            const int ax = area.x + x;
            uint8_t& b = dst[ax / 2];
            b = (ax & 1) ? (uint8_t)((b & 0xF0) | c) : (uint8_t)((b & 0x0F) | (c << 4));
        }
    }
}
