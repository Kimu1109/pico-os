#pragma once

#include "gui/widgets/Widget.hpp"

class FileSaveDialog : public Widget {
    private:
        char path[128];

    public:
        FileSaveDialog(const char* path){
            strncpy(this->path, path, sizeof(this->path) - 1);
        }

        void render() override;
};