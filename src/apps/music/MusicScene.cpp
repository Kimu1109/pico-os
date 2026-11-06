#include "apps/music/MusicScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "apps/music/ui/MusicPlayerPanel.hpp"
#include "apps/music/Music_Library.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

#include <cstdint>
#include <cstring>

// ================================================================ 画面

void MusicScene::onEnter(){
    //曲に合う周波数(44100か48000Hz)へ先に切り替えておく(loop()も毎フレーム preferredSampleRate() を渡すが、切り替えは鳴っている音を止めるので鳴らす前に)
    SoundFunctions::RequestSampleRate(this->preferredSampleRate());
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, y0, TR("戻る"));
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    //プレイリストを見ていればライブラリへ、ライブラリならアプリを閉じる(曲は鳴り続ける)
    this->back_button->setOnPressEnd([this](){
        if(this->view == kLibrary) SceneFunctions::Pop();
        else if(this->view == kArtistTracks) this->showGroups(kArtists);
        else if(this->view == kAlbumTracks) this->showGroups(kAlbums);
        else this->showLibrary();
    });
    WidgetFunctions::Add(this->back_button);
    const Rect back = this->back_button->getLocalRect();
    const int row_h = back.h;

    this->play_all_button = new Button(0, y0, TR("全曲"));
    this->play_all_button->setFontSize(FontFn::Small);
    this->play_all_button->setH(20 + Button::kFrameExtra);
    this->play_all_button->setX(content.x + content.w - MARGIN - this->play_all_button->getLocalRect().w);
    //ミックスなら最初の曲も混ぜる
    this->play_all_button->setOnPressEnd([this](){ this->startPlaylist(-1); });
    WidgetFunctions::Add(this->play_all_button);

    //スリープタイマー。位置は refreshHeader() が決める(全曲のボタンの左、無ければ右端)
    this->sleep_button = new Button(0, y0, "");
    this->sleep_button->setIcon(IconID::Hourglass, IconSize::Px16);
    this->sleep_button->setFontSize(FontFn::Small);
    this->sleep_button->setH(20 + Button::kFrameExtra);
    this->sleep_button->setOnPressEnd([this](){ this->showSleepPicker(); });
    WidgetFunctions::Add(this->sleep_button);
    this->shown_sleep = -2;

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
    this->list->setFontSize(FontFn::Text); //一覧の文字は文字の大きさの設定に従う
    this->list->setEnableIcon(true);
    this->list->setOnSelectItem([this](int index, bool already){ this->onListSelect(index, already); });
    WidgetFunctions::Add(this->list);

    MusicService::LoadSettings();
    this->scanFolders();
    //索引(タグ)を作り直し始める(この画面を開いている間に少しずつ。変わった曲だけタグを読む)
    MusicLibrary::StartIndexUpdate();
    this->seen_index = MusicLibrary::IndexRevision();
    //鳴っているプレイリストがあればそれを開く(アプリを閉じて開き直したとき)
    this->view_group.clear();
    int open = kLibrary;
    if(MusicService::HasQueue()){
        const int pl = MusicService::Playlist();
        if(pl == kArtistTracks || pl == kAlbumTracks){
            if(!this->view_group.assign(MusicService::PlaylistKey())) this->view_group.clear();
            open = pl;
        }else{
            FixedString<PICO_STR_M> name;
            this->playlistName(pl, name);
            if((pl == kAll || (pl >= 0 && pl < this->folder_count)) && strcmp(name.c_str(), MusicService::PlaylistName()) == 0) open = pl;
        }
    }
    if(open == kLibrary) this->showLibrary();
    else if(open == kArtistTracks || open == kAlbumTracks) this->showGroupTracks(open, this->view_group.c_str());
    else this->showPlaylist(open);
    this->seen_revision = MusicService::Revision();
    this->refreshPanel();
}

