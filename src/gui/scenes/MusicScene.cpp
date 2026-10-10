#include "gui/scenes/MusicScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/apps/MusicPlayerPanel.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "sound/Mml_Compiler.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    // 拡張子(".mml"等、小文字で渡す)で終わるか。大小は区別しない
    bool EndsWith(const char* name, const char* ext){
        const size_t n = strlen(name), e = strlen(ext);
        if(n < e) return false;
        for(size_t i = 0; i < e; i++){
            char c = name[n - e + i];
            if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if(c != ext[i]) return false;
        }
        return true;
    }
    bool IsMml(const char* name){ return EndsWith(name, ".mml"); }
    bool IsWav(const char* name){ return EndsWith(name, ".wav"); }

    // 一覧に出す名前(拡張子を落とす)
    void DisplayName(const char* file, FixedString<PICO_PATH_LEN>& out){
        const char* dot = strrchr(file, '.');
        out.assign(file, dot ? (size_t)(dot - file) : strlen(file));
    }

    // 1つ前の曲へ戻らず、今の曲の頭へ戻す境目
    constexpr uint32_t kRestartMs = 3000;
    // 飛ばした曲の理由を出しておく時間
    constexpr uint32_t kErrorShowMs = 8000;
}

// ================================================================ 画面

void MusicScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    //プレイリストを見ていればライブラリへ、ライブラリならアプリを閉じる
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
    this->panel->setOnSeek([this](uint32_t ms){
        if(this->playing_kind == 2) SoundFunctions::WavSeekMs(ms);
    });
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

    this->loadSettings();
    this->queue.seed(millis() ^ 0x5A17u);
    this->playing_kind = 0;
    this->finished = false;
    this->fail_streak = 0;
    this->error_until_ms = 0;
    this->error_sticky = false;
    this->failed_mask = 0;
    this->queue_playlist = kLibrary;
    this->scanFolders();
    this->showLibrary();
    this->refreshPanel();
}

void MusicScene::onExit(){
    //閉じたら止める(このアプリの外から止める手段が無いため)
    SoundFunctions::MusicStop();
    SoundFunctions::WavStop();
    this->playing_kind = 0;

    this->back_button = nullptr;
    this->play_all_button = nullptr;
    this->title_label = nullptr;
    this->list = nullptr;
    this->panel = nullptr;
}

void MusicScene::onUpdate(){
    //曲の終わりを見張る
    if(this->playing_kind != 0){
        const bool wav = this->playing_kind == 2;
        if(!(wav ? SoundFunctions::WavPlaying() : SoundFunctions::MusicPlaying())){
            this->advance(false);
        }else if(!wav && this->queue.repeat() != PlayQueue::Repeat::One && !SoundFunctions::MusicPaused()){
            //終わりの無い曲(L で戻る)は1周したら次へ
            bool loops = false;
            const uint32_t total = SoundFunctions::MusicTotalMs(&loops);
            if(loops && total > 0 && SoundFunctions::MusicElapsedMs() >= total) this->advance(false);
        }
    }
    this->refreshPanel();
}

