// テキストエディタ(TextEditorScene)の選択・クリップボード・元に戻す・編集メニュー・検索/置換
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
            this->refreshStatus("切り取れるのは1KiBまでです");
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
        this->refreshStatus(!whole ? "1KiBまでコピーしました" : has_sel ? "コピーしました" : "1行コピーしました");
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
        "コピー", "切り取り", "貼り付け", "検索", "置換", "新規作成", "最近開いたファイル",
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
            case 10:
                //ダイアログからダイアログへは1フレーム空ける
                this->pending = idx == 7 ? Pending::OpenFind : idx == 8 ? Pending::OpenReplace :
                                idx == 9 ? Pending::NewDocument : Pending::OpenRecent;
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
