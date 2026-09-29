#include "gui/scenes/TextEditorScene.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

int TextEditorScene::lineCount() const {
    int n = 1;
    for(int i = 0; i < this->len; i++){
        if(this->text[i] == '\n') n++;
    }
    return n;
}

void TextEditorScene::lineRange(int i, int& start, int& end) const {
    int line = 0;
    start = 0;
    for(int p = 0; p < this->len && line < i; p++){
        if(this->text[p] == '\n'){
            line++;
            start = p + 1;
        }
    }
    end = start;
    while(end < this->len && this->text[end] != '\n') end++;
}

bool TextEditorScene::replaceLine(int i, const char* s){
    int start, end;
    this->lineRange(i, start, end);

    const int new_len = (int)strlen(s);
    const int total = this->len - (end - start) + new_len;
    if(total > kMaxBytes) return false;

    memmove(this->text + start + new_len, this->text + end, this->len - end);
    memcpy(this->text + start, s, new_len);
    this->len = total;
    this->text[this->len] = '\0';
    return true;
}

bool TextEditorScene::insertLineAfter(int i){
    if(this->len + 1 > kMaxBytes || this->lineCount() >= kMaxLines) return false;

    int start, end;
    this->lineRange(i, start, end);
    memmove(this->text + end + 1, this->text + end, this->len - end);
    this->text[end] = '\n';
    this->len++;
    this->text[this->len] = '\0';
    return true;
}

void TextEditorScene::deleteLine(int i){
    int start, end;
    this->lineRange(i, start, end);

    int from = start;
    int to = end;
    if(end < this->len){
        to = end + 1;        // 後ろに行がある: 自分の改行ごと消す
    }else if(start > 0){
        from = start - 1;    // 最後の行: 手前の改行ごと消す
    }
    memmove(this->text + from, this->text + to, this->len - to);
    this->len -= (to - from);
    this->text[this->len] = '\0';
}

void TextEditorScene::refreshItem(int i){
    ScrollListTools::Item* item = this->list->itemAt(i);
    if(!item) return;

    int start, end;
    this->lineRange(i, start, end);

    char line[kMaxLineBytes + 1];
    int n = end - start;
    if(n > kMaxLineBytes) n = kMaxLineBytes;
    memcpy(line, this->text + start, n);
    line[n] = '\0';

    char buf[PICO_PATH_LEN];
    snprintf(buf, sizeof(buf), "%d %s", i + 1, line);
    item->text.assign(buf);
    this->list->needsRender();
}

void TextEditorScene::refreshList(){
    const int selected = this->list->getSelectedIndex();
    const int count = this->lineCount();

    this->list->clear();
    for(int i = 0; i < count; i++){
        this->list->add(ScrollListTools::Item{});
        this->refreshItem(i);
    }
    if(selected >= 0) this->list->setSelectedIndex(selected < count ? selected : count - 1);
}

void TextEditorScene::refreshStatus(){
    char buf[PICO_STR_L];
    snprintf(buf, sizeof(buf), "%s%s",
        this->path.empty() ? "(新規)" : PICO_IO::filename(this->path.c_str()),
        this->dirty ? " (未保存)" : "");
    this->status_label->setText(buf);
}

void TextEditorScene::setDirty(bool value){
    this->dirty = value;
    this->refreshStatus();
}

