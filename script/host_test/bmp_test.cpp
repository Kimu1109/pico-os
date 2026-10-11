// BMPの読み書き(gui/icons/Bmp_Codec)。スクリーンショットの保存(4bpp)と、.bmpの表示(4bppへ寄せる・フルカラーのまま)。
#include "gui/icons/Bmp_Codec.hpp"
#include "functions/GFX_Functions.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("[NG] %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

static void put16(std::string& s, uint16_t v){ s += (char)(v & 0xFF); s += (char)(v >> 8); }
static void put32(std::string& s, uint32_t v){ for(int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF); }

// 任意のBMPを組み立てる。pixels は上の行から、1画素=bppに応じたバイト列を作る関数で詰める
static std::string MakeBmp(int w, int h, int bpp, bool top_down, const std::vector<uint32_t>& pal,
                           uint32_t comp, const uint32_t* masks,
                           uint32_t (*px)(int x, int y)){
    const uint32_t row = ((w * bpp + 31) / 32) * 4;
    const uint32_t dib = 40;
    const uint32_t extra = (comp == 3) ? 12 : 0;
    const uint32_t off = 14 + dib + extra + (uint32_t)pal.size() * 4;
    std::string s = "BM";
    put32(s, off + row * h); put32(s, 0); put32(s, off);
    put32(s, dib); put32(s, (uint32_t)w); put32(s, (uint32_t)(top_down ? -h : h));
    put16(s, 1); put16(s, (uint16_t)bpp); put32(s, comp); put32(s, row * h);
    put32(s, 2835); put32(s, 2835); put32(s, (uint32_t)pal.size()); put32(s, 0);
    if(comp == 3){ put32(s, masks[0]); put32(s, masks[1]); put32(s, masks[2]); }
    for(uint32_t c : pal){ s += (char)(c & 0xFF); s += (char)((c >> 8) & 0xFF); s += (char)((c >> 16) & 0xFF); s += (char)0; }
    for(int fy = 0; fy < h; fy++){
        const int y = top_down ? fy : h - 1 - fy;
        std::string r(row, '\0');
        for(int x = 0; x < w; x++){
            const uint32_t v = px(x, y);
            switch(bpp){
                case 1: if(v) r[x >> 3] |= (char)(0x80 >> (x & 7)); break;
                case 4: r[x >> 1] |= (char)((x & 1) ? v : (v << 4)); break;
                case 8: r[x] = (char)v; break;
                case 16: r[x * 2] = (char)v; r[x * 2 + 1] = (char)(v >> 8); break;
                case 24: r[x * 3] = (char)v; r[x * 3 + 1] = (char)(v >> 8); r[x * 3 + 2] = (char)(v >> 16); break;
                case 32: for(int i = 0; i < 4; i++) r[x * 4 + i] = (char)(v >> (8 * i)); break;
            }
        }
        s += r;
    }
    return s;
}

static uint32_t Rgb(int r, int g, int b){ return (uint32_t)((r << 16) | (g << 8) | b); }

// パレットの番号(0〜15)の色
static uint32_t PalRgb(int i){
    uint8_t p[16][3];
    Bmp::CurrentPalette888(p);
    return Rgb(p[i][0], p[i][1], p[i][2]);
}

static uint32_t PxIndex(int x, int y){ return (uint32_t)((x + y * 3) & 15); }
static uint32_t PxColor24(int x, int y){ return PalRgb((x + y * 3) & 15); }
static uint32_t PxBit(int x, int y){ return (uint32_t)((x ^ y) & 1); }

struct Collect { std::vector<uint16_t> px; int w = 0; };
static bool CollectRow(void* ctx, int, const uint16_t* row, int w){
    Collect* c = static_cast<Collect*>(ctx);
    c->w = w;
    c->px.insert(c->px.end(), row, row + w);
    return true;
}

