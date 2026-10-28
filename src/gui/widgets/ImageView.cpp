#include "gui/widgets/ImageView.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "gui/Fill4bpp.hpp"

int ImageView::maxOffX() const {
    const int m = this->img_w - this->l_rect.w;
    return m > 0 ? m : 0;
}

int ImageView::maxOffY() const {
    const int m = this->img_h - this->l_rect.h;
    return m > 0 ? m : 0;
}

void ImageView::unload(){
    PICO_GFX::CancelDirectPaint(this);
    if(this->loaded) this->sprite.deleteSprite();
    this->loaded = false;
    this->info = ImageCodec::Info();
    this->opt = ImageCodec::Options();
    this->img_w = this->img_h = 0;
    this->off_x = this->off_y = 0;
    this->win_x = this->win_y = this->win_w = this->win_h = 0;
}

bool ImageView::decodeWindow(int x, int y){
    FsFile f = OSData::SD.open(this->path.c_str());
    if(!f) return false;
    this->sprite.fillScreen(this->info.alpha ? 0 : (this->background_color & 0x0F));
    const bool ok = ImageCodec::DecodeTo4bpp(f, this->info, this->sprite, x, y, this->win_w, this->win_h, this->opt);
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
    ImageCodec::Info info;
    const bool header_ok = ImageCodec::ReadInfo(f, info);
    f.close();
    if(!header_ok) return false;
    this->info = info;

    // JPEG・PNG は表示欄に収まる一番大きい段へ縮める(写真は表示欄より大きいことがほとんど。
    // 1/8 でも収まらなければ窓で持つ)
    this->opt.scale = ImageCodec::FitScale(info, this->l_rect.w, this->l_rect.h);
    this->opt.background = (uint8_t)(this->background_color & 0x0F);
    this->img_w = ImageCodec::ScaledW(info, this->opt.scale);
    this->img_h = ImageCodec::ScaledH(info, this->opt.scale);

    this->full = ImageCodec::Bytes4bpp(this->img_w, this->img_h) <= kMaxFullBytes;
    if(this->full){
        this->win_w = this->img_w;
        this->win_h = this->img_h;
    }else{
        this->win_w = this->img_w < this->l_rect.w ? this->img_w : this->l_rect.w;
        this->win_h = this->img_h < this->l_rect.h ? this->img_h : this->l_rect.h;
    }

    this->sprite.setColorDepth(4);
    if(!this->sprite.createSprite(this->win_w, this->win_h)){
        LOG_SYS_WARN("ImageView: %dx%dのスプライトを確保できませんでした", this->win_w, this->win_h);
        this->info = ImageCodec::Info();
        this->img_w = this->img_h = 0;
        return false;
    }
    for(int i = 0; i < 16; i++) this->sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
    this->palette_rev = PICO_GFX::paletteRevision;
    this->loaded = true;

    if(!this->decodeWindow(0, 0)){
        //途中で切れたファイル。読めたところまでは見せる
        LOG_SYS_WARN("ImageView: %s の画素が足りません(壊れている可能性)", path);
    }
    return true;
}

void ImageView::setFullColor(bool on){
    if(this->full_color == on) return;
    this->full_color = on;
    if(!on) PICO_GFX::CancelDirectPaint(this);
    this->needsRender();
}

void ImageView::setVisible(bool v){
    if(!v) PICO_GFX::CancelDirectPaint(this);
    Widget::setVisible(v);
}

void ImageView::imageOrigin(const Rect& g, int& x, int& y) const {
    x = g.x + (this->img_w < g.w ? (g.w - this->img_w) / 2 : 0) - this->off_x;
    y = g.y + (this->img_h < g.h ? (g.h - this->img_h) / 2 : 0) - this->off_y;
}

namespace {
struct PaintCtx {
    int x, y;   // 窓の左上が来る液晶の位置
};

void PushToLcd(void* ctx, int x, int y, int w, int h, const uint16_t* rgb){
    const PaintCtx* p = static_cast<const PaintCtx*>(ctx);
    OSData::lcd->pushImage(p->x + x, p->y + y, w, h, reinterpret_cast<const lgfx::rgb565_t*>(rgb));
}
}

