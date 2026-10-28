// 画像の窓口(gui/icons/Image_Codec)と PNG(gui/icons/Png_Codec)。
// 形式の見分け・PNGの色の種類ごとの読み取り・縮め方・窓・透過・フルカラーのまま(RGB565)。
#include "gui/icons/Image_Codec.hpp"
#include "gui/core/GFX_Functions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "png_test_data.hpp"
#include "jpeg_test_data.hpp"

static int failures = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("[NG] %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } }while(0)

template<size_t N>
static void Put(const char* path, const unsigned char (&data)[N], size_t n = N){
    HostSd::files[path] = std::string((const char*)data, n);
}

static int C_RED, C_GREEN, C_BLUE, C_WHITE, C_BLACK;

// 4分割の色が合っているか(各4分の1の真ん中を見る)
static bool QuadOk(LGFX_Sprite& s, int w, int h, int tl, int tr, int bl, int br){
    const int a = (int)s.readPixelValue(w / 4, h / 4), b = (int)s.readPixelValue(w * 3 / 4, h / 4);
    const int c = (int)s.readPixelValue(w / 4, h * 3 / 4), d = (int)s.readPixelValue(w * 3 / 4, h * 3 / 4);
    if(a == tl && b == tr && c == bl && d == br) return true;
    std::printf("  4分割 %d,%d,%d,%d (期待 %d,%d,%d,%d)\n", a, b, c, d, tl, tr, bl, br);
    return false;
}

static bool Load(const char* path, IconRender::PimgSprite& out, ImageCodec::Info& info, ImageCodec::Options opt = ImageCodec::Options()){
    SdFat sd;
    FsFile f = sd.open(path);
    if(!f) return false;
    if(!ImageCodec::ReadInfo(f, info)) return false;
    const bool ok = ImageCodec::LoadToSprite(f, info, out, opt);
    f.close();
    return ok;
}

struct Got565 {
    int w, h;
    std::vector<uint16_t> px;
    int calls = 0;
};

static void Collect565(void* ctx, int x, int y, int w, int h, const uint16_t* rgb){
    Got565* g = static_cast<Got565*>(ctx);
    g->calls++;
    for(int r = 0; r < h; r++) for(int i = 0; i < w; i++){
        const int px = x + i, py = y + r;
        if(px < 0 || py < 0 || px >= g->w || py >= g->h){ std::printf("  窓の外 %d,%d\n", px, py); failures++; continue; }
        g->px[(size_t)py * g->w + px] = rgb[r * w + i];
    }
}

static uint16_t To565(int r, int g, int b){ return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

int main(){
    // ホストのスタブは TFT_RED 等が色の番号そのもの(RGB565ではない)なので、本物の色のパレットにする
    static const uint8_t kRealPal[16][3] = {
        {0,0,0}, {0,0,128}, {0,128,0}, {0,128,128}, {128,0,0}, {128,0,128}, {128,128,0}, {192,192,192},
        {128,128,128}, {0,0,255}, {0,255,0}, {0,255,255}, {255,0,0}, {255,0,255}, {255,255,0}, {255,255,255},
    };
    for(int i = 0; i < 16; i++) PICO_GFX::COLORS[i] = PICO_GFX::Rgb565(kRealPal[i][0], kRealPal[i][1], kRealPal[i][2]);
    uint8_t pal[16][3];
    Bmp::CurrentPalette888(pal);
    C_RED = Bmp::NearestIndex(pal, 255, 0, 0);
    C_GREEN = Bmp::NearestIndex(pal, 0, 170, 0);
    C_BLUE = Bmp::NearestIndex(pal, 0, 0, 255);
    C_WHITE = Bmp::NearestIndex(pal, 255, 255, 255);
    C_BLACK = Bmp::NearestIndex(pal, 0, 0, 0);
    CHECK(C_RED == 12 && C_GREEN == 2 && C_BLUE == 9 && C_WHITE == 15 && C_BLACK == 0);
    // 透過の絵は0番を使わない
    CHECK(Bmp::NearestIndex(pal, 0, 0, 0, 1) != 0);

    Put("/q.png", kPngQuad32);
    Put("/q64.png", kPngQuad64x48);
    Put("/qi.png", kPngQuadInterlaced);
    Put("/q16.png", kPngQuad16bit);
    Put("/qa.png", kPngQuadAlpha);
    Put("/qp.png", kPngQuadPalette);
    Put("/g.png", kPngGray);
    Put("/trunc.png", kPngQuad64x48, 80);
    Put("/q.jpg", kJpegQuad32);
    Put("/q64.jpg", kJpegQuad64x48);
    HostSd::files["/junk.png"] = std::string("\x89PNG\r\n\x1a\n" "garbage-garbage-garbage", 31);

    // --- 形式の見分けと大きさ ---
    {
        SdFat sd;
        ImageCodec::Info info;
        FsFile f = sd.open("/q64.png");
        CHECK(ImageCodec::ReadInfo(f, info));
        CHECK(info.format == ImageCodec::Format::Png && info.width == 64 && info.height == 48 && !info.alpha);
        CHECK(ImageCodec::IsFullColor(info) && ImageCodec::CanScale(info));
        f.close();
        f = sd.open("/qa.png");
        CHECK(ImageCodec::ReadInfo(f, info) && info.alpha);
        f.close();
        f = sd.open("/qi.png");
        CHECK(ImageCodec::ReadInfo(f, info) && info.png.interlaced);
        f.close();
        f = sd.open("/q.jpg");
        CHECK(ImageCodec::ReadInfo(f, info) && info.format == ImageCodec::Format::Jpeg && info.width == 32);
        f.close();
        f = sd.open("/junk.png");
        CHECK(!ImageCodec::ReadInfo(f, info));
        f.close();

        // 縮め方: 幅・高さ・バイト数に収まる一番小さい段
        ImageCodec::Info png;
        png.format = ImageCodec::Format::Png;
        png.width = 640; png.height = 480;
        CHECK(ImageCodec::FitScale(png, 240, 300) == 2);          // 160x120
        CHECK(ImageCodec::FitScale(png, 0, 0) == 0);
        CHECK(ImageCodec::FitScale(png, 0, 0, 20000) == 2);       // 1/4 = 160x120 = 9600B
        CHECK(ImageCodec::FitScale(png, 0, 0, 5000) == 3);        // 1/8 = 80x60 = 2400B
        CHECK(ImageCodec::FitScale(png, 10, 10) == 3);            // 1/8でも収まらなければ3
        ImageCodec::Info pimg;
        pimg.format = ImageCodec::Format::Pimg;
        pimg.width = 640; pimg.height = 480;
        CHECK(ImageCodec::FitScale(pimg, 10, 10) == 0);           // 縮められない形式
    }

    // --- PNG の色の種類ごと(ディザ無し) ---
    {
        ImageCodec::Options nd;
        nd.dither = 0;
        const char* paths[] = {"/q.png", "/qi.png", "/q16.png"};
        const int sizes[] = {32, 32, 16};
        for(int i = 0; i < 3; i++){
            IconRender::PimgSprite s;
            ImageCodec::Info info;
            CHECK(Load(paths[i], s, info, nd));
            CHECK(s.usable && s.width == sizes[i] && s.height == sizes[i] && !s.transparent);
            CHECK(QuadOk(s.sprite, s.width, s.height, C_RED, C_GREEN, C_BLUE, C_WHITE));
        }
        // 既定(ディザあり)でも、パレットの色にほぼ等しい画素にはディザを掛けない(ドット絵が汚れない)
        {
            IconRender::PimgSprite s;
            ImageCodec::Info info;
            CHECK(Load("/q.png", s, info));
            bool flat = true;
            for(int y = 0; y < 16; y++) for(int x = 0; x < 16; x++) if((int)s.sprite.readPixelValue(x, y) != C_RED) flat = false;
            CHECK(flat);
        }
        // グレー
        {
            IconRender::PimgSprite s;
            ImageCodec::Info info;
            CHECK(Load("/g.png", s, info, nd));
            CHECK(s.width == 8 && (int)s.sprite.readPixelValue(1, 1) == C_BLACK && (int)s.sprite.readPixelValue(6, 6) == C_WHITE);
        }
    }

    // --- 透過: アルファの列(既定で透過)、tRNS(transparent=1で透過、無ければ下地と混ぜる) ---
    {
        IconRender::PimgSprite s;
        ImageCodec::Info info;
        ImageCodec::Options nd;
        nd.dither = 0;
        CHECK(Load("/qa.png", s, info, nd));
        CHECK(s.transparent);
        CHECK((int)s.sprite.readPixelValue(4, 4) == 0);          // α=0 → 透過(0番)
        CHECK((int)s.sprite.readPixelValue(24, 4) == C_GREEN);   // α=128(閾値96以上)→ 不透明の色
        CHECK((int)s.sprite.readPixelValue(24, 24) == C_WHITE);

        // 透過にしない: 下地(既定は背景=15番=白)と混ぜる
        IconRender::PimgSprite s2;
        ImageCodec::Options op = nd;
        op.transparent = 0;
        CHECK(Load("/qa.png", s2, info, op));
        CHECK(!s2.transparent);
        CHECK((int)s2.sprite.readPixelValue(4, 4) == C_WHITE);   // α=0 → 下地の白

        // パレット+tRNS: 既定では透過にしない(赤は下地の白)、transparent=1で透過
        IconRender::PimgSprite s3;
        CHECK(Load("/qp.png", s3, info, nd));
        CHECK(!s3.transparent && (int)s3.sprite.readPixelValue(2, 2) == C_WHITE && (int)s3.sprite.readPixelValue(12, 2) == C_GREEN);
        IconRender::PimgSprite s4;
        ImageCodec::Options ot = nd;
        ot.transparent = 1;
        CHECK(Load("/qp.png", s4, info, ot));
        CHECK(s4.transparent && (int)s4.sprite.readPixelValue(2, 2) == 0 && (int)s4.sprite.readPixelValue(12, 12) == C_WHITE);
    }

    // --- 縮める(インターレースでない=平均、インターレース=間引き) ---
    {
        for(int sc = 1; sc <= 3; sc++){
            IconRender::PimgSprite s;
            ImageCodec::Info info;
            ImageCodec::Options o;
            o.dither = 0;
            o.scale = (uint8_t)sc;
            CHECK(Load("/q64.png", s, info, o));
            CHECK(s.width == (64 >> sc) && s.height == (48 >> sc));
            CHECK(QuadOk(s.sprite, s.width, s.height, C_RED, C_GREEN, C_BLUE, C_WHITE));
            IconRender::PimgSprite si;
            CHECK(Load("/qi.png", si, info, o));
            CHECK(si.width == (32 >> sc));
            CHECK(QuadOk(si.sprite, si.width, si.height, C_RED, C_GREEN, C_BLUE, C_WHITE));
        }
    }

    // --- 窓(画像の一部だけ) ---
    {
        SdFat sd;
        ImageCodec::Info info;
        ImageCodec::Options o;
        o.dither = 0;
        const char* paths[] = {"/q64.png", "/q64.jpg"};
        for(const char* p : paths){
            FsFile f = sd.open(p);
            CHECK(ImageCodec::ReadInfo(f, info));
            LGFX_Sprite win;
            win.createSprite(8, 8);
            // 右上(緑)と右下(白)の境目をまたぐ窓
            CHECK(ImageCodec::DecodeTo4bpp(f, info, win, 40, 20, 8, 8, o));
            CHECK((int)win.readPixelValue(2, 0) == C_GREEN && (int)win.readPixelValue(2, 7) == C_WHITE);
            f.close();
        }
    }

    // --- フルカラーのまま(RGB565): 窓の中だけ・色はそのまま ---
    {
        SdFat sd;
        const char* paths[] = {"/q64.png", "/q64.jpg"};
        for(const char* p : paths){
            ImageCodec::Info info;
            FsFile f = sd.open(p);
            CHECK(ImageCodec::ReadInfo(f, info));
            Got565 g{20, 10, std::vector<uint16_t>(200, 0xDEAD)};
            CHECK(ImageCodec::DecodeRgb565(f, info, 22, 18, 20, 10, ImageCodec::Options(), &Collect565, &g));
            f.close();
            bool filled = true;
            for(uint16_t v : g.px) if(v == 0xDEAD) filled = false;
            CHECK(filled);
            // 窓の左上(22,18)=赤、右上(41,18)=緑、左下(22,27)=青、右下(41,27)=白
            const bool exact = info.format == ImageCodec::Format::Png;
            auto near = [exact](uint16_t got, int r, int gg, int b){
                if(exact) return got == To565(r, gg, b);
                const int gr = (got >> 11) << 3, ggg = ((got >> 5) & 0x3F) << 2, gb = (got & 0x1F) << 3;
                return std::abs(gr - r) < 40 && std::abs(ggg - gg) < 40 && std::abs(gb - b) < 40;
            };
            CHECK(near(g.px[0], 255, 0, 0));
            CHECK(near(g.px[19], 0, 170, 0));
            CHECK(near(g.px[9 * 20], 0, 0, 255));
            CHECK(near(g.px[9 * 20 + 19], 255, 255, 255));
        }
        // 半透明は下地と混ぜる(下地=白): α=0の左上は白、α=128の右上は緑と白の中間
        ImageCodec::Info info;
        FsFile f = sd.open("/qa.png");
        CHECK(ImageCodec::ReadInfo(f, info));
        Got565 g{32, 32, std::vector<uint16_t>(32 * 32, 0)};
        CHECK(ImageCodec::DecodeRgb565(f, info, 0, 0, 32, 32, ImageCodec::Options(), &Collect565, &g));
        f.close();
        CHECK(g.px[4 * 32 + 4] == To565(255, 255, 255));
        CHECK(g.px[4 * 32 + 24] == To565(127, 212, 127));
        CHECK(g.px[24 * 32 + 4] == To565(0, 0, 255));
        // 縮めて(1/2)
        f = sd.open("/q64.png");
        CHECK(ImageCodec::ReadInfo(f, info));
        ImageCodec::Options o;
        o.scale = 1;
        Got565 g2{32, 24, std::vector<uint16_t>(32 * 24, 0)};
        CHECK(ImageCodec::DecodeRgb565(f, info, 0, 0, 32, 24, o, &Collect565, &g2));
        f.close();
        CHECK(g2.px[0] == To565(255, 0, 0) && g2.px[23 * 32 + 31] == To565(255, 255, 255));
        // .pimg はフルカラーで持たない
        ImageCodec::Info pi;
        pi.format = ImageCodec::Format::Pimg;
        pi.width = pi.height = 4;
        Got565 g3{4, 4, std::vector<uint16_t>(16, 0)};
        CHECK(!ImageCodec::DecodeRgb565(f, pi, 0, 0, 4, 4, ImageCodec::Options(), &Collect565, &g3));
    }

    // --- 壊れた・途中で切れたPNG ---
    {
        IconRender::PimgSprite s;
        ImageCodec::Info info;
        CHECK(!Load("/trunc.png", s, info));
        CHECK(!s.usable);
    }

    if(failures){
        std::printf("image_codec_test: %d 件失敗\n", failures);
        return 1;
    }
    std::printf("image_codec_test: OK\n");
    return 0;
}
