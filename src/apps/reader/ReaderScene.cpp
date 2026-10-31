#include "apps/reader/ReaderScene.hpp"
#include "apps/reader/ui/ReaderView.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/apps/SeekBar.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace {
    constexpr int MARGIN = 4;
}

ReaderScene::ReaderScene(const char* path){
    this->path_.assign(path ? path : "");
}

ReaderScene::~ReaderScene(){
    this->closeBook();
}

// ================================================================ 本

bool ReaderScene::openBook(){
    if(this->book_) return true;
    if(!this->file_.open(this->path_.c_str())){
        LOG_APP_FAIL("読書: %s を開けません", this->path_.c_str());
        return false;
    }
    this->book_ = new ReaderBook(this->file_);
    if(!this->book_){
        this->file_.close();
        return false;
    }

    // 題名: 青空文庫の形なら1行目、違えばファイル名(拡張子なし)
    const Aozora::Info& info = this->book_->info();
    if(!info.title.empty()){
        this->title_.assign(info.title.c_str());
    }else{
        const char* name = PICO_IO::filename(this->path_.c_str());
        const char* dot = strrchr(name, '.');
        this->title_.assign(name, dot ? (size_t)(dot - name) : strlen(name));
    }

    ReaderLayout::State at;
    if(!this->loaded_){
        // 初めて開いた: 覚えている続きの位置としおり
        ReaderStore::LoadBook(this->path_.c_str(), at, this->marks_, this->mark_count_);
        this->loaded_ = true;
    }else if(this->has_resume_){
        at = this->resume_;
    }
    this->applyParams();
    this->book_->goTo(at);
    LOG_APP_MSG("読書: %s を開きました(%u バイト、%s)", this->path_.c_str(), (unsigned)this->book_->size(),
                info.enc == Aozora::Encoding::ShiftJis ? "Shift_JIS" : "UTF-8");
    return true;
}

void ReaderScene::closeBook(){
    if(this->book_){
        this->resume_ = this->book_->current();
        this->has_resume_ = true;
    }
    delete this->book_;
    this->book_ = nullptr;
    this->file_.close();
}

void ReaderScene::applyParams(){
    if(!this->book_ || !this->view_) return;
    this->view_->setLargeFont(this->settings_.large_font);
    ReaderLayout::Params p = this->view_->params();
    p.ruby = this->settings_.ruby;
    this->book_->setParams(p);
}

void ReaderScene::markChanged(){
    this->dirty_ = true;
    this->changed_ms_ = millis();
}

void ReaderScene::save(){
    this->dirty_ = false;
    if(!this->book_) return;
    ReaderStore::SaveBook(this->path_.c_str(), this->title_.c_str(), this->book_->size(),
                          this->book_->current(), this->marks_, this->mark_count_);
}

// ================================================================ 画面

void ReaderScene::onEnter(){
    const Rect content = Scene::contentRect();
    this->settings_ = ReaderStore::LoadSettings();
    this->menu_ = false;
    this->shown_page_ = -2;

    this->view_ = new ReaderView(content.x, content.y, content.w, content.h);
    this->view_->setOnTurn([this](int dir){ this->turn(dir); });
    this->view_->setOnMenu([this](){ this->setMenu(!this->menu_); });
    WidgetFunctions::Add(this->view_);

    this->buildMenu();
    this->setMenu(false);

    if(!this->openBook()){
        this->view_->setMessage(TR("本を開けません"));
        ErrorFunctions::ShowFatal(TR("本を開けません(SDカードとファイルを確かめてください)"));
        return;
    }
    this->title_label_->setText(this->title_.c_str());
    this->refresh();
}

void ReaderScene::onExit(){
    if(this->dirty_ || this->book_) this->save();
    this->closeBook();
    this->view_ = nullptr;
    this->back_button_ = nullptr;
    this->title_label_ = nullptr;
    this->seek_ = nullptr;
    this->mark_button_ = this->list_button_ = this->font_button_ = this->ruby_button_ = nullptr;
    this->picker_ = nullptr;
}

