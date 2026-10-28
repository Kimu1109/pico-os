#include "gui/icons/Jpeg_Codec.hpp"

#include <stdlib.h>
#include <string.h>

// 実機・PC/Webビルドは LovyanGFX の中のもの、ホストテストは script/host_test/stubs/lgfx/utility/ の写し
#include <lgfx/utility/lgfx_tjpgd.h>

namespace Jpeg {

namespace {

// TJpgDec の作業場所(LovyanGFX の drawJpg と同じ大きさ。ハフマン表・量子化表・MCU・入力の溜め場所)
constexpr uint16_t kPoolBytes = 3900;

struct Ctx {
    FsFile* file;
    BlockFn fn;
    void* user;
    int stop_y;
    bool stop;   // fn が打ち切った / stop_y を過ぎた
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

// 出力: rect の範囲の RGB888(行の幅は rect の幅)をそのまま渡す。0を返すと TJpgDec は JDR_INTR で止まる
uint32_t WriteOutput(void* device, void* bitmap, JRECT* rect){
    Ctx* c = static_cast<Ctx*>(device);
    if(c->stop_y >= 0 && (int)rect->top > c->stop_y){ c->stop = true; return 0; }
    const int w = (int)(rect->right - rect->left + 1);
    const int h = (int)(rect->bottom - rect->top + 1);
    if(!c->fn(c->user, (int)rect->left, (int)rect->top, w, h, static_cast<const uint8_t*>(bitmap))){
        c->stop = true;
        return 0;
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

bool Decode(FsFile& f, uint8_t scale, BlockFn fn, void* user, int stop_y){
    if(!f.seekSet(0)) return false;
    if(scale > 3) scale = 3;
    uint8_t* pool = (uint8_t*)malloc(kPoolBytes);
    if(!pool) return false;
    Ctx ctx{};
    ctx.file = &f;
    ctx.fn = fn;
    ctx.user = user;
    ctx.stop_y = stop_y;

    lgfxJdec jd;
    bool ok = lgfx_jd_prepare(&jd, &ReadInput, pool, kPoolBytes, &ctx) == JDR_OK;
    if(ok){
        const JRESULT r = lgfx_jd_decomp(&jd, &WriteOutput, scale);
        ok = r == JDR_OK || (r == JDR_INTR && ctx.stop);
    }
    free(pool);
    return ok;
}

}  // namespace Jpeg
