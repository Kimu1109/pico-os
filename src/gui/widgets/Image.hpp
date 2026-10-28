#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/icons/icon_render.h"
#include "util/FixedString.hpp"
#include "SdFat.h"

// SDの画像を1枚置く。onRAM=false は .pimg を描くたびにSDから読む(RAMを使わない)、true は開いたときにスプライトへ解いて持つ。
// .bmp / .jpg / .png(ImageCodec)は onRAM に関わらずスプライトへ解いて持つ(描くたびに解くと重すぎるため)。
// そのとき JPEG・PNG は setMaxSize() の幅と大きさ(4bppのバイト数)に収まるよう 1/2〜1/8 に縮める
// (MarkdownView が本文の幅に合わせる。画像の数はプールの数までなので、持つ量は 枚数 x 上限 で決まる)。
class Image : public Widget {
    public:
        static constexpr size_t kDefaultMaxBytes = 32 * 1024;

        // path を開いたときの大きさ(.pimg はそのまま、JPEG・PNG は縮めた後)。読めなければfalse
        static bool DisplaySize(const char* path, int max_w, size_t max_bytes, uint16_t& w, uint16_t& h);

    private:
        FixedString<PICO_PATH_LEN> path;
        FsFile imgFile;
        bool onRAM;
        bool decoded = false;   // onRAM=false でも、.pimg 以外はスプライトに持つ
        int16_t max_w = 0;      // 0なら幅は見ない
        size_t max_bytes = kDefaultMaxBytes;

        IconRender::PimgSprite sprite;

        void updatePath();
        void updateSprite();
        bool loadSprite(FsFile& f);

    public:
        template<size_t N>
        Image(FixedString<N> path, int16_t x, int16_t y, bool onRAM){
            this->path.assign(path);
            this->l_rect = {x, y, 0, 0};
            this->onRAM = onRAM;
            if(onRAM){
                this->updateSprite();
            }else{
                this->updatePath();
            }
        }
        Image(const char* path, int16_t x, int16_t y, bool onRAM)
            : Image(FixedString<PICO_PATH_LEN>(path), x, y, onRAM) {}

        void render() override;

        // 次に開く画像から効く(今の画像は開き直さない)
        void setMaxSize(int max_w, size_t max_bytes = kDefaultMaxBytes){ this->max_w = (int16_t)max_w; this->max_bytes = max_bytes; }

        WidgetType getWidgetType() const override { return WidgetType::Image; }

        const FixedString<PICO_PATH_LEN>* getPath() { return &this->path; }
        template<size_t N>
        void setPath(FixedString<N> path) {
            this->path.assign(path);
            if(this->onRAM){
                this->updateSprite();
            }else{
                this->updatePath();
            }
            this->needsRender();
        }
        void setPath(const char* path) {
            this->setPath(FixedString<PICO_PATH_LEN>(path));
        }
};