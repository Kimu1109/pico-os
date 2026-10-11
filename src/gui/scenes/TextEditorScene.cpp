#include "gui/scenes/TextEditorScene.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "functions/Clipboard_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "functions/GFX_Functions.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
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

bool TextEditorScene::edit(int start, int end, const char* s, int n, bool chained){
    if(this->len - (end - start) + n > kMaxBytes) return false;

    //変わっていない頭と尻を除いて、変わった所だけを記録する(1文字打つたびに行全体を記録しない)。
    //切れ目はUTF-8の文字の境目に揃える(元に戻した後のカーソルが文字の途中へ行かないように)
    const int old_n = end - start;
    int pre = 0;
    while(pre < old_n && pre < n && this->text[start + pre] == s[pre]) pre++;
    while(pre > 0 && ((pre < old_n && ((uint8_t)this->text[start + pre] & 0xC0) == 0x80) ||
                      (pre < n && ((uint8_t)s[pre] & 0xC0) == 0x80))) pre--;
    int suf = 0;
    while(suf < old_n - pre && suf < n - pre && this->text[end - 1 - suf] == s[n - 1 - suf]) suf++;
    while(suf > 0 && ((uint8_t)this->text[end - suf] & 0xC0) == 0x80) suf--;

    const int a = start + pre;
    const int del_n = old_n - pre - suf;
    const int ins_n = n - pre - suf;
    if(del_n == 0 && ins_n == 0) return true;

    this->history.record(a, this->text + a, del_n, s + pre, ins_n, (uint32_t)millis(), chained);
    return this->replaceRange(a, a + del_n, s + pre, ins_n);
}

const char* TextEditorScene::checkLimits(int start, int end, const char* s, int n) const {
    if(this->len - (end - start) + n > kMaxBytes) return "容量の上限(32KiB)を超えます";

    int lines = this->lineCount();
    for(int i = start; i < end; i++) if(this->text[i] == '\n') lines--;
    for(int i = 0; i < n; i++) if(s[i] == '\n') lines++;
    if(lines > kMaxLines) return "行数の上限(1000行)を超えます";

    int ls, le, es, ee;
    this->lineRange(this->lineOfByte(start), ls, le);
    this->lineRange(this->lineOfByte(end), es, ee);
    int cur = start - ls;
    for(int i = 0; i < n; i++){
        if(s[i] == '\n'){
            if(cur > kMaxLineBytes) return "1行が長くなりすぎます(191バイトまで)";
            cur = 0;
        }else{
            cur++;
        }
    }
    if(cur + (ee - end) > kMaxLineBytes) return "1行が長くなりすぎます(191バイトまで)";
    return nullptr;
}

int TextEditorScene::findForward(const char* needle, int n, int from) const {
    if(n <= 0) return -1;
    for(int i = from < 0 ? 0 : from; i + n <= this->len; i++){
        if(this->text[i] == needle[0] && memcmp(this->text + i, needle, n) == 0) return i;
    }
    return -1;
}

