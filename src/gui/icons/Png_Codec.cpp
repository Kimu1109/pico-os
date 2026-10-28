#include "gui/icons/Png_Codec.hpp"

#include <stdlib.h>
#include <string.h>

// 実機・PC/Webビルドは LovyanGFX の中のもの、ホストテストは script/host_test/stubs/lgfx/utility/ の写し
#include <lgfx/utility/lgfx_pngle.h>

namespace Png {

namespace {

constexpr int kChunk = 32;   // fn へ1回で渡す画素の数の上限(スタックの溜め場所 128B)

struct Ctx {
    FsFile* file;
    PixelFn fn;
    void* user;
    uint8_t scale;
    int stop_y;
    int width, height;     // 元の大きさ
    int out_w;             // 縮めた後の幅
    bool interlaced;
    bool stop = false;     // fn が打ち切った / stop_y まで渡し終えた(読むのをやめる)
    // 縮める(インターレースでない)ときの1行ぶんの足し算(r,g,b,a を out_w 個)
    uint16_t* acc = nullptr;
};

uint32_t ReadInput(void* user, uint8_t* buf, uint32_t n){
    Ctx* c = static_cast<Ctx*>(user);
    if(c->stop) return 0;   // pngle は「足りない」で止まる(Decode()が打ち切りとして扱う)
    if(buf){
        const int got = c->file->read(buf, n);
        return got > 0 ? (uint32_t)got : 0;
    }
    const uint32_t pos = (uint32_t)c->file->position();
    const uint32_t size = (uint32_t)c->file->size();
    const uint32_t k = (pos + n <= size) ? n : (pos < size ? size - pos : 0);
    if(!c->file->seek(pos + k)) return 0;
    return k;
}

bool Emit(Ctx* c, int x, int y, int n, const uint8_t* rgba){
    if(c->stop) return false;
    if(!c->fn(c->user, x, y, n, rgba)){
        c->stop = true;
        return false;
    }
    return true;
}

// pngle の画素(1画素 = a,r,g,b の4バイト)を r,g,b,a へ並べ替える
inline void ToRgba(const uint8_t* argb, uint8_t* out){
    out[0] = argb[1]; out[1] = argb[2]; out[2] = argb[3]; out[3] = argb[0];
}

// 縮めない: そのまま渡す(インターレースの途中は div_x おきに飛ぶので1画素ずつ)
void DrawDirect(void* user, uint32_t x, uint32_t y, uint_fast8_t div_x, size_t len, const uint8_t* argb){
    Ctx* c = static_cast<Ctx*>(user);
    if(c->stop) return;
    uint8_t buf[kChunk * 4];
    if(div_x == 1){
        size_t i = 0;
        while(i < len){
            const int n = (len - i) < (size_t)kChunk ? (int)(len - i) : kChunk;
            for(int k = 0; k < n; k++) ToRgba(argb + (i + k) * 4, buf + k * 4);
            if(!Emit(c, (int)(x + i), (int)y, n, buf)) return;
            i += n;
        }
    }else{
        for(size_t i = 0; i < len; i++){
            ToRgba(argb + i * 4, buf);
            if(!Emit(c, (int)(x + i * div_x), (int)y, 1, buf)) return;
        }
    }
    if(!c->interlaced && c->stop_y >= 0 && (int)y >= c->stop_y && x + len * div_x >= (uint32_t)c->width) c->stop = true;
}

// 縮める・インターレース: 四角の左上の1画素だけを取る
void DrawPoint(void* user, uint32_t x, uint32_t y, uint_fast8_t div_x, size_t len, const uint8_t* argb){
    Ctx* c = static_cast<Ctx*>(user);
    if(c->stop) return;
    const uint32_t mask = (1u << c->scale) - 1;
    if(y & mask) return;
    uint8_t buf[4];
    for(size_t i = 0; i < len; i++){
        const uint32_t px = x + i * div_x;
        if(px & mask) continue;
        ToRgba(argb + i * 4, buf);
        if(!Emit(c, (int)(px >> c->scale), (int)(y >> c->scale), 1, buf)) return;
    }
}

// 縮める・インターレースでない: 四角の中を足していき、四角の最後の行で平均を渡す
void DrawAverage(void* user, uint32_t x, uint32_t y, uint_fast8_t div_x, size_t len, const uint8_t* argb){
    (void)div_x;   // インターレースでなければ常に1
    Ctx* c = static_cast<Ctx*>(user);
    if(c->stop) return;
    const uint8_t s = c->scale;
    for(size_t i = 0; i < len; i++){
        uint16_t* a = c->acc + ((x + i) >> s) * 4;
        const uint8_t* p = argb + i * 4;
        a[0] += p[1]; a[1] += p[2]; a[2] += p[3]; a[3] += p[0];
    }
    if(x + len < (uint32_t)c->width) return;   // 行の途中

    const int div = 1 << s;
    const bool band_end = ((y + 1) & (uint32_t)(div - 1)) == 0 || (int)y + 1 >= c->height;
    if(!band_end) return;
    const int rows = (int)y - (int)(y & ~(uint32_t)(div - 1)) + 1;
    const int oy = (int)(y >> s);
    uint8_t buf[kChunk * 4];
    int ox = 0;
    while(ox < c->out_w){
        const int n = (c->out_w - ox) < kChunk ? (c->out_w - ox) : kChunk;
        for(int k = 0; k < n; k++){
            const int col = ox + k;
            int cols = c->width - col * div;
            if(cols > div) cols = div;
            const int cnt = cols * rows;
            const uint16_t* a = c->acc + col * 4;
            for(int ch = 0; ch < 4; ch++) buf[k * 4 + ch] = (uint8_t)((a[ch] + cnt / 2) / cnt);
        }
        if(!Emit(c, ox, oy, n, buf)) return;
        ox += n;
    }
    memset(c->acc, 0, sizeof(uint16_t) * 4 * (size_t)c->out_w);
    if(c->stop_y >= 0 && oy >= c->stop_y) c->stop = true;
}

uint32_t Rd32(const uint8_t* p){
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

}  // namespace

bool LooksLikePng(const uint8_t* head, size_t n){
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return n >= 8 && memcmp(head, kSig, 8) == 0;
}

bool ReadInfo(FsFile& f, Info& out){
    uint8_t h[29];
    if(!f.seek(0)) return false;
    if(f.read(h, sizeof(h)) != (int)sizeof(h)) return false;
    if(!LooksLikePng(h, 8) || memcmp(h + 12, "IHDR", 4) != 0) return false;
    const uint32_t w = Rd32(h + 16), hh = Rd32(h + 20);
    // 大きさは uint16_t に入るものだけ(この画面で扱う大きさには十分)
    if(w == 0 || hh == 0 || w > 0xFFFF || hh > 0xFFFF) return false;
    const uint8_t depth = h[24], ct = h[25];
    if(ct != 0 && ct != 2 && ct != 3 && ct != 4 && ct != 6) return false;
    if(h[26] != 0 || h[27] != 0 || h[28] > 1) return false;   // 圧縮・フィルタは0だけ、インターレースは0/1
    out.width = (uint16_t)w;
    out.height = (uint16_t)hh;
    out.color_type = ct;
    out.depth = depth;
    out.interlaced = h[28] == 1;
    out.alpha = ct == 4 || ct == 6;
    return true;
}

bool Decode(FsFile& f, uint8_t scale, PixelFn fn, void* user, int stop_y){
    Info info;
    if(!ReadInfo(f, info)) return false;
    if(!f.seek(0)) return false;
    if(scale > 3) scale = 3;

    Ctx ctx{};
    ctx.file = &f;
    ctx.fn = fn;
    ctx.user = user;
    ctx.scale = scale;
    ctx.stop_y = stop_y;
    ctx.width = info.width;
    ctx.height = info.height;
    ctx.out_w = ScaledSize(info.width, scale);
    ctx.interlaced = info.interlaced;

    lgfx_pngle_draw_callback_t draw = &DrawDirect;
    if(scale > 0){
        if(info.interlaced){
            draw = &DrawPoint;
        }else{
            ctx.acc = (uint16_t*)calloc((size_t)ctx.out_w * 4, sizeof(uint16_t));
            if(!ctx.acc) return false;
            draw = &DrawAverage;
        }
    }

    pngle_t* png = lgfx_pngle_new();
    if(!png){ free(ctx.acc); return false; }
    bool ok = lgfx_pngle_prepare(png, &ReadInput, &ctx) >= 0;
    if(ok){
        const int r = lgfx_pngle_decomp(png, draw);
        // 自分で読むのをやめた(fn が打ち切った・stop_y まで渡し終えた)ときは「足りない」で返ってくるが、それは成功
        ok = r >= 0 || ctx.stop;
    }
    lgfx_pngle_destroy(png);
    free(ctx.acc);
    return ok;
}

}  // namespace Png
