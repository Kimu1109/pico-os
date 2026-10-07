#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/icons/icon_render.h"
#include "util/FixedString.hpp"

// .pimg画像の表示欄(汎用)。ファイルビューワーが使う。
//
// 以前のファイルビューワーは Image(onRAM=false)をScrollContainerへ入れていたため、
// 描き直すたびに(=スクロールで1px動くたびに)SDから.pimgを頭から読み直してRLEを
// 1画素ずつ解いていた。ここでは**開いたときに1回だけ**スプライトへ解いておき、
// 描くときはそれをpushSprite()するだけにする。
//   - 画像全体が kMaxFullBytes(4bppで w*h/2)に収まるなら全体を持つ
//   - 収まらない大きな画像は、表示欄と同じ大きさの「窓」だけを持つ。ドラッグ中は窓を
//     ずらして見せ、指を離したときに新しい位置の窓をSDから読み直す(その1回だけ読む)
// 上下左右のドラッグでスクロールする。表示欄より小さい画像は中央に置く。
class ImageView : public Widget {
    public:
        static constexpr size_t kMaxFullBytes = 64 * 1024;

    private:
        FixedString<PICO_PATH_LEN> path;
        IconRender::PimgHeader header = {0, 0, 0};
        bool loaded = false;

        LGFX_Sprite sprite;
        uint32_t palette_rev = 0; // スプライトに反映済みのPICO_GFX::paletteRevision
        bool full = true;
        // スプライトが持つ範囲(画像座標)
        int win_x = 0, win_y = 0, win_w = 0, win_h = 0;
        // 表示欄の左上に来る画像座標
        int off_x = 0, off_y = 0;

        int ref_touch_x = 0, ref_touch_y = 0;
        int ref_off_x = 0, ref_off_y = 0;

        int maxOffX() const;
        int maxOffY() const;
        bool decodeWindow(int x, int y);

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
        int getImageW() const { return this->header.width; }
        int getImageH() const { return this->header.height; }
        // 画像全体をメモリに持っているか(falseなら窓だけ)
        bool isFullyLoaded() const { return this->full; }

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::ImageView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
