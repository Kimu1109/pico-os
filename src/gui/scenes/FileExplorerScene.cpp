#include "gui/scenes/FileExplorerScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "gui/scenes/FileViewerScene.hpp"

void FileExplorerScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    const int body_y = content.y + MARGIN + back_box.h + MARGIN;

    this->explorer = new FileExplorer(
        content.x, (int16_t)body_y,
        content.w, (int16_t)(content.y + content.h - body_y)
    );
    //ファイルを2回タップしたら中身を見る(ファイルビューワーを上へ積む。「戻る」でここへ戻る)
    this->explorer->setOnFileTap([](const char* path){
        SceneFunctions::Push(new FileViewerScene(path));
    });
    WidgetFunctions::Add(this->explorer);
}

void FileExplorerScene::onExit(){
    this->back_button = nullptr;
    this->explorer     = nullptr;
}
