#include "apps/music/MusicScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "apps/music/ui/MusicPlayerPanel.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstring>

// ================================================================ 画面

void MusicScene::onEnter(){
    //44100Hzへ先に切り替えておく(loop()も毎フレーム preferredSampleRate() を渡すが、切り替えは鳴っている音を止めるので鳴らす前に)
    SoundFunctions::RequestSampleRate(this->preferredSampleRate());
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    //プレイリストを見ていればライブラリへ、ライブラリならアプリを閉じる(曲は鳴り続ける)
    this->back_button->setOnPressEnd([this](){
        if(this->view == kLibrary) SceneFunctions::Pop();
        else this->showLibrary();
    });
    WidgetFunctions::Add(this->back_button);
    const Rect back = this->back_button->getLocalRect();
    const int row_h = back.h;

    this->play_all_button = new Button(0, y0, "全曲");
    this->play_all_button->setFontSize(FontFn::Small);
    this->play_all_button->setH(20 + Button::kFrameExtra);
    this->play_all_button->setX(content.x + content.w - MARGIN - this->play_all_button->getLocalRect().w);
    //ミックスなら最初の曲も混ぜる
    this->play_all_button->setOnPressEnd([this](){ this->startPlaylist(-1); });
    WidgetFunctions::Add(this->play_all_button);

    this->title_label = new Label<PICO_STR_M>(back.x + back.w + MARGIN, y0, "");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setY(y0 + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    this->title_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->title_label);

    const int panel_y = content.y + content.h - MusicPlayerPanel::kHeight;
    this->panel = new MusicPlayerPanel(content.x, panel_y, content.w);
    this->panel->setOnButton([this](int b){ this->onPanelButton(b); });
    this->panel->setOnTextTap([this](){ this->showErrorDetail(); });
    this->panel->setOnSeek([](uint32_t ms){ MusicService::SeekMs(ms); });
    WidgetFunctions::Add(this->panel);

    const int list_y = y0 + row_h + MARGIN;
    this->list = new ScrollList(
        content.x + MARGIN, list_y,
        content.w - MARGIN * 2, panel_y - MARGIN - list_y,
        kMaxTracks
    );
    this->list->setFontSize(FontFn::Small);
    this->list->setEnableIcon(true);
    this->list->setOnSelectItem([this](int index, bool already){ this->onListSelect(index, already); });
    WidgetFunctions::Add(this->list);

    MusicService::LoadSettings();
    this->scanFolders();
    //鳴っているプレイリストがあればそれを開く(アプリを閉じて開き直したとき)
    int open = kLibrary;
    if(MusicService::HasQueue()){
        const int pl = MusicService::Playlist();
        FixedString<PICO_STR_M> name;
        this->playlistName(pl, name);
        if((pl == kAll || (pl >= 0 && pl < this->folder_count)) && strcmp(name.c_str(), MusicService::PlaylistName()) == 0) open = pl;
    }
    if(open == kLibrary) this->showLibrary();
    else this->showPlaylist(open);
    this->seen_revision = MusicService::Revision();
    this->refreshPanel();
}

void MusicScene::onExit(){
    //曲は止めない(MusicService が鳴らし続ける)
    this->back_button = nullptr;
    this->play_all_button = nullptr;
    this->title_label = nullptr;
    this->list = nullptr;
    this->panel = nullptr;
}

void MusicScene::onUpdate(){
    //曲の終わりで次へ進むのは MusicService(BackgroundFunctions から毎フレーム)。ここは見せるだけ。
    //この画面を開いている間は、鳴っていればスリープさせない(以前と同じ。閉じていれば、曲の切り替わりで眠ってもよい)
    if(MusicService::Active() && !MusicService::Paused()) PowerFunctions::KeepAwake();
    if(MusicService::Revision() != this->seen_revision){
        this->seen_revision = MusicService::Revision();
        this->refreshListColors();
        if(this->viewIsQueue() && MusicService::Current() >= 0){
            this->list->setSelectedIndex(MusicService::Current());
            this->list->ensureVisible(MusicService::Current());
        }
    }
    this->refreshPanel();
}