bool MusicScene::onKey(const KeyInputFunctions::Event& ev){
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
            const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && (IsMml(name) || IsWav(name));
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
        DisplayName(this->view_tracks[i].name.c_str(), item.text);
        this->list->add(item);
    }
    this->refreshListColors();
    this->refreshHeader();

    //鳴っている曲のプレイリストなら、その曲が見えるように
    if(this->queue_playlist == playlist && this->playing_kind != 0){
        const int cur = this->queue.current();
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

void MusicScene::refreshListColors(){
    if(!this->list) return;
    const int cur = this->playing_kind != 0 ? this->queue.current() : -1;
    if(this->view == kLibrary){
        //鳴っているプレイリストを緑に
        for(int i = 0; i <= this->folder_count; i++){
            ScrollListTools::Item* it = this->list->itemAt(i);
            if(!it) break;
            const int pl = i == 0 ? kAll : i - 1;
            it->color = (this->playing_kind != 0 && pl == this->queue_playlist) ? (int8_t)PICO_DARKGREEN : (int8_t)-1;
        }
    }else{
        for(int i = 0; i < this->view_count; i++){
            ScrollListTools::Item* it = this->list->itemAt(i);
            if(!it) break;
            int8_t c = -1;
            if(this->queue_playlist == this->view){
                if(i == cur) c = PICO_DARKGREEN;
                else if(this->failed_mask & ((uint64_t)1 << i)) c = PICO_RED;
            }
            it->color = c;
        }
    }
    this->list->needsRender();
}

void MusicScene::onListSelect(int index, bool already_selected){
    //タッチなら1回で、キー/コントローラーで選んでいるときは決定(=2回目)で開く/鳴らす
    //(↑↓で選ぶたびに曲が変わらないように)
    if(!OSData::isTouchStart && !already_selected) return;
    if(this->view == kLibrary){
        if(index < 0 || index > this->folder_count) return;
        this->showPlaylist(index == 0 ? kAll : index - 1);
        return;
    }
    if(index < 0 || index >= this->view_count) return;
    this->startPlaylist(index);
}

// ================================================================ 再生

void MusicScene::startPlaylist(int track){
    if(this->view == kLibrary || this->view_count == 0) return;
    for(int i = 0; i < this->view_count; i++) this->queue_tracks[i] = this->view_tracks[i];
    this->queue_playlist = this->view;
    this->queue.reset(this->view_count);
    this->fail_streak = 0;
    this->failed_mask = 0;
    //全曲再生(track<0)は並びの頭から(ミックスなら混ぜた並びの頭 = どれかの曲)。読めない曲は飛ばす
    const bool all = track < 0;
    if(!all) this->queue.start(track);
    this->playTrack(this->queue.current(), all);
}

bool MusicScene::tryPlay(int track, FixedString<PICO_STR_256B>& error){
    const Track& t = this->queue_tracks[track];
    FixedString<PICO_PATH_LEN> path;
    path.assign(PICO_Path::DIR::MUSIC);
    if(t.folder >= 0){
        path.append(this->folders[t.folder].c_str());
        path.append("/");
    }
    path.append(t.name.c_str());

    //MMLとWAVは同時には鳴らさない(このアプリで鳴らすのは1曲ずつ)
    if(IsWav(t.name.c_str())){
        const char* err = "";
        if(!SoundFunctions::WavPlay(path.c_str(), false, 100, &err)){
            error.assign(err && *err ? err : "WAVとして読めません");
            return false;
        }
        SoundFunctions::MusicStop();
        this->playing_kind = 2;
        return true;
    }
    MmlResult r;
    if(!SoundFunctions::MusicPlayFile(path.c_str(), &r)){
        error.assign("");
        if(r.line > 0) error.appendFormat("%d行%d列: ", r.line, r.col);
        error.append(r.message.c_str());
        return false;
    }
    SoundFunctions::WavStop();
    this->playing_kind = 1;
    return true;
}

void MusicScene::playTrack(int track, bool skip_on_error){
    const int n = this->queue.count();
    while(track >= 0){
        FixedString<PICO_STR_256B> err;
        if(this->tryPlay(track, err)){
            this->fail_streak = 0;
            this->finished = false;
            this->error_sticky = false;
            this->failed_mask &= ~((uint64_t)1 << track);
            break;
        }
        LOG_APP_WARN("ミュージック: %s を読めません: %s", this->queue_tracks[track].name.c_str(), err.c_str());
        this->failed_mask |= (uint64_t)1 << track;
        this->error_track.assign(this->queue_tracks[track].name.c_str());
        this->error_detail = err;
        //選んだ曲なら止まって理由を出し続ける(飛ばすと何が悪かったのか読めない)
        if(!skip_on_error){
            this->stopPlayback();
            this->error_sticky = true;
            this->error_until_ms = 0;
            break;
        }
        this->error_sticky = false;
        this->error_until_ms = millis() + kErrorShowMs;
        //全部読めなければ止まる
        if(++this->fail_streak >= n){
            this->stopPlayback();
            this->error_sticky = true;
            break;
        }
        track = this->queue.next(true);
        if(track < 0){
            this->stopPlayback();
            this->finished = true;
            break;
        }
    }
    this->refreshListColors();
    if(this->view == this->queue_playlist && this->queue.current() >= 0){
        this->list->setSelectedIndex(this->queue.current());
        this->list->ensureVisible(this->queue.current());
    }
}

bool MusicScene::errorShown() const {
    if(this->error_sticky) return true;
    return this->error_until_ms != 0 && (int32_t)(millis() - this->error_until_ms) < 0;
}

void MusicScene::showErrorDetail(){
    if(!this->errorShown()) return;
    //理由は欄に収まらないので全文をダイアログで(長ければダイアログの中でスクロールする)
    FixedString<PICO_STR_512B> text;
    text.appendFormat("%s を読めません\n\n%s", this->error_track.c_str(), this->error_detail.c_str());
    MsgDialog* dialog = new MsgDialog(text.c_str(), "", "閉じる");
    if(!dialog) return;
    dialog->setVisibleIcon(true);
    dialog->setIconId(IconID::AlertTriangle);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([dialog](bool){ WidgetFunctions::DestroyLater(dialog); });
}

void MusicScene::advance(bool by_user){
    if(this->queue.count() == 0) return;
    const int next = this->queue.next(by_user);
    if(next < 0){
        //リピートしない: 最後まで鳴らした。次に再生を押したら頭から
        this->stopPlayback();
        this->finished = true;
        this->refreshListColors();
        return;
    }
    //曲の終わりで進んだ先が読めなければ飛ばす。「次へ」で選んだ曲なら止まって理由を出す
    this->playTrack(next, !by_user);
}

void MusicScene::previous(){
    if(this->queue.count() == 0) return;
    //鳴らし始めて少し経っていれば今の曲の頭へ
    if(this->playing_kind == 2 && SoundFunctions::WavPositionMs() > kRestartMs){
        SoundFunctions::WavSeekMs(0);
        return;
    }
    if(this->playing_kind == 1 && SoundFunctions::MusicElapsedMs() > kRestartMs){
        this->playTrack(this->queue.current(), false);
        return;
    }
    this->playTrack(this->queue.prev(), false);
}

void MusicScene::togglePlay(){
    if(this->soundActive()){
        if(this->playing_kind == 2) SoundFunctions::WavPause(!SoundFunctions::WavPaused());
        else                        SoundFunctions::MusicPause(!SoundFunctions::MusicPaused());
        return;
    }
    //止まっていれば: 前の順番があれば続き(最後まで行っていたら頭)から、無ければ見ているプレイリストを鳴らす
    if(this->queue.count() > 0){
        if(this->finished){
            this->queue.start(this->queue.orderAt(0));
            this->finished = false;
        }
        this->fail_streak = 0;
        this->playTrack(this->queue.current(), false);
        return;
    }
    if(this->view == kLibrary){
        this->showPlaylist(kAll);
    }
    this->startPlaylist(-1);
}

void MusicScene::stopPlayback(){
    SoundFunctions::MusicStop();
    SoundFunctions::WavStop();
    this->playing_kind = 0;
}

bool MusicScene::soundActive() const {
    if(this->playing_kind == 2) return SoundFunctions::WavPlaying();
    if(this->playing_kind == 1) return SoundFunctions::MusicPlaying();
    return false;
}

void MusicScene::onPanelButton(int button){
    switch(button){
        case MusicPlayerPanel::kPlay: this->togglePlay(); break;
        case MusicPlayerPanel::kNext: if(this->queue.count() > 0){ this->fail_streak = 0; this->advance(true); } break;
        case MusicPlayerPanel::kPrev: this->fail_streak = 0; this->previous(); break;
        case MusicPlayerPanel::kShuffle:
            this->queue.setShuffle(!this->queue.shuffle());
            this->saveSettings();
            break;
        case MusicPlayerPanel::kRepeat:
            this->queue.cycleRepeat();
            this->saveSettings();
            break;
        default: break;
    }
    this->refreshPanel();
}

// ================================================================ 表示

void MusicScene::refreshPanel(){
    if(!this->panel) return;
    const bool active = this->soundActive();
    const bool wav = this->playing_kind == 2;
    const bool paused = active && (wav ? SoundFunctions::WavPaused() : SoundFunctions::MusicPaused());
    this->panel->setPlaying(active, paused);
    this->panel->setModes(this->queue.shuffle(), (uint8_t)this->queue.repeat());
    this->panel->setHasTracks(this->queue.count() > 0 || (this->view != kLibrary && this->view_count > 0)
                              || (this->view == kLibrary && OSData::SD_usable));

    //曲名と下の行
    FixedString<PICO_PATH_LEN> title;
    FixedString<PICO_STR_256B> sub;
    int8_t sub_color = PICO_DARKGREY;
    const int cur = this->queue.current();
    if(cur >= 0 && (active || this->finished || this->playing_kind == 0) && this->queue.count() > 0){
        DisplayName(this->queue_tracks[cur].name.c_str(), title);
        FixedString<PICO_STR_M> pl;
        this->playlistName(this->queue_playlist, pl);
        if(active){
            sub.appendFormat("%s · %d/%d", pl.c_str(), this->queue.position() + 1, this->queue.count());
            if(paused) sub.append(" 一時停止中");
            //音が出ない本体でも曲は進むので、画面で知らせる
            if(!SoundFunctions::IsAvailable()) sub.append("(音は出ません)");
        }else if(this->finished){
            sub.appendFormat("%s · 再生が終わりました", pl.c_str());
        }else{
            sub.appendFormat("%s · 停止中", pl.c_str());
        }
    }else if(!OSData::SD_usable){
        title.assign("SDカードがありません");
    }else if(this->view == kLibrary && this->folder_count == 0 && this->queue.count() == 0){
        title.assign("曲が選ばれていません");
        sub.assign("/music/ に .mml か .wav を置くか、フォルダ(=プレイリスト)を作ってください");
    }else{
        title.assign("曲が選ばれていません");
        sub.assign("曲をタップすると再生します");
    }
    //読めなかった理由。欄には頭しか入らないので、タップで全文を出せることを1行目に書く
    if(this->errorShown()){
        FixedString<PICO_PATH_LEN> name;
        DisplayName(this->error_track.c_str(), name);
        sub.assign("");
        if(this->error_sticky) sub.append("読めません(タップで全文)\n");
        else sub.appendFormat("%s を飛ばしました(タップで全文)\n", name.c_str());
        sub.append(this->error_detail.c_str());
        sub_color = PICO_RED;
    }else if(this->error_until_ms != 0){
        this->error_until_ms = 0;
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
            const bool endless = loops && this->queue.repeat() == PlayQueue::Repeat::One;
            this->panel->setTime(SoundFunctions::MusicElapsedMs(), total, endless, false);
        }
    }
}