// PICO_GFX::FlushDirty() の最後に呼ばれる。見えている所の画像を、SDから解きながら液晶へ送る
void ImageView::paintFullColor(void* ctx, const Rect& clip){
    ImageView* self = static_cast<ImageView*>(ctx);
    if(!self->loaded) return;
    const Rect g = self->getScreenRect();
    int ox, oy;
    self->imageOrigin(g, ox, oy);
    // 画像の中で見えている範囲(画像座標)
    int x0 = clip.x - ox, y0 = clip.y - oy;
    int x1 = x0 + clip.w, y1 = y0 + clip.h;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 > self->img_w) x1 = self->img_w;
    if(y1 > self->img_h) y1 = self->img_h;
    if(x0 >= x1 || y0 >= y1) return;

    FsFile f = OSData::SD.open(self->path.c_str());
    if(!f) return;
    PaintCtx p = { ox + x0, oy + y0 };
    if(!ImageCodec::DecodeRgb565(f, self->info, x0, y0, x1 - x0, y1 - y0, self->opt, &PushToLcd, &p)){
        LOG_SYS_WARN("ImageView: %s をフルカラーで描けませんでした", self->path.c_str());
    }
    f.close();
}

void ImageView::armFullColor(const Rect& g){
    if(!this->isFullColorImage() || this->is_pressing){
        PICO_GFX::CancelDirectPaint(this);
        return;
    }
    const Rect area = this->clippedScreenRect().intersection(g);
    // 上に何か載っていれば4bppの絵のまま(直描きは上に載ったものを塗りつぶすため)。どいたら描き直しで来る
    if(WidgetFunctions::IsCoveredAbove(this, area)) return;
    PICO_GFX::ArmDirectPaint(&ImageView::paintFullColor, this, area);
}

void ImageView::causeOnPressStart(){
    Widget::causeOnPressStart();
    this->ref_touch_x = OSData::touchX;
    this->ref_touch_y = OSData::touchY;
    this->ref_off_x = this->off_x;
    this->ref_off_y = this->off_y;
    this->dragged = false;
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
    //ドラッグ中は4bppの絵だけを動かす(1pxごとにSDから解き直さない)
    this->dragged = true;
    PICO_GFX::CancelDirectPaint(this);
    this->needsRender();
}

void ImageView::causeOnPressEnd(){
    Widget::causeOnPressEnd();
    //窓だけを持っている場合は、止まった位置の窓を読み直す(ドラッグ中は読まない)
    if(this->loaded && !this->full && (this->off_x != this->win_x || this->off_y != this->win_y)){
        this->decodeWindow(this->off_x, this->off_y);
    }
    //動かしたなら、指を離したところでフルカラーで描き直す
    if(this->dragged && this->isFullColorImage()) this->needsRender();
    this->dragged = false;
}

void ImageView::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;
    // pico.set_palette()でパレットが変わっていたら、自前スプライトのパレットも合わせる
    if(this->loaded && this->palette_rev != PICO_GFX::paletteRevision){
        for(int i = 0; i < 16; i++) this->sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
        this->palette_rev = PICO_GFX::paletteRevision;
    }

    const Rect g = this->getScreenRect();
    markdirty(g);
    if(!Fill4bpp::FillRect(g.x, g.y, g.w, g.h, this->background_color)){
        OSData::frame->fillRect(g.x, g.y, g.w, g.h, this->background_color);
    }

    if(this->loaded){
        int ox, oy;
        this->imageOrigin(g, ox, oy);
        const int sx = ox + this->win_x;
        const int sy = oy + this->win_y;

        const bool transparent = this->info.alpha;
        //4bppどうしならバッファを直接写す(今のクリップと表示欄の重なりの中だけ)
        int32_t cx, cy, cw, ch;
        OSData::frame->getClipRect(&cx, &cy, &cw, &ch);
        const Rect orig = { (int16_t)cx, (int16_t)cy, (int16_t)cw, (int16_t)ch };
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
        this->armFullColor(g);
    }

    this->needs_redraw = false;
}