bool MusicScene::onKey(const KeyInputFunctions::Event& ev){
    //キーボードのメディアキー
    switch(ev.key){
        case KeyInputFunctions::Key::PlayPause: this->onPanelButton(MusicPlayerPanel::kPlay); return true;
        case KeyInputFunctions::Key::NextTrack: this->onPanelButton(MusicPlayerPanel::kNext); return true;
        case KeyInputFunctions::Key::PrevTrack: this->onPanelButton(MusicPlayerPanel::kPrev); return true;
        case KeyInputFunctions::Key::Stop:      MusicService::Stop(); this->refreshPanel(); return true;
        default: break;
    }
    if(!ev.isPlainChar()) return false;
    switch(ev.cp){
        case 'n': this->onPanelButton(MusicPlayerPanel::kNext); return true;
        case 'p': this->onPanelButton(MusicPlayerPanel::kPrev); return true;
        case 'k': this->onPanelButton(MusicPlayerPanel::kPlay); return true;
        case 's': this->onPanelButton(MusicPlayerPanel::kShuffle); return true;
        case 'r': this->onPanelButton(MusicPlayerPanel::kRepeat); return true;
        default: return false;
    }
}

// ================================================================ 一覧

void MusicScene::scanFolders(){
    this->folder_count = 0;
    if(!OSData::SD_usable) return;
    FsFile dir = OSData::SD.open(PICO_Path::DIR::MUSIC);
    if(!dir) return;
    FsFile file;
    while(file.openNext(&dir, O_RDONLY)){
        char name[128];
        const bool ok = file.getName(name, sizeof(name)) && file.isDirectory() && name[0] != '.';
        file.close();
        if(!ok) continue;
        FixedString<PICO_STR_M> n;
        if(!n.assign(name)){
            LOG_APP_WARN("ミュージック: フォルダ名が長すぎるため並べません: %s", name);
            continue;
        }
        if(this->folder_count >= kMaxFolders){
            LOG_APP_WARN("ミュージック: フォルダが多すぎるため %s を並べません(上限%d)", name, kMaxFolders);
            continue;
        }
        //名前順(SdFatの列挙順は作った順なので並べ替える。高々16件の挿入ソート)
        int i = this->folder_count++;
        while(i > 0 && strcmp(this->folders[i - 1].c_str(), n.c_str()) > 0){
            this->folders[i] = this->folders[i - 1];
            i--;
        }
        this->folders[i] = n;
    }
    dir.close();
}

int MusicScene::scanTracks(int playlist, Track* out){
    int count = 0;
    if(!OSData::SD_usable) return 0;

    //「すべての曲」は直下と全部のフォルダ、それ以外はそのフォルダだけ
    const int first = playlist == kAll ? -1 : playlist;
    const int last  = playlist == kAll ? this->folder_count - 1 : playlist;
    for(int f = first; f <= last; f++){
        FixedString<PICO_PATH_LEN> path;
        path.assign(PICO_Path::DIR::MUSIC);
        if(f >= 0) path.append(this->folders[f].c_str());
        FsFile dir = OSData::SD.open(path.c_str());
        if(!dir) continue;
        FsFile file;
        while(file.openNext(&dir, O_RDONLY)){
            char name[128];
            const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && (MusicService::IsMml(name) || MusicService::IsWav(name));
            file.close();
            if(!ok) continue;
            Track t;
            t.folder = (int8_t)f;
            if(!t.name.assign(name)){
                LOG_APP_WARN("ミュージック: ファイル名が長すぎるため並べません: %s", name);
                continue;
            }
            if(count >= kMaxTracks){
                LOG_APP_WARN("ミュージック: 曲が多すぎるため %s を並べません(上限%d)", name, kMaxTracks);
                continue;
            }
            //名前順の挿入ソート(同じ名前ならフォルダ順)
            int i = count++;
            while(i > 0){
                const int c = strcmp(out[i - 1].name.c_str(), t.name.c_str());
                if(c < 0 || (c == 0 && out[i - 1].folder <= t.folder)) break;
                out[i] = out[i - 1];
                i--;
            }
            out[i] = t;
        }
        dir.close();
    }
    return count;
}

void MusicScene::playlistName(int playlist, FixedString<PICO_STR_M>& out) const {
    if(playlist == kAll) out.assign("すべての曲");
    else if(playlist >= 0 && playlist < this->folder_count) out.assign(this->folders[playlist].c_str());
    else out.assign("ミュージック");
}

void MusicScene::showLibrary(){
    this->view = kLibrary;
    this->view_count = 0;
    this->list->clear();
    if(OSData::SD_usable){
        ScrollListTools::Item all;
        all.icon = IconID::Music;
        all.text.assign("すべての曲");
        this->list->add(all);
        for(int i = 0; i < this->folder_count; i++){
            ScrollListTools::Item item;
            item.icon = IconID::Folder;
            item.text.assign(this->folders[i].c_str());
            this->list->add(item);
        }
    }
    this->refreshListColors();
    this->refreshHeader();
}

