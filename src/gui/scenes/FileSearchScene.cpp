#include "gui/scenes/FileSearchScene.hpp"
#include "gui/scenes/FileViewerScene.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "task/StepBudget.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

namespace {
    inline char Lower(char c){ return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
}

bool FileSearchScene::NameMatches(const char* name, const char* query){
    if(!name || !query) return false;
    if(!*query) return true;
    for(const char* s = name; *s; s++){
        const char* a = s;
        const char* b = query;
        while(*a && *b && Lower(*a) == Lower(*b)){ a++; b++; }
        if(!*b) return true;
    }
    return false;
}

bool FileSearchScene::TakeJump(FixedString<PICO_PATH_LEN>& out){
    if(!has_jump) return false;
    has_jump = false;
    out = pending_jump;
    return true;
}

FileSearchScene::FileSearchScene(const char* root, const char* query){
    this->root.assign(root && *root ? root : "/");
    this->query.assign(query ? query : "");
}

void FileSearchScene::restart(){
    this->walker.end();
    this->result_count = 0;
    this->scanned = 0;
    this->overflow = false;
    this->done = false;
    this->shown_count = -1;
    this->shown_done = false;
    if(this->list) this->list->clear();
    this->started = this->walker.begin(this->root.c_str());
    if(!this->started) this->done = true;
}

void FileSearchScene::step(){
    if(this->done) return;
    StepBudget budget(kStepBudgetUs);
    SdWalker::Entry e;
    while(budget.ShouldContinue()){
        if(!this->walker.next(e)){
            this->done = true;
            break;
        }
        this->scanned++;
        if(!NameMatches(e.name, this->query.c_str())) continue;
        if(this->result_count >= kMaxResults){
            this->overflow = true;
            this->done = true;
            this->walker.end();
            break;
        }
        this->results[this->result_count].assign(e.path);
        this->result_is_dir[this->result_count] = e.is_dir;
        this->result_count++;
    }
}

void FileSearchScene::refreshList(){
    if(!this->list) return;
    if(this->shown_count < 0){
        this->list->clear();
        this->shown_count = 0;
    }
    for(int i = this->shown_count; i < this->result_count; i++){
        ScrollListTools::Item item;
        item.icon = this->result_is_dir[i] ? IconID::Folder : IconID::File;
        // 名前の後ろに、どのフォルダにあるか
        const char* p = this->results[i].c_str();
        const char* name = PICO_IO::filename(p);
        item.text.assign(name);
        const size_t dir_len = (size_t)(name - p);
        if(dir_len > 1){
            item.text.append("  ");
            item.text.append(p, dir_len - 1);
        }
        this->list->add(item);
    }
    this->shown_count = this->result_count;
}

void FileSearchScene::refreshStatus(){
    if(!this->status_label) return;
    char buf[PICO_STR_L];
    if(!this->started){
        snprintf(buf, sizeof(buf), "%s を開けません", this->root.c_str());
    }else if(!this->done){
        snprintf(buf, sizeof(buf), "検索中… %lu件中 %d件", (unsigned long)this->scanned, this->result_count);
    }else if(this->overflow){
        snprintf(buf, sizeof(buf), "「%s」%d件以上(先頭%d件)", this->query.c_str(), kMaxResults, kMaxResults);
    }else{
        snprintf(buf, sizeof(buf), "「%s」%d件", this->query.c_str(), this->result_count);
    }
    this->status_label->setTextColor((!this->started) ? PICO_RED : PICO_FORECOLOR);
    this->status_label->setText(buf);
}

void FileSearchScene::openResult(int index){
    if(index < 0 || index >= this->result_count) return;
    if(this->result_is_dir[index]){
        pending_jump = this->results[index];
        has_jump = true;
        SceneFunctions::Pop();
    }else{
        SceneFunctions::Push(new FileViewerScene(this->results[index].c_str()));
    }
}

void FileSearchScene::askQuery(){
    auto* d = new InputDialog("探す名前:", true);
    WidgetFunctions::AddDialog(d);
    d->setInput(this->query.c_str());
    d->setVisible(true);
    d->openKeyboard();
    d->setOnClosed([this, d](bool ok){
        if(ok){
            const auto q = d->getInput();
            if(q.length() > 0){
                this->query.assign(q.c_str());
                this->restart();
            }
        }
        WidgetFunctions::DestroyLater(d);
    });
}

void FileSearchScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int top = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, top, "戻る");
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    this->again_button = new Button(0, top, "再検索");
    this->again_button->setFontSize(FontFn::Small);
    this->again_button->setH(20 + Button::kFrameExtra);
    this->again_button->setX(content.x + content.w - MARGIN - this->again_button->getLocalRect().w);
    this->again_button->setOnPressEnd([this](){ this->askQuery(); });
    WidgetFunctions::Add(this->again_button);

    const Rect bb = this->back_button->getLocalRect();
    const int sx = bb.x + bb.w + MARGIN;
    this->status_label = new Label<PICO_STR_L>(sx, top + 3, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(this->again_button->getLocalRect().x - MARGIN - sx);
    this->status_label->setMaxHeight(bb.h);
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    const int body_y = top + bb.h + MARGIN;
    this->list = new ScrollList(content.x, (int16_t)body_y, content.w, (int16_t)(content.y + content.h - body_y));
    this->list->setFontSize(FontFn::Small);
    this->list->setEnableIcon(true);
    this->list->setOnSelectItem([this](int index, bool already){
        if(already) this->openResult(index);
    });
    WidgetFunctions::Add(this->list);

    if(!this->started && !this->done) this->restart();
    this->shown_count = -1;   // ウィジェットを作り直したので並べ直す
    this->refreshList();
    this->refreshStatus();
}

void FileSearchScene::onExit(){
    // 探している途中のwalkerはメンバなので残る(ファイルビューワーから戻ったら続きから)
    this->back_button = nullptr;
    this->again_button = nullptr;
    this->status_label = nullptr;
    this->list = nullptr;
}

void FileSearchScene::onUpdate(){
    if(this->done && this->shown_done && this->shown_count == this->result_count) return;
    this->step();
    if(this->shown_count != this->result_count) this->refreshList();
    this->refreshStatus();
    if(this->done) this->shown_done = true;
}