void ReaderScene::buildMenu(){
    const Rect content = Scene::contentRect();
    const int btn_h = 20 + Button::kFrameExtra;

    this->back_button_ = new Button(content.x + MARGIN, content.y + 2, TR("本棚"));
    this->back_button_->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button_->setFontSize(FontFn::Small);
    this->back_button_->setH(kTopBand - 4);
    this->back_button_->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button_);
    const Rect back = this->back_button_->getLocalRect();

    const int title_x = back.x + back.w + MARGIN;
    this->title_label_ = new Label<PICO_STR_L>(title_x, content.y + (kTopBand - FontFn::GetFontSize(FontFn::Small)) / 2, "");
    this->title_label_->setFontSize(FontFn::Small);
    this->title_label_->setMaxWidth(content.x + content.w - MARGIN - title_x);
    this->title_label_->setDisableAutoTextDecoration(true);
    this->title_label_->setBackgroundColor(PICO_WHITE);
    this->title_label_->setHitTransparent(true);
    WidgetFunctions::Add(this->title_label_);

    const int band_y = content.y + content.h - kBottomBand;
    this->seek_ = new SeekBar((int16_t)(content.x + MARGIN * 2), (int16_t)(band_y + 4),
                              (int16_t)(content.w - MARGIN * 4), 14);
    this->seek_->setBackgroundColor(PICO_WHITE);
    this->seek_->setOnSeek([this](float r){
        if(!this->book_) return;
        this->book_->goToRatio(r);
        this->markChanged();
        this->refresh();
    });
    WidgetFunctions::Add(this->seek_);

    // 下の段のボタン(しおり / 一覧 / 文字 / ルビ)を均等に並べる
    const int row_y = band_y + 4 + 14 + 6;
    const char* labels[4] = { TR("しおり"), TR("一覧"), TR("文字"), TR("ルビ") };
    Button** slots[4] = { &this->mark_button_, &this->list_button_, &this->font_button_, &this->ruby_button_ };
    const int cell = (content.w - MARGIN) / 4;
    for(int i = 0; i < 4; i++){
        Button* b = new Button(content.x + MARGIN + cell * i, row_y, labels[i]);
        b->setFontSize(FontFn::Small);
        b->setW(cell - MARGIN);
        b->setH(btn_h - 2);
        WidgetFunctions::Add(b);
        *slots[i] = b;
    }
    this->mark_button_->setOnPressEnd([this](){ this->toggleMark(); });
    this->list_button_->setOnPressEnd([this](){ this->openList(); });
    this->font_button_->setOnPressEnd([this](){
        this->settings_.large_font = !this->settings_.large_font;
        ReaderStore::SaveSettings(this->settings_);
        this->applyParams();
        this->refresh();
    });
    this->ruby_button_->setOnPressEnd([this](){
        this->settings_.ruby = !this->settings_.ruby;
        ReaderStore::SaveSettings(this->settings_);
        this->applyParams();
        this->refresh();
    });
}

void ReaderScene::setMenu(bool on){
    this->menu_ = on;
    if(this->view_) this->view_->setMenuBands(on ? kTopBand : 0, on ? kBottomBand : 0);
    Widget* ws[] = { this->back_button_, this->title_label_, this->seek_,
                     this->mark_button_, this->list_button_, this->font_button_, this->ruby_button_ };
    for(Widget* w : ws) if(w) w->setVisible(on);
    if(on) this->refresh();
}

void ReaderScene::refresh(){
    if(!this->view_) return;
    if(!this->book_){
        this->view_->setPage(nullptr);
        return;
    }
    this->view_->setPage(&this->book_->page());
    this->view_->setBookmarked(this->markOnPage() >= 0);
    if(this->menu_){
        this->seek_->setRatio(this->book_->progress());
        this->mark_button_->setText(this->markOnPage() >= 0 ? TR("外す") : TR("しおり"));
        this->ruby_button_->setTextColor(this->settings_.ruby ? PICO_BLACK : PICO_DARKGREY);
    }
    this->refreshFooter();
}

void ReaderScene::refreshFooter(){
    if(!this->view_ || !this->book_) return;
    const int n = this->book_->pageNumber();
    const bool complete = this->book_->indexComplete();
    char left[24], right[24];
    snprintf(left, sizeof(left), "%d%%", (int)(this->book_->progress() * 100.0f + 0.5f));
    if(n < 0) snprintf(right, sizeof(right), "-/-");
    else if(complete) snprintf(right, sizeof(right), "%d/%d", n + 1, this->book_->pageCount());
    else snprintf(right, sizeof(right), "%d/...", n + 1);
    this->view_->setFooter(left, right, this->book_->progress());
    this->shown_page_ = n;
    this->shown_complete_ = complete;
}

void ReaderScene::turn(int dir){
    if(!this->book_) return;
    const bool moved = dir > 0 ? this->book_->next() : this->book_->prev();
    if(!moved) return;
    this->markChanged();
    this->refresh();
}

void ReaderScene::onUpdate(){
    if(!this->book_) return;
    // 索引を少しずつ作る(1フレーム kIndexBudgetMs まで)
    if(!this->book_->indexComplete()){
        const uint32_t t0 = millis();
        do {
            this->book_->indexStep(2);
        } while(!this->book_->indexComplete() && millis() - t0 < kIndexBudgetMs);
    }
    if(this->book_->indexComplete() != this->shown_complete_ ||
       (this->shown_page_ < 0 && this->book_->pageNumber() >= 0)){
        this->refreshFooter();
    }
    if(this->dirty_ && millis() - this->changed_ms_ >= kSaveDelayMs) this->save();
}

