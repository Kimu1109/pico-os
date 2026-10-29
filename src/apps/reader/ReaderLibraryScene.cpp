#include "apps/reader/ReaderLibraryScene.hpp"
#include "apps/reader/ReaderScene.hpp"
#include "apps/reader/Reader_Store.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
    // 割合(0〜100)。大きさが分からなければ -1
    int Percent(const ReaderStore::BookRecord& r){
        if(r.size == 0) return -1;
        uint32_t pos = r.at.pos > r.size ? r.size : r.at.pos;
        return (int)((uint64_t)pos * 100 / r.size);
    }

    bool IsBookFile(const char* name){
        const char* dot = strrchr(name, '.');
        return dot && strcasecmp(dot, ".txt") == 0;
    }

    void TitleOf(const char* path, FixedString<PICO_STR_L>& out){
        const char* name = PICO_IO::filename(path);
        const char* dot = strrchr(name, '.');
        out.assign(name, dot ? (size_t)(dot - name) : strlen(name));
    }
}

void ReaderLibraryScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;
    const int btn_h = 20 + Button::kFrameExtra;

    this->back_button_ = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button_->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button_->setH(btn_h);
    this->back_button_->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button_);
    const Rect back = this->back_button_->getLocalRect();

    auto* title = new Label<PICO_STR_L>(back.x + back.w + MARGIN * 2, y0 + (back.h - 24) / 2, "本棚");
    title->setHitTransparent(true);
    WidgetFunctions::Add(title);

    this->open_button_ = new Button(0, y0, "開く");
    this->open_button_->setIcon(IconID::FolderOpen, IconSize::Px16);
    this->open_button_->setH(btn_h);
    this->open_button_->setX(content.x + content.w - MARGIN - this->open_button_->getLocalRect().w);
    this->open_button_->setOnPressEnd([this](){ this->openPicker(); });
    WidgetFunctions::Add(this->open_button_);

    int y = y0 + back.h + MARGIN;
    this->list_top_ = y;
    this->continue_button_ = new Button(content.x + MARGIN, y, "続きから読む");
    this->continue_button_->setFontSize(FontFn::Small);
    this->continue_button_->setW(content.w - MARGIN * 2);
    this->continue_button_->setH(34);
    WidgetFunctions::Add(this->continue_button_);
    y += this->continue_button_->getLocalRect().h + MARGIN;
    this->list_top_continue_ = y;
    this->list_bottom_ = content.y + content.h - MARGIN;

    this->list_ = new ScrollList(content.x + MARGIN, y, content.w - MARGIN * 2, content.y + content.h - MARGIN - y);
    this->list_->setFontSize(FontFn::Text); //一覧の文字は文字の大きさの設定に従う
    this->list_->setEnableIcon(true);
    this->list_->setOnSelectItem([this](int index, bool){
        if(index >= 0 && index < this->count_) this->openBook(this->paths_[index].c_str());
    });
    WidgetFunctions::Add(this->list_);

    this->empty_label_ = new Label<PICO_STR_L>(content.x + MARGIN * 2, y + MARGIN,
                                               "本がありません。SDの /books/ に青空文庫のテキスト(.txt)を置くか、[開く]から選んでください");
    this->empty_label_->setFontSize(FontFn::Small);
    this->empty_label_->setMaxWidth(content.w - MARGIN * 4);
    this->empty_label_->setTextColor(PICO_DARKGREY);
    this->empty_label_->setDisableAutoTextDecoration(true);
    this->empty_label_->setHitTransparent(true);
    WidgetFunctions::Add(this->empty_label_);

    this->reload();
}

void ReaderLibraryScene::onExit(){
    delete[] this->paths_;
    this->paths_ = nullptr;
    this->count_ = 0;
    this->back_button_ = this->open_button_ = this->continue_button_ = nullptr;
    this->empty_label_ = nullptr;
    this->list_ = nullptr;
    this->picker_ = nullptr;
}

