#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/icons/Image_Codec.hpp"
#include "util/FixedString.hpp"

// 画像(.pimg / .bmp / .jpg / .png)の表示欄(汎用)。ファイルビューワーが使う。
// 形式はファイルの先頭で見分ける(ImageCodec)。JPEG・PNG は表示欄に収まる一番大きい段(1/1〜1/8)へ縮めて開く。
//
// 以前のファイルビューワーは Image(onRAM=false)をScrollContainerへ入れていたため、
// 描き直すたびに(=スクロールで1px動くたびに)SDから.pimgを頭から読み直してRLEを
// 1画素ずつ解いていた。ここでは**開いたときに1回だけ**4bppのスプライトへ解いておき、
// 描くときはそれをframeへ写すだけにする。
//   - 画像全体が kMaxFullBytes(4bppで w*h/2)に収まるなら全体を持つ
//   - 収まらない大きな画像は、表示欄と同じ大きさの「窓」だけを持つ。ドラッグ中は窓を
//     ずらして見せ、指を離したときに新しい位置の窓をSDから読み直す(その1回だけ読む)
// 上下左右のドラッグでスクロールする。表示欄より小さい画像は中央に置く。
//
// フルカラー(JPEG・PNG・8bpp以上のBMP): 4bppの絵(16色へ寄せたもの)を frame へ描いた上に、
// SDから解きながら液晶へそのままの色で直接描く(PICO_GFX::ArmDirectPaint。絵をメモリに持たない)。
// 直描きは描き直すたびに1回だけ。ドラッグ中と、上にダイアログ・オーバーレイ等が載っている間は
// 4bppの絵のまま(直描きは上に載ったものを塗りつぶしてしまうため)。setFullColor(false)で常に4bpp。
class ImageView : public Widget {
    public:
        static constexpr size_t kMaxFullBytes = 64 * 1024;

    private:
        FixedString<PICO_PATH_LEN> path;
        ImageCodec::Info info;
        ImageCodec::Options opt;      // 縮め方(scale)・下地の色
        uint16_t img_w = 0, img_h = 0; // 縮めた後の大きさ
        bool loaded = false;
        bool full_color = true;

        LGFX_Sprite sprite;
        uint32_t palette_rev = 0; // スプライトに反映済みのPICO_GFX::paletteRevision
        bool full = true;
        // スプライトが持つ範囲(画像座標)
        int win_x = 0, win_y = 0, win_w = 0, win_h = 0;
        // 表示欄の左上に来る画像座標
        int off_x = 0, off_y = 0;

        int ref_touch_x = 0, ref_touch_y = 0;
        int ref_off_x = 0, ref_off_y = 0;
        bool dragged = false;

        int maxOffX() const;
        int maxOffY() const;
        bool decodeWindow(int x, int y);
        // 画像の左上が来る画面の位置(表示欄より小さい画像は中央)
        void imageOrigin(const Rect& g, int& x, int& y) const;
        void armFullColor(const Rect& g);
        static void paintFullColor(void* ctx, const Rect& clip);

    public:
        ImageView(int16_t x, int16_t y, int16_t w, int16_t h) {
            this->l_rect = {x, y, w, h};
        }
        ~ImageView() override { this->unload(); }

        // 読み込めなければfalse(表示は空になる)
        bool load(const char* path);
        void unload();

        // 表示欄の大きさを変える。読み込み済みなら窓を取り直すために読み直す
        void setSize(int w, int h);
        void setW(int w){ this->setSize(w, this->l_rect.h); }
        void setH(int h){ this->setSize(this->l_rect.w, h); }
        const char* getPath() const { return this->path.c_str(); }

        bool isLoaded() const { return this->loaded; }
        // 見せている大きさ(縮めた後)
        int getImageW() const { return this->img_w; }
        int getImageH() const { return this->img_h; }
        // 元の大きさと縮め方(1 / 2 / 4 / 8)
        int getSourceW() const { return this->info.width; }
        int getSourceH() const { return this->info.height; }
        int getScaleDiv() const { return 1 << this->opt.scale; }
        ImageCodec::Format getFormat() const { return this->info.format; }
        // 画像全体をメモリに持っているか(falseなら窓だけ)
        bool isFullyLoaded() const { return this->full; }
        bool isBmp() const { return this->info.format == ImageCodec::Format::Bmp; }
        int getBmpBpp() const { return this->isBmp() ? this->info.bmp.bpp : 4; }

        // フルカラーで見せるか(既定はtrue。形式が16色に収まるもの=.pimg・4bpp以下のBMPは常に4bpp)
        void setFullColor(bool on);
        bool getFullColor() const { return this->full_color; }
        // 今フルカラーで見せられる画像か
        bool isFullColorImage() const { return this->loaded && this->full_color && ImageCodec::IsFullColor(this->info); }

        void setVisible(bool v) override;
        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::ImageView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
