#include "gui/scenes/FileExplorerScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/CrashDump_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/scenes/FileViewerScene.hpp"
#include "gui/scenes/FileSearchScene.hpp"
#include "gui/scenes/StorageScene.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

namespace {
    Button* MakeToolButton(const char* text, int x, int y){
        Button* b = new Button(x, y, text);
        b->setFontSize(FontFn::Small);
        b->setH(20 + Button::kFrameExtra);
        return b;
    }
}

void FileExplorerScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int top = content.y + MARGIN;

    //上の行: [戻る] [コピー][移動][貼付][他](各ボタンの実測幅で左から詰める)
    int x = content.x + MARGIN;
    auto place = [&](Button* b){
        b->setX(x);
        x += b->getLocalRect().w + MARGIN;
        WidgetFunctions::Add(b);
    };

    this->back_button = MakeToolButton("戻る", x, top);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    place(this->back_button);

    this->copy_button = MakeToolButton("コピー", x, top);
    this->copy_button->setOnPressEnd([this](){ this->onCopyOrMove(false); });
    place(this->copy_button);

    this->move_button = MakeToolButton("移動", x, top);
    this->move_button->setOnPressEnd([this](){ this->onCopyOrMove(true); });
    place(this->move_button);

    this->paste_button = MakeToolButton("貼付", x, top);
    this->paste_button->setOnPressEnd([this](){ this->onPaste(); });
    place(this->paste_button);

    this->more_button = MakeToolButton("他", x, top);
    this->more_button->setOnPressEnd([this](){ this->onMore(); });
    place(this->more_button);

    const Rect back_box = this->back_button->getLocalRect();
    const int status_y = top + back_box.h + 1;
    constexpr int kStatusH = 16;

    this->status_label = new Label<PICO_STR_LL>(content.x + MARGIN, status_y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setMaxHeight(kStatusH);
    WidgetFunctions::Add(this->status_label);

    const int body_y = status_y + kStatusH + 1;
    this->explorer = new FileExplorer(
        content.x, (int16_t)body_y,
        content.w, (int16_t)(content.y + content.h - body_y)
    );
    //ファイルを2回タップしたら中身を見る(ファイルビューワーを上へ積む。「戻る」でここへ戻る)
    this->explorer->setOnFileTap([](const char* path){
        SceneFunctions::Push(new FileViewerScene(path));
    });
    WidgetFunctions::Add(this->explorer);

    //検索でフォルダを選んで戻ってきたらそのフォルダ、そうでなければ前に開いていたフォルダ
    FixedString<PICO_PATH_LEN> jump;
    if(FileSearchScene::TakeJump(jump)){
        this->explorer->setCurrentFolderPath(jump.c_str());
    }else if(this->saved_folder.length() > 0 && OSData::SD.exists(this->saved_folder.c_str())){
        this->explorer->setCurrentFolderPath(this->saved_folder.c_str());
    }

    this->showClipStatus();
}

void FileExplorerScene::onMore(){
    auto* d = new PickerDialog(PickerDialog::Mode::Choice, "ファイル", "", "キャンセル");
    d->addChoice("名前の変更");
    d->addChoice("検索(SD全体)");
    d->addChoice("容量");
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClosed([this, d](bool ok){
        const int sel = ok ? d->getSelectedIndex() : -1;
        WidgetFunctions::DestroyLater(d);
        //ダイアログからダイアログへは1フレーム空ける(閉じたダイアログの跡が残らないように)
        if(sel == 0){
            this->pending = Pending::Rename;
        }else if(sel == 1){
            this->pending = Pending::Search;
        }else if(sel == 2){
            const char* cur = this->explorer ? this->explorer->getCurrentFolderPath() : "/";
            SceneFunctions::Push(new StorageScene(cur && *cur ? cur : "/"));
        }
    });
}

void FileExplorerScene::onUpdate(){
    if(this->pending == Pending::None) return;
    if(++this->pending_frames < 2) return;
    const Pending p = this->pending;
    this->pending = Pending::None;
    this->pending_frames = 0;
    if(p == Pending::Rename) this->onRename();
    else if(p == Pending::Search) this->askSearch();
}

void FileExplorerScene::askSearch(){
    if(!OSData::SD_usable){ this->setStatus("SDカードがありません", true); return; }
    auto* d = new InputDialog("探す名前(一部でよい):", true);
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->openKeyboard();
    d->setOnClosed([d](bool ok){
        const auto q = d->getInput();
        WidgetFunctions::DestroyLater(d);
        if(ok && q.length() > 0) SceneFunctions::Push(new FileSearchScene("/", q.c_str()));
    });
}

void FileExplorerScene::onExit(){
    //クリップボード(clip_path)はメンバなので残す。ウィジェットは破棄されるので忘れる
    if(this->explorer) this->saved_folder.assign(this->explorer->getCurrentFolderPath());
    this->back_button   = nullptr;
    this->more_button   = nullptr;
    this->copy_button   = nullptr;
    this->move_button   = nullptr;
    this->paste_button  = nullptr;
    this->status_label  = nullptr;
    this->explorer      = nullptr;
}

void FileExplorerScene::setStatus(const char* text, bool is_error){
    if(!this->status_label) return;
    this->status_label->setTextColor(is_error ? PICO_RED : PICO_FORECOLOR);
    this->status_label->setText(text);
}

