#include "gui/scenes/RecentFilesScene.hpp"
#include "gui/scenes/FileViewerScene.hpp"
#include "gui/icons/File_Icon.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "storage/Recent_Files.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

void RecentFilesScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int top = content.y + MARGIN;

    this->back_button_ = new Button(content.x + MARGIN, top, TR("戻る"));
    this->back_button_->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button_->setFontSize(FontFn::Small);
    this->back_button_->setH(20 + Button::kFrameExtra);
    this->back_button_->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button_);
    const Rect bb = this->back_button_->getLocalRect();

    auto* title = new Label<PICO_STR_L>(bb.x + bb.w + MARGIN * 2, top + 3, TR("最近開いたファイル"));
    title->setFontSize(FontFn::Small);
    title->setHitTransparent(true);
    WidgetFunctions::Add(title);

    const int body_y = top + bb.h + MARGIN;
    this->list_ = new ScrollList(content.x, (int16_t)body_y, content.w, (int16_t)(content.y + content.h - body_y));
    this->list_->setFontSize(FontFn::Text); //一覧の文字は文字の大きさの設定に従う
    this->list_->setEnableIcon(true);
    this->list_->setOnSelectItem([this](int index, bool already){
        if(already) this->open(index);
    });
    WidgetFunctions::Add(this->list_);

    this->empty_label_ = new Label<PICO_STR_L>(content.x + MARGIN * 2, body_y + MARGIN,
                                               TR("まだありません。エディタやファイルアプリで開いたファイルがここに並びます"));
    this->empty_label_->setFontSize(FontFn::Small);
    this->empty_label_->setMaxWidth(content.w - MARGIN * 4);
    this->empty_label_->setTextColor(PICO_DARKGREY);
    this->empty_label_->setHitTransparent(true);
    WidgetFunctions::Add(this->empty_label_);

    this->reload();
}

void RecentFilesScene::onExit(){
    delete[] this->paths_;
    this->paths_ = nullptr;
    this->count_ = 0;
    this->back_button_ = nullptr;
    this->empty_label_ = nullptr;
    this->list_ = nullptr;
}

void RecentFilesScene::reload(){
    if(!this->paths_) this->paths_ = new FixedString<PICO_PATH_LEN>[RecentFiles::kMaxFiles];
    this->count_ = 0;
    if(this->list_) this->list_->clear();
    if(!this->paths_) return;
    const int n = RecentFiles::List(this->paths_, RecentFiles::kMaxFiles);
    for(int i = 0; i < n; i++){
        //消した・SDの外で動かしたファイルは、並べる前に忘れる
        if(!OSData::SD.exists(this->paths_[i].c_str())){
            RecentFiles::Forget(this->paths_[i].c_str());
            continue;
        }
        if(this->count_ != i) this->paths_[this->count_] = this->paths_[i];
        const char* p = this->paths_[this->count_].c_str();
        const char* name = PICO_IO::filename(p);
        ScrollListTools::Item item;
        item.icon = FileIcon::For(name, false);
        item.text.assign(name);
        const size_t dir_len = (size_t)(name - p);
        if(dir_len > 1){
            (void)item.text.append("  ");
            (void)item.text.append(p, dir_len - 1);
        }
        if(this->list_) this->list_->add(item);
        this->count_++;
    }
    if(this->empty_label_) this->empty_label_->setVisible(this->count_ == 0);
}

void RecentFilesScene::open(int index){
    if(index < 0 || index >= this->count_) return;
    SceneFunctions::Push(new FileViewerScene(this->paths_[index].c_str()));
}