void MusicScene::onExit(){
    //曲は止めない(MusicService が鳴らし続ける)。作りかけの索引はやめる(前の索引は残る。次に開いたらまた作る)
    MusicLibrary::CancelIndexUpdate();
    this->back_button = nullptr;
    this->play_all_button = nullptr;
    this->sleep_button = nullptr;
    this->title_label = nullptr;
    this->list = nullptr;
    this->panel = nullptr;
}

void MusicScene::onUpdate(){
    //曲の終わりで次へ進むのは MusicService(BackgroundFunctions から毎フレーム)。ここは見せるだけ。
    //この画面を開いている間は、鳴っていればスリープさせない(以前と同じ。閉じていれば、曲の切り替わりで眠ってもよい)
    if(MusicService::Active() && !MusicService::Paused()) PowerFunctions::KeepAwake();
    //索引を少しずつ作る(1フレーム8msまで)。差し替わったら今の一覧を作り直す(曲名・アーティスト)
    if(MusicLibrary::IndexUpdating()) MusicLibrary::StepIndexUpdate(8);
    if(MusicLibrary::IndexRevision() != this->seen_index){
        this->seen_index = MusicLibrary::IndexRevision();
        this->reloadView();
    }
    if(MusicService::Revision() != this->seen_revision){
        this->seen_revision = MusicService::Revision();
        this->refreshListColors();
        if(this->viewIsQueue() && MusicService::Current() >= 0){
            this->list->setSelectedIndex(MusicService::Current());
            this->list->ensureVisible(MusicService::Current());
        }
    }
    this->refreshSleepButton();
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
    this->folder_count = MusicLibrary::ScanFolders(this->folders, kMaxFolders);
}

void MusicScene::playlistName(int playlist, FixedString<PICO_STR_M>& out) const {
    if(playlist == kAll) out.assign(TR("すべての曲"));
    else if(playlist >= 0 && playlist < this->folder_count) out.assign(this->folders[playlist].c_str());
    else if(playlist == kArtistTracks || playlist == kAlbumTracks) MusicService::GroupDisplayName(playlist, this->view_group.c_str(), out);
    else out.assign(TR("ミュージック"));
}