void FileExplorerScene::showClipStatus(){
    if(this->paste_button) this->paste_button->setEnabled(!(this->clip_path.length() == 0));
    if((this->clip_path.length() == 0)){
        this->setStatus("選んでコピー/移動→貼付");
        return;
    }
    char buf[PICO_STR_LL];
    snprintf(buf, sizeof(buf), "%s: %s", this->clip_is_move ? "移動" : "コピー",
             PICO_IO::filename(this->clip_path.c_str()));
    this->setStatus(buf);
}

void FileExplorerScene::clearClip(){
    this->clip_path.clear();
    this->clip_is_move = false;
    this->showClipStatus();
}

void FileExplorerScene::onCopyOrMove(bool is_move){
    if(!OSData::SD_usable){ this->setStatus("SDカードがありません", true); return; }
    const char* path = this->explorer ? this->explorer->getSelectedPath() : nullptr;
    if(!path){ this->setStatus("先にファイルかフォルダを選んでください", true); return; }
    this->clip_path.assign(path);
    this->clip_is_move = is_move;
    this->showClipStatus();
}

void FileExplorerScene::onPaste(){
    if((this->clip_path.length() == 0) || !this->explorer) return;
    if(!OSData::SD_usable){ this->setStatus("SDカードがありません", true); return; }

    const char* src = this->clip_path.c_str();
    if(!OSData::SD.exists(src)){
        this->clearClip();
        this->setStatus("元のファイルが見つかりません", true);
        return;
    }

    FixedString<PICO_PATH_LEN> dest_dir(this->explorer->getCurrentFolderPath());

    //フォルダを自分自身の中へは入れられない
    if(PICO_IO::isSameOrInside(dest_dir.c_str(), src)){
        this->setStatus("フォルダをその中へは置けません", true);
        return;
    }

    FixedString<PICO_PATH_LEN> src_parent;
    PICO_IO::parent(src_parent, src);
    if(this->clip_is_move && src_parent == dest_dir){
        //同じ場所への移動は何もしない
        this->clearClip();
        return;
    }

    FixedString<PICO_PATH_LEN> dst;
    if(!PICO_IO::uniquePath(dst, dest_dir.c_str(), PICO_IO::filename(src))){
        this->setStatus("貼り付け先の名前を作れません", true);
        return;
    }

    bool ok;
    if(this->clip_is_move){
        ok = OSData::SD.rename(src, dst.c_str());
    }else{
        this->setStatus("コピー中...");
        ok = PICO_IO::copyRecursive(src, dst.c_str(), &CrashDumpFunctions::Feed);
    }

    if(!ok){
        LOG_APP_WARN("ファイル: %sに失敗 %s -> %s", this->clip_is_move ? "移動" : "コピー", src, dst.c_str());
        this->setStatus(this->clip_is_move ? "移動できませんでした" : "コピーできませんでした", true);
        return;
    }

    this->explorer->refresh();
    this->explorer->selectByName(PICO_IO::filename(dst.c_str()));
    if(this->clip_is_move){
        this->clearClip(); //移動したものは元の場所に無いので忘れる
    }else{
        this->showClipStatus(); //コピーは続けて何度でも貼れる
    }
}

void FileExplorerScene::onRename(){
    if(!OSData::SD_usable){ this->setStatus("SDカードがありません", true); return; }
    const char* path = this->explorer ? this->explorer->getSelectedPath() : nullptr;
    if(!path){ this->setStatus("先にファイルかフォルダを選んでください", true); return; }

    //ダイアログが閉じるまでに一覧が変わっても、どれを変えるかは覚えた時点のパスで決める
    FixedString<PICO_PATH_LEN> old_path(path);
    auto dialog = new InputDialog("新しい名前:", true);
    dialog->setInput(PICO_IO::filename(old_path.c_str()));
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, old_path](bool is_submit){
        if(is_submit){
            const FixedString<PICO_STR_LL> name = dialog->getInput();
            this->commitRename(old_path.c_str(), name.c_str());
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void FileExplorerScene::commitRename(const char* old_path, const char* new_name){
    if(!this->explorer) return;
    if(!PICO_IO::isValidName(new_name)){
        this->setStatus("その名前は使えません", true);
        return;
    }
    if(strcmp(PICO_IO::filename(old_path), new_name) == 0) return; //変えていない

    FixedString<PICO_PATH_LEN> dir;
    FixedString<PICO_PATH_LEN> new_path;
    PICO_IO::parent(dir, old_path);
    if(!PICO_IO::join(new_path, dir.c_str(), new_name)){
        this->setStatus("名前が長すぎます", true);
        return;
    }
    //FATは大文字小文字を区別しないので、大小だけ変える場合は exists() が自分自身を指す
    const bool only_case = strcasecmp(PICO_IO::filename(old_path), new_name) == 0;
    if(!only_case && OSData::SD.exists(new_path.c_str())){
        this->setStatus("同じ名前があります", true);
        return;
    }
    if(!OSData::SD.rename(old_path, new_path.c_str())){
        LOG_APP_WARN("ファイル: 名前の変更に失敗 %s -> %s", old_path, new_path.c_str());
        this->setStatus("名前を変えられませんでした", true);
        return;
    }

    //クリップボードがこれ(またはこの中)を指していたら、もう同じパスには無い
    if(!(this->clip_path.length() == 0) && PICO_IO::isSameOrInside(this->clip_path.c_str(), old_path)){
        this->clearClip();
    }else{
        this->showClipStatus();
    }
    this->explorer->refresh();
    this->explorer->selectByName(new_name);
}
