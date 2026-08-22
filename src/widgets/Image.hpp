#pragma once

#include "widgets/Widget.hpp"
#include "icons/icon_render.h"
#include "SdFat.h"

class Image : public Widget {
    private:
        String path;
        FsFile imgFile;
        bool onRAM;

        IconRender::PimgSprite sprite;

        void updatePath();
        void updateSprite();

    public:
        Image(String path, int16_t x, int16_t y, bool onRAM){
            this->path = path;
            this->l_rect = {x, y, 0, 0};
            this->onRAM = onRAM;
            if(onRAM){
                this->updateSprite();
            }else{
                this->updatePath();
            }
        }

        void render() override;

        String Path() { return this->path; }
        void Path(String path) {
            this->path = path;
            if(this->onRAM){
                this->updateSprite();
            }else{
                this->updatePath();
            }
            this->needsRender();
        }
};