void MusicScene::showLibrary(){
    this->view = kLibrary;
    this->view_count = 0;
    this->list->clear();
    if(OSData::SD_usable){
        ScrollListTools::Item all;
        all.icon = IconID::Music;
        all.text.assign(TR("すべての曲"));
        this->list->add(all);
        ScrollListTools::Item artists;
        artists.icon = IconID::User;
        artists.text.assign(TR("アーティスト"));
        this->list->add(artists);
        ScrollListTools::Item albums;
        albums.icon = IconID::Circle;
        albums.text.assign(TR("アルバム"));
        this->list->add(albums);
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
    this->view_count = MusicLibrary::ScanTracks(this->folders, this->folder_count, playlist, this->view_tracks, kMaxTracks);
    MusicLibrary::FillTitles(this->view_tracks, this->view_count, this->folders, this->folder_count);
    this->list->clear();
    for(int i = 0; i < this->view_count; i++){
        ScrollListTools::Item item;
        item.icon = IconID::Music;
        MusicService::TrackTitle(this->view_tracks[i], item.text);
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

void MusicScene::showGroups(int view){
    this->view = view;
    this->view_count = 0;
    const MusicLibrary::Group g = view == kArtists ? MusicLibrary::Group::Artist : MusicLibrary::Group::Album;
    this->group_count = MusicLibrary::ListGroups(g, this->groups, kMaxTracks);
    this->list->clear();
    for(int i = 0; i < this->group_count; i++){
        ScrollListTools::Item item;
        item.icon = view == kArtists ? IconID::User : IconID::Circle;
        FixedString<PICO_STR_M> name;
        MusicService::GroupDisplayName(view == kArtists ? kArtistTracks : kAlbumTracks, this->groups[i].c_str(), name);
        item.text.assign(name.c_str());
        this->list->add(item);
    }
    if(this->group_count == 0){
        //索引がまだ無い・タグの付いた曲が無い(押しても何もしない行)
        ScrollListTools::Item item;
        item.icon = IconID::InfoCircle;
        item.text.assign(MusicLibrary::IndexUpdating() ? TR("曲のタグを読んでいます…") : TR("曲がありません"));
        this->list->add(item);
    }
    this->refreshListColors();
    this->refreshHeader();
}

void MusicScene::showGroupTracks(int playlist, const char* key){
    this->view = playlist;
    if(!this->view_group.assign(key ? key : "")) this->view_group.clear();
    const MusicLibrary::Group g = playlist == kArtistTracks ? MusicLibrary::Group::Artist : MusicLibrary::Group::Album;
    this->view_count = MusicLibrary::ListGroupTracks(g, this->view_group.c_str(), this->folders, this->folder_count, this->view_tracks, kMaxTracks);
    this->list->clear();
    for(int i = 0; i < this->view_count; i++){
        ScrollListTools::Item item;
        item.icon = IconID::Music;
        MusicService::TrackTitle(this->view_tracks[i], item.text);
        this->list->add(item);
    }
    this->refreshListColors();
    this->refreshHeader();
    if(this->viewIsQueue() && MusicService::Current() >= 0){
        const int cur = MusicService::Current();
        this->list->setSelectedIndex(cur);
        this->list->ensureVisible(cur);
    }
}

void MusicScene::reloadView(){
    const int top = this->list ? this->list->getSelectedIndex() : -1;
    if(this->view == kLibrary) return;      //ライブラリの行は索引に依らない
    if(this->view == kArtists || this->view == kAlbums) this->showGroups(this->view);
    else if(this->view == kArtistTracks || this->view == kAlbumTracks){
        FixedString<PICO_STR_M> key = this->view_group;
        this->showGroupTracks(this->view, key.c_str());
    }else this->showPlaylist(this->view);
    if(top >= 0 && this->list && !this->viewIsQueue()) this->list->setSelectedIndex(top);
}

void MusicScene::refreshHeader(){
    this->play_all_button->setVisible(this->view != kLibrary && this->view != kArtists && this->view != kAlbums && this->view_count > 0);
    const int edge = this->play_all_button->getVisible()
        ? this->play_all_button->getLocalRect().x : Scene::contentRect().x + Scene::contentRect().w;
    this->sleep_button->setX(edge - MARGIN - this->sleep_button->getLocalRect().w);
    const int right = this->sleep_button->getLocalRect().x;
    const int avail = right - this->title_label->getLocalRect().x - MARGIN;

    FixedString<PICO_STR_M> name, count;
    if(this->view == kLibrary){
        name.assign(TR("ミュージック"));
    }else if(this->view == kArtists || this->view == kAlbums){
        name.assign(this->view == kArtists ? TR("アーティスト") : TR("アルバム"));
        count.appendFormat(" (%d)", this->group_count);
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
    if(this->view == kLibrary || this->view == kArtists || this->view == kAlbums || !MusicService::HasQueue()) return false;
    if(MusicService::Playlist() != this->view) return false;
    if(this->view == kArtistTracks || this->view == kAlbumTracks) return strcmp(this->view_group.c_str(), MusicService::PlaylistKey()) == 0;
    FixedString<PICO_STR_M> name;
    this->playlistName(this->view, name);
    return strcmp(name.c_str(), MusicService::PlaylistName()) == 0;
}

void MusicScene::refreshListColors(){
    if(!this->list) return;
    const bool playing = MusicService::Active();
    if(this->view == kLibrary){
        //鳴っているプレイリストを緑に(アーティスト/アルバムの行は、その種類の並びが鳴っていれば)
        const int qpl = MusicService::HasQueue() ? MusicService::Playlist() : INT32_MIN;
        for(int i = 0; i < kLibraryFixedRows + this->folder_count; i++){
            ScrollListTools::Item* it = this->list->itemAt(i);
            if(!it) break;
            bool same;
            if(i == 1) same = qpl == kArtistTracks;
            else if(i == 2) same = qpl == kAlbumTracks;
            else{
                const int pl = i == 0 ? kAll : i - kLibraryFixedRows;
                FixedString<PICO_STR_M> name;
                this->playlistName(pl, name);
                same = qpl == pl && strcmp(name.c_str(), MusicService::PlaylistName()) == 0;
            }
            it->color = (playing && same) ? (int8_t)PICO_DARKGREEN : (int8_t)-1;
        }
    }else if(this->view == kArtists || this->view == kAlbums){
        const int want = this->view == kArtists ? kArtistTracks : kAlbumTracks;
        const bool kind = MusicService::HasQueue() && MusicService::Playlist() == want;
        for(int i = 0; i < this->group_count; i++){
            ScrollListTools::Item* it = this->list->itemAt(i);
            if(!it) break;
            const bool same = kind && strcmp(this->groups[i].c_str(), MusicService::PlaylistKey()) == 0;
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
        if(index < 0 || index >= kLibraryFixedRows + this->folder_count) return;
        if(index == 1) this->showGroups(kArtists);
        else if(index == 2) this->showGroups(kAlbums);
        else this->showPlaylist(index == 0 ? kAll : index - kLibraryFixedRows);
        return;
    }
    if(this->view == kArtists || this->view == kAlbums){
        if(index < 0 || index >= this->group_count) return;     //「読んでいます」の行
        this->showGroupTracks(this->view == kArtists ? kArtistTracks : kAlbumTracks, this->groups[index].c_str());
        return;
    }
    if(index < 0 || index >= this->view_count) return;
    this->startPlaylist(index);
}

// ================================================================ 再生(MusicService へ渡すだけ)

void MusicScene::startPlaylist(int track){
    if(this->view == kLibrary || this->view == kArtists || this->view == kAlbums || this->view_count == 0) return;
    FixedString<PICO_STR_M> name;
    this->playlistName(this->view, name);
    //「続きから」で同じ並びを作り直すための名前(フォルダ名・タグの名前。「不明」は空)
    const char* key = (this->view == kArtistTracks || this->view == kAlbumTracks) ? this->view_group.c_str() : name.c_str();
    MusicService::StartPlaylist(this->view_tracks, this->view_count, this->folders, this->folder_count,
                                this->view, name.c_str(), track, key);
    this->refreshListColors();
}

void MusicScene::refreshSleepButton(){
    const MusicService::SleepMode mode = MusicService::SleepTimerMode();
    const int v = mode == MusicService::SleepMode::TrackEnd ? -1
                : mode == MusicService::SleepMode::Timer ? (int)((MusicService::SleepRemainingMs() + 59999) / 60000) : 0;
    if(v == this->shown_sleep) return;
    this->shown_sleep = v;
    FixedString<PICO_STR_S> text;
    if(v > 0) text.appendFormat(TR("%d分"), v);
    else if(v < 0) text.assign(TR("曲末"));
    //Buttonはアイコンか文字のどちらかを描くので、動いている間は残りの文字、止まっていれば⌛
    this->sleep_button->setText(text.c_str());
    if(v != 0) this->sleep_button->clearIcon();
    else this->sleep_button->setIcon(IconID::Hourglass, IconSize::Px16);
    this->sleep_button->setTextColor(v != 0 ? PICO_BLUE : PICO_FORECOLOR);
    //幅が変わったので並べ直す(題名の縮め方も変わる)
    this->refreshHeader();
}

void MusicScene::showSleepPicker(){
    static constexpr uint8_t kMinutes[] = {15, 30, 45, 60, 90};
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Choice, TR("スリープタイマー"), "", TR("キャンセル"));
    if(!d) return;
    d->addChoice(TR("オフ"));
    for(uint8_t m : kMinutes){
        FixedString<PICO_STR_S> item;
        item.appendFormat(TR("%d分後に止める"), (int)m);
        d->addChoice(item.c_str());
    }
    d->addChoice(TR("この曲の終わりで止める"));
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClosed([this, d](bool ok){
        const int i = d->getSelectedIndex();
        if(ok && i >= 0){
            constexpr int n = (int)(sizeof(kMinutes) / sizeof(kMinutes[0]));
            if(i == 0) MusicService::ClearSleepTimer();
            else if(i <= n) MusicService::SetSleepTimer(kMinutes[i - 1]);
            else MusicService::SetSleepAtTrackEnd();
            this->refreshSleepButton();
        }
        WidgetFunctions::DestroyLater(d);
    });
}

void MusicScene::showErrorDetail(){
    if(!MusicService::ErrorShown()) return;
    //理由は欄に収まらないので全文をダイアログで(長ければダイアログの中でスクロールする)
    FixedString<PICO_STR_512B> text;
    text.appendFormat(TR("%s を読めません\n\n%s"), MusicService::ErrorTrack(), MusicService::ErrorDetail());
    MsgDialog* dialog = new MsgDialog(text.c_str(), "", TR("閉じる"));
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
            }else if(MusicService::HasResume() && MusicService::Resume()){
                //前回の続き: その並びを開く
                const int pl = MusicService::Playlist();
                if(pl == kArtistTracks || pl == kAlbumTracks) this->showGroupTracks(pl, MusicService::PlaylistKey());
                else if(pl == kAll || (pl >= 0 && pl < this->folder_count)) this->showPlaylist(pl);
            }else{
                if(this->view == kLibrary || this->view == kArtists || this->view == kAlbums) this->showPlaylist(kAll);
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
    this->panel->setHasTracks(MusicService::HasQueue() || MusicService::HasResume() || this->view_count > 0
                              || ((this->view == kLibrary || this->view == kArtists || this->view == kAlbums) && OSData::SD_usable));

    //曲名と下の行
    FixedString<PICO_PATH_LEN> title;
    FixedString<PICO_STR_256B> sub;
    int8_t sub_color = PICO_DARKGREY;
    const int cur = MusicService::Current();
    if(cur >= 0 && MusicService::HasQueue()){
        MusicService::TrackTitle(*MusicService::TrackAt(cur), title);
        const char* pl = MusicService::PlaylistName();
        if(active){
            sub.appendFormat("%s · %d/%d", pl, MusicService::Position() + 1, MusicService::Count());
            if(paused) sub.append(TR(" 一時停止中"));
            //音が出ない本体でも曲は進むので、画面で知らせる
            if(!SoundFunctions::IsAvailable()) sub.append(TR("(音は出ません)"));
        }else if(MusicService::Finished()){
            sub.appendFormat(TR("%s · 再生が終わりました"), pl);
        }else if(MusicService::Interrupted()){
            sub.appendFormat(TR("%s · 他のアプリが音を使ったため止まりました"), pl);
        }else{
            sub.appendFormat(TR("%s · 停止中"), pl);
        }
    }else if(!OSData::SD_usable){
        title.assign(TR("SDカードがありません"));
    }else if(MusicService::HasResume()){
        //起動した後: 前回の曲(再生で続きから)
        title.assign(MusicService::ResumeTitle());
        const uint32_t s = MusicService::ResumePositionMs() / 1000;
        sub.appendFormat(TR("前回の続き(%lu:%02lu から)"), (unsigned long)(s / 60), (unsigned long)(s % 60));
    }else if(this->view == kLibrary && this->folder_count == 0){
        title.assign(TR("曲が選ばれていません"));
        sub.assign(TR("/music/ に .mml か .wav を置くか、フォルダ(=プレイリスト)を作ってください"));
    }else{
        title.assign(TR("曲が選ばれていません"));
        sub.assign(TR("曲をタップすると再生します"));
    }
    //読めなかった理由。欄には頭しか入らないので、タップで全文を出せることを1行目に書く
    if(MusicService::ErrorShown()){
        FixedString<PICO_PATH_LEN> name;
        MusicService::DisplayName(MusicService::ErrorTrack(), name);
        sub.assign("");
        if(MusicService::ErrorSticky()) sub.append(TR("読めません(タップで全文)\n"));
        else sub.appendFormat(TR("%s を飛ばしました(タップで全文)\n"), name.c_str());
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