void TextEditorScene::editLine(int i){
    if(i < 0 || i >= this->lineCount()) return;

    int start, end;
    this->lineRange(i, start, end);
    char line[kMaxLineBytes + 1];
    int n = end - start;
    if(n > kMaxLineBytes) n = kMaxLineBytes;
    memcpy(line, this->text + start, n);
    line[n] = '\0';

    char label[PICO_STR_S];
    snprintf(label, sizeof(label), "%d行目:", i + 1);

    auto* dialog = new InputDialog(label, true);
    if(!dialog) return;
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(line);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, i](bool is_submit){
        if(is_submit && this->list){
            if(this->replaceLine(i, dialog->getInput().c_str())){
                this->refreshItem(i);
                this->setDirty(true);
            }else{
                ErrorFunctions::ShowFatal("容量の上限(4KiB)を超えるため反映できません");
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void TextEditorScene::addLine(){
    const int sel = this->list->getSelectedIndex();
    const int at = (sel >= 0) ? sel : this->lineCount() - 1;

    if(!this->insertLineAfter(at)){
        ErrorFunctions::ShowFatal("これ以上行を増やせません(4KiB/100行まで)");
        return;
    }
    this->list->setSelectedIndex(at + 1);
    this->refreshList();
    this->setDirty(true);
    this->editLine(at + 1);
}

void TextEditorScene::deleteSelected(){
    const int sel = this->list->getSelectedIndex();
    if(sel < 0) return;
    this->deleteLine(sel);
    this->refreshList();
    this->setDirty(true);
}

void TextEditorScene::confirmDiscard(const char* msg, std::function<void()> then){
    if(!this->dirty){
        then();
        return;
    }
    MsgDialog* dialog = new MsgDialog(msg, "キャンセル", "破棄する");
    if(!dialog) return;
    dialog->setVisibleIcon(true);
    dialog->setIconId(IconID::AlertTriangle);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([dialog, then](bool is_ok){
        WidgetFunctions::DestroyLater(dialog);
        if(is_ok) then();
    });
}

void TextEditorScene::newDocument(){
    this->len = 0;
    this->text[0] = '\0';
    this->path.clear();
    this->list->clearSelectedIndex();
    this->refreshList();
    this->setDirty(false);
}

bool TextEditorScene::loadFile(const char* file){
    if(!OSData::SD_usable){
        ErrorFunctions::ShowFatal("SDカードが使えません");
        return false;
    }
    FsFile f = OSData::SD.open(file);
    if(!f){
        ErrorFunctions::ShowFatal("ファイルを開けませんでした");
        return false;
    }
    if(f.fileSize() > (uint32_t)kMaxBytes){
        f.close();
        ErrorFunctions::ShowFatal("大きすぎて開けません(4KiBまで)");
        return false;
    }

    // 上限を超える内容は保存時に消えてしまうので、切り詰めずに断る
    static char buf[kMaxBytes + 1];
    int n = f.read((uint8_t*)buf, kMaxBytes);
    f.close();
    if(n < 0) n = 0;

    int out = 0, lines = 1, line_bytes = 0;
    for(int i = 0; i < n; i++){
        const char c = buf[i];
        if(c == '\r') continue;
        if(c == '\n'){
            lines++;
            line_bytes = 0;
        }else if(++line_bytes > kMaxLineBytes){
            ErrorFunctions::ShowFatal("1行が長すぎて開けません(190バイトまで)");
            return false;
        }
        buf[out++] = c;
    }
    if(out > 0 && buf[out - 1] == '\n'){ // 末尾の改行は最後の行の一部として扱う
        out--;
        lines--;
    }
    if(lines > kMaxLines){
        ErrorFunctions::ShowFatal("行数が多すぎて開けません(100行まで)");
        return false;
    }

    memcpy(this->text, buf, out);
    this->len = out;
    this->text[out] = '\0';
    this->path.assign(file);
    this->list->clearSelectedIndex();
    this->refreshList();
    this->setDirty(false);
    return true;
}

void TextEditorScene::openPicker(){
    FileSelectDialog* dialog = new FileSelectDialog("/");
    if(!dialog) return;
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClose([this, dialog](bool is_ok){
        const char* selected = is_ok ? dialog->getSelectedPath() : nullptr;
        if(selected && this->list) this->loadFile(selected);
        WidgetFunctions::DestroyLater(dialog);
    });
}

bool TextEditorScene::writeFile(const char* file){
    if(!OSData::SD_usable) return false;
    FsFile f = OSData::SD.open(file, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f) return false;

    bool ok = (this->len == 0) || (f.write((const uint8_t*)this->text, this->len) == (size_t)this->len);
    ok = ok && (f.write((const uint8_t*)"\n", 1) == 1); // 末尾に改行を付ける
    f.close();
    return ok;
}

void TextEditorScene::saveFile(){
    if(this->path.empty()){
        this->saveAsPicker();
        return;
    }
    if(this->writeFile(this->path.c_str())){
        this->setDirty(false);
    }else{
        ErrorFunctions::ShowFatal("保存に失敗しました");
    }
}

void TextEditorScene::saveAsPicker(){
    FileSaveDialog* dialog = new FileSaveDialog("/");
    if(!dialog) return;
    dialog->setFileName("memo.txt");
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClose([this, dialog](bool is_ok){
        if(is_ok && this->list){
            const char* target = dialog->getSavePath();
            if(target && *target && target[strlen(target) - 1] != '/'){
                if(this->writeFile(target)){
                    this->path.assign(target);
                    this->setDirty(false);
                }else{
                    ErrorFunctions::ShowFatal("保存に失敗しました");
                }
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void TextEditorScene::onEnter(){
    const Rect content = Scene::contentRect();

    auto make_button = [&](const char* label, int x, int y){
        Button* b = new Button(x, y, label);
        b->setFontSize(FontFn::Small);
        b->setH(20);
        WidgetFunctions::Add(b);
        return b;
    };

    // 1段目: 戻る 新規 開く 保存
    int x = content.x + MARGIN;
    const int y1 = content.y + MARGIN;
    this->back_button = make_button("戻る", x, y1);
    x += this->back_button->getLocalRect().w + MARGIN;
    this->new_button = make_button("新規", x, y1);
    x += this->new_button->getLocalRect().w + MARGIN;
    this->open_button = make_button("開く", x, y1);
    x += this->open_button->getLocalRect().w + MARGIN;
    this->save_button = make_button("保存", x, y1);

    // 2段目: 追加 削除 + ファイル名/状態。行の高さはボタンの実測値(フォントで変わるため)
    const int row_h = this->back_button->getLocalRect().h;
    const int y2 = y1 + row_h + MARGIN;
    this->add_button = make_button("追加", content.x + MARGIN, y2);
    x = content.x + MARGIN + this->add_button->getLocalRect().w + MARGIN;
    this->del_button = make_button("削除", x, y2);
    x += this->del_button->getLocalRect().w + MARGIN;

    this->status_label = new Label<PICO_STR_L>(x, y2, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.x + content.w - MARGIN - x);
    this->status_label->setMaxHeight(row_h);
    this->status_label->setDisableAutoTextDecoration(true); //ファイル名の_や*を装飾にしない
    WidgetFunctions::Add(this->status_label);

    const int list_y = y2 + row_h + MARGIN;
    this->list = new ScrollList(content.x, list_y, content.w, content.y + content.h - list_y);
    this->list->setFontSize(FontFn::Small);
    this->list->setOnSelectItem([this](int index, bool already_selected){
        if(already_selected) this->editLine(index);
    });
    WidgetFunctions::Add(this->list);

    this->back_button->setOnPressEnd([this](){
        this->confirmDiscard("保存していない変更があります。破棄して戻りますか?", [](){
            SceneFunctions::Pop();
        });
    });
    this->new_button->setOnPressEnd([this](){
        this->confirmDiscard("保存していない変更があります。破棄して新規作成しますか?", [this](){
            this->newDocument();
        });
    });
    this->open_button->setOnPressEnd([this](){
        this->confirmDiscard("保存していない変更があります。破棄して別のファイルを開きますか?", [this](){
            this->pending = Pending::OpenPicker;
            this->pending_wait_frames = 1;
        });
    });
    this->save_button->setOnPressEnd([this](){ this->saveFile(); });
    this->add_button->setOnPressEnd([this](){ this->addLine(); });
    this->del_button->setOnPressEnd([this](){ this->deleteSelected(); });

    // Pop()で戻ってきたときも文書はメンバに残っている
    this->refreshList();
    this->refreshStatus();
}

void TextEditorScene::onExit(){
    this->back_button = nullptr;
    this->new_button = nullptr;
    this->open_button = nullptr;
    this->save_button = nullptr;
    this->add_button = nullptr;
    this->del_button = nullptr;
    this->status_label = nullptr;
    this->list = nullptr;
    this->pending = Pending::None;
}

void TextEditorScene::onUpdate(){
    if(this->pending == Pending::None) return;
    if(this->pending_wait_frames > 0){
        this->pending_wait_frames--;
        return;
    }
    const Pending p = this->pending;
    this->pending = Pending::None;
    if(p == Pending::OpenPicker) this->openPicker();
}
