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

// ---------------------------------------------------------------- シーン

TextEditorScene::~TextEditorScene(){
    KeyboardFunctions::UnregisterInputTarget(this);
    this->freeRecent();
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
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    x += this->back_button->getLocalRect().w + MARGIN;
    this->open_button = make_button("開く", x, y1);
    this->open_button->setIcon(IconID::FolderOpen, IconSize::Px16);
    x += this->open_button->getLocalRect().w + MARGIN;
    this->save_button = make_button("保存", x, y1);
    this->save_button->setIcon(IconID::Save, IconSize::Px16);

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
        if(this->initial_is_new){
            this->newDocument();
            this->path = this->initial_path;
            this->refreshStatus();
        }else if(!this->initial_path.empty()){
            this->loadFile(this->initial_path.c_str());
        }
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
    this->freeRecent();
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
    else if(p == Pending::OpenRecent) this->openRecentPicker();
    else if(p == Pending::OpenFind) this->openFindDialog(false);
    else if(p == Pending::OpenReplace) this->openFindDialog(true);
    else if(p == Pending::OpenReplaceWith) this->openReplaceWithDialog();
    else if(p == Pending::NewDocument){
        this->confirmDiscard("保存していない変更があります。破棄して新規作成しますか?", [this](){
            this->newDocument();
        });
    }
}