int main(){
    SdFat sd;
    // --- 4bpp(パレットはOSのもの)。上下の並び・窓 ---
    for(int td = 0; td < 2; td++){
        std::vector<uint32_t> pal;
        for(int i = 0; i < 16; i++) pal.push_back(PalRgb(i));
        HostSd::files["/a.bmp"] = MakeBmp(13, 7, 4, td != 0, pal, 0, nullptr, &PxIndex);
        FsFile f = sd.open("/a.bmp");
        Bmp::Info info;
        CHECK(Bmp::ReadInfo(f, info));
        CHECK(info.width == 13 && info.height == 7 && info.bpp == 4 && info.top_down == (td != 0));
        LGFX_Sprite sp;
        sp.createSprite(13, 7);
        CHECK(Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 13, 7));
        bool same = true;
        for(int y = 0; y < 7; y++) for(int x = 0; x < 13; x++) if((int)sp.readPixelValue(x, y) != (int)PxIndex(x, y)) same = false;
        CHECK(same);
        // 窓
        LGFX_Sprite win;
        win.createSprite(4, 3);
        CHECK(Bmp::DecodeTo4bpp(f, info, win, 5, 2, 4, 3));
        same = true;
        for(int y = 0; y < 3; y++) for(int x = 0; x < 4; x++) if((int)win.readPixelValue(x, y) != (int)PxIndex(x + 5, y + 2)) same = false;
        CHECK(same);
        f.close();
    }

    // --- 4bppで違うパレット: raw_indicesなら番号のまま、無ければ近い色へ ---
    {
        std::vector<uint32_t> pal(16, Rgb(255, 255, 255));   // 全部白
        HostSd::files["/w.bmp"] = MakeBmp(4, 2, 4, false, pal, 0, nullptr, &PxIndex);
        FsFile f = sd.open("/w.bmp");
        Bmp::Info info;
        CHECK(Bmp::ReadInfo(f, info));
        LGFX_Sprite sp; sp.createSprite(4, 2);
        CHECK(Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 4, 2));
        CHECK(sp.readPixelValue(1, 0) == 15 && sp.readPixelValue(3, 1) == 15);
        Bmp::Options o; o.raw_indices = true;
        CHECK(Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 4, 2, o));
        CHECK(sp.readPixelValue(1, 0) == 1 && sp.readPixelValue(3, 1) == 6);
        f.close();
    }

    // --- 24bpp・32bpp・16bpp(555/565)・1bpp ---
    {
        HostSd::files["/c.bmp"] = MakeBmp(9, 5, 24, false, {}, 0, nullptr, &PxColor24);
        FsFile f = sd.open("/c.bmp");
        Bmp::Info info;
        CHECK(Bmp::ReadInfo(f, info));
        LGFX_Sprite sp; sp.createSprite(9, 5);
        CHECK(Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 9, 5));
        bool same = true;
        for(int y = 0; y < 5; y++) for(int x = 0; x < 9; x++) if((int)sp.readPixelValue(x, y) != (int)PxIndex(x, y)) same = false;
        CHECK(same);
        // フルカラーのまま
        Collect c;
        std::vector<uint16_t> row(9);
        CHECK(Bmp::DecodeRgb565(f, info, 0, 0, 9, 5, row.data(), &CollectRow, &c));
        CHECK(c.px.size() == 45);
        CHECK(c.px[0] == (uint16_t)PICO_GFX::COLORS[0] && c.px[9 + 1] == (uint16_t)PICO_GFX::COLORS[4]);
        // ディザでも落ちない・パレットの色そのものは同じ色になる
        Bmp::Options o; o.dither = true;
        CHECK(Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 9, 5, o));
        CHECK(sp.readPixelValue(0, 0) == 0);
        f.close();
    }
    {
        HostSd::files["/d.bmp"] = MakeBmp(3, 2, 32, true, {}, 0, nullptr, &PxColor24);
        FsFile f = sd.open("/d.bmp");
        Bmp::Info info;
        CHECK(Bmp::ReadInfo(f, info));
        LGFX_Sprite sp; sp.createSprite(3, 2);
        CHECK(Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 3, 2));
        CHECK((int)sp.readPixelValue(2, 1) == (int)PxIndex(2, 1));
        f.close();
    }
    {
        // 565のBI_BITFIELDS。純粋な赤/緑/青/白
        static const uint32_t m565[3] = {0xF800, 0x07E0, 0x001F};
        HostSd::files["/e.bmp"] = MakeBmp(4, 1, 16, false, {}, 3, m565,
            [](int x, int)->uint32_t{ const uint32_t v[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF}; return v[x]; });
        FsFile f = sd.open("/e.bmp");
        Bmp::Info info;
        CHECK(Bmp::ReadInfo(f, info));
        Collect c;
        std::vector<uint16_t> row(4);
        CHECK(Bmp::DecodeRgb565(f, info, 0, 0, 4, 1, row.data(), &CollectRow, &c));
        CHECK(c.px.size() == 4 && c.px[0] == 0xF800 && c.px[1] == 0x07E0 && c.px[2] == 0x001F && c.px[3] == 0xFFFF);
        f.close();
        // 555(BI_RGBの16bpp)
        HostSd::files["/e2.bmp"] = MakeBmp(2, 1, 16, false, {}, 0, nullptr,
            [](int x, int)->uint32_t{ return x ? 0x7FFF : 0x7C00; });
        f = sd.open("/e2.bmp");
        CHECK(Bmp::ReadInfo(f, info));
        Collect c2;
        CHECK(Bmp::DecodeRgb565(f, info, 0, 0, 2, 1, row.data(), &CollectRow, &c2));
        CHECK(c2.px.size() == 2 && c2.px[0] == 0xF800 && c2.px[1] == 0xFFFF);
        f.close();
    }
    {
        HostSd::files["/b.bmp"] = MakeBmp(10, 3, 1, false, {Rgb(0, 0, 0), Rgb(255, 255, 255)}, 0, nullptr, &PxBit);
        FsFile f = sd.open("/b.bmp");
        Bmp::Info info;
        CHECK(Bmp::ReadInfo(f, info));
        LGFX_Sprite sp; sp.createSprite(10, 3);
        CHECK(Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 10, 3));
        CHECK(sp.readPixelValue(0, 0) == 0 && sp.readPixelValue(1, 0) == 15 && sp.readPixelValue(9, 2) == 15);
        f.close();
    }

    // --- 断るもの ---
    {
        std::string s = MakeBmp(2, 2, 24, false, {}, 0, nullptr, &PxColor24);
        std::string rle = s; rle[30] = 1;      // BI_RLE8
        HostSd::files["/r.bmp"] = rle;
        FsFile f = sd.open("/r.bmp");
        Bmp::Info info;
        CHECK(!Bmp::ReadInfo(f, info));
        f.close();
        std::string bad = s; bad[0] = 'X';
        HostSd::files["/x.bmp"] = bad;
        f = sd.open("/x.bmp");
        CHECK(!Bmp::ReadInfo(f, info));
        f.close();
        std::string huge = s; huge[18] = 0; huge[19] = 0; huge[20] = 1; // 幅65536
        HostSd::files["/h.bmp"] = huge;
        f = sd.open("/h.bmp");
        CHECK(!Bmp::ReadInfo(f, info));
        f.close();
        // 途中で切れたファイル: 読めたところまでで止まる
        HostSd::files["/t.bmp"] = s.substr(0, s.size() - 4);
        f = sd.open("/t.bmp");
        CHECK(Bmp::ReadInfo(f, info));
        LGFX_Sprite sp; sp.createSprite(2, 2);
        CHECK(!Bmp::DecodeTo4bpp(f, info, sp, 0, 0, 2, 2));
        f.close();
    }

    // --- 書き出して読み直す(スクリーンショット) ---
    {
        LGFX_Sprite src;
        src.createSprite(7, 5);
        for(int y = 0; y < 5; y++) for(int x = 0; x < 7; x++) src.writePixel(x, y, (int)PxIndex(x, y));
        FsFile f = sd.open("/s.bmp", O_WRONLY | O_CREAT | O_TRUNC);
        CHECK(Bmp::Encode4bpp(src, 7, 5, f));
        f.close();
        const std::string& data = HostSd::files["/s.bmp"];
        CHECK(data.size() == 14 + 40 + 64 + 4 * 5);
        f = sd.open("/s.bmp");
        Bmp::Info info;
        CHECK(Bmp::ReadInfo(f, info));
        CHECK(info.bpp == 4 && info.palette_count == 16);
        LGFX_Sprite back; back.createSprite(7, 5);
        Bmp::Options o; o.raw_indices = true;
        CHECK(Bmp::DecodeTo4bpp(f, info, back, 0, 0, 7, 5, o));
        bool same = true;
        for(int y = 0; y < 5; y++) for(int x = 0; x < 7; x++) if(back.readPixelValue(x, y) != src.readPixelValue(x, y)) same = false;
        CHECK(same);
        // パレットでも同じになる(既定のパレットは16色とも違う色)
        CHECK(Bmp::DecodeTo4bpp(f, info, back, 0, 0, 7, 5));
        same = true;
        for(int y = 0; y < 5; y++) for(int x = 0; x < 7; x++) if(back.readPixelValue(x, y) != src.readPixelValue(x, y)) same = false;
        CHECK(same);
        f.close();
    }

    if(failures){ std::printf("bmp_test: %d件失敗\n", failures); return 1; }
    std::printf("bmp_test: OK\n");
    return 0;
}
