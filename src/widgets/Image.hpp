#pragma once

#include "widgets/Widget.hpp"
#include "SdFat.h"

class Image : public Widget {
    private:
        String path;
        FsFile imgFile;

        void updatePath();

    public:
        Image(String path, int16_t x, int16_t y){
            this->path = path;
            this->rect.x = x;
            this->rect.y = y;
            this->updatePath();
        }

        void render() override;

        String Path() { return this->path; }
        void Path(String path) {
            this->path = path;
            this->updatePath();
            this->needsRender();
        }
};