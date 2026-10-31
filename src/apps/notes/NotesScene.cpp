#include "apps/notes/NotesScene.hpp"
#include "apps/texteditor/TextEditorScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "storage/Recent_Files.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "system/Time_Functions.hpp"
#include "task/StepBudget.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
    // "/notes/" の末尾の "/" を落としたもの(SdWalker・mkdir に渡す)
    void NotesRoot(FixedString<PICO_PATH_LEN>& out){
        const char* d = PICO_Path::DIR::NOTES;
        out.assign(d, strlen(d) - 1);
    }

    // 並べる順: 最近開いたものが先(新しい順)、残りは名前の新しい順(新規メモの名前は日時なので作った順になる)
    bool Before(const NotesScene::Note& a, const NotesScene::Note& b){
        const bool ra = a.recent >= 0, rb = b.recent >= 0;
        if(ra != rb) return ra;
        if(ra) return a.recent < b.recent;
        return strcmp(PICO_IO::filename(a.path.c_str()), PICO_IO::filename(b.path.c_str())) > 0;
    }
}

bool NotesScene::IsNoteFile(const char* name){
    if(!name || name[0] == '.') return false;
    const char* dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".md") == 0);
}

bool NotesScene::MakeNewNotePath(FixedString<PICO_PATH_LEN>& out){
    char name[40];
    if(TimeFunctions::IsClockSet()){
        const time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        snprintf(name, sizeof(name), "%04d-%02d-%02d_%02d%02d.txt",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    }else{
        snprintf(name, sizeof(name), "memo.txt");
    }
    return PICO_IO::uniquePath(out, PICO_Path::DIR::NOTES, name);
}

NotesScene::~NotesScene(){
    this->stopScan();
    delete this->index_;
    this->index_ = nullptr;
}

const NotesScene::Note* NotesScene::shownAt(int i) const {
    if(!this->index_ || i < 0 || i >= this->index_->shown) return nullptr;
    return &this->index_->notes[this->index_->order[i]];
}

void NotesScene::setQuery(const char* q){
    FixedString<PICO_STR_M> next(q ? q : "");
    if(next == this->query_) return;
    this->query_ = next;
    this->rescan_ = true;   // 出てくる数は中身を読み直して数える
}

void NotesScene::setTagFilter(const char* tag){
    this->tag_filter_.assign(tag ? tag : "");
    if(!this->scanning_){
        this->applyFilter();
        this->refreshList();
        this->refreshStatus();
    }
}

// ---------------------------------------------------------------- 読み込み

void NotesScene::stopScan(){
    this->walker_.end();
    if(this->file_) this->file_.close();
    this->scanning_ = false;
}

void NotesScene::startScan(){
    this->stopScan();
    this->rescan_ = false;
    if(!this->index_) this->index_ = new Index();
    if(!this->index_) return;
    Index& ix = *this->index_;
    ix.count = 0;
    ix.overflow = false;
    ix.tag_count = 0;
    ix.shown = 0;
    this->matcher_.setNeedle(this->query_.c_str());
    if(!OSData::SD_usable){
        this->finishScan();
        return;
    }
    FixedString<PICO_PATH_LEN> root;
    NotesRoot(root);
    this->scanning_ = this->walker_.begin(root.c_str());
    if(!this->scanning_) this->finishScan();   // /notes/ がまだ無い
}

int NotesScene::tagIndexOf(const char* name) const {
    if(!this->index_) return -1;
    for(int i = 0; i < this->index_->tag_count; i++){
        // ASCIIの大文字/小文字は同じタグ(#Work と #work)
        if(strcasecmp(this->index_->tags[i].name.c_str(), name) == 0) return i;
    }
    return -1;
}

void NotesScene::OnTag(void* ctx, const char* tag){
    NotesScene* self = static_cast<NotesScene*>(ctx);
    Index& ix = *self->index_;
    Note& n = ix.notes[ix.count];
    int t = self->tagIndexOf(tag);
    if(t < 0){
        if(ix.tag_count >= kMaxTags) return;   // それより多いタグは数えない
        t = ix.tag_count++;
        ix.tags[t].name.assign(tag);
        ix.tags[t].count = 0;
    }
    const uint32_t bit = 1u << t;
    if(n.tags & bit) return;   // このメモでは数えた
    n.tags |= bit;
    ix.tags[t].count++;
}

void NotesScene::finishFile(){
    if(this->file_) this->file_.close();
    this->parser_.finish();
    Index& ix = *this->index_;
    Note& n = ix.notes[ix.count];
    n.title.assign(this->parser_.title());
    if(n.title.empty()){
        //中身が空なら名前(拡張子なし)を題名に
        const char* name = PICO_IO::filename(n.path.c_str());
        const char* dot = strrchr(name, '.');
        n.title.assign(name, dot ? (size_t)(dot - name) : strlen(name));
    }
    n.hits = (uint16_t)this->matcher_.count();
    ix.count++;
}

void NotesScene::stepFile(){
    char buf[kChunkBytes];
    const int n = this->file_.read((uint8_t*)buf, sizeof(buf));
    if(n > 0){
        this->parser_.feed(buf, n);
        this->matcher_.feed(buf, n);
        this->file_read_ += (uint32_t)n;
        if(this->file_read_ < kMaxReadBytes) return;
    }
    this->finishFile();
}

void NotesScene::stepScan(){
    if(!this->scanning_) return;
    Index& ix = *this->index_;
    StepBudget budget(kStepBudgetUs);
    SdWalker::Entry e;
    while(budget.ShouldContinue() && this->scanning_){
        if(this->file_){
            this->stepFile();
            continue;
        }
        if(!this->walker_.next(e)){
            this->finishScan();
            return;
        }
        if(e.is_dir || !IsNoteFile(e.name)) continue;
        if(ix.count >= kMaxNotes){
            ix.overflow = true;
            this->finishScan();
            return;
        }
        this->file_ = OSData::SD.open(e.path, O_RDONLY);
        if(!this->file_) continue;
        Note& note = ix.notes[ix.count];
        note.path.assign(e.path);
        note.title.clear();
        note.tags = 0;
        note.hits = 0;
        note.recent = -1;
        this->file_read_ = 0;
        this->parser_.begin(&NotesScene::OnTag, this);
        this->matcher_.reset();
    }
}

void NotesScene::finishScan(){
    this->stopScan();
    if(!this->index_) return;
    Index& ix = *this->index_;
    //最近開いた順を付ける(一覧は付けるあいだだけ読む)
    auto* recent = new FixedString<PICO_PATH_LEN>[RecentFiles::kMaxFiles];
    const int rn = recent ? RecentFiles::List(recent, RecentFiles::kMaxFiles) : 0;
    for(int i = 0; i < ix.count; i++){
        for(int r = 0; r < rn; r++){
            if(recent[r] == ix.notes[i].path){ ix.notes[i].recent = (int16_t)r; break; }
        }
    }
    delete[] recent;
    //消えたタグの絞り込みは解く
    if(!this->tag_filter_.empty() && this->tagIndexOf(this->tag_filter_.c_str()) < 0) this->tag_filter_.clear();
    this->applyFilter();
    this->refreshList();
    this->refreshStatus();
}

// ---------------------------------------------------------------- 表示

void NotesScene::applyFilter(){
    if(!this->index_) return;
    Index& ix = *this->index_;
    const int t = this->tag_filter_.empty() ? -1 : this->tagIndexOf(this->tag_filter_.c_str());
    const bool searching = !this->query_.empty();
    ix.shown = 0;
    for(int i = 0; i < ix.count; i++){
        const Note& n = ix.notes[i];
        if(t >= 0 && !(n.tags & (1u << t))) continue;
        if(searching && n.hits == 0) continue;
        //挿入ソート(多くて kMaxNotes 件)
        int j = ix.shown++;
        while(j > 0 && Before(n, ix.notes[ix.order[j - 1]])){
            ix.order[j] = ix.order[j - 1];
            j--;
        }
        ix.order[j] = (int16_t)i;
    }
}

void NotesScene::refreshList(){
    if(!this->list_) return;
    this->list_->clear();
    const bool searching = !this->query_.empty();
    for(int i = 0; i < this->shownCount(); i++){
        const Note* n = this->shownAt(i);
        ScrollListTools::Item item;
        item.icon = IconID::FileText;
        item.text.assign(n->title.c_str());
        if(searching){
            char hits[16];
            snprintf(hits, sizeof(hits), "  (%u)", (unsigned)n->hits);
            (void)item.text.append(hits);
        }
        this->list_->add(item);
    }
    if(this->empty_label_){
        const bool empty = !this->scanning_ && this->shownCount() == 0;
        this->empty_label_->setVisible(empty);
        if(empty){
            const bool filtered = !this->query_.empty() || !this->tag_filter_.empty();
            this->empty_label_->setText(filtered ? TR("当てはまるメモはありません") :
                TR("メモがありません。[新規]で作るか、SDの /notes/ に .txt / .md を置いてください。"
                "本文に #タグ と書くと[タグ]で絞り込めます"));
        }
    }
}

void NotesScene::refreshStatus(){
    if(!this->status_label_) return;
    FixedString<PICO_STR_L> s;
    if(this->scanning_){
        s.appendFormat(TR("読み込み中… %d件"), this->index_ ? this->index_->count : 0);
    }else{
        if(!this->tag_filter_.empty()) s.appendFormat("#%s ", this->tag_filter_.c_str());
        if(!this->query_.empty()) s.appendFormat("「%s」", this->query_.c_str());
        if(s.empty()) s.assign(TR("メモ "));
        s.appendFormat(TR("%d件"), this->shownCount());
        if(this->index_ && this->index_->overflow) s.appendFormat(TR("(先頭%d件まで)"), kMaxNotes);
    }
    this->status_label_->setText(s.c_str());
    if(this->clear_button_){
        this->clear_button_->setVisible(!this->query_.empty() || !this->tag_filter_.empty());
    }
}

// ---------------------------------------------------------------- 操作

void NotesScene::open(int shown_index){
    const Note* n = this->shownAt(shown_index);
    if(!n) return;
    if(!OSData::SD_usable || !OSData::SD.exists(n->path.c_str())){
        ErrorFunctions::ShowFatal(TR("メモが見つかりません"));
        this->rescan_ = true;
        return;
    }
    SceneFunctions::Push(new TextEditorScene(n->path.c_str()));
}

void NotesScene::onNew(){
    if(!OSData::SD_usable){
        ErrorFunctions::ShowFatal(TR("SDカードが使えません"));
        return;
    }
    FixedString<PICO_PATH_LEN> root;
    NotesRoot(root);
    if(!OSData::SD.exists(root.c_str()) && !OSData::SD.mkdir(root.c_str())){
        ErrorFunctions::ShowFatal(TR("/notes/ を作れませんでした"));
        return;
    }
    FixedString<PICO_PATH_LEN> path;
    if(!MakeNewNotePath(path)){
        ErrorFunctions::ShowFatal(TR("新しいメモの名前を作れません"));
        return;
    }
    SceneFunctions::Push(new TextEditorScene(path.c_str(), true));
}

void NotesScene::askQuery(){
    auto* d = new InputDialog(TR("中身から探す(空で解除):"), true);
    WidgetFunctions::AddDialog(d);
    d->setInput(this->query_.c_str());
    d->setVisible(true);
    d->openKeyboard();
    d->setOnClosed([this, d](bool ok){
        if(ok) this->setQuery(d->getInput().c_str());
        WidgetFunctions::DestroyLater(d);
    });
}

int NotesScene::sortedTags(int* order) const {
    if(!this->index_) return 0;
    const int n = this->index_->tag_count;
    for(int i = 0; i < n; i++){
        int j = i;
        while(j > 0 && strcmp(this->index_->tags[order[j - 1]].name.c_str(), this->index_->tags[i].name.c_str()) > 0){
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    return n;
}

void NotesScene::pickTag(){
    if(!this->index_ || this->scanning_) return;
    if(this->index_->tag_count == 0){
        if(this->status_label_) this->status_label_->setText(TR("タグはまだありません(本文に #タグ と書く)"));
        return;
    }
    auto* d = new PickerDialog(PickerDialog::Mode::Choice, TR("タグ"), "", TR("閉じる"));
    d->addChoice(TR("すべて"));
    int order[kMaxTags];
    const int tn = this->sortedTags(order);
    for(int i = 0; i < tn; i++){
        const Tag& t = this->index_->tags[order[i]];
        char label[PICO_STR_M + 8];
        snprintf(label, sizeof(label), "#%s  (%u)", t.name.c_str(), (unsigned)t.count);
        d->addChoice(label);
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    //選んだ番号 → タグの名前は、閉じたときに今の一覧から引き直す(並べ順は名前の順で決まる)
    d->setOnClosed([this, d](bool ok){
        const int sel = ok ? d->getSelectedIndex() : -1;
        WidgetFunctions::DestroyLater(d);
        if(sel < 0 || !this->index_) return;
        if(sel == 0){ this->setTagFilter(""); return; }
        int order2[kMaxTags];
        const int n = this->sortedTags(order2);
        if(sel - 1 < n) this->setTagFilter(this->index_->tags[order2[sel - 1]].name.c_str());
    });
}

// ---------------------------------------------------------------- シーン

void NotesScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int top = content.y + MARGIN;
    const int btn_h = 20 + Button::kFrameExtra;

    // 1段目: [戻る][新規][検索][#タグ](各ボタンの実測幅で左から詰める)
    int x = content.x + MARGIN;
    auto make = [&](const char* label, IconID icon, bool with_icon){
        Button* b = new Button(x, top, label);
        b->setFontSize(FontFn::Small);
        if(with_icon) b->setIcon(icon, IconSize::Px16);
        b->setH(btn_h);
        x += b->getLocalRect().w + MARGIN;
        WidgetFunctions::Add(b);
        return b;
    };
    this->back_button_ = make(TR("戻る"), IconID::ArrowLeft, true);
    this->back_button_->setOnPressEnd([](){ SceneFunctions::Pop(); });
    this->new_button_ = make(TR("新規"), IconID::FilePlus, true);
    this->new_button_->setOnPressEnd([this](){ this->onNew(); });
    this->search_button_ = make(TR("検索"), IconID::Search, true);
    this->search_button_->setOnPressEnd([this](){ this->askQuery(); });
    this->tag_button_ = make(TR("#タグ"), IconID::AppBox, false);
    this->tag_button_->setOnPressEnd([this](){ this->pickTag(); });

    // 2段目: 今の絞り込みと件数 … [×]
    const int y2 = top + this->back_button_->getLocalRect().h + MARGIN;
    this->clear_button_ = new Button(0, y2, "×");
    this->clear_button_->setIcon(IconID::X, IconSize::Px16);
    this->clear_button_->setFontSize(FontFn::Small);
    this->clear_button_->setH(btn_h);
    this->clear_button_->setX(content.x + content.w - MARGIN - this->clear_button_->getLocalRect().w);
    this->clear_button_->setOnPressEnd([this](){
        this->tag_filter_.clear();
        if(!this->query_.empty()) this->setQuery("");
        else this->setTagFilter("");
    });
    WidgetFunctions::Add(this->clear_button_);

    this->status_label_ = new Label<PICO_STR_L>(content.x + MARGIN, y2 + 3, "");
    this->status_label_->setFontSize(FontFn::Small);
    this->status_label_->setMaxWidth(this->clear_button_->getLocalRect().x - MARGIN * 2 - content.x);
    this->status_label_->setMaxHeight(btn_h);
    this->status_label_->setDisableAutoTextDecoration(true);   // タグの _ や * を装飾にしない
    this->status_label_->setHitTransparent(true);
    WidgetFunctions::Add(this->status_label_);

    const int body_y = y2 + this->clear_button_->getLocalRect().h + MARGIN;
    this->list_ = new ScrollList(content.x, (int16_t)body_y, content.w, (int16_t)(content.y + content.h - body_y));
    this->list_->setFontSize(FontFn::Text); //一覧の文字は文字の大きさの設定に従う(行の高さは文字に追従する)
    this->list_->setEnableIcon(true);
    this->list_->setOnSelectItem([this](int index, bool already){
        if(already) this->open(index);
    });
    WidgetFunctions::Add(this->list_);

    this->empty_label_ = new Label<PICO_STR_LL>(content.x + MARGIN * 2, body_y + MARGIN, "");
    this->empty_label_->setFontSize(FontFn::Small);
    this->empty_label_->setMaxWidth(content.w - MARGIN * 4);
    this->empty_label_->setTextColor(PICO_DARKGREY);
    this->empty_label_->setDisableAutoTextDecoration(true);
    this->empty_label_->setHitTransparent(true);
    this->empty_label_->setVisible(false);
    WidgetFunctions::Add(this->empty_label_);

    // メモを開いて戻ってきたら読み直す(題名・タグが変わっているかもしれない)
    this->startScan();
    this->refreshList();
    this->refreshStatus();
}

void NotesScene::onExit(){
    this->stopScan();
    delete this->index_;
    this->index_ = nullptr;
    this->back_button_ = this->new_button_ = this->search_button_ = this->tag_button_ = this->clear_button_ = nullptr;
    this->status_label_ = nullptr;
    this->empty_label_ = nullptr;
    this->list_ = nullptr;
}

void NotesScene::onUpdate(){
    if(this->rescan_){
        this->startScan();
        this->refreshList();
        this->refreshStatus();
    }
    if(!this->scanning_) return;
    const int before = this->index_ ? this->index_->count : 0;
    this->stepScan();
    if(this->scanning_ && this->index_ && this->index_->count != before) this->refreshStatus();
}
