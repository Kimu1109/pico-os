// テキストエディタ(TextEditorScene)のファイル(新規・開く・保存)
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
