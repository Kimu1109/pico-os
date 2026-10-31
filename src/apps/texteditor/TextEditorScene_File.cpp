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
#include "storage/Recent_Files.hpp"
#include "gui/icons/File_Icon.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>


// ---------------------------------------------------------------- ファイル

void TextEditorScene::confirmDiscard(const char* msg, InplaceFunction<void()> then){
    if(!this->dirty){
        then();
        return;
    }
    KeyboardFunctions::HideAll(); //キーボードはダイアログより手前に出るので先に閉じる
    MsgDialog* dialog = new MsgDialog(msg, TR("キャンセル"), TR("破棄する"));
    if(!dialog) return;
    dialog->setVisibleIcon(true);
    dialog->setIconId(IconID::AlertTriangle);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    this->discard_then = then;
    dialog->setOnClosed([this, dialog](bool is_ok){
        WidgetFunctions::DestroyLater(dialog);
        //呼ぶ前に外す(thenの中でまたconfirmDiscardを呼んでもよいように)
        InplaceFunction<void()> next = this->discard_then;
        this->discard_then = nullptr;
        if(is_ok && next) next();
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
        ErrorFunctions::ShowFatal(TR("SDカードが使えません"));
        return false;
    }
    FsFile f = OSData::SD.open(file);
    if(!f){
        ErrorFunctions::ShowFatal(TR("ファイルを開けませんでした"));
        return false;
    }
    if(f.fileSize() > (uint32_t)kMaxBytes){
        f.close();
        ErrorFunctions::ShowFatal(TR("大きすぎて開けません(32KiBまで)"));
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
                ErrorFunctions::ShowFatal(TR("1行が長すぎて開けません(191バイトまで)"));
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
        ErrorFunctions::ShowFatal(TR("行数が多すぎて開けません(1000行まで)"));
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
    RecentFiles::Touch(file);
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

void TextEditorScene::openRecentPicker(){
    //一覧はダイアログを開いている間だけ確保する(閉じたら・画面を出たら放す)
    this->freeRecent();
    this->recent_paths = new FixedString<PICO_PATH_LEN>[RecentFiles::kMaxFiles];
    if(!this->recent_paths) return;
    FixedString<PICO_PATH_LEN>* paths = this->recent_paths;
    const int n = RecentFiles::List(paths, RecentFiles::kMaxFiles);
    //エディタで開けそうなもの(文字のファイル)だけ。今開いているものは除く
    int count = 0;
    for(int i = 0; i < n; i++){
        if(!FileIcon::IsText(PICO_IO::filename(paths[i].c_str()))) continue;
        if(paths[i] == this->path) continue;
        if(count != i) paths[count] = paths[i];
        count++;
    }
    this->recent_count = count;
    if(count == 0){
        this->freeRecent();
        this->refreshStatus(TR("最近開いたファイルはありません"));
        return;
    }
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Choice, TR("最近開いたファイル"), "", TR("閉じる"));
    if(!d){ this->freeRecent(); return; }
    for(int i = 0; i < count; i++){
        //名前の後ろに、どのフォルダにあるか(ファイル検索と同じ見せ方)
        const char* p = paths[i].c_str();
        const char* name = PICO_IO::filename(p);
        FixedString<PICO_PATH_LEN> label(name);
        const size_t dir_len = (size_t)(name - p);
        if(dir_len > 1){
            (void)label.append("  ");
            (void)label.append(p, dir_len - 1);
        }
        d->addChoice(label.c_str());
    }
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClosed([this, d](bool is_ok){
        const int idx = is_ok ? d->getSelectedIndex() : -1;
        WidgetFunctions::DestroyLater(d);
        if(idx < 0 || idx >= this->recent_count || !this->view){
            this->freeRecent();
            return;
        }
        const FixedString<PICO_PATH_LEN> target = this->recent_paths[idx];
        this->freeRecent();
        if(!OSData::SD_usable || !OSData::SD.exists(target.c_str())){
            RecentFiles::Forget(target.c_str());
            ErrorFunctions::ShowFatal(TR("ファイルが見つかりません(一覧から外しました)"));
            return;
        }
        this->discard_open_path = target;
        this->confirmDiscard(TR("保存していない変更があります。破棄して別のファイルを開きますか?"), [this](){
            this->loadFile(this->discard_open_path.c_str());
        });
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
        RecentFiles::Touch(this->path.c_str());
        this->history.markSaved();
        this->setDirty(false);
    }else{
        ErrorFunctions::ShowFatal(TR("保存に失敗しました"));
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
                    RecentFiles::Touch(target);
                    this->history.markSaved();
                    this->setDirty(false);
                }else{
                    ErrorFunctions::ShowFatal(TR("保存に失敗しました"));
                }
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}
