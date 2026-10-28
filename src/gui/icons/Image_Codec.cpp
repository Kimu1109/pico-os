#include "gui/icons/Image_Codec.hpp"
#include "gui/core/GFX_Functions.hpp"

#include <stdlib.h>
#include <string.h>

namespace ImageCodec {

namespace {

// 4x4のBayer行列(-8〜7。Bmp_Codec と同じ)
const int8_t kBayer[4][4] = {
    { -8,  0, -6,  2 },
    {  4, -4,  6, -2 },
    { -5,  3, -7,  1 },
    {  7, -1,  5, -3 },
};

// generate_pimg.py の ALPHA_THRESHOLD と同じ
constexpr int kAlphaThreshold = 96;
// パレットの色とこれ以下しか違わない画素にはディザを掛けない(Bmp::NearestIndex と同じ重みの距離。各色およそ8以内)
constexpr int32_t kExactDist = 8 * 8 * (3 + 4 + 2);

// 窓。画素を受け取る側の共通部分
struct Window {
    int x, y, w, h;
    // 行 y の [x0, x0+n) と窓の重なり。重ならなければfalse。窓の中の開始位置と、元の列の何番目からかを返す
    bool clipRow(int py, int px, int n, int& wx, int& skip, int& len) const {
        if(py < y || py >= y + h) return false;
        int a = px, b = px + n;
        if(a < x) a = x;
        if(b > x + w) b = x + w;
        if(a >= b) return false;
        wx = a - x;
        skip = a - px;
        len = b - a;
        return true;
    }
};

// 半透明の画素を下地の色と混ぜる
inline void Blend(int& r, int& g, int& b, int a, const uint8_t* bg){
    r = (r * a + bg[0] * (255 - a) + 127) / 255;
    g = (g * a + bg[1] * (255 - a) + 127) / 255;
    b = (b * a + bg[2] * (255 - a) + 127) / 255;
}

// ---- 4bppへ寄せる側 ----
struct Sink4 {
    LGFX_Sprite* dst;
    Window win;
    uint8_t pal[16][3];
    bool dither;
    bool transparent;
    uint8_t bg[3];

    uint8_t index(int r, int g, int b, int a, int ix, int iy) const {
        if(transparent){
            if(a < kAlphaThreshold) return 0;
        }else if(a < 255){
            Blend(r, g, b, a, bg);
        }
        const int first = transparent ? 1 : 0;
        if(!dither) return Bmp::NearestIndex(pal, r, g, b, first);
        const uint8_t plain = Bmp::NearestIndex(pal, r, g, b, first);
        const int dr = r - pal[plain][0], dg = g - pal[plain][1], db = b - pal[plain][2];
        if(dr * dr * 3 + dg * dg * 4 + db * db * 2 <= kExactDist) return plain;
        const int d = kBayer[iy & 3][ix & 3] * 4;
        return Bmp::NearestIndex(pal, r + d, g + d, b + d, first);
    }

    // 1行ぶん(px は1画素 stride バイト: 3=RGB, 4=RGBA)。同じ番号が続く所はまとめて横線で書く
    void row(int py, int px0, int n, const uint8_t* px, int stride){
        int wx, skip, len;
        if(!win.clipRow(py, px0, n, wx, skip, len)) return;
        const uint8_t* p = px + skip * stride;
        const int wy = py - win.y;
        int run_start = 0;
        uint8_t run_c = 0;
        for(int i = 0; i < len; i++, p += stride){
            const uint8_t c = index(p[0], p[1], p[2], stride == 4 ? p[3] : 255, px0 + skip + i, py);
            if(i == 0){ run_c = c; continue; }
            if(c != run_c){
                dst->drawFastHLine(wx + run_start, wy, i - run_start, run_c);
                run_start = i;
                run_c = c;
            }
        }
        dst->drawFastHLine(wx + run_start, wy, len - run_start, run_c);
    }
};

bool Jpeg4(void* ctx, int x, int y, int w, int h, const uint8_t* rgb){
    Sink4* s = static_cast<Sink4*>(ctx);
    for(int r = 0; r < h; r++) s->row(y + r, x, w, rgb + (size_t)r * w * 3, 3);
    return true;
}

bool Png4(void* ctx, int x, int y, int n, const uint8_t* rgba){
    static_cast<Sink4*>(ctx)->row(y, x, n, rgba, 4);
    return true;
}

// ---- フルカラーのまま渡す側 ----
struct Sink565 {
    Rgb565Fn fn;
    void* user;
    Window win;
    uint8_t bg[3];
    uint16_t buf[16 * 16];   // JPEG の四角(最大16x16)・PNG の32画素ぶん

