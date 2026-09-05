#include "gui/widgets/FileExplorer.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "functions/Widget_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_IO.hpp"

void FileExplorer::update_list(){
    if(!OSData::SD_usable) return;

    this->list->clear();

    FsFile dir = OSData::SD.open(this->currentPath);
    FsFile file;

    const char* filename = PICO_IO::filename(this->currentPath);

    this->currentFolder->setText(filename[0] == '\0' ? "root" : filename);

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
    PICO_IO::parent(this->currentPath, this->currentPath);
    this->update_list();
}
void FileExplorer::on_press_create(){
    auto folder_create = new InputDialog("新しくフォルダを作成\n名前:", true);
    WidgetFunctions::AddDialog(folder_create);
    folder_create->setVisible(true);
    folder_create->setOnClosed([this, folder_create](bool is_submit){
        if(is_submit){
            char new_folder_path[256];
            PICO_IO::join(new_folder_path, this->currentPath, folder_create->getInput().c_str());
            OSData::SD.mkdir(new_folder_path);
            this->update_list();
        }
        WidgetFunctions::DestroyLater(folder_create);
    });
}
void FileExplorer::on_press_delete(){
    auto item = this->list->itemAt(this->list->getSelectedIndex());
    if(item){
        auto sure_dialog = new MsgDialog("本当に削除しますか?", "いいえ", "はい");
        WidgetFunctions::AddDialog(sure_dialog);
        sure_dialog->setVisible(true);
        sure_dialog->setOnClosed([this, sure_dialog, item](bool is_ok){
            if(is_ok){
                char delete_file_path[256];
                PICO_IO::join(delete_file_path, this->currentPath, item->text);

                FsFile file = OSData::SD.open(delete_file_path);
                if(file.isDir()){
                    file.close();
                    PICO_IO::removeRecursive(delete_file_path);
                }else{
                    file.close();
                    OSData::SD.remove(delete_file_path);
                }

                this->update_list();
            }
            WidgetFunctions::DestroyLater(sure_dialog);
        });
    }
}

void FileExplorer::on_press_item(int index){
    auto item = this->list->itemAt(index);
    if(item && item->icon == IconID::Folder){
        PICO_IO::join(this->currentPath, this->currentPath, item->text);
        this->update_list();
    }
}

const char* FileExplorer::getSelectedPath(){
    auto item = this->list->itemAt(this->list->getSelectedIndex());
    if(item){
        static char path[256];
        if(PICO_IO::join(path, this->currentPath, item->text)){
            return path;
        }else{
            return nullptr;
        }
    }else{
        return nullptr;
    }
}

const char* FileExplorer::getCurrentFolderPath(){
    return this->currentPath;
}

void FileExplorer::render(){
    
};