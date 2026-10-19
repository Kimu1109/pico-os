#include "gui/icons/Jpeg_Codec.hpp"
#include "gui/icons/Bmp_Codec.hpp"
#include "gui/core/GFX_Functions.hpp"

#include <stdlib.h>
#include <string.h>

// 実機・PC/Webビルドは LovyanGFX の中のもの、ホストテストは script/host_test/stubs/lgfx/utility/ の写し
#include <lgfx/utility/lgfx_tjpgd.h>

namespace Jpeg {

namespace {

// TJpgDec の作業場所(LovyanGFX の drawJpg と同じ大きさ。ハフマン表・量子化表・MCU・入力の溜め場所)
constexpr uint16_t kPoolBytes = 3900;

// 4x4のBayer行列(-8〜7。Bmp_Codec と同じ)
const int8_t kBayer[4][4] = {
    { -8,  0, -6,  2 },
    {  4, -4,  6, -2 },
    { -5,  3, -7,  1 },
    {  7, -1,  5, -3 },
};

struct Ctx {
    FsFile* file;
    LGFX_Sprite* dst;
    uint8_t pal[16][3];
    bool dither;
};

// 入力: buf が nullptr なら n バイト読み飛ばす
uint32_t ReadInput(void* device, uint8_t* buf, uint32_t n){
    Ctx* c = static_cast<Ctx*>(device);
    if(buf){
        const int got = c->file->read(buf, n);
        return got > 0 ? (uint32_t)got : 0;
    }
    uint8_t tmp[64];
    uint32_t done = 0;
    while(done < n){
        const uint32_t want = (n - done) < sizeof(tmp) ? (n - done) : (uint32_t)sizeof(tmp);
        const int got = c->file->read(tmp, want);
        if(got <= 0) break;
        done += (uint32_t)got;
    }
    return done;
}

// 出力: rect の範囲の RGB888(r,g,b の順、行の幅は rect の幅)を16色へ寄せてスプライトへ書く
uint32_t WriteOutput(void* device, void* bitmap, JRECT* rect){
    Ctx* c = static_cast<Ctx*>(device);
    const uint8_t* p = static_cast<const uint8_t*>(bitmap);
    const int w = (int)(rect->right - rect->left + 1);
    for(uint32_t y = rect->top; y <= rect->bottom; y++){
        int run_start = 0;
        uint8_t run_c = 0;
        for(int i = 0; i < w; i++, p += 3){
            int r = p[0], g = p[1], b = p[2];
            if(c->dither){
                const int d = kBayer[y & 3][(rect->left + i) & 3] * 4;
                r += d; g += d; b += d;
            }
            const uint8_t idx = Bmp::NearestIndex(c->pal, r, g, b);
            if(i == 0){ run_c = idx; continue; }
            if(idx != run_c){
                c->dst->drawFastHLine((int)rect->left + run_start, (int)y, i - run_start, run_c);
                run_start = i;
                run_c = idx;
            }
        }
        c->dst->drawFastHLine((int)rect->left + run_start, (int)y, w - run_start, run_c);
    }
    return 1;
}

}  // namespace

bool LooksLikeJpeg(const uint8_t* head, size_t n){
    return n >= 2 && head[0] == 0xFF && head[1] == 0xD8;
}

bool ReadInfo(FsFile& f, Info& out){
    if(!f.seekSet(0)) return false;
    uint8_t* pool = (uint8_t*)malloc(kPoolBytes);
    if(!pool) return false;
    Ctx ctx{};
    ctx.file = &f;
    lgfxJdec jd;
    const JRESULT r = lgfx_jd_prepare(&jd, &ReadInput, pool, kPoolBytes, &ctx);
    if(r == JDR_OK){
        out.width = jd.width;
        out.height = jd.height;
    }
    free(pool);
    return r == JDR_OK && out.width > 0 && out.height > 0;
}

uint8_t FitScale(const Info& info, int max_w, int max_h){
    for(uint8_t s = 0; s < 3; s++){
        const bool fit_w = max_w <= 0 || ScaledSize(info.width, s) <= max_w;
        const bool fit_h = max_h <= 0 || ScaledSize(info.height, s) <= max_h;
        if(fit_w && fit_h) return s;
    }
    return 3;
}

bool LoadToSprite(FsFile& f, IconRender::PimgSprite& out, const Options& opt){
    out.usable = false;
    if(!f.seekSet(0)) return false;
    const uint8_t scale = opt.scale > 3 ? 3 : opt.scale;

    uint8_t* pool = (uint8_t*)malloc(kPoolBytes);
    if(!pool) return false;
    Ctx ctx{};
    ctx.file = &f;
    ctx.dst = &out.sprite;
    ctx.dither = opt.dither;
    Bmp::CurrentPalette888(ctx.pal);

    lgfxJdec jd;
    bool ok = lgfx_jd_prepare(&jd, &ReadInput, pool, kPoolBytes, &ctx) == JDR_OK;
    if(ok){
        out.width = ScaledSize(jd.width, scale);
        out.height = ScaledSize(jd.height, scale);
        out.transparent = false;
        out.sprite.setColorDepth(4);
        ok = out.sprite.createSprite(out.width, out.height);
        if(ok){
            for(int i = 0; i < 16; i++) out.sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
            ok = lgfx_jd_decomp(&jd, &WriteOutput, scale) == JDR_OK;
            if(!ok) out.sprite.deleteSprite();
        }
    }
    free(pool);
    out.usable = ok;
    return ok;
}

}  // namespace Jpeg