bool ReaderScene::onKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;
    if(ev.key == K::Escape){
        if(this->menu_) this->setMenu(false);
        else SceneFunctions::Pop();
        return true;
    }
    if(!this->book_) return false;
    switch(ev.key){
        case K::Right: case K::Down: case K::PageDown:
            this->turn(+1);
            return true;
        case K::Left: case K::Up: case K::PageUp:
            this->turn(-1);
            return true;
        case K::Home:
            this->book_->goToRatio(0.0f);
            this->markChanged();
            this->refresh();
            return true;
        case K::Enter:
            this->setMenu(!this->menu_);
            return true;
        default:
            break;
    }
    if(ev.isPlainChar()){
        if(ev.cp == ' '){ this->turn(ev.shift() ? -1 : +1); return true; }
        if(ev.cp == 'b'){ this->toggleMark(); return true; }
        if(ev.cp == 't'){ this->openList(); return true; }
    }
    return false;
}

// ================================================================ しおり・一覧

int ReaderScene::markOnPage() const {
    if(!this->book_) return -1;
    const uint32_t from = this->book_->current().pos;
    const uint32_t to = this->book_->endPos();
    for(int i = 0; i < this->mark_count_; i++){
        const uint32_t p = this->marks_[i].at.pos;
        if(p == from || (p > from && p < to)) return i;
    }
    return -1;
}

void ReaderScene::toggleMark(){
    if(!this->book_) return;
    const int i = this->markOnPage();
    if(i >= 0){
        for(int k = i; k + 1 < this->mark_count_; k++) this->marks_[k] = this->marks_[k + 1];
        this->mark_count_--;
    }else{
        if(this->mark_count_ >= ReaderStore::kMaxMarks){
            ErrorFunctions::ShowFatal(TR("しおりがいっぱいです(先に外してください)"));
            return;
        }
        // 位置の順に並べておく
        const ReaderLayout::State at = this->book_->current();
        int k = this->mark_count_;
        while(k > 0 && this->marks_[k - 1].at.pos > at.pos){
            this->marks_[k] = this->marks_[k - 1];
            k--;
        }
        ReaderStore::Mark& m = this->marks_[k];
        m.at = at;
        const ReaderLayout::Page& pg = this->book_->page();
        // 空行でない最初の行を見出しにする
        m.label.clear();
        for(int l = 0; l < pg.count && m.label.empty(); l++) ReaderStore::MakeLabel(pg.lines[l].text, m.label);
        this->mark_count_++;
    }
    this->save();
    this->refresh();
}

void ReaderScene::openList(){
    if(!this->book_ || this->picker_) return;
    const ReaderLayout::Toc& toc = this->book_->toc();
    const bool complete = this->book_->indexComplete();

    // 目次としおりを位置の順に混ぜる
    ListItem* items = this->list_items_;
    int count = 0;
    int ti = 0, mi = 0;
    while(count < kMaxListItems && (ti < toc.count() || mi < this->mark_count_)){
        const bool take_mark = mi < this->mark_count_ &&
                               (ti >= toc.count() || this->marks_[mi].at.pos <= toc.at(ti).pos);
        if(take_mark){
            items[count++] = { true, mi, this->marks_[mi].at.pos };
            mi++;
        }else{
            items[count++] = { false, ti, toc.at(ti).pos };
            ti++;
        }
    }

    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Choice,
                                       complete ? TR("目次・しおり") : TR("目次・しおり(目次は読み込み中)"), "", TR("閉じる"));
    if(!d) return;
    this->list_count_ = count;
    if(count == 0) d->addChoice(TR("(見出し・しおりはありません)"));
    for(int i = 0; i < count; i++){
        char text[PICO_STR_LL];
        if(items[i].mark){
            const ReaderStore::Mark& m = this->marks_[items[i].index];
            snprintf(text, sizeof(text), TR("しおり: %s"), m.label.empty() ? TR("(空行)") : m.label.c_str());
        }else{
            snprintf(text, sizeof(text), "%s", toc.at(items[i].index).title);
        }
        d->addChoice(text);
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    this->picker_ = d;
    d->setOnClosed([this, d](bool is_ok){
        const int sel = d->getSelectedIndex();
        if(is_ok && this->book_ && sel >= 0 && sel < this->list_count_){
            const ListItem& it = this->list_items_[sel];
            if(it.mark) this->book_->goTo(this->marks_[it.index].at);
            else this->book_->goToPos(it.pos);
            this->markChanged();
            this->setMenu(false);
            this->refresh();
        }
        this->picker_ = nullptr;
        WidgetFunctions::DestroyLater(d);
    });
}