void MusicScene::loadSettings(){
    bool shuffle = false;
    PlayQueue::Repeat repeat = PlayQueue::Repeat::Off;
    //無くてよい(無ければ既定: 順番通り・リピートしない)
    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_MUSIC_CFG)) PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_MUSIC_CFG, [&](const char* key, const char* value){
        if(strcmp(key, "shuffle") == 0){
            bool b;
            if(PICO_Config::ConfigValue::AsBool(value, b)) shuffle = b;
        }else if(strcmp(key, "repeat") == 0){
            if(strcmp(value, "all") == 0) repeat = PlayQueue::Repeat::All;
            else if(strcmp(value, "one") == 0) repeat = PlayQueue::Repeat::One;
            else repeat = PlayQueue::Repeat::Off;
        }
    });
    this->queue.setShuffle(shuffle);
    this->queue.setRepeat(repeat);
}

void MusicScene::saveSettings(){
    if(!OSData::SD_usable) return;
    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_MUSIC_CFG, "shuffle", PICO_Config::ConfigValue::FromBool(this->queue.shuffle()));
    const PlayQueue::Repeat r = this->queue.repeat();
    PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_MUSIC_CFG, "repeat",
                          r == PlayQueue::Repeat::All ? "all" : (r == PlayQueue::Repeat::One ? "one" : "off"));
}
