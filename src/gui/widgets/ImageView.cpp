#include "gui/widgets/ImageView.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "gui/Fill4bpp.hpp"

int ImageView::maxOffX() const {
    const int m = this->header.width - this->l_rect.w;
    return m > 0 ? m : 0;
}

int ImageView::maxOffY() const {
    const int m = this->header.height - this->l_rect.h;
    return m > 0 ? m : 0;
}

void ImageView::unload(){
    if(this->loaded) this->sprite.deleteSprite();
    this->loaded = false;
    this->header = {0, 0, 0};
    this->off_x = this->off_y = 0;
    this->win_x = this->win_y = this->win_w = this->win_h = 0;
}

bool ImageView::decodeWindow(int x, int y){
    FsFile f = OSData::SD.open(this->path.c_str());
    if(!f) return false;
    this->sprite.fillScreen(0);
    const bool ok = IconRender::DecodePimgWindow(f, this->header, this->sprite, x, y, this->win_w, this->win_h);
    f.close();
    this->win_x = x;
    this->win_y = y;
    return ok;
}

void ImageView::setSize(int w, int h){
    if(w == this->l_rect.w && h == this->l_rect.h) return;
    markdirty(this->getScreenRect());
    this->l_rect.w = (int16_t)w;
    this->l_rect.h = (int16_t)h;
    if(this->loaded){
        FixedString<PICO_PATH_LEN> p = this->path; // load()がpathを作り直すのでコピーを渡す
        this->load(p.c_str());
    }
    this->needsRender();
}

bool ImageView::load(const char* path){
    this->unload();
    this->path.assign(path);
    this->needsRender();

    FsFile f = OSData::SD.open(path);
    if(!f) return false;
    IconRender::PimgHeader h;
    const bool header_ok = IconRender::ReadPimgHeader(f, h);
    f.close();
    if(!header_ok || h.width == 0 || h.height == 0) return false;
    this->header = h;

    const size_t full_bytes = ((size_t)h.width * h.height + 1) / 2;
    this->full = full_bytes <= kMaxFullBytes;
    if(this->full){
        this->win_w = h.width;
        this->win_h = h.height;
    }else{
        this->win_w = h.width  < this->l_rect.w ? h.width  : this->l_rect.w;
        this->win_h = h.height < this->l_rect.h ? h.height : this->l_rect.h;
    }

    this->sprite.setColorDepth(4);
    if(!this->sprite.createSprite(this->win_w, this->win_h)){
        LOG_SYS_WARN("ImageView: %dx%dのスプライトを確保できませんでした", this->win_w, this->win_h);
        this->header = {0, 0, 0};
        return false;
    }
    for(int i = 0; i < 16; i++) this->sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
    this->loaded = true;

    if(!this->decodeWindow(0, 0)){
        //途中で切れたファイル。読めたところまでは見せる
        LOG_SYS_WARN("ImageView: %s の画素が足りません(壊れている可能性)", path);
    }
    return true;
}

void ImageView::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->ref_touch_x = OSData::touchX;
    this->ref_touch_y = OSData::touchY;
    this->ref_off_x = this->off_x;
    this->ref_off_y = this->off_y;
}

void ImageView::causeOnPressMove(){
    Widget::causeOnPressMove();
    if(!this->loaded) return;

    int x = this->ref_off_x - (OSData::touchX - this->ref_touch_x);
    int y = this->ref_off_y - (OSData::touchY - this->ref_touch_y);
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(x > this->maxOffX()) x = this->maxOffX();
    if(y > this->maxOffY()) y = this->maxOffY();
    if(x == this->off_x && y == this->off_y) return;
    this->off_x = x;
    this->off_y = y;
    this->needsRender();
}

void ImageView::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    //窓だけを持っている場合は、止まった位置の窓を読み直す(ドラッグ中は読まない)
    if(this->loaded && !this->full && (this->off_x != this->win_x || this->off_y != this->win_y)){
        this->decodeWindow(this->off_x, this->off_y);
        this->needsRender();
    }
}

void ImageView::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    const Rect g = this->getScreenRect();
    markdirty(g);
    if(!Fill4bpp::FillRect(g.x, g.y, g.w, g.h, this->background_color)){
        OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);
    }

    if(this->loaded){
        //表示欄より小さい画像は中央へ
        const int base_x = g.x + (this->header.width  < g.w ? (g.w - this->header.width)  / 2 : 0);
        const int base_y = g.y + (this->header.height < g.h ? (g.h - this->header.height) / 2 : 0);
        const int sx = base_x - this->off_x + this->win_x;
        const int sy = base_y - this->off_y + this->win_y;

        const bool transparent = this->header.flags & IconRender::kPimgFlagTransparent;
        //4bppどうしならバッファを直接写す(今のクリップと表示欄の重なりの中だけ)
        int32_t ox, oy, ow, oh;
        OSData::frame->getClipRect(&ox, &oy, &ow, &oh);
        const Rect orig = { (int16_t)ox, (int16_t)oy, (int16_t)ow, (int16_t)oh };
        const Rect clip = orig.intersection(g);
        OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);

        if(!IconRender::Blit4bpp(this->sprite, 0, 0, this->sprite.width(), this->sprite.height(), sx, sy, transparent)){
            if(transparent){
                this->sprite.pushSprite(OSData::frame, sx, sy, 0);
            }else{
                this->sprite.pushSprite(OSData::frame, sx, sy);
            }
        }

        OSData::frame->setClipRect(orig.x, orig.y, orig.w, orig.h);
    }

    this->needs_redraw = false;
}
