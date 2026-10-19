#include "gui/icons/Bmp_Codec.hpp"
#include "gui/core/GFX_Functions.hpp"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

namespace Bmp {

namespace {

constexpr uint32_t kBiRgb = 0;
constexpr uint32_t kBiBitfields = 3;
constexpr uint32_t kBiAlphaBitfields = 6;
constexpr uint32_t kMaxSide = 8192;       // 読む画像の一辺の上限(壊れた値で巨大な確保をしない)

uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
void wr16(uint8_t* p, uint16_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void wr32(uint8_t* p, uint32_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

// マスクの位置と幅(色を8bitへ広げるため)
struct Field { uint8_t shift; uint8_t bits; };
Field FieldOf(uint32_t mask){
    Field f = {0, 0};
    if(mask == 0) return f;
    while(!(mask & 1)){ mask >>= 1; f.shift++; }
    while(mask & 1){ mask >>= 1; f.bits++; }
    return f;
}
inline int Expand(uint32_t v, Field f){
    if(f.bits == 0) return 0;
    v = (v >> f.shift) & ((1u << f.bits) - 1);
    if(f.bits >= 8) return (int)(v >> (f.bits - 8));
    // 足りない下位ビットは上位を繰り返して埋める(5bitの31が255になるように)
    int out = (int)(v << (8 - f.bits));
    out |= out >> f.bits;
    if(f.bits < 4) out |= out >> (2 * f.bits);
    return out & 0xFF;
}

// 1行を読む作業場所(読み終えたら返す)
struct Reader {
    FsFile& f;
    const Info& info;
    uint8_t* row = nullptr;
    Field fr, fg, fb;
    uint8_t (*pal)[3] = nullptr;   // bpp<=8のときだけ確保(256色x3。スタックに置かない)

    Reader(FsFile& file, const Info& in) : f(file), info(in) {
        fr = FieldOf(info.mask[0]); fg = FieldOf(info.mask[1]); fb = FieldOf(info.mask[2]);
    }
    ~Reader(){ free(row); free(pal); }

    bool begin(){
        row = (uint8_t*)malloc(info.row_bytes);
        if(!row) return false;
        if(info.bpp <= 8){
            pal = (uint8_t (*)[3])calloc(256, 3);
            if(!pal) return false;
            const int n = info.palette_count > 256 ? 256 : info.palette_count;
            if(!f.seek(info.palette_offset)) return false;
            uint8_t q[4];
            for(int i = 0; i < n; i++){
                if(f.read(q, 4) != 4) return false;
                pal[i][0] = q[2]; pal[i][1] = q[1]; pal[i][2] = q[0];
            }
        }
        return true;
    }

    // 画像の上からy行目を読む
    bool load(int y){
        const uint32_t file_row = info.top_down ? (uint32_t)y : (uint32_t)(info.height - 1 - y);
        if(!f.seek(info.data_offset + file_row * info.row_bytes)) return false;
        return f.read(row, info.row_bytes) == (int)info.row_bytes;
    }

    // bpp<=8: パレットの番号
    int index(int x) const {
        switch(info.bpp){
            case 1: return (row[x >> 3] >> (7 - (x & 7))) & 1;
            case 4: return (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
            default: return row[x];
        }
    }

    void rgb(int x, int& r, int& g, int& b) const {
        if(info.bpp <= 8){
            const int i = index(x);
            r = pal[i][0]; g = pal[i][1]; b = pal[i][2];
            return;
        }
        if(info.bpp == 24){
            const uint8_t* p = row + x * 3;
            b = p[0]; g = p[1]; r = p[2];
            return;
        }
        const uint32_t v = (info.bpp == 16) ? rd16(row + x * 2) : rd32(row + x * 4);
        r = Expand(v, fr); g = Expand(v, fg); b = Expand(v, fb);
    }
};

// 4x4のBayer行列(-8〜7。ディザの揺らし幅に掛ける)
const int8_t kBayer[4][4] = {
    { -8,  0, -6,  2 },
    {  4, -4,  6, -2 },
    { -5,  3, -7,  1 },
    {  7, -1,  5, -3 },
};

}  // namespace

void CurrentPalette888(uint8_t (*out)[3]){
    for(int i = 0; i < 16; i++){
        const uint16_t c = (uint16_t)PICO_GFX::COLORS[i];
        const int r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
        out[i][0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        out[i][1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        out[i][2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
}

uint8_t NearestIndex(const uint8_t (*pal)[3], int r, int g, int b){
    // 人の目に合わせて緑を重く(おおよその輝度の重み)
    int best = 0;
    int32_t best_d = INT32_MAX;
    for(int i = 0; i < 16; i++){
        const int dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
        const int32_t d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if(d < best_d){ best_d = d; best = i; }
    }
    return (uint8_t)best;
}

bool ReadInfo(FsFile& f, Info& out){
    uint8_t h[54];
    if(!f.seek(0)) return false;
    if(f.read(h, 26) != 26) return false;
    if(h[0] != 'B' || h[1] != 'M') return false;
    const uint32_t data_offset = rd32(h + 10);
    const uint32_t dib = rd32(h + 14);
    if(dib < 40 || dib > 124) return false;   // BITMAPCOREHEADER(12)は扱わない
    if(f.read(h + 26, 28) != 28) return false;

    const int32_t w = (int32_t)rd32(h + 18);
    const int32_t hh = (int32_t)rd32(h + 22);
    const uint16_t planes = rd16(h + 26);
    const uint16_t bpp = rd16(h + 28);
    const uint32_t comp = rd32(h + 30);
    uint32_t colors = rd32(h + 46);

    if(planes != 1) return false;
    if(w <= 0 || (uint32_t)w > kMaxSide) return false;
    const uint32_t ah = (uint32_t)(hh < 0 ? -(int64_t)hh : hh);
    if(ah == 0 || ah > kMaxSide) return false;
    if(!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) return false;

    Info info;
    info.width = (uint16_t)w;
    info.height = (uint16_t)ah;
    info.bpp = (uint8_t)bpp;
    info.top_down = hh < 0;
    info.data_offset = data_offset;
    info.row_bytes = (((uint32_t)w * bpp + 31) / 32) * 4;

    if(comp == kBiRgb){
        if(bpp == 16){ info.mask[0] = 0x7C00; info.mask[1] = 0x03E0; info.mask[2] = 0x001F; }
        if(bpp == 32){ info.mask[0] = 0x00FF0000; info.mask[1] = 0x0000FF00; info.mask[2] = 0x000000FF; }
    }else if((comp == kBiBitfields || comp == kBiAlphaBitfields) && (bpp == 16 || bpp == 32)){
        // マスクはDIBの見出しの後ろ(40バイトの見出しなら直後に12バイト、V4/V5なら見出しの中)
        uint8_t m[12];
        if(!f.seek(14 + 40)) return false;
        if(f.read(m, 12) != 12) return false;
        info.mask[0] = rd32(m); info.mask[1] = rd32(m + 4); info.mask[2] = rd32(m + 8);
        if(!info.mask[0] || !info.mask[1] || !info.mask[2]) return false;
    }else{
        return false;   // RLE・JPEG・PNGは読まない
    }

    if(bpp <= 8){
        const uint32_t max_colors = 1u << bpp;
        if(colors == 0 || colors > max_colors) colors = max_colors;
        info.palette_count = (uint16_t)colors;
        info.palette_offset = 14 + dib + ((comp == kBiBitfields && dib == 40) ? 12 : 0);
    }

    // 画素がファイルに収まっているか(途中で切れたファイルはDecodeが途中で止まるだけなので、
    // ここでは始まりの位置だけ見る)
    if(data_offset < 14 + dib) return false;
    out = info;
    return true;
}

bool DecodeTo4bpp(FsFile& f, const Info& info, LGFX_Sprite& dst,
                  int src_x, int src_y, int w, int h, const Options& opt){
    if(src_x < 0){ w += src_x; src_x = 0; }
    if(src_y < 0){ h += src_y; src_y = 0; }
    if(src_x + w > info.width)  w = info.width - src_x;
    if(src_y + h > info.height) h = info.height - src_y;
    if(w <= 0 || h <= 0) return true;

    Reader rd(f, info);
    if(!rd.begin()) return false;

    uint8_t pal888[16][3];
    CurrentPalette888(pal888);

    // パレット付き: 番号→OSの番号を先に1回だけ決める
    uint8_t map[256];
    const bool indexed = info.bpp <= 8;
    if(indexed){
        for(int i = 0; i < 256; i++){
            if(opt.raw_indices && info.bpp <= 4) map[i] = (uint8_t)(i & 0x0F);
            else map[i] = NearestIndex(pal888, rd.pal[i][0], rd.pal[i][1], rd.pal[i][2]);
        }
    }

    for(int y = 0; y < h; y++){
        if(!rd.load(src_y + y)) return false;
        // 同じ番号が続く所はまとめて横線で書く(パレット付きの絵はたいてい長く続く)
        int run_start = 0;
        uint8_t run_c = 0;
        for(int x = 0; x < w; x++){
            uint8_t c;
            if(indexed){
                c = map[rd.index(src_x + x)];
            }else{
                int r, g, b;
                rd.rgb(src_x + x, r, g, b);
                if(opt.dither){
                    const int d = kBayer[(src_y + y) & 3][(src_x + x) & 3] * 4;
                    r += d; g += d; b += d;
                }
                c = NearestIndex(pal888, r, g, b);
            }
            if(x == 0){ run_c = c; continue; }
            if(c != run_c){
                dst.drawFastHLine(run_start, y, x - run_start, run_c);
                run_start = x;
                run_c = c;
            }
        }
        dst.drawFastHLine(run_start, y, w - run_start, run_c);
    }
    return true;
}

bool DecodeRgb565(FsFile& f, const Info& info, int src_x, int src_y, int w, int h,
                  uint16_t* row, RowFn fn, void* ctx){
    if(src_x < 0){ w += src_x; src_x = 0; }
    if(src_y < 0){ h += src_y; src_y = 0; }
    if(src_x + w > info.width)  w = info.width - src_x;
    if(src_y + h > info.height) h = info.height - src_y;
    if(w <= 0 || h <= 0) return true;

    Reader rd(f, info);
    if(!rd.begin()) return false;
    for(int y = 0; y < h; y++){
        if(!rd.load(src_y + y)) return false;
        for(int x = 0; x < w; x++){
            int r, g, b;
            rd.rgb(src_x + x, r, g, b);
            row[x] = (uint16_t)PICO_GFX::Rgb565(r, g, b);
        }
        if(!fn(ctx, y, row, w)) return false;
    }
    return true;
}

namespace {
struct SpriteCtx { LGFX_Sprite* dst; };
bool PutRow16(void* ctx, int y, const uint16_t* rgb, int w){
    LGFX_Sprite* s = static_cast<SpriteCtx*>(ctx)->dst;
    for(int x = 0; x < w; x++) s->writePixel(x, y, (uint16_t)rgb[x]);
    return true;
}
}

bool DecodeToSprite16(FsFile& f, const Info& info, LGFX_Sprite& dst,
                      int src_x, int src_y, int w, int h){
    if(w <= 0) return true;
    uint16_t* row = (uint16_t*)malloc(sizeof(uint16_t) * (size_t)w);
    if(!row) return false;
    SpriteCtx ctx = { &dst };
    const bool ok = DecodeRgb565(f, info, src_x, src_y, w, h, row, &PutRow16, &ctx);
    free(row);
    return ok;
}

bool LoadToSprite(FsFile& f, IconRender::PimgSprite& out, const Options& opt){
    out.usable = false;
    Info info;
    if(!ReadInfo(f, info)) return false;
    out.width = info.width;
    out.height = info.height;
    out.transparent = false;
    out.sprite.setColorDepth(4);
    if(!out.sprite.createSprite(info.width, info.height)) return false;
    for(int i = 0; i < 16; i++) out.sprite.setPaletteColor(i, PICO_GFX::COLORS[i]);
    if(!DecodeTo4bpp(f, info, out.sprite, 0, 0, info.width, info.height, opt)){
        out.sprite.deleteSprite();
        return false;
    }
    out.usable = true;
    return true;
}

bool Encode4bpp(LGFX_Sprite& src, uint16_t w, uint16_t h, FsFile& f){
    if(w == 0 || h == 0) return false;
    const uint32_t row_bytes = (((uint32_t)w * 4 + 31) / 32) * 4;
    const uint32_t header = 14 + 40 + 16 * 4;
    const uint32_t size = header + row_bytes * h;

    uint8_t hd[14 + 40];
    memset(hd, 0, sizeof(hd));
    hd[0] = 'B'; hd[1] = 'M';
    wr32(hd + 2, size);
    wr32(hd + 10, header);
    wr32(hd + 14, 40);
    wr32(hd + 18, w);
    wr32(hd + 22, h);            // 正 = 下の行から(どのソフトでも読める並び)
    wr16(hd + 26, 1);
    wr16(hd + 28, 4);
    wr32(hd + 30, kBiRgb);
    wr32(hd + 34, row_bytes * h);
    wr32(hd + 38, 2835);         // 72dpi
    wr32(hd + 42, 2835);
    wr32(hd + 46, 16);
    wr32(hd + 50, 16);
    if(f.write(hd, sizeof(hd)) != sizeof(hd)) return false;

    uint8_t pal888[16][3];
    CurrentPalette888(pal888);
    uint8_t pal[64];
    for(int i = 0; i < 16; i++){
        pal[i * 4 + 0] = pal888[i][2];
        pal[i * 4 + 1] = pal888[i][1];
        pal[i * 4 + 2] = pal888[i][0];
        pal[i * 4 + 3] = 0;
    }
    if(f.write(pal, sizeof(pal)) != sizeof(pal)) return false;

    uint8_t* row = (uint8_t*)malloc(row_bytes);
    if(!row) return false;
    bool ok = true;
    for(int y = h - 1; y >= 0 && ok; y--){
        memset(row, 0, row_bytes);
        for(int x = 0; x < w; x++){
            const uint8_t c = (uint8_t)(src.readPixelValue(x, y) & 0x0F);
            row[x >> 1] |= (x & 1) ? c : (uint8_t)(c << 4);
        }
        ok = f.write(row, row_bytes) == row_bytes;
    }
    free(row);
    return ok;
}

}  // namespace Bmp
