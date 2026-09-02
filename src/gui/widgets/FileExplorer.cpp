#include "gui/widgets/FileExplorer.hpp"
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"

void FileExplorer::update_list(){
    if(!OSData::SD_usable) return;

    this->list->clear();

    FsFile dir = OSData::SD.open(this->currentPath);
    FsFile file;

    while (file.openNext(&dir, O_RDONLY))
    {
        char name[128];

        if (file.getName(name, sizeof(name)))
        {
            ScrollListTools::Item item;
            if (file.isDirectory())
                item.icon = IconID::Folder;
            else
                item.icon = IconID::File;

            strncpy(item.text, name, sizeof(item.text) - 1);

            this->list->add(item);
        }
        file.close();
    }

    dir.close();
}

void FileExplorer::on_press_back(){
    PICO_Path::parent(this->currentPath, this->currentPath);
    this->update_list();
}

void FileExplorer::on_press_item(int index){
    auto item = this->list->itemAt(index);
    if(item && item->icon == IconID::Folder){
        PICO_Path::join(this->currentPath, this->currentPath, item->text);
        this->update_list();
    }
}

void FileExplorer::render(){
    
};