    static uint16_t To565(int r, int g, int b){
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    // 四角(行の幅 w、1画素 stride バイト)の窓に重なる所を渡す
    void block(int x, int y, int w, int h, const uint8_t* px, int stride){
        int y0 = y, y1 = y + h;
        if(y0 < win.y) y0 = win.y;
        if(y1 > win.y + win.h) y1 = win.y + win.h;
        int wx, skip, len;
        if(y0 >= y1 || !win.clipRow(y0, x, w, wx, skip, len)) return;
        const int rows = y1 - y0;
        uint16_t* o = buf;
        for(int r = 0; r < rows; r++){
            const uint8_t* p = px + ((size_t)(y0 - y + r) * w + skip) * stride;
            for(int i = 0; i < len; i++, p += stride){
                int cr = p[0], cg = p[1], cb = p[2];
                if(stride == 4 && p[3] < 255) Blend(cr, cg, cb, p[3], bg);
                *o++ = To565(cr, cg, cb);
            }
        }
        fn(user, wx, y0 - win.y, len, rows, buf);
    }
};

bool Jpeg565(void* ctx, int x, int y, int w, int h, const uint8_t* rgb){
    static_cast<Sink565*>(ctx)->block(x, y, w, h, rgb, 3);
    return true;
}

bool Png565(void* ctx, int x, int y, int n, const uint8_t* rgba){
    static_cast<Sink565*>(ctx)->block(x, y, n, 1, rgba, 4);
    return true;
}

struct BmpRowCtx {
    Rgb565Fn fn;
    void* user;
};

bool BmpRow565(void* ctx, int y, const uint16_t* rgb565, int w){
    BmpRowCtx* c = static_cast<BmpRowCtx*>(ctx);
    c->fn(c->user, 0, y, w, 1, rgb565);
    return true;
}

void PaletteColor888(uint8_t index, uint8_t* out){
    uint8_t pal[16][3];
    Bmp::CurrentPalette888(pal);
    memcpy(out, pal[index & 0x0F], 3);
}

}  // namespace

bool ReadInfo(FsFile& f, Info& out){
    out = Info();
    uint8_t head[8] = {0};
    if(!f.seek(0)) return false;
    const int got = f.read(head, sizeof(head));
    if(got < 2) return false;

    if(head[0] == 'B' && head[1] == 'M'){
        if(!Bmp::ReadInfo(f, out.bmp)) return false;
        out.format = Format::Bmp;
        out.width = out.bmp.width;
        out.height = out.bmp.height;
    }else if(Jpeg::LooksLikeJpeg(head, (size_t)got)){
        Jpeg::Info j;
        if(!Jpeg::ReadInfo(f, j)) return false;
        out.format = Format::Jpeg;
        out.width = j.width;
        out.height = j.height;
    }else if(Png::LooksLikePng(head, (size_t)got)){
        if(!Png::ReadInfo(f, out.png)) return false;
        out.format = Format::Png;
        out.width = out.png.width;
        out.height = out.png.height;
        out.alpha = out.png.alpha;
    }else{
        if(!IconRender::ReadPimgHeader(f, out.pimg)) return false;
        out.format = Format::Pimg;
        out.width = out.pimg.width;
        out.height = out.pimg.height;
        out.alpha = out.pimg.flags & IconRender::kPimgFlagTransparent;
    }
    return out.width > 0 && out.height > 0;
}

bool IsFullColor(const Info& info){
    switch(info.format){
        case Format::Jpeg:
        case Format::Png: return true;
        case Format::Bmp: return info.bmp.bpp >= 8;
        default: return false;
    }
}

uint8_t FitScale(const Info& info, int max_w, int max_h, size_t max_bytes){
    if(!CanScale(info)) return 0;
    for(uint8_t s = 0; s < 3; s++){
        const int w = ScaledSize(info.width, s), h = ScaledSize(info.height, s);
        const bool fit_w = max_w <= 0 || w <= max_w;
        const bool fit_h = max_h <= 0 || h <= max_h;
        const bool fit_b = max_bytes == 0 || Bytes4bpp(w, h) <= max_bytes;
        if(fit_w && fit_h && fit_b) return s;
    }
    return 3;
}

bool UsesTransparency(const Info& info, const Options& opt){
    if(info.format == Format::Pimg) return info.alpha;
    if(info.format != Format::Png) return false;
    return opt.transparent < 0 ? info.alpha : opt.transparent > 0;
}

bool DecodeTo4bpp(FsFile& f, const Info& info, LGFX_Sprite& dst, int x, int y, int w, int h, const Options& opt){
    if(w <= 0 || h <= 0) return true;
    switch(info.format){
        case Format::Pimg:
            return IconRender::DecodePimgWindow(f, info.pimg, dst, x, y, w, h);
        case Format::Bmp: {
            Bmp::Options bo;
            bo.dither = opt.dither < 0 ? info.bmp.bpp > 8 : opt.dither > 0;
            bo.raw_indices = opt.raw_indices;
            return Bmp::DecodeTo4bpp(f, info.bmp, dst, x, y, w, h, bo);
        }
        case Format::Jpeg:
        case Format::Png: {
            Sink4 s{};
            s.dst = &dst;
            s.win = {x, y, w, h};
            Bmp::CurrentPalette888(s.pal);
            s.dither = opt.dither != 0;
            s.transparent = UsesTransparency(info, opt);
            memcpy(s.bg, s.pal[opt.background & 0x0F], 3);
            const int stop_y = y + h - 1;
            if(info.format == Format::Jpeg) return Jpeg::Decode(f, opt.scale, &Jpeg4, &s, stop_y);
            return Png::Decode(f, opt.scale, &Png4, &s, stop_y);
        }
        default:
            return false;
    }
}

bool LoadToSprite(FsFile& f, const Info& info, IconRender::PimgSprite& out, const Options& opt){
    out.usable = false;
    if(info.format == Format::Pimg) return IconRender::LoadPimgToSprite(f, out);

    const uint8_t scale = CanScale(info) ? (opt.scale > 3 ? 3 : opt.scale) : 0;
    out.width = ScaledW(info, scale);
    out.height = ScaledH(info, scale);
    out.transparent = UsesTransparency(info, opt);
    out.sprite.setColorDepth(4);
    if(!out.sprite.createSprite(out.width, out.height)) return false;
    for(int i = 0; i < 16; i++) out.sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
    // 透過の絵は届かなかった画素も透過にしておく
    out.sprite.fillScreen(out.transparent ? 0 : (opt.background & 0x0F));
    Options o = opt;
    o.scale = scale;
    if(!DecodeTo4bpp(f, info, out.sprite, 0, 0, out.width, out.height, o)){
        out.sprite.deleteSprite();
        return false;
    }
    out.usable = true;
    return true;
}

bool DecodeRgb565(FsFile& f, const Info& info, int x, int y, int w, int h, const Options& opt,
                  Rgb565Fn fn, void* ctx){
    if(w <= 0 || h <= 0) return true;
    switch(info.format){
        case Format::Bmp: {
            uint16_t* row = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)w);
            if(!row) return false;
            BmpRowCtx c = { fn, ctx };
            const bool ok = Bmp::DecodeRgb565(f, info.bmp, x, y, w, h, row, &BmpRow565, &c);
            free(row);
            return ok;
        }
        case Format::Jpeg:
        case Format::Png: {
            // buf が大きい(512B)のでスタックに置かない
            Sink565* s = (Sink565*)malloc(sizeof(Sink565));
            if(!s) return false;
            s->fn = fn;
            s->user = ctx;
            s->win = {x, y, w, h};
            PaletteColor888(opt.background, s->bg);
            const int stop_y = y + h - 1;
            const bool ok = info.format == Format::Jpeg
                ? Jpeg::Decode(f, opt.scale, &Jpeg565, s, stop_y)
                : Png::Decode(f, opt.scale, &Png565, s, stop_y);
            free(s);
            return ok;
        }
        default:
            return false;
    }
}

}  // namespace ImageCodec
