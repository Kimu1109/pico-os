#include "gui/scenes/VideoScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ProgressBar.hpp"
#include "gui/widgets/apps/VideoView.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

namespace {
    constexpr int MARGIN = 6;
    constexpr int kBarH = 8;

    void FormatTime(char* out, size_t n, uint32_t ms){
        const uint32_t s = ms / 1000;
        snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

VideoScene::VideoScene(const char* p){
    if(p) this->path.assign(p);
}

// ================================================================ 画面

void VideoScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;
    const int btn_h = 20 + Button::kFrameExtra;

    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(btn_h);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);
    const Rect back = this->back_button->getLocalRect();

    this->open_button = new Button(back.x + back.w + MARGIN, y0, "開く");
    this->open_button->setFontSize(FontFn::Small);
    this->open_button->setH(btn_h);
    this->open_button->setOnPressEnd([this](){
        if(this->pending == Pending::None && !this->picker){
            this->pending = Pending::OpenPicker;
            this->pending_wait_frames = 0;
        }
    });
    WidgetFunctions::Add(this->open_button);
    const Rect open = this->open_button->getLocalRect();

    const int title_x = open.x + open.w + MARGIN;
    this->title_label = new Label<PICO_STR_M>(title_x, y0, "");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setY(y0 + (back.h - FontFn::GetFontSize(FontFn::Small)) / 2);
    this->title_label->setMaxWidth(content.x + content.w - MARGIN - title_x);
    this->title_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->title_label);

    // 下から: ボタンの行 → 時間の行(バー+時刻)
    const int ctrl_y = content.y + content.h - MARGIN - btn_h;
    const int time_h = FontFn::GetFontSize(FontFn::Small);
    const int time_y = ctrl_y - MARGIN - time_h;

    this->time_label = new Label<PICO_STR_S>(0, time_y, "0:00 / 0:00");
    this->time_label->setFontSize(FontFn::Small);
    this->time_label->setDisableAutoTextDecoration(true);
    this->time_label->setX(content.x + content.w - MARGIN - this->time_label->getLocalRect().w);
    WidgetFunctions::Add(this->time_label);

    const int bar_w = this->time_label->getLocalRect().x - MARGIN - (content.x + MARGIN);
    this->bar = new ProgressBar((int16_t)(content.x + MARGIN), (int16_t)(time_y + (time_h - kBarH) / 2),
                                (int16_t)bar_w, (int16_t)kBarH);
    this->bar->setMinValue(0);
    this->bar->setMaxValue(1);
    WidgetFunctions::Add(this->bar);

    const char* labels[3] = { "-10秒", "一時停止", "+10秒" };
    Button** slots[3] = { &this->rew_button, &this->play_button, &this->fwd_button };
    const int bw = (content.w - MARGIN * 4) / 3;
    for(int i = 0; i < 3; i++){
        Button* b = new Button(content.x + MARGIN + i * (bw + MARGIN), ctrl_y, labels[i]);
        b->setFontSize(FontFn::Small);
        b->setW(bw);
        b->setH(btn_h);
        WidgetFunctions::Add(b);
        *slots[i] = b;
    }
    this->rew_button->setOnPressEnd([this](){
        const uint32_t t = this->nowMs();
        this->seekMs(t > kSkipMs ? t - kSkipMs : 0);
    });
    this->play_button->setOnPressEnd([this](){ this->togglePlay(); });
    this->fwd_button->setOnPressEnd([this](){ this->seekMs(this->nowMs() + kSkipMs); });

    const int view_y = y0 + back.h + MARGIN;
    this->view = new VideoView(content.x, view_y, content.w, time_y - MARGIN - view_y);
    // 動画をタップしても再生/一時停止
    this->view->setOnPressEnd([this](){ this->togglePlay(); });
    WidgetFunctions::Add(this->view);

    this->last_shown_sec = -1;
    if(!this->path.empty()){
        // 初めて開く(ファイルアプリから)・上へ載った画面から戻ってきた
        this->pending_path = this->path;
        this->pending = Pending::Load;
        this->pending_wait_frames = 1;
    }else{
        // シーンの切り替えと同じフレームで半透明のダイアログを出すと下の絵が残るので1フレーム空ける
        this->view->setMessage("動画を選んでください");
        this->pending = Pending::OpenPicker;
        this->pending_wait_frames = 1;
    }
    this->refreshControls(true);
}

