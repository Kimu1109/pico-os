#include "gui/widgets/Image.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/icons/icon_render.h"
#include "gui/icons/Image_Codec.hpp"
#include "OS_Data.hpp"

void Image::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;
    const bool use_sprite = this->onRAM || this->decoded;
    if(use_sprite){
        if(!this->sprite.usable) return;
    }else{
        if(!this->imgFile) return;
    }

    if(this->l_rect != this->prev_l_rect)
        markdirty(getScreenPrevRect());

    const Rect g_rect = this->getScreenRect();

    if(use_sprite){
        IconRender::DrawPimgSprite(this->sprite, g_rect.x, g_rect.y);
    }else{
        IconRender::DrawImageRLE4bpp(this->imgFile, g_rect.x, g_rect.y);
    }

    markdirty(g_rect);
    this->prev_l_rect.copy(this->l_rect);

    this->needs_redraw = false;
}

bool Image::DisplaySize(const char* path, int max_w, size_t max_bytes, uint16_t& w, uint16_t& h){
    FsFile f = OSData::SD.open(path);
    if(!f) return false;
    ImageCodec::Info info;
    const bool ok = ImageCodec::ReadInfo(f, info);
    f.close();
    if(!ok) return false;
    const uint8_t scale = ImageCodec::FitScale(info, max_w, 0, max_bytes);
    w = ImageCodec::ScaledW(info, scale);
    h = ImageCodec::ScaledH(info, scale);
    return true;
}

// 形式を見分けてスプライトへ解く(JPEG・PNG は max_w / max_bytes に収まるよう縮める)
bool Image::loadSprite(FsFile& f){
    ImageCodec::Info info;
    if(!ImageCodec::ReadInfo(f, info)) return false;
    ImageCodec::Options opt;
    opt.scale = ImageCodec::FitScale(info, this->max_w, 0, this->max_bytes);
    if(!ImageCodec::LoadToSprite(f, info, this->sprite, opt)) return false;
    this->l_rect.w = this->sprite.width;
    this->l_rect.h = this->sprite.height;
    return true;
}

void Image::updatePath(){
    // setPath()で同じウィジェットを使い回す呼び出し元(MarkdownViewのプール等)が、
    // 壊れたpimg/存在しないパスを踏んだ際に前回の画像の大きさを
    // 引きずらないよう、まず0へ戻してから読み直す
    this->l_rect.w = 0;
    this->l_rect.h = 0;
    if(this->decoded){
        this->sprite.sprite.deleteSprite();
        this->sprite.usable = false;
        this->decoded = false;
    }

    this->imgFile = OSData::SD.open(this->path.c_str());
    if(this->imgFile){
        ImageCodec::Info info;
        if(!ImageCodec::ReadInfo(this->imgFile, info)) return;
        if(info.format == ImageCodec::Format::Pimg){
            this->l_rect.w = info.width;
            this->l_rect.h = info.height;
            return;
        }
        // .pimg 以外は描くたびに解けないので、ここで1回だけ解いて持つ
        this->decoded = this->loadSprite(this->imgFile);
        this->imgFile.close();
    }
}

void Image::updateSprite(){
    this->l_rect.w = 0;
    this->l_rect.h = 0;

    this->imgFile = OSData::SD.open(this->path.c_str());

    if(this->imgFile){
        this->loadSprite(this->imgFile);
        this->imgFile.close();
    }
}