void MusicScene::showPlaylist(int playlist){
    this->view = playlist;
    this->view_count = this->scanTracks(playlist, this->view_tracks);
    this->list->clear();
    for(int i = 0; i < this->view_count; i++){
        ScrollListTools::Item item;
        item.icon = IconID::Music;
        MusicService::DisplayName(this->view_tracks[i].name.c_str(), item.text);
        this->list->add(item);
    }
    this->refreshListColors();
    this->refreshHeader();

    //鳴っている曲のプレイリストなら、その曲が見えるように
    if(this->viewIsQueue() && MusicService::Current() >= 0){
        const int cur = MusicService::Current();
        this->list->setSelectedIndex(cur);
        this->list->ensureVisible(cur);
    }
}

void MusicScene::refreshHeader(){
    this->play_all_button->setVisible(this->view != kLibrary && this->view_count > 0);
    const int right = this->play_all_button->getVisible()
        ? this->play_all_button->getLocalRect().x : Scene::contentRect().x + Scene::contentRect().w;
    const int avail = right - this->title_label->getLocalRect().x - MARGIN;

    FixedString<PICO_STR_M> name, count;
    if(this->view == kLibrary){
        name.assign("ミュージック");
    }else{
        this->playlistName(this->view, name);
        count.appendFormat(" (%d)", this->view_count);
    }
    //Labelは幅を超えると折り返すので、1行に収まるまで名前を縮めて「..」を付ける
    FixedString<PICO_STR_M> t;
    t.assign(name.c_str());
    t.append(count.c_str());
    int chars = FixedString<PICO_STR_M>::charCount(name.c_str());
    while(chars > 1 && Label<PICO_STR_M>::GetTextWidth(FontFn::Small, t.c_str()) > avail){
        chars--;
        t.assign(name.c_str(), (size_t)FixedString<PICO_STR_M>::byteOffsetOfChar(name.c_str(), chars));
        t.append("..");
        t.append(count.c_str());
    }
    this->title_label->setText(t.c_str());
}

bool MusicScene::viewIsQueue() const {
    if(this->view == kLibrary || !MusicService::HasQueue()) return false;
    if(MusicService::Playlist() != this->view) return false;
    FixedString<PICO_STR_M> name;
    this->playlistName(this->view, name);
    return strcmp(name.c_str(), MusicService::PlaylistName()) == 0;
}

void MusicScene::refreshListColors(){
    if(!this->list) return;
    const bool playing = MusicService::Active();
    if(this->view == kLibrary){
        //鳴っているプレイリストを緑に
        for(int i = 0; i <= this->folder_count; i++){
            ScrollListTools::Item* it = this->list->itemAt(i);
            if(!it) break;
            const int pl = i == 0 ? kAll : i - 1;
            FixedString<PICO_STR_M> name;
            this->playlistName(pl, name);
            const bool same = MusicService::HasQueue() && MusicService::Playlist() == pl
                              && strcmp(name.c_str(), MusicService::PlaylistName()) == 0;
            it->color = (playing && same) ? (int8_t)PICO_DARKGREEN : (int8_t)-1;
        }
    }else{
        const bool same = this->viewIsQueue();
        const int cur = playing ? MusicService::Current() : -1;
        const uint64_t failed = MusicService::FailedMask();
        for(int i = 0; i < this->view_count; i++){
            ScrollListTools::Item* it = this->list->itemAt(i);
            if(!it) break;
            int8_t c = -1;
            if(same){
                if(i == cur) c = PICO_DARKGREEN;
                else if(failed & ((uint64_t)1 << i)) c = PICO_RED;
            }
            it->color = c;
        }
    }
    this->list->needsRender();
}

void MusicScene::onListSelect(int index, bool already_selected){
    //タッチ(離した瞬間)なら1回で、キー/コントローラーで選んでいるときは決定(=2回目)で開く/鳴らす
    //(↑↓で選ぶたびに曲が変わらないように)
    if(!OSData::isTouchEnd && !already_selected) return;
    if(this->view == kLibrary){
        if(index < 0 || index > this->folder_count) return;
        this->showPlaylist(index == 0 ? kAll : index - 1);
        return;
    }
    if(index < 0 || index >= this->view_count) return;
    this->startPlaylist(index);
}

// ================================================================ 再生(MusicService へ渡すだけ)

void MusicScene::startPlaylist(int track){
    if(this->view == kLibrary || this->view_count == 0) return;
    FixedString<PICO_STR_M> name;
    this->playlistName(this->view, name);
    MusicService::StartPlaylist(this->view_tracks, this->view_count, this->folders, this->folder_count,
                                this->view, name.c_str(), track);
    this->refreshListColors();
}