void VideoScene::onExit(){
    // 戻ってきたら同じ時刻から(ランチャへ戻る場合はシーンごと消えるので関係ない)
    if(this->player.isOpen()) this->start_at_ms = this->ended ? 0 : this->nowMs();
    this->closeVideo();
    this->view = nullptr;
    this->back_button = this->open_button = this->play_button = this->rew_button = this->fwd_button = nullptr;
    this->bar = nullptr;
    this->title_label = nullptr;
    this->time_label = nullptr;
    this->picker = nullptr;
    this->pending = Pending::None;
}

void VideoScene::openPicker(){
    if(this->picker) return;
    FixedString<PICO_PATH_LEN> start;
    if(!this->path.empty()) PICO_IO::parent(start, this->path.c_str());
    if(start.empty()){
        if(OSData::SD_usable && OSData::SD.exists(PICO_Path::DIR::VIDEO)){
            //末尾の"/"を落とす(FileExplorerはフォルダ名を末尾の要素から取るため)
            start.assign(PICO_Path::DIR::VIDEO, strlen(PICO_Path::DIR::VIDEO) - 1);
        }else{
            start.assign("/");
        }
    }
    FileSelectDialog* dialog = new FileSelectDialog(start.c_str());
    if(!dialog) return;
    this->picker = dialog;
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClose([this, dialog](bool is_ok){
        const char* selected = is_ok ? dialog->getSelectedPath() : nullptr;
        if(selected){
            this->pending_path.assign(selected);
            this->pending = Pending::Load;
        }else{
            this->pending = Pending::Cancelled;
        }
        this->pending_wait_frames = 1;
        this->picker = nullptr;
        WidgetFunctions::DestroyLater(dialog);
    });
}

// ================================================================ 再生

void VideoScene::closeVideo(){
    if(this->audio_active || SoundFunctions::WavPlaying()) SoundFunctions::WavStop();
    this->audio_active = false;
    this->playing = false;
    this->player.close();
    PICO_GFX::ResetPalette();
    if(this->view) this->view->setPlayer(nullptr);
}

bool VideoScene::load(const char* p, uint32_t at_ms){
    this->closeVideo();
    const PvidPlayer::Error e = this->player.open(p);
    if(e != PvidPlayer::Error::None){
        LOG_APP_FAIL("Video: %s を開けません: %s", p, PvidPlayer::ErrorToStr(e));
        this->view->setMessage("動画を選んでください");
        this->title_label->setText("");
        this->path.clear();
        this->refreshControls(true);
        ErrorFunctions::ShowFatal(PvidPlayer::ErrorToStr(e));
        return false;
    }
    this->path.assign(p);

    // 題名はファイル名から拡張子を落としたもの
    const char* name = PICO_IO::filename(p);
    FixedString<PICO_STR_M> title;
    const char* dot = strrchr(name, '.');
    title.assign(name, dot ? (size_t)(dot - name) : strlen(name));
    this->title_label->setText(title.c_str());

    // 16色全部を動画のものに(ダークテーマでも0番=黒・15番=白のまま見せる)
    const uint16_t* pal = this->player.palette();
    for(int i = 0; i < 16; i++){
        if(PICO_GFX::COLORS[i] != pal[i]){
            PICO_GFX::COLORS[i] = pal[i];
            PICO_GFX::paletteRevision++;
        }
    }

    // 同じ名前の .wav
    this->wav_path.clear();
    if(dot){
        FixedString<PICO_PATH_LEN> w;
        w.assign(p, strlen(p) - strlen(dot));
        w.append(".wav");
        if(OSData::SD.exists(w.c_str())) this->wav_path = w;
    }

    this->view->setMessage("");
    this->view->setPlayer(&this->player);
    this->ended = false;
    if(at_ms >= this->player.durationMs()) at_ms = 0;
    this->clock_ms = at_ms;
    this->resume_millis = millis();
    if(at_ms > 0){
        if(this->player.seekTo(this->player.msToFrame(at_ms)) != PvidPlayer::Error::None) at_ms = 0;
    }
    if(at_ms == 0) this->player.decodeNext();
    this->view->onFrame();

    this->audio_active = false;
    if(!this->wav_path.empty()){
        const char* err = nullptr;
        if(SoundFunctions::WavPlay(this->wav_path.c_str(), false, 100, &err)){
            this->audio_active = true;
            if(at_ms > 0) SoundFunctions::WavSeekMs(at_ms);
        }else{
            LOG_APP_WARN("Video: 音を鳴らせません: %s", err ? err : "?");
        }
    }
    this->playing = true;
    LOG_APP_MSG("Video: %s %dx%d %u/%u fps %u フレーム%s", p, this->player.width(), this->player.height(),
                this->player.fpsNum(), this->player.fpsDen(), (unsigned)this->player.frameCount(),
                this->audio_active ? " (音あり)" : "");
    this->refreshControls(true);
    return true;
}

