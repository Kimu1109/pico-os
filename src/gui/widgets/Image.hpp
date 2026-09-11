#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/icons/icon_render.h"
#include "util/FixedString.hpp"
#include "SdFat.h"

class Image : public Widget {
    private:
        FixedString<PICO_PATH_LEN> path;
        FsFile imgFile;
        bool onRAM;

        IconRender::PimgSprite sprite;

        void updatePath();
        void updateSprite();

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