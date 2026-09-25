#include "gui/scenes/MusicScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "sound/Mml_Compiler.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    bool EndsWithMml(const char* name){
        const size_t n = strlen(name);
        if(n < 4) return false;
        const char* ext = name + n - 4;
        return (ext[0] == '.') &&
               (ext[1] == 'm' || ext[1] == 'M') &&
               (ext[2] == 'm' || ext[2] == 'M') &&
               (ext[3] == 'l' || ext[3] == 'L');
    }

    enum Status { kNoSd, kEmpty, kStopped, kPlaying, kError };
}

void MusicScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    //Buttonの箱は文字の余白と立体ぶんが足された大きさになるので、実測して並べる
    const Rect back = this->back_button->getLocalRect();
    const int row_h = back.h;

    this->title_label = new Label<PICO_STR_M>(back.x + back.w + MARGIN * 2, y0, "ミュージック");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setY(y0 + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(this->title_label);

    // ---- 下から: [停止] と状態の1行 ----
    this->stop_button = new Button(content.x + MARGIN, 0, "停止");
    this->stop_button->setFontSize(FontFn::Small);
    this->stop_button->setH(20);
    const int stop_y = content.y + content.h - MARGIN - this->stop_button->getLocalRect().h - 8;
    this->stop_button->setY(stop_y);
    this->stop_button->setOnPressEnd([this](){
        SoundFunctions::MusicStop();
        this->refreshStatus();
    });
    WidgetFunctions::Add(this->stop_button);

    //状態の欄は2行ぶん取る(読めなかった理由は長くなる)
    const int status_y = stop_y - MARGIN - FontFn::GetFontSize(FontFn::Small) * 2 - 8;
    this->status_label = new Label<PICO_STR_256B>(content.x + MARGIN, status_y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    //ファイル名の _ や * をマークアップとして消さない
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    // ---- 曲の一覧 ----
    const int list_y = y0 + row_h + MARGIN + 8;
    this->list = new ScrollList(
        content.x + MARGIN, list_y,
        content.w - MARGIN * 2, status_y - MARGIN - list_y,
        kMaxFiles
    );
    this->list->setFontSize(FontFn::Small);
    //1回目のタップで選択、2回目で鳴らす(ScrollListの流儀)
    this->list->setOnSelectItem([this](int index, bool already_selected){
        if(already_selected){
            this->playIndex(index);
        }else{
            this->last_status = -1;     //誤りの表示を消して案内へ戻す
            this->refreshStatus();
        }
    });
    WidgetFunctions::Add(this->list);

    this->last_status = -1;
    this->reloadList();
    this->refreshStatus();
}

void MusicScene::onExit(){
    //閉じたら止める(このアプリの外から止める手段が無いため)
    SoundFunctions::MusicStop();

    this->back_button = nullptr;
    this->stop_button = nullptr;
    this->title_label = nullptr;
    this->status_label = nullptr;
    this->list = nullptr;
}

void MusicScene::onUpdate(){
    //L の無い曲が終わったら「止まっています」へ戻す
    this->refreshStatus();
}

void MusicScene::reloadList(){
    this->file_count = 0;
    if(this->list) this->list->clear();
    if(!OSData::SD_usable) return;

    FsFile dir = OSData::SD.open(PICO_Path::DIR::MUSIC);
    if(!dir) return;
    FsFile file;
    while(file.openNext(&dir, O_RDONLY)){
        char name[128];
        const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && EndsWithMml(name);
        file.close();
        if(!ok) continue;
        if(this->file_count >= kMaxFiles){
            LOG_APP_WARN("ミュージック: 曲が多すぎるため %s を並べません(上限%d)", name, kMaxFiles);
            continue;
        }
        FixedString<PICO_STR_M> n;
        if(!n.assign(name)){
            LOG_APP_WARN("ミュージック: ファイル名が長すぎるため並べません: %s", name);
            continue;
        }
        //挿入ソート(高々32件)
        int i = this->file_count++;
        while(i > 0 && strcmp(this->file_names[i - 1].c_str(), n.c_str()) > 0){
            this->file_names[i] = this->file_names[i - 1];
            i--;
        }
        this->file_names[i] = n;
    }
    dir.close();

    for(int i = 0; i < this->file_count; i++){
        ScrollListTools::Item item;
        item.icon = IconID::Music;
        item.text.assign(this->file_names[i].c_str());
        this->list->add(item);
    }
}

void MusicScene::playIndex(int index){
    if(index < 0 || index >= this->file_count) return;
    FixedString<PICO_PATH_LEN> path;
    path.assign(PICO_Path::DIR::MUSIC);
    path.append(this->file_names[index].c_str());

    MmlResult r;
    if(!SoundFunctions::MusicPlayFile(path.c_str(), &r)){
        //理由は状態の欄へ赤で出す(ダイアログは1行しか見せられず、行・列が切れてしまう)
        FixedString<PICO_STR_256B> msg;
        msg.appendFormat("%s を読めません\n", this->file_names[index].c_str());
        if(r.line > 0) msg.appendFormat("%d行%d列: ", r.line, r.col);
        msg.append(r.message.c_str());
        LOG_APP_WARN("ミュージック: %s", msg.c_str());
        this->status_label->setTextColor(PICO_RED);
        this->status_label->setText(msg.c_str());
        //次に状態が変わるまで(別の曲を選ぶ/鳴らす/止まる)この表示を残す
        this->last_status = kError;
        return;
    }
    this->last_status = -1;     //鳴らせたら誤りの表示を消す(同じ曲を鳴らし直した場合も)
    this->refreshStatus();
}

void MusicScene::refreshStatus(){
    if(!this->status_label) return;

    int status;
    if(!OSData::SD_usable) status = kNoSd;
    else if(SoundFunctions::MusicPlaying()) status = kPlaying;
    else if(this->file_count == 0) status = kEmpty;
    else status = kStopped;

    //読めなかった理由を出している間は、鳴っている曲が変わるまでそのまま
    if(this->last_status == kError && status != kPlaying) return;
    if(this->last_status == kError && status == kPlaying && this->last_title == SoundFunctions::MusicTitle()) return;

    const char* title = SoundFunctions::MusicTitle();
    if(status == this->last_status && (status != kPlaying || this->last_title == title)) return;
    this->last_status = status;
    this->last_title.assign(title);

    FixedString<PICO_STR_L> text;
    switch(status){
        case kNoSd:    text.assign("SDカードがありません"); break;
        case kEmpty:   text.assign("/music/ に .mml を置いてください"); break;
        case kStopped: text.assign("2回タップで再生"); break;
        case kPlaying:
            text.assign("再生中: ");
            text.append(title);
            //音が出ない本体でも曲は進むので、画面で知らせる
            if(!SoundFunctions::IsAvailable()) text.append("(音は出ません)");
            break;
    }
    this->status_label->setTextColor(PICO_BLACK);
    this->status_label->setText(text.c_str());
    if(this->stop_button) this->stop_button->setVisible(status == kPlaying);
}