void ReaderLibraryScene::reload(){
    this->list_->clear();
    if(!this->paths_) this->paths_ = new FixedString<PICO_PATH_LEN>[kMaxEntries];
    this->count_ = 0;
    this->recent_count_ = 0;
    if(!this->paths_) return;

    // 最近読んだ本
    ReaderStore::BookRecord* recs = new ReaderStore::BookRecord[kMaxEntries];
    int n = recs ? ReaderStore::ListBooks(recs, kMaxEntries) : 0;
    for(int i = 0; i < n && this->count_ < kMaxEntries; i++){
        const ReaderStore::BookRecord& r = recs[i];
        ScrollListTools::Item item;
        item.icon = IconID::Book;
        FixedString<PICO_STR_L> title;
        if(!r.title.empty()) title.assign(r.title.c_str());
        else TitleOf(r.path.c_str(), title);
        const int pct = Percent(r);
        char text[PICO_STR_LL];
        if(pct >= 0) snprintf(text, sizeof(text), "%s  %d%%", title.c_str(), pct);
        else snprintf(text, sizeof(text), "%s", title.c_str());
        item.text.assign(text);
        this->list_->add(item);
        this->paths_[this->count_++] = r.path;
    }
    this->recent_count_ = this->count_;

    // 続きから読む
    if(n > 0){
        const ReaderStore::BookRecord& r = recs[0];
        FixedString<PICO_STR_L> title;
        if(!r.title.empty()) title.assign(r.title.c_str());
        else TitleOf(r.path.c_str(), title);
        FixedString<PICO_STR_M> label;
        label.assign("続きから: ");
        (void)label.append(title.c_str());   // 長い題名は切れてよい
        this->continue_button_->setText(label.c_str());
        this->continue_path_ = r.path;
        this->continue_button_->setOnPressEnd([this](){ this->openBook(this->continue_path_.c_str()); });
        this->continue_button_->setVisible(true);
    }else{
        this->continue_button_->setVisible(false);
    }
    // 続きから読む本が無ければ、一覧をその分だけ上へ広げる
    const int top = n > 0 ? this->list_top_continue_ : this->list_top_;
    this->list_->setY(top);
    this->list_->setH(this->list_bottom_ - top);
    this->empty_label_->setY(top + MARGIN);

    // /books/ のまだ開いていない本
    if(OSData::SD_usable){
        FsFile dir = OSData::SD.open(PICO_Path::DIR::BOOKS);
        if(dir){
            FsFile file;
            while(this->count_ < kMaxEntries && file.openNext(&dir, O_RDONLY)){
                char name[128];
                const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && name[0] != '.' && IsBookFile(name);
                file.close();
                if(!ok) continue;
                FixedString<PICO_PATH_LEN> path;
                PICO_IO::join(path, PICO_Path::DIR::BOOKS, name);
                bool known = false;
                for(int i = 0; i < this->recent_count_ && !known; i++) known = (this->paths_[i] == path);
                if(known) continue;
                ScrollListTools::Item item;
                item.icon = IconID::FileText;
                FixedString<PICO_STR_L> title;
                TitleOf(path.c_str(), title);
                item.text.assign(title.c_str());
                this->list_->add(item);
                this->paths_[this->count_++] = path;
            }
            dir.close();
        }
    }
    delete[] recs;
    this->empty_label_->setVisible(this->count_ == 0);
}

void ReaderLibraryScene::openBook(const char* path){
    if(!path || !path[0]) return;
    if(!OSData::SD_usable || !OSData::SD.exists(path)){
        // 消された・移された本は一覧から忘れる
        ReaderStore::ForgetBook(path);
        ErrorFunctions::ShowFatal("本が見つかりません(消すか移したため、一覧から外しました)");
        this->reload();
        return;
    }
    SceneFunctions::Push(new ReaderScene(path));
}

void ReaderLibraryScene::openPicker(){
    if(this->picker_) return;
    const char* start = "/";
    FixedString<PICO_PATH_LEN> books;
    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::DIR::BOOKS)){
        //末尾の"/"を落とす(FileExplorerはフォルダ名を末尾の要素から取るため)
        books.assign(PICO_Path::DIR::BOOKS, strlen(PICO_Path::DIR::BOOKS) - 1);
        start = books.c_str();
    }
    FileSelectDialog* d = new FileSelectDialog(start);
    if(!d) return;
    this->picker_ = d;
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClose([this, d](bool is_ok){
        const char* selected = is_ok ? d->getSelectedPath() : nullptr;
        if(selected && selected[0]){
            FixedString<PICO_PATH_LEN> path;
            path.assign(selected);
            this->openBook(path.c_str());
        }
        this->picker_ = nullptr;
        WidgetFunctions::DestroyLater(d);
    });
}

bool ReaderLibraryScene::onKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;
    if(ev.key == K::Escape){
        SceneFunctions::Pop();
        return true;
    }
    if(ev.key == K::Enter && this->count_ > 0 && this->recent_count_ > 0){
        this->openBook(this->paths_[0].c_str());
        return true;
    }
    return false;
}