int TextEditorScene::findBackward(const char* needle, int n, int before) const {
    if(n <= 0) return -1;
    int i = before - 1;
    if(i + n > this->len) i = this->len - n;
    for(; i >= 0; i--){
        if(this->text[i] == needle[0] && memcmp(this->text + i, needle, n) == 0) return i;
    }
    return -1;
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
    const int top = this->search_open ? this->view_top_search : this->view_top_normal;
    if(bottom == this->last_view_bottom && top == this->last_view_top) return;
    this->last_view_bottom = bottom;
    this->last_view_top = top;

    if(top != this->view->getLocalRect().y){
        PICO_GFX::MarkDirty(this->view->getScreenRect());
        this->view->setY(top);
    }
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

        if(this->lineCount() >= kMaxLines || !this->edit(start, end, s.c_str(), (int)s.length())){
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
        this->sel_anchor = -1;
        this->updateDirty();
        this->refreshView();
        this->attachLine(kb, this->cur_line + 1, col);
        return;
    }

    //行の中身が変わっていれば文書へ書き戻す
    const int old_n = end - start;
    if(old_n != (int)s.length() || memcmp(this->text + start, s.c_str(), old_n) != 0){
        if(!this->edit(start, end, s.c_str(), (int)s.length())){
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
            this->sel_anchor = -1; //文字を打ったら選択は外れる
            this->updateDirty();
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
    this->refreshSelection();
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
    this->edit(pe, pe + 1, "", 0);
    this->sel_anchor = -1;
    this->updateDirty();
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
    this->edit(ce, ce + 1, "", 0);
    this->sel_anchor = -1;
    this->updateDirty();
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

    if(ev.key == Key::Char && ev.ctrl() && !ev.alt()){
        uint32_t c = ev.cp;
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        switch(c){
            case 's': this->saveFile(); return true;
            case 'z': if(ev.shift()) this->redo(); else this->undo(); return true;
            case 'y': this->redo(); return true;
            case 'c': this->copySelection(false); return true;
            case 'x': this->copySelection(true); return true;
            case 'v': this->paste(); return true;
            case 'a': this->selectAll(); return true;
            case 'f': this->openFindDialog(false); return true;
            case 'h': this->openFindDialog(true); return true;
            case 'g':
                if(this->find_text.empty()) this->openFindDialog(false);
                else this->findNext(ev.shift());
                return true;
            default: break;
        }
    }

    const bool nav = ev.key == Key::Left || ev.key == Key::Right || ev.key == Key::Up || ev.key == Key::Down
                  || ev.key == Key::Home || ev.key == Key::End || ev.key == Key::PageUp || ev.key == Key::PageDown;
    if(nav){
        //Shift+移動で選択を広げる。Shift無しで動かすと選択は外れる
        if(ev.shift()){
            if(this->sel_from_search) this->sel_from_search = false; //見つけた所から選択を広げる
            if(this->sel_anchor < 0) this->sel_anchor = this->cursorPos();
        }else if(this->sel_anchor >= 0){
            this->clearSelection();
        }
    }

    if(ev.key == Key::Escape){
        if(this->sel_anchor >= 0){ this->clearSelection(); return true; }
        if(this->search_open){ this->setSearchOpen(false); return true; }
        return false;
    }

    //選択中のBackspace/Deleteは選択範囲を消す
    if((ev.key == Key::Backspace || ev.key == Key::Delete) && !ev.ctrl() && !ev.alt()){
        int a, b;
        if(this->selection(a, b)){
            this->deleteSelection();
            return true;
        }
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


// ---------------------------------------------------------------- 選択・クリップボード・元に戻す

int TextEditorScene::cursorPos() const {
    int c = this->view ? (int)this->view->getCursor() : this->len;
    return c > this->len ? this->len : c;
}

bool TextEditorScene::selection(int& start, int& end) const {
    if(this->sel_anchor < 0) return false;
    const int c = this->cursorPos();
    int a = this->sel_anchor > this->len ? this->len : this->sel_anchor;
    if(a == c) return false;
    start = a < c ? a : c;
    end = a < c ? c : a;
    return true;
}

void TextEditorScene::clearSelection(){
    this->sel_anchor = -1;
    this->refreshSelection();
}

void TextEditorScene::refreshSelection(){
    if(this->sel_from_search && this->cursorPos() != this->sel_search_cursor){
        this->sel_from_search = false;
        this->sel_anchor = -1;
    }
    if(!this->view) return;
    int a, b;
    if(this->selection(a, b)) this->view->setSelection((size_t)a, (size_t)(b - a));
    else this->view->setSelection(0, 0);
}

ITextInputWidget* TextEditorScene::attachedPanel(){
    KeyboardPanel* panel = KeyboardFunctions::VisiblePanel();
    if(panel && panel->getInputTarget() == this) return panel;
    return nullptr;
}

void TextEditorScene::moveCursorTo(int byte_offset){
    if(byte_offset < 0) byte_offset = 0;
    if(byte_offset > this->len) byte_offset = this->len;
    this->history.breakMerge();
    if(this->view) this->view->setCursor((size_t)byte_offset);

    ITextInputWidget* kb = this->attachedPanel();
    if(kb){
        //キーボードへ今の行を渡し直す(syncFromKeyboard()がカーソル・選択の表示まで合わせる)
        const int line = this->lineOfByte(byte_offset);
        int start, end;
        this->lineRange(line, start, end);
        this->attachLine(kb, line, byte_offset - start);
        return;
    }
    if(this->view) this->view->ensureCursorVisible();
    this->refreshSelection();
}

void TextEditorScene::undo(){
    EditHistory::Step st;
    int cursor = -1;
    while(this->history.undo(st)){
        this->replaceRange(st.pos, st.pos + st.ins_n, st.del, st.del_n);
        cursor = st.pos + st.del_n;
        if(!st.chained) break;
    }
    if(cursor < 0){
        this->refreshStatus("これ以上元に戻せません");
        return;
    }
    this->sel_anchor = -1;
    this->refreshView();
    this->moveCursorTo(cursor);
    this->updateDirty();
    if(this->search_open) this->refreshFindCount();
}

void TextEditorScene::redo(){
    EditHistory::Step st;
    int cursor = -1;
    do{
        if(!this->history.redo(st)) break;
        this->replaceRange(st.pos, st.pos + st.del_n, st.ins, st.ins_n);
        cursor = st.pos + st.ins_n;
    }while(this->history.nextRedoChained());
    if(cursor < 0){
        this->refreshStatus("やり直せる操作がありません");
        return;
    }
    this->sel_anchor = -1;
    this->refreshView();
    this->moveCursorTo(cursor);
    this->updateDirty();
    if(this->search_open) this->refreshFindCount();
}

void TextEditorScene::copySelection(bool cut){
    int a, b;
    const bool has_sel = this->selection(a, b);
    bool whole = true;
    if(has_sel){
        //入り切らない分まで消してしまわないよう、切り取りはクリップボードの上限まで
        if(cut && b - a > (int)ClipboardFunctions::kMaxBytes){
            this->refreshStatus("切り取れるのは4KiBまでです");
            return;
        }
        whole = ClipboardFunctions::Set(this->text + a, (size_t)(b - a));
    }else{
        //選択が無ければ今の行を改行込みで
        const int line = this->lineOfByte(this->cursorPos());
        this->lineRange(line, a, b);
        FixedString<PICO_STR_256B> one;
        one.assign(this->text + a, (size_t)(b - a));
        one.append("\n");
        ClipboardFunctions::Set(one.c_str(), one.length());
        //消すときは行ごと(改行も)。最後の行なら前の改行を消す
        if(b < this->len) b++;
        else if(a > 0) a--;
    }

    if(!cut){
        this->refreshStatus(!whole ? "4KiBまでコピーしました" : has_sel ? "コピーしました" : "1行コピーしました");
        return;
    }
    const char* why = this->checkLimits(a, b, "", 0);
    if(why){
        this->refreshStatus(why);
        return;
    }
    this->edit(a, b, "", 0);
    this->sel_anchor = -1;
    this->refreshView();
    //行を消したときは、次の行(最後の行なら前の行)の頭へ
    int to = a;
    if(!has_sel && a > 0 && a == this->len){
        int s0, e0;
        this->lineRange(this->lineOfByte(a), s0, e0);
        to = s0;
    }
    this->moveCursorTo(to);
    this->updateDirty();
    this->refreshStatus("切り取りました");
    if(this->search_open) this->refreshFindCount();
}

void TextEditorScene::paste(){
    if(ClipboardFunctions::Empty()){
        this->refreshStatus("クリップボードは空です");
        return;
    }
    const char* clip = ClipboardFunctions::Get();
    const int n = (int)ClipboardFunctions::Length();
    int a, b;
    if(!this->selection(a, b)) a = b = this->cursorPos();

    const char* why = this->checkLimits(a, b, clip, n);
    if(why){
        this->refreshStatus(why);
        return;
    }
    this->history.breakMerge(); //貼り付けは1回で1件
    this->edit(a, b, clip, n);
    this->history.breakMerge();
    this->sel_anchor = -1;
    this->refreshView();
    this->moveCursorTo(a + n);
    this->updateDirty();
    if(this->search_open) this->refreshFindCount();
}

void TextEditorScene::deleteSelection(){
    int a, b;
    if(!this->selection(a, b)) return;
    const char* why = this->checkLimits(a, b, "", 0);
    if(why){
        this->refreshStatus(why);
        return;
    }
    this->history.breakMerge();
    this->edit(a, b, "", 0);
    this->history.breakMerge();
    this->sel_anchor = -1;
    this->refreshView();
    this->moveCursorTo(a);
    this->updateDirty();
    if(this->search_open) this->refreshFindCount();
}

void TextEditorScene::selectAll(){
    this->sel_from_search = false;
    this->sel_anchor = 0;
    this->moveCursorTo(this->len);
}

void TextEditorScene::toggleSelect(){
    if(this->sel_anchor >= 0){
        this->clearSelection();
        this->refreshStatus("選択を解除しました");
        return;
    }
    this->sel_from_search = false;
    this->sel_anchor = this->cursorPos();
    this->refreshSelection();
    this->refreshStatus("範囲選択: 終わりの位置をタップ");
}

void TextEditorScene::openMenu(){
    //据え置きのキーボードはダイアログより手前に出るので閉じる(終わったら開き直す)
    const bool reopen = KeyboardFunctions::IsDocked();
    KeyboardFunctions::HideAll();

    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Choice, "編集", "", "閉じる");
    if(!d) return;
    static const char* const kItems[] = {
        "元に戻す", "やり直し", "範囲選択", "すべて選択",
        "コピー", "切り取り", "貼り付け", "検索", "置換", "新規作成",
    };
    for(const char* item : kItems){
        if(item == kItems[2] && this->sel_anchor >= 0) d->addChoice("選択を解除");
        else d->addChoice(item);
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClosed([this, d, reopen](bool is_ok){
        const int idx = is_ok ? d->getSelectedIndex() : -1;
        WidgetFunctions::DestroyLater(d);
        if(!this->view) return;
        switch(idx){
            case 0: this->undo(); break;
            case 1: this->redo(); break;
            case 2: this->toggleSelect(); break;
            case 3: this->selectAll(); break;
            case 4: this->copySelection(false); break;
            case 5: this->copySelection(true); break;
            case 6: this->paste(); break;
            case 7:
            case 8:
            case 9:
                //ダイアログからダイアログへは1フレーム空ける
                this->pending = idx == 7 ? Pending::OpenFind : idx == 8 ? Pending::OpenReplace : Pending::NewDocument;
                this->pending_wait_frames = 1;
                return;
            default: break;
        }
        if(reopen) this->openKeyboard();
    });
}

// ---------------------------------------------------------------- 検索/置換

void TextEditorScene::openFindDialog(bool replace){
    for(Widget* w : WidgetFunctions::dialog_roots){
        if(w && w->getVisible()) return; //別のダイアログが開いている
    }
    if(KeyboardFunctions::VisiblePanel()){
        //据え置きのキーボードを閉じてから、1フレーム空けて開く(半透明のダイアログの下に跡が残らないように)
        KeyboardFunctions::HideAll();
        this->pending = replace ? Pending::OpenReplace : Pending::OpenFind;
        this->pending_wait_frames = 1;
        return;
    }
    InputDialog* d = new InputDialog(replace ? "置換: 探す文字列" : "検索する文字列", true, "決定", "キャンセル");
    if(!d) return;
    d->setInput(this->find_text.c_str());
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->openKeyboard();
    d->setOnClosed([this, d, replace](bool is_ok){
        FixedString<PICO_STR_LL> input = d->getInput();
        WidgetFunctions::DestroyLater(d);
        if(!is_ok || !this->view || input.empty()) return;
        this->find_text.assign(input.c_str());
        if(replace){
            this->pending = Pending::OpenReplaceWith;
            this->pending_wait_frames = 1;
            return;
        }
        this->replace_mode = false;
        this->setSearchOpen(true);
        if(!this->findNext(false)) this->refreshStatus("見つかりません");
    });
}

void TextEditorScene::openReplaceWithDialog(){
    InputDialog* d = new InputDialog("置き換える文字列(空なら消す)", true, "決定", "キャンセル");
    if(!d) return;
    d->setInput(this->replace_text.c_str());
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->openKeyboard();
    d->setOnClosed([this, d](bool is_ok){
        FixedString<PICO_STR_LL> input = d->getInput();
        WidgetFunctions::DestroyLater(d);
        if(!is_ok || !this->view) return;
        this->replace_text.assign(input.c_str());
        this->replace_mode = true;
        this->setSearchOpen(true);
        if(!this->findNext(false)) this->refreshStatus("見つかりません");
    });
}

void TextEditorScene::setSearchOpen(bool open){
    this->search_open = open;
    Widget* bar[] = { this->find_count_label, this->find_prev_button, this->find_next_button, this->find_close_button };
    for(Widget* w : bar) if(w) w->setVisible(open);
    if(this->replace_one_button) this->replace_one_button->setVisible(open && this->replace_mode);
    if(this->replace_all_button) this->replace_all_button->setVisible(open && this->replace_mode);
    //見えているボタンを右から詰めて並べる: 前 次 [置換 全部] ×
    if(this->find_close_button){
        Button* order[] = { this->find_close_button, this->replace_all_button, this->replace_one_button,
                            this->find_next_button, this->find_prev_button };
        int right = this->find_close_button->getLocalRect().x + this->find_close_button->getLocalRect().w;
        for(Button* b : order){
            if(!b || !b->getVisible()) continue;
            right -= b->getLocalRect().w;
            b->setX(right);
            right -= MARGIN;
        }
    }
    if(open) this->refreshFindCount();
    else this->refreshStatus();
    this->layoutView();
}

void TextEditorScene::refreshFindCount(){
    if(!this->find_count_label) return;
    const int n = (int)this->find_text.length();
    int total = 0, index = 0;
    int a, b;
    const bool on_match = this->selection(a, b) && b - a == n && memcmp(this->text + a, this->find_text.c_str(), n) == 0;
    for(int p = this->findForward(this->find_text.c_str(), n, 0); p >= 0; p = this->findForward(this->find_text.c_str(), n, p + n)){
        total++;
        if(on_match && p == a) index = total;
    }
    char buf[PICO_STR_S];
    if(total == 0) snprintf(buf, sizeof(buf), "0件");
    else if(index > 0) snprintf(buf, sizeof(buf), "%d/%d", index, total);
    else snprintf(buf, sizeof(buf), "%d件", total);
    this->find_count_label->setText(buf);
}

bool TextEditorScene::findNext(bool backward){
    const int n = (int)this->find_text.length();
    if(n == 0) return false;
    int a, b;
    if(!this->selection(a, b)) a = b = this->cursorPos();

    int p;
    if(backward){
        p = this->findBackward(this->find_text.c_str(), n, a);
        if(p < 0) p = this->findBackward(this->find_text.c_str(), n, this->len + 1); //末尾から回り込む
    }else{
        p = this->findForward(this->find_text.c_str(), n, b);
        if(p < 0) p = this->findForward(this->find_text.c_str(), n, 0); //先頭から回り込む
    }
    if(p < 0){
        this->refreshFindCount();
        return false;
    }
    //見つけた所を選択する(カーソルは終わり)
    this->sel_anchor = p;
    this->sel_from_search = true;
    this->sel_search_cursor = p + n;
    this->moveCursorTo(p + n);
    this->refreshFindCount();
    return true;
}

void TextEditorScene::replaceOne(){
    const int n = (int)this->find_text.length();
    int a, b;
    if(!(this->selection(a, b) && b - a == n && memcmp(this->text + a, this->find_text.c_str(), n) == 0)){
        //選んでいる所が一致していなければ、まず次を探すだけ
        if(!this->findNext(false)) this->refreshStatus("見つかりません");
        return;
    }
    const int rn = (int)this->replace_text.length();
    const char* why = this->checkLimits(a, b, this->replace_text.c_str(), rn);
    if(why){
        this->refreshStatus(why);
        return;
    }
    this->history.breakMerge();
    this->edit(a, b, this->replace_text.c_str(), rn);
    this->history.breakMerge();
    this->updateDirty();
    this->refreshView();
    this->sel_anchor = -1;
    this->moveCursorTo(a + rn);
    if(!this->findNext(false)) this->refreshStatus("すべて置換しました");
}

void TextEditorScene::replaceAll(){
    const int n = (int)this->find_text.length();
    if(n == 0) return;
    const int rn = (int)this->replace_text.length();
    int count = 0;
    int last = -1;
    const char* why = nullptr;
    this->history.breakMerge();
    for(int p = this->findForward(this->find_text.c_str(), n, 0); p >= 0;
        p = this->findForward(this->find_text.c_str(), n, p + rn)){
        why = this->checkLimits(p, p + n, this->replace_text.c_str(), rn);
        if(why) break;
        //2件目からは1件目とひとまとまり(元に戻すと全部戻る)
        this->edit(p, p + n, this->replace_text.c_str(), rn, count > 0);
        count++;
        last = p + rn;
    }
    this->history.breakMerge();
    if(count == 0){
        this->refreshStatus(why ? why : "見つかりません");
        return;
    }
    this->sel_anchor = -1;
    this->refreshView();
    this->moveCursorTo(last);
    this->updateDirty();
    this->refreshFindCount();
    char buf[PICO_STR_L];
    if(why) snprintf(buf, sizeof(buf), "%d件置換(残りは%s)", count, why);
    else snprintf(buf, sizeof(buf), "%d件置換しました", count);
    this->refreshStatus(buf);
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
    this->sel_anchor = -1;
    this->history.clear();
    this->refreshView();
    if(this->view) this->view->setCursor(0);
    this->refreshSelection();
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
    this->sel_anchor = -1;
    this->history.clear();
    this->refreshView();
    if(this->view) this->view->setCursor(0);
    this->refreshSelection();
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
        this->history.markSaved();
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
                    this->history.markSaved();
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
        b->setH(20 + Button::kFrameExtra);
        WidgetFunctions::Add(b);
        return b;
    };

    // 1段目: 戻る 開く 保存 … 元に戻す 編集メニュー キーボードの出し入れ(右端)。
    // 「新規」は幅が足りないので編集メニューの中
    int x = content.x + MARGIN;
    const int y1 = content.y + MARGIN;
    this->back_button = make_button("戻る", x, y1);
    x += this->back_button->getLocalRect().w + MARGIN;
    this->open_button = make_button("開く", x, y1);
    x += this->open_button->getLocalRect().w + MARGIN;
    this->save_button = make_button("保存", x, y1);

    const int row_h = this->back_button->getLocalRect().h;
    this->kb_button = new Button(0, y1, "");
    this->kb_button->setIcon(IconID::Keyboard, IconSize::Px16);
    this->kb_button->setW(28 + Button::kFrameExtra);
    this->kb_button->setH(20 + Button::kFrameExtra); //他のボタンと同じ指定(描かれる箱は枠のぶん大きくなる)
    this->kb_button->setX(content.x + content.w - MARGIN - this->kb_button->getLocalRect().w);
    WidgetFunctions::Add(this->kb_button);

    // 元に戻す・編集メニュー(キーボードのボタンの左)
    auto make_icon_button = [&](IconID icon, int right){
        Button* b = new Button(0, y1, "");
        b->setIcon(icon, IconSize::Px16);
        b->setW(22 + Button::kFrameExtra);
        b->setH(20 + Button::kFrameExtra);
        b->setX(right - b->getLocalRect().w);
        WidgetFunctions::Add(b);
        return b;
    };
    this->menu_button = make_icon_button(IconID::DotsVertical, this->kb_button->getLocalRect().x - MARGIN);
    this->undo_button = make_icon_button(IconID::Undo, this->menu_button->getLocalRect().x - MARGIN);

    // 2段目: ファイル名/状態
    const int y2 = y1 + row_h + MARGIN;
    this->status_label = new Label<PICO_STR_L>(content.x + MARGIN, y2, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setDisableAutoTextDecoration(true); //ファイル名の_や*を装飾にしない
    WidgetFunctions::Add(this->status_label);

    const int view_y = y2 + Label<PICO_STR_L>::GetLineHeight(FontFn::Small) + MARGIN;

    // 検索バー(検索中だけ出す): 件数 … 前 次 [置換 すべて] ×
    const int y3 = view_y;
    this->find_count_label = new Label<PICO_STR_S>(content.x + MARGIN, y3 + 4, "");
    this->find_count_label->setFontSize(FontFn::Small);
    this->find_count_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->find_count_label);
    int bx = content.x + content.w - MARGIN;
    auto make_bar_button = [&](const char* label){
        Button* b = make_button(label, 0, y3);
        bx -= b->getLocalRect().w;
        b->setX(bx);
        bx -= MARGIN;
        return b;
    };
    this->find_close_button = make_bar_button("×");
    this->replace_all_button = make_bar_button("全部");
    this->replace_one_button = make_bar_button("置換");
    this->find_next_button = make_bar_button("次");
    this->find_prev_button = make_bar_button("前");
    this->view_top_normal = view_y;
    this->view_top_search = y3 + row_h + MARGIN;

    const int start_top = this->search_open ? this->view_top_search : view_y;
    this->view = new TextView(content.x, start_top, content.w, content.y + content.h - start_top);
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
    this->open_button->setOnPressEnd([this](){
        this->confirmDiscard("保存していない変更があります。破棄して別のファイルを開きますか?", [this](){
            KeyboardFunctions::HideAll();
            this->pending = Pending::OpenPicker;
            this->pending_wait_frames = 1;
        });
    });
    this->save_button->setOnPressEnd([this](){ this->saveFile(); });
    this->kb_button->setOnPressEnd([this](){ this->toggleKeyboard(); });
    this->undo_button->setOnPressEnd([this](){ this->undo(); });
    this->menu_button->setOnPressEnd([this](){ this->openMenu(); });
    this->find_prev_button->setOnPressEnd([this](){ if(!this->findNext(true)) this->refreshStatus("見つかりません"); });
    this->find_next_button->setOnPressEnd([this](){ if(!this->findNext(false)) this->refreshStatus("見つかりません"); });
    this->replace_one_button->setOnPressEnd([this](){ this->replaceOne(); });
    this->replace_all_button->setOnPressEnd([this](){ this->replaceAll(); });
    this->find_close_button->setOnPressEnd([this](){ this->setSearchOpen(false); });

    // Pop()で戻ってきたときも文書はメンバに残っている
    this->refreshView();
    this->refreshStatus();
    this->last_view_bottom = -1;
    this->last_view_top = -1;
    this->setSearchOpen(this->search_open); //検索バーの表示(Pop()で戻ってきたときも前の状態)
    this->refreshSelection();

    if(!this->initial_loaded){
        this->initial_loaded = true;
        if(!this->initial_path.empty()) this->loadFile(this->initial_path.c_str());
    }
}

void TextEditorScene::onExit(){
    //キーボードはSceneFunctionsが先に閉じている(onHide済み)
    KeyboardFunctions::UnregisterInputTarget(this);
    this->back_button = nullptr;
    this->open_button = nullptr;
    this->save_button = nullptr;
    this->kb_button = nullptr;
    this->undo_button = nullptr;
    this->menu_button = nullptr;
    this->status_label = nullptr;
    this->view = nullptr;
    this->find_count_label = nullptr;
    this->find_prev_button = nullptr;
    this->find_next_button = nullptr;
    this->replace_one_button = nullptr;
    this->replace_all_button = nullptr;
    this->find_close_button = nullptr;
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
    else if(p == Pending::OpenFind) this->openFindDialog(false);
    else if(p == Pending::OpenReplace) this->openFindDialog(true);
    else if(p == Pending::OpenReplaceWith) this->openReplaceWithDialog();
    else if(p == Pending::NewDocument){
        this->confirmDiscard("保存していない変更があります。破棄して新規作成しますか?", [this](){
            this->newDocument();
        });
    }
}
