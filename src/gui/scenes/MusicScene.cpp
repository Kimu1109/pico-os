#include "gui/scenes/MusicScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/NumberSlider.hpp"
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

    enum Status { kNoSd, kEmpty, kStopped, kPlaying, kError };

    // 時間の表示は m:ss(1時間を超えたら h:mm:ss)
    void FormatTime(FixedString<PICO_STR_S>& out, uint32_t ms){
        const uint32_t sec = ms / 1000;
        out.assign("");
        if(sec >= 3600) out.appendFormat("%u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
        else            out.appendFormat("%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    }

    constexpr int kCurW = 44;       // 現在時間の欄(「12:34」が収まる幅)
    constexpr int kTotalW = 52;     // 全体の長さの欄(「ループ」が収まる幅)
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

    // ---- 下から: [一時停止][停止] / シークバーの行 / 状態の2行 ----
    this->pause_button = new Button(content.x + MARGIN, 0, "一時停止");
    this->pause_button->setFontSize(FontFn::Small);
    this->pause_button->setH(20);
    this->pause_button->setOnPressEnd([this](){ this->togglePause(); });
    WidgetFunctions::Add(this->pause_button);
    const Rect pause_rect = this->pause_button->getLocalRect();

    this->stop_button = new Button(pause_rect.x + pause_rect.w + MARGIN, 0, "停止");
    this->stop_button->setFontSize(FontFn::Small);
    this->stop_button->setH(20);
    const int stop_y = content.y + content.h - MARGIN - this->stop_button->getLocalRect().h - 8;
    this->stop_button->setY(stop_y);
    this->pause_button->setY(stop_y);
    this->stop_button->setOnPressEnd([this](){
        SoundFunctions::MusicStop();
        SoundFunctions::WavStop();
        this->refreshStatus();
    });
    WidgetFunctions::Add(this->stop_button);

    //シークバーの行: 現在時間 [バー] 全体の長さ(バーはWAVだけ。MMLは時間の2つだけ)
    const int bar_h = 21;
    const int bar_y = stop_y - MARGIN - bar_h;
    const int label_y = bar_y + (bar_h - FontFn::GetFontSize(FontFn::Small)) / 2;
    this->cur_label = new Label<PICO_STR_S>(content.x + MARGIN, label_y, "0:00");
    this->cur_label->setFontSize(FontFn::Small);
    WidgetFunctions::Add(this->cur_label);

    this->total_label = new Label<PICO_STR_S>(content.x + content.w - MARGIN - kTotalW, label_y, "0:00");
    this->total_label->setFontSize(FontFn::Small);
    WidgetFunctions::Add(this->total_label);

    this->bar_w = content.w - MARGIN * 2 - kCurW - kTotalW;
    this->seek_bar = new NumberSlider(content.x + MARGIN + kCurW, bar_y, this->bar_w);
    this->seek_bar->setVisibleNum(false);
    this->seek_bar->setMaxValue(1);
    //つまみを動かしている間は飛ばず、離したところへ1回だけ飛ぶ(動かすたびにSDを読み直さないため)
    this->seek_bar->setOnValueChanged([this](){
        if(!this->updating_bar) this->seek_pending = true;
    });
    //押した瞬間にもその位置へつまみを動かす(NumberSliderは動かさないと値が変わらない。タップだけで飛べるように)
    this->seek_bar->setOnPressStart([this](){ this->seek_bar->causeOnPressMove(); });
    this->seek_bar->setOnPressEnd([this](){
        if(!this->seek_pending) return;
        this->seek_pending = false;
        SoundFunctions::WavSeekMs((uint32_t)this->seek_bar->getValue());
    });
    WidgetFunctions::Add(this->seek_bar);

    //状態の欄は2行ぶん取る(読めなかった理由は長くなる)
    const int status_y = bar_y - MARGIN - FontFn::GetFontSize(FontFn::Small) * 2 - 8;
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
    this->transport_mode = -1;      //最初の refreshTransport() で必ず表示状態を決め直す
    this->last_cur_sec = this->last_total_ms = -1;
    this->last_bar_px = -1;
    this->bar_duration = 0;
    this->seek_pending = false;
    this->reloadList();
    this->refreshStatus();
    this->refreshTransport();
}

void MusicScene::onExit(){
    //閉じたら止める(このアプリの外から止める手段が無いため)
    SoundFunctions::MusicStop();
    SoundFunctions::WavStop();

    this->back_button = nullptr;
    this->stop_button = nullptr;
    this->pause_button = nullptr;
    this->cur_label = nullptr;
    this->total_label = nullptr;
    this->seek_bar = nullptr;
    this->title_label = nullptr;
    this->status_label = nullptr;
    this->list = nullptr;
}

void MusicScene::onUpdate(){
    //L の無い曲が終わったら「止まっています」へ戻す
    this->refreshStatus();
    this->refreshTransport();
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
        const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && (IsMml(name) || IsWav(name));
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

    //MMLとWAVは同時には鳴らさない(このアプリで鳴らすのは1曲ずつ)
    if(IsWav(this->file_names[index].c_str())){
        const char* err = "";
        if(!SoundFunctions::WavPlay(path.c_str(), false, 100, &err)){
            FixedString<PICO_STR_256B> msg;
            msg.appendFormat("%s を読めません\n%s", this->file_names[index].c_str(), err);
            LOG_APP_WARN("ミュージック: %s", msg.c_str());
            this->status_label->setTextColor(PICO_RED);
            this->status_label->setText(msg.c_str());
            this->last_status = kError;
            return;
        }
        SoundFunctions::MusicStop();
        this->last_status = -1;
        this->refreshStatus();
        return;
    }

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
    SoundFunctions::WavStop();
    this->last_status = -1;     //鳴らせたら誤りの表示を消す(同じ曲を鳴らし直した場合も)
    this->refreshStatus();
}

void MusicScene::refreshStatus(){
    if(!this->status_label) return;

    const bool wav = SoundFunctions::WavPlaying();
    int status;
    if(!OSData::SD_usable) status = kNoSd;
    else if(wav || SoundFunctions::MusicPlaying()) status = kPlaying;
    else if(this->file_count == 0) status = kEmpty;
    else status = kStopped;

    //読めなかった理由を出している間は、鳴っている曲が変わるまでそのまま
    if(this->last_status == kError && status != kPlaying) return;
    const char* title = wav ? SoundFunctions::WavTitle() : SoundFunctions::MusicTitle();
    if(this->last_status == kError && status == kPlaying && this->last_title == title) return;

    const bool paused = wav ? SoundFunctions::WavPaused() : SoundFunctions::MusicPaused();
    if(status == this->last_status && (status != kPlaying || (this->last_title == title && paused == this->last_paused))) return;
    this->last_status = status;
    this->last_title.assign(title);
    this->last_paused = paused;

    FixedString<PICO_STR_L> text;
    switch(status){
        case kNoSd:    text.assign("SDカードがありません"); break;
        case kEmpty:   text.assign("/music/ に .mml か .wav を置いてください"); break;
        case kStopped: text.assign("2回タップで再生"); break;
        case kPlaying:
            text.assign(paused ? "一時停止中: " : "再生中: ");
            text.append(title);
            //音が出ない本体でも曲は進むので、画面で知らせる
            if(!SoundFunctions::IsAvailable()) text.append("(音は出ません)");
            break;
    }
    this->status_label->setTextColor(PICO_BLACK);
    this->status_label->setText(text.c_str());
}

void MusicScene::togglePause(){
    if(SoundFunctions::WavPlaying()) SoundFunctions::WavPause(!SoundFunctions::WavPaused());
    else                             SoundFunctions::MusicPause(!SoundFunctions::MusicPaused());
    this->refreshStatus();
    this->refreshTransport();
}

void MusicScene::refreshTransport(){
    if(!this->seek_bar) return;

    const bool wav = SoundFunctions::WavPlaying();
    const int mode = wav ? 2 : (SoundFunctions::MusicPlaying() ? 1 : 0);

    if(mode != this->transport_mode){
        this->transport_mode = mode;
        const bool on = mode != 0;
        this->pause_button->setVisible(on);
        this->stop_button->setVisible(on);
        this->cur_label->setVisible(on);
        this->total_label->setVisible(on);
        this->seek_bar->setVisible(mode == 2);
        //次に出すとき(別の曲へ替えたときも)全部書き直す
        this->last_cur_sec = this->last_total_ms = -1;
        this->last_bar_px = -1;
        this->bar_duration = 0;
        this->seek_pending = false;
        this->transport_paused = !on;   //「一時停止/再開」の文字も書き直させる
    }
    if(mode == 0) return;

    //一時停止中は「再開」を出す
    const bool paused = wav ? SoundFunctions::WavPaused() : SoundFunctions::MusicPaused();
    if(paused != this->transport_paused){
        this->transport_paused = paused;
        this->pause_button->setText(paused ? "再開" : "一時停止");
    }

    uint32_t pos, total;
    bool loops = false;
    if(wav){
        pos = SoundFunctions::WavPositionMs();
        total = SoundFunctions::WavDurationMs();
    }else{
        pos = SoundFunctions::MusicElapsedMs();
        total = SoundFunctions::MusicTotalMs(&loops);
    }

    //時間は秒が変わったときだけ書く(毎フレーム書くとLabelがdirtyを積み続ける)
    const int64_t sec = pos / 1000;
    if(sec != this->last_cur_sec){
        this->last_cur_sec = sec;
        FixedString<PICO_STR_S> t;
        FormatTime(t, pos);
        this->cur_label->setText(t.c_str());
    }
    const int64_t total_key = loops ? -2 : (int64_t)total;
    if(total_key != this->last_total_ms){
        this->last_total_ms = total_key;
        FixedString<PICO_STR_S> t;
        if(loops) t.assign("ループ");
        else      FormatTime(t, total);
        this->total_label->setText(t.c_str());
    }

    //シークバー。つまみを持っている間は指に任せる
    if(wav && !this->seek_bar->is_pressing){
        if(total != this->bar_duration){
            this->bar_duration = total;
            this->seek_bar->setMaxValue((float)(total > 0 ? total : 1));
            this->last_bar_px = -1;
        }
        const int px = total > 0 ? (int)((uint64_t)pos * this->bar_w / total) : 0;
        if(px != this->last_bar_px){
            this->last_bar_px = px;
            this->updating_bar = true;
            this->seek_bar->setValue((float)pos);
            this->updating_bar = false;
        }
    }
}