uint32_t VideoScene::nowMs() const {
    if(this->audio_active) return SoundFunctions::WavPositionMs();
    if(!this->playing) return this->clock_ms;
    return this->clock_ms + (millis() - this->resume_millis);
}

void VideoScene::setPlaying(bool on){
    if(!this->player.isOpen() || on == this->playing) return;
    if(!on){
        this->clock_ms = this->nowMs();
        if(this->audio_active) SoundFunctions::WavPause(true);
    }else{
        this->resume_millis = millis();
        if(this->audio_active) SoundFunctions::WavPause(false);
    }
    this->playing = on;
    this->refreshControls(false);
}

void VideoScene::togglePlay(){
    if(!this->player.isOpen()) return;
    if(this->ended){
        this->seekMs(0);
        this->setPlaying(true);
        return;
    }
    this->setPlaying(!this->playing);
}

void VideoScene::seekMs(uint32_t ms){
    if(!this->player.isOpen()) return;
    const uint32_t dur = this->player.durationMs();
    if(ms >= dur) ms = dur > 0 ? dur - 1 : 0;
    const PvidPlayer::Error e = this->player.seekTo(this->player.msToFrame(ms));
    if(e != PvidPlayer::Error::None){
        ErrorFunctions::ShowFatal(PvidPlayer::ErrorToStr(e));
        this->closeVideo();
        this->refreshControls(true);
        return;
    }
    this->view->onFrame();
    this->ended = false;
    this->clock_ms = ms;
    this->resume_millis = millis();
    if(!this->wav_path.empty()){
        // 終わって閉じていても WavSeekMs() は開き直して飛ぶ。止めていた(WavStop)なら鳴らし直す
        bool ok = SoundFunctions::WavSeekMs(ms);
        if(!ok && SoundFunctions::WavPlay(this->wav_path.c_str())) ok = SoundFunctions::WavSeekMs(ms) || ms == 0;
        this->audio_active = ok;
        if(ok) SoundFunctions::WavPause(!this->playing);
    }
    this->last_shown_sec = -1;
    this->refreshControls(false);
}

void VideoScene::refreshControls(bool force){
    if(!this->play_button) return;
    const bool open = this->player.isOpen();
    const bool show_pause = open && this->playing && !this->ended;
    if(force || show_pause != this->last_playing_label){
        this->play_button->setText(show_pause ? "一時停止" : (this->ended ? "最初から" : "再生"));
        this->last_playing_label = show_pause;
    }
    this->play_button->setEnabled(open);
    this->rew_button->setEnabled(open);
    this->fwd_button->setEnabled(open);

    const uint32_t t = open ? this->nowMs() : 0;
    const int sec = (int)(t / 1000);
    if(force || sec != this->last_shown_sec){
        this->last_shown_sec = sec;
        char a[12], b[12], text[28];
        FormatTime(a, sizeof(a), t);
        FormatTime(b, sizeof(b), open ? this->player.durationMs() : 0);
        snprintf(text, sizeof(text), "%s / %s", a, b);
        this->time_label->setText(text);
    }
    const uint32_t dur = open ? this->player.durationMs() : 0;
    //画素の単位に丸める(毎フレーム値が変わるとバーを毎フレーム描き直すため)
    const int bw = this->bar->getLocalRect().w;
    const int px = (dur > 0 && bw > 0) ? (int)((uint64_t)(t > dur ? dur : t) * bw / dur) : 0;
    this->bar->setValue(bw > 0 ? (float)px / (float)bw : 0.0f);
}

void VideoScene::onUpdate(){
    if(this->pending != Pending::None){
        if(this->pending_wait_frames > 0){
            this->pending_wait_frames--;
        }else{
            const Pending p = this->pending;
            this->pending = Pending::None;
            if(p == Pending::OpenPicker){
                this->openPicker();
            }else if(p == Pending::Load){
                FixedString<PICO_PATH_LEN> target = this->pending_path;
                const uint32_t at = (target == this->path) ? this->start_at_ms : 0;
                this->start_at_ms = 0;
                this->load(target.c_str(), at);
            }else if(p == Pending::Cancelled){
                // 何も開いていなければランチャへ戻る
                if(!this->player.isOpen()) SceneFunctions::Pop();
            }
        }
    }
    if(!this->player.isOpen()){
        return;
    }
    if(!this->playing){
        this->refreshControls(false);
        return;
    }
    PowerFunctions::KeepAwake();

    // 音が先に終わった/止まったら millis() で続ける
    if(this->audio_active && !SoundFunctions::WavPlaying()){
        this->clock_ms = this->player.frameToMs((uint32_t)(this->player.current() + 1));
        this->resume_millis = millis();
        this->audio_active = false;
    }

    const uint32_t t = this->nowMs();
    const uint32_t count = this->player.frameCount();
    uint32_t target = this->player.msToFrame(t);
    if(target >= count) target = count - 1;

    PvidPlayer::Error e = PvidPlayer::Error::None;
    const int32_t cur = this->player.current();
    if((int32_t)target - cur > kMaxDecodePerUpdate * 2){
        // 大きく遅れた: 途中の絵は描かずに飛ぶ(キーフレームが近ければそこから)
        e = this->player.seekTo(target);
    }else{
        for(int i = 0; i < kMaxDecodePerUpdate && this->player.current() < (int32_t)target; i++){
            e = this->player.decodeNext();
            if(e != PvidPlayer::Error::None) break;
        }
    }
    if(e != PvidPlayer::Error::None && e != PvidPlayer::Error::Ended){
        LOG_APP_FAIL("Video: %s", PvidPlayer::ErrorToStr(e));
        ErrorFunctions::ShowFatal(PvidPlayer::ErrorToStr(e));
        this->closeVideo();
        this->view->setMessage(PvidPlayer::ErrorToStr(e));
        this->refreshControls(true);
        return;
    }
    this->view->onFrame();

    // 最後のフレームを出して、長さぶんの時間が経ったら終わり
    if(this->player.current() + 1 >= (int32_t)count && t >= this->player.durationMs() &&
       !(this->audio_active && SoundFunctions::WavPlaying())){
        this->clock_ms = this->player.durationMs();
        this->playing = false;
        this->ended = true;
        this->refreshControls(true);
        return;
    }
    this->refreshControls(false);
}

bool VideoScene::onKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;
    if(ev.key == K::Escape){ SceneFunctions::Pop(); return true; }
    if(!this->player.isOpen()) return false;
    if(ev.key == K::Left){
        const uint32_t t = this->nowMs();
        this->seekMs(t > kSkipMs ? t - kSkipMs : 0);
        return true;
    }
    if(ev.key == K::Right){ this->seekMs(this->nowMs() + kSkipMs); return true; }
    if(ev.isPlainChar() && (ev.cp == ' ' || ev.cp == 'k')){ this->togglePlay(); return true; }
    return false;
}