void MusicScene::showErrorDetail(){
    if(!MusicService::ErrorShown()) return;
    //理由は欄に収まらないので全文をダイアログで(長ければダイアログの中でスクロールする)
    FixedString<PICO_STR_512B> text;
    text.appendFormat("%s を読めません\n\n%s", MusicService::ErrorTrack(), MusicService::ErrorDetail());
    MsgDialog* dialog = new MsgDialog(text.c_str(), "", "閉じる");
    if(!dialog) return;
    dialog->setVisibleIcon(true);
    dialog->setIconId(IconID::AlertTriangle);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([dialog](bool){ WidgetFunctions::DestroyLater(dialog); });
}

void MusicScene::onPanelButton(int button){
    switch(button){
        case MusicPlayerPanel::kPlay:
            //前の順番があれば続きから、無ければ見ているプレイリスト(ライブラリなら全部の曲)を鳴らす
            if(MusicService::HasQueue()){
                MusicService::TogglePlay();
            }else{
                if(this->view == kLibrary) this->showPlaylist(kAll);
                this->startPlaylist(-1);
            }
            break;
        case MusicPlayerPanel::kNext: MusicService::Next(); break;
        case MusicPlayerPanel::kPrev: MusicService::Prev(); break;
        case MusicPlayerPanel::kShuffle: MusicService::SetShuffle(!MusicService::Shuffle()); break;
        case MusicPlayerPanel::kRepeat: MusicService::CycleRepeat(); break;
        default: break;
    }
    this->refreshPanel();
}

// ================================================================ 表示

void MusicScene::refreshPanel(){
    if(!this->panel) return;
    const bool active = MusicService::Active();
    const bool wav = MusicService::PlayingKind() == MusicService::Kind::Wav;
    const bool paused = MusicService::Paused();
    this->panel->setPlaying(active, paused);
    this->panel->setModes(MusicService::Shuffle(), (uint8_t)MusicService::Repeat());
    this->panel->setHasTracks(MusicService::HasQueue() || (this->view != kLibrary && this->view_count > 0)
                              || (this->view == kLibrary && OSData::SD_usable));

    //曲名と下の行
    FixedString<PICO_PATH_LEN> title;
    FixedString<PICO_STR_256B> sub;
    int8_t sub_color = PICO_DARKGREY;
    const int cur = MusicService::Current();
    if(cur >= 0 && MusicService::HasQueue()){
        MusicService::DisplayName(MusicService::TrackAt(cur)->name.c_str(), title);
        const char* pl = MusicService::PlaylistName();
        if(active){
            sub.appendFormat("%s · %d/%d", pl, MusicService::Position() + 1, MusicService::Count());
            if(paused) sub.append(" 一時停止中");
            //音が出ない本体でも曲は進むので、画面で知らせる
            if(!SoundFunctions::IsAvailable()) sub.append("(音は出ません)");
        }else if(MusicService::Finished()){
            sub.appendFormat("%s · 再生が終わりました", pl);
        }else if(MusicService::Interrupted()){
            sub.appendFormat("%s · 他のアプリが音を使ったため止まりました", pl);
        }else{
            sub.appendFormat("%s · 停止中", pl);
        }
    }else if(!OSData::SD_usable){
        title.assign("SDカードがありません");
    }else if(this->view == kLibrary && this->folder_count == 0){
        title.assign("曲が選ばれていません");
        sub.assign("/music/ に .mml か .wav を置くか、フォルダ(=プレイリスト)を作ってください");
    }else{
        title.assign("曲が選ばれていません");
        sub.assign("曲をタップすると再生します");
    }
    //読めなかった理由。欄には頭しか入らないので、タップで全文を出せることを1行目に書く
    if(MusicService::ErrorShown()){
        FixedString<PICO_PATH_LEN> name;
        MusicService::DisplayName(MusicService::ErrorTrack(), name);
        sub.assign("");
        if(MusicService::ErrorSticky()) sub.append("読めません(タップで全文)\n");
        else sub.appendFormat("%s を飛ばしました(タップで全文)\n", name.c_str());
        sub.append(MusicService::ErrorDetail());
        sub_color = PICO_RED;
    }
    FixedString<PICO_STR_M> t;
    t.assign(title.c_str());
    this->panel->setTrack(t.c_str(), sub.c_str(), sub_color);

    //時間
    if(active){
        if(wav){
            this->panel->setTime(SoundFunctions::WavPositionMs(), SoundFunctions::WavDurationMs(), false, true);
        }else{
            bool loops = false;
            const uint32_t total = SoundFunctions::MusicTotalMs(&loops);
            //1周で次へ進むときは、1周を曲の長さとして見せる
            const bool endless = loops && MusicService::Repeat() == PlayQueue::Repeat::One;
            this->panel->setTime(SoundFunctions::MusicElapsedMs(), total, endless, false);
        }
    }
}
