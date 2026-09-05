#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Icon.hpp"
#include "gui/widgets/ScrollList.hpp"

class FileExplorer : public Widget {
    private:
        std::vector<Widget*> children_;

        char currentPath[128] = "/";

        void update_list();
        void update_places(){
            this->list->setW(this->l_rect.w);
            this->list->setH(this->l_rect.h - 20);

            this->currentFolder->setMaxWidth(this->l_rect.w - 20 * 2);
            
            this->createFolder->setX(this->l_rect.w - 20);
            this->deleteFile->setX(this->l_rect.w - 20 * 2);
        }
        
        void on_press_back();
        void on_press_create();
        void on_press_delete();
        void on_press_item(int index);

        ScrollList* list;
        Icon* backToParent;
        Icon* createFolder;
        Icon* deleteFile;
        Label* currentFolder;

    public:
        FileExplorer(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};

            backToParent = new Icon(0, (20 - 16) / 2, IconID::ArrowLeft, IconSize::Px16);
            backToParent->setParent(this);
            backToParent->setOnPressStart([this](){
                this->on_press_back();
            });

            createFolder = new Icon(w - 20, (20 - 16) / 2, IconID::Folder, IconSize::Px16);
            createFolder->setParent(this);
            createFolder->setOnPressStart([this](){
                this->on_press_create();
            });

            deleteFile = new Icon(w - 20 * 2, (20 - 16) / 2, IconID::Trash, IconSize::Px16);
            deleteFile->setParent(this);
            deleteFile->setOnPressStart([this](){
                this->on_press_delete();
            });

            currentFolder = new Label(20, 0, "");
            currentFolder->setParent(this);
            currentFolder->setMaxWidth(w - 20 * 3);
            currentFolder->setMaxHeight(20);
            currentFolder->setFontSize(FontFn::Small);
            currentFolder->setTextAlign(TextAlign::Center);

            list = new ScrollList(0, 20, w, h - 20);
            list->setParent(this);
            list->setFontSize(FontFn::FontSize::Small);
            list->setEnableIcon(true);
            list->setOnSelectItem([this](int index, bool already_selected){
                if(already_selected)
                    this->on_press_item(index);
            });

            children_.push_back(backToParent);
            children_.push_back(createFolder);
            children_.push_back(deleteFile);
            children_.push_back(list);
            children_.push_back(currentFolder);

            this->update_list();
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        const char* getSelectedPath();
        const char* getCurrentFolderPath();

        void setCurrentFolderPath(const char* path){
            strncpy(currentPath, path, sizeof(currentPath) - 1);
            this->update_list();
        }

        void render() override;

        ~FileExplorer(){
            delete backToParent;
            delete createFolder;
            delete deleteFile;
            delete list;
            delete currentFolder;
        }
};