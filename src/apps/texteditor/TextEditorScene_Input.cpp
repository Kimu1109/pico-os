// テキストエディタ(TextEditorScene)のキー盤との受け渡し(1行ずつ渡す)と物理キーボード
#include "apps/texteditor/TextEditorScene.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "system/Clipboard_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "gui/core/Error_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>


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
