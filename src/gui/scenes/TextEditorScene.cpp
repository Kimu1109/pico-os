#include "gui/scenes/TextEditorScene.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------- 文書

int TextEditorScene::lineCount() const {
    int n = 1;
    for(int i = 0; i < this->len; i++){
        if(this->text[i] == '\n') n++;
    }
    return n;
}

int TextEditorScene::lineOfByte(int byte_offset) const {
    int line = 0;
    for(int i = 0; i < byte_offset && i < this->len; i++){
        if(this->text[i] == '\n') line++;
    }
    return line;
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

bool TextEditorScene::replaceRange(int start, int end, const char* s, int n){
    const int total = this->len - (end - start) + n;
    if(total > kMaxBytes) return false;

    memmove(this->text + start + n, this->text + end, this->len - end);
    memcpy(this->text + start, s, n);
    this->len = total;
    this->text[this->len] = '\0';
    return true;
}

// ---------------------------------------------------------------- 表示

void TextEditorScene::refreshView(){
    if(this->view) this->view->setDocument(this->text, this->len);
}

void TextEditorScene::refreshStatus(const char* message){
    if(!this->status_label) return;
    char buf[PICO_STR_L];
    if(message){
        snprintf(buf, sizeof(buf), "%s", message);
    }else{
        snprintf(buf, sizeof(buf), "%s%s",
            this->path.empty() ? "(新規)" : PICO_IO::filename(this->path.c_str()),
            this->dirty ? " (未保存)" : "");
    }
    this->status_label->setText(buf);
}

void TextEditorScene::setDirty(bool value){
    this->dirty = value;
    this->refreshStatus();
}

void TextEditorScene::layoutView(){
    if(!this->view) return;
    //据え置きのキーボードが出ていれば、その上端までに本文欄を縮める
    const int bottom = KeyboardFunctions::IsDocked() ? KeyboardFunctions::VisibleTop() : SCREEN_HEIGHT;
    if(bottom == this->last_view_bottom) return;
    this->last_view_bottom = bottom;

    const int top = this->view->getLocalRect().y;
    this->view->setH(bottom - top);
    this->view->ensureCursorVisible();
}

// ---------------------------------------------------------------- キーボード

void TextEditorScene::openKeyboard(){
    KeyboardFunctions::Show(this, KeyboardFunctions::Layout::Japanese, true);
}

void TextEditorScene::toggleKeyboard(){
    if(KeyboardFunctions::IsDocked()){
        KeyboardFunctions::HideAll();
    }else{
        this->openKeyboard();
    }
}

void TextEditorScene::attachLine(ITextInputWidget* kb, int line, int col){
    int start, end;
    this->lineRange(line, start, end);
    if(col > end - start) col = end - start;
    this->cur_line = line;

    FixedString<PICO_STR_LL> s;
    s.assign(this->text + start, (size_t)(end - start));

    this->syncing = true;
    kb->setText(s);
    kb->setCursorByteOffset((size_t)col);
    this->syncing = false;

    this->syncFromKeyboard(kb);
}

void TextEditorScene::syncFromKeyboard(ITextInputWidget* kb){
    FixedString<PICO_STR_LL> s = kb->getText();
    size_t cursor = kb->getCursorByteOffset();

    int start, end;
    this->lineRange(this->cur_line, start, end);

    //改行キー: '\n'の後ろを次の行としてキーボードへ渡し直す
    const char* nl = strchr(s.c_str(), '\n');
    if(nl){
        const int p = (int)(nl - s.c_str());
        FixedString<PICO_STR_LL> rest;
        rest.assign(nl + 1);
        int col = (int)cursor - p - 1;
        if(col < 0) col = 0;

        if(this->lineCount() >= kMaxLines || !this->replaceRange(start, end, s.c_str(), (int)s.length())){
            //増やせないので改行を取り消す
            FixedString<PICO_STR_LL> joined;
            joined.assign(s.c_str(), (size_t)p);
            joined.append(rest);
            this->syncing = true;
            kb->setText(joined);
            kb->setCursorByteOffset((size_t)p);
            this->syncing = false;
            this->refreshStatus("これ以上行を増やせません");
            this->syncFromKeyboard(kb);
            return;
        }
        this->setDirty(true);
        this->refreshView();
        this->attachLine(kb, this->cur_line + 1, col);
        return;
    }

    //行の中身が変わっていれば文書へ書き戻す
    const int old_n = end - start;
    if(old_n != (int)s.length() || memcmp(this->text + start, s.c_str(), old_n) != 0){
        if(!this->replaceRange(start, end, s.c_str(), (int)s.length())){
            //容量の上限。キーボード側を文書の内容へ戻す
            FixedString<PICO_STR_LL> old;
            old.assign(this->text + start, (size_t)old_n);
            size_t c = cursor > (size_t)old_n ? (size_t)old_n : cursor;
            this->syncing = true;
            kb->setText(old);
            kb->setCursorByteOffset(c);
            this->syncing = false;
            this->refreshStatus("容量の上限(32KiB)です");
            s = old;
            cursor = c;
        }else{
            this->setDirty(true);
        }
        this->refreshView();
    }

    size_t comp_start = 0, comp_len = 0;
    kb->getComposition(comp_start, comp_len);
    if(this->view){
        this->view->setCursor((size_t)start + cursor);
        this->view->setComposition((size_t)start + comp_start, comp_len);
        this->view->ensureCursorVisible();
    }
}

void TextEditorScene::onShow(ITextInputWidget* keyboard){
    const int cursor = this->view ? (int)this->view->getCursor() : this->len;
    const int line = this->lineOfByte(cursor);
    int start, end;
    this->lineRange(line, start, end);
    this->attachLine(keyboard, line, cursor - start);
}

void TextEditorScene::onTextChanged(ITextInputWidget*){
    //文書への書き戻しはonDisplayChanged()でまとめて行う
}

void TextEditorScene::onHide(ITextInputWidget*){
    //変換中の読みはそのまま文字として残る(文書へは書き戻し済み)
    if(this->view) this->view->setComposition(0, 0);
}

void TextEditorScene::onDisplayChanged(ITextInputWidget* keyboard){
    if(this->syncing) return;
    this->syncFromKeyboard(keyboard);
}

bool TextEditorScene::onBackspaceAtStart(ITextInputWidget* keyboard){
    if(this->cur_line <= 0) return true;

    int ps, pe, cs, ce;
    this->lineRange(this->cur_line - 1, ps, pe);
    this->lineRange(this->cur_line, cs, ce);
    if((pe - ps) + (ce - cs) > kMaxLineBytes){
        this->refreshStatus("1行が長くなりすぎるため繋げられません");
        return true;
    }

    //前の行の末尾の'\n'を消すだけで2行が繋がる
    this->replaceRange(pe, pe + 1, "", 0);
    this->setDirty(true);
    this->refreshView();
    this->attachLine(keyboard, this->cur_line - 1, pe - ps);
    return true;
}

bool TextEditorScene::onCursorAtEdge(ITextInputWidget* keyboard, int dir){
    if(dir < 0 && this->cur_line > 0){
        int s, e;
        this->lineRange(this->cur_line - 1, s, e);
        this->attachLine(keyboard, this->cur_line - 1, e - s);
    }else if(dir > 0 && this->cur_line + 1 < this->lineCount()){
        this->attachLine(keyboard, this->cur_line + 1, 0);
    }
    return true;
}

bool TextEditorScene::onDeleteAtEnd(ITextInputWidget* keyboard){
    if(this->cur_line + 1 >= this->lineCount()) return true;

    int cs, ce, ns, ne;
    this->lineRange(this->cur_line, cs, ce);
    this->lineRange(this->cur_line + 1, ns, ne);
    if((ce - cs) + (ne - ns) > kMaxLineBytes){
        this->refreshStatus("1行が長くなりすぎるため繋げられません");
        return true;
    }

    //この行の末尾の'\n'を消すだけで次の行と繋がる。カーソルは繋ぎ目のまま
    this->replaceRange(ce, ce + 1, "", 0);
    this->setDirty(true);
    this->refreshView();
    this->attachLine(keyboard, this->cur_line, ce - cs);
    return true;
}

bool TextEditorScene::onKey(const KeyInputFunctions::Event& ev){
    using KeyInputFunctions::Key;

    //ダイアログ(保存/開く/破棄の確認)を出している間は触らない(キー盤へ回す/捨てる)
    for(Widget* d : WidgetFunctions::dialog_roots){
        if(d && d->getVisible()) return false;
    }

    if(ev.key == Key::Char && ev.ctrl() && (ev.cp == 's' || ev.cp == 'S')){
        this->saveFile();
        return true;
    }

    KeyboardPanel* panel = KeyboardFunctions::VisiblePanel();
    if(panel && panel->getInputTarget() != this) panel = nullptr;

    int lines = 0;
    switch(ev.key){
        case Key::Up:       lines = -1;  break;
        case Key::Down:     lines = 1;   break;
        case Key::PageUp:   lines = -10; break;
        case Key::PageDown: lines = 10;  break;
        default: break;
    }
    if(lines != 0){
        if(!panel){
            this->openKeyboard(); //onShow()がカーソルの行を渡す
            panel = KeyboardFunctions::VisiblePanel();
            if(!panel) return true;
        }
        int next = this->cur_line + lines;
        if(next < 0) next = 0;
        if(next > this->lineCount() - 1) next = this->lineCount() - 1;
        if(next != this->cur_line){
            //桁はバイト数のまま引き継ぐ(attachLine()が行の長さで頭打ちにする)
            this->attachLine(panel, next, (int)panel->getCursorByteOffset());
        }
        return true;
    }

    //閉じているキーボードを開いてから、打鍵そのものはキー盤に入れてもらう
    if(!panel){
        const bool edits = ev.isPlainChar() || ev.key == Key::Enter || ev.key == Key::Backspace
                        || ev.key == Key::Delete || ev.key == Key::Left || ev.key == Key::Right
                        || ev.key == Key::Home || ev.key == Key::End;
        if(edits) this->openKeyboard();
    }
    return false;
}

// ---------------------------------------------------------------- ファイル

void TextEditorScene::confirmDiscard(const char* msg, std::function<void()> then){
    if(!this->dirty){
        then();
        return;
    }
    KeyboardFunctions::HideAll(); //キーボードはダイアログより手前に出るので先に閉じる
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
    KeyboardFunctions::HideAll();
    this->len = 0;
    this->text[0] = '\0';
    this->path.clear();
    this->refreshView();
    if(this->view) this->view->setCursor(0);
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
        ErrorFunctions::ShowFatal("大きすぎて開けません(32KiBまで)");
        return false;
    }

    // 上限を超える内容は保存時に消えてしまうので、切り詰めずに断る。
    // 文書バッファと同じ大きさの作業領域は持たず、先に小さなチャンクで検査してから本体へ読み込む
    char chunk[256];
    int out = 0, lines = 1, line_bytes = 0;
    char last = '\0';
    for(;;){
        const int n = f.read((uint8_t*)chunk, sizeof(chunk));
        if(n <= 0) break;
        for(int i = 0; i < n; i++){
            const char c = chunk[i];
            if(c == '\r') continue;
            if(c == '\n'){
                lines++;
                line_bytes = 0;
            }else if(++line_bytes > kMaxLineBytes){
                f.close();
                ErrorFunctions::ShowFatal("1行が長すぎて開けません(191バイトまで)");
                return false;
            }
            out++;
            last = c;
        }
    }
    if(out > 0 && last == '\n'){ // 末尾の改行は保存時に付け直す
        out--;
        lines--;
    }
    if(lines > kMaxLines){
        f.close();
        ErrorFunctions::ShowFatal("行数が多すぎて開けません(1000行まで)");
        return false;
    }

    KeyboardFunctions::HideAll();
    f.seekSet(0);
    int w = 0;
    for(;;){
        const int n = f.read((uint8_t*)chunk, sizeof(chunk));
        if(n <= 0) break;
        for(int i = 0; i < n && w < out; i++){
            if(chunk[i] != '\r') this->text[w++] = chunk[i];
        }
    }
    f.close();
    this->len = out;
    this->text[out] = '\0';
    this->path.assign(file);
    this->refreshView();
    if(this->view) this->view->setCursor(0);
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
        if(selected && this->view) this->loadFile(selected);
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
    KeyboardFunctions::HideAll();
    FileSaveDialog* dialog = new FileSaveDialog("/");
    if(!dialog) return;
    dialog->setFileName("memo.txt");
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClose([this, dialog](bool is_ok){
        if(is_ok && this->view){
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

// ---------------------------------------------------------------- シーン

TextEditorScene::~TextEditorScene(){
    KeyboardFunctions::UnregisterInputTarget(this);
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

    // 1段目: 戻る 新規 開く 保存 … キーボードの出し入れ(右端)
    int x = content.x + MARGIN;
    const int y1 = content.y + MARGIN;
    this->back_button = make_button("戻る", x, y1);
    x += this->back_button->getLocalRect().w + MARGIN;
    this->new_button = make_button("新規", x, y1);
    x += this->new_button->getLocalRect().w + MARGIN;
    this->open_button = make_button("開く", x, y1);
    x += this->open_button->getLocalRect().w + MARGIN;
    this->save_button = make_button("保存", x, y1);

    const int row_h = this->back_button->getLocalRect().h;
    this->kb_button = new Button(0, y1, "");
    this->kb_button->setIcon(IconID::Keyboard, IconSize::Px16);
    this->kb_button->setW(28);
    this->kb_button->setH(20); //他のボタンと同じ指定(描かれる箱は枠のぶん大きくなる)
    this->kb_button->setX(content.x + content.w - MARGIN - this->kb_button->getLocalRect().w);
    WidgetFunctions::Add(this->kb_button);

    // 2段目: ファイル名/状態
    const int y2 = y1 + row_h + MARGIN;
    this->status_label = new Label<PICO_STR_L>(content.x + MARGIN, y2, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setDisableAutoTextDecoration(true); //ファイル名の_や*を装飾にしない
    WidgetFunctions::Add(this->status_label);

    const int view_y = y2 + Label<PICO_STR_L>::GetLineHeight(FontFn::Small) + MARGIN;
    this->view = new TextView(content.x, view_y, content.w, content.y + content.h - view_y);
    this->view->setCursorVisible(true);
    this->view->setOnTap([this](size_t byte_offset){
        this->view->setCursor(byte_offset);
        if(!KeyboardFunctions::IsDocked()){
            this->openKeyboard(); //onShow()がカーソルの行をキーボードへ渡す
            return;
        }
        //開いているキー盤(日本語/英字のどちらか)へ、タップした行を渡し直す
        Widget* panels[] = { OSData::keyboard_jpn, OSData::keyboard_eng, OSData::keyboard_num };
        for(Widget* w : panels){
            if(!w->getVisible()) continue;
            ITextInputWidget* panel = static_cast<ITextInputWidget*>(static_cast<KeyboardPanel*>(w));
            const int line = this->lineOfByte((int)byte_offset);
            int start, end;
            this->lineRange(line, start, end);
            this->attachLine(panel, line, (int)byte_offset - start);
            break;
        }
    });
    WidgetFunctions::Add(this->view);

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
            KeyboardFunctions::HideAll();
            this->pending = Pending::OpenPicker;
            this->pending_wait_frames = 1;
        });
    });
    this->save_button->setOnPressEnd([this](){ this->saveFile(); });
    this->kb_button->setOnPressEnd([this](){ this->toggleKeyboard(); });

    // Pop()で戻ってきたときも文書はメンバに残っている
    this->refreshView();
    this->refreshStatus();
    this->last_view_bottom = -1;
    this->layoutView();

    if(!this->initial_loaded){
        this->initial_loaded = true;
        if(!this->initial_path.empty()) this->loadFile(this->initial_path.c_str());
    }
}

void TextEditorScene::onExit(){
    //キーボードはSceneFunctionsが先に閉じている(onHide済み)
    KeyboardFunctions::UnregisterInputTarget(this);
    this->back_button = nullptr;
    this->new_button = nullptr;
    this->open_button = nullptr;
    this->save_button = nullptr;
    this->kb_button = nullptr;
    this->status_label = nullptr;
    this->view = nullptr;
    this->pending = Pending::None;
}

void TextEditorScene::onUpdate(){
    this->layoutView();

    if(this->pending == Pending::None) return;
    if(this->pending_wait_frames > 0){
        this->pending_wait_frames--;
        return;
    }
    const Pending p = this->pending;
    this->pending = Pending::None;
    if(p == Pending::OpenPicker) this->openPicker();
}
