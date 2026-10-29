#include "gui/scenes/FileViewerScene.hpp"
#include "apps/texteditor/TextEditorScene.hpp"
#include "storage/Recent_Files.hpp"
#include "apps/video/VideoScene.hpp"
#include "apps/firmware/FirmwareScene.hpp"
#include "apps/reader/ReaderScene.hpp"
#include "config/App_Config.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "sound/Mml_Compiler.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

namespace {
    // pathの拡張子がextと一致するか(大文字小文字を区別しない)
    bool HasExtension(const char* path, const char* ext){
        const size_t path_len = strlen(path);
        const size_t ext_len  = strlen(ext);
        if(ext_len > path_len) return false;
        return strcasecmp(path + (path_len - ext_len), ext) == 0;
    }

    //ImageView で開く画像(中身の形式は ImageView がファイルの先頭で見分ける)
    bool IsImagePath(const char* path){
        return HasExtension(path, ".pimg") || HasExtension(path, ".bmp") || HasExtension(path, ".png")
            || HasExtension(path, ".jpg") || HasExtension(path, ".jpeg");
    }
}

Rect FileViewerScene::bodyRect(int top_row_h) const {
    const Rect content = Scene::contentRect();
    const int16_t body_y = content.y + MARGIN + top_row_h + MARGIN;
    return { content.x, body_y, content.w, (int16_t)(content.y + content.h - this->media_h - body_y) };
}

void FileViewerScene::showText(const Rect& body, const char* message){
    this->text_buf.assign(message);
    if(!this->text_view){
        this->text_view = new TextView(body.x, body.y, body.w, body.h);
        this->text_view->setFontSize(FontFn::Text);
        WidgetFunctions::Add(this->text_view);
    }
    this->text_view->setDocument(this->text_buf.c_str(), (int)this->text_buf.length());
}

bool FileViewerScene::loadPlainText(const Rect& body, bool& truncated){
    truncated = false;
    FsFile f = OSData::SD.open(this->path.c_str());
    if(!f) return false;

    //ファイル全体ぶんの一時バッファを確保せず、小さなチャンクで読み進めて本文へ追記する。
    //'\r'は捨てる(TextViewは'\n'だけを改行として扱う)
    this->text_buf.clear();
    char chunk[256];
    char clean[256];
    for(;;){
        const int got = f.read((uint8_t*)chunk, sizeof(chunk));
        if(got <= 0) break;
        int n = 0;
        for(int i = 0; i < got; i++){
            if(chunk[i] != '\r' && chunk[i] != '\0') clean[n++] = chunk[i];
        }
        if(this->text_buf.length() + (size_t)n > FixedString<kMaxTextBytes>::capacity()){
            //入るところまで入れて打ち切る(appendがUTF-8の文字の途中では切らない)
            this->text_buf.append(clean, (size_t)n);
            truncated = true;
            break;
        }
        this->text_buf.append(clean, (size_t)n);
    }
    f.close();

    this->text_view = new TextView(body.x, body.y, body.w, body.h);
    this->text_view->setFontSize(FontFn::Text); //本文は文字の大きさの設定に従う
    WidgetFunctions::Add(this->text_view);
    this->text_view->setDocument(this->text_buf.c_str(), (int)this->text_buf.length());
    if(this->text_view->isTruncated()) truncated = true;
    return true;
}

void FileViewerScene::onEnter(){
    const Rect content = Scene::contentRect();

    if(!this->recorded){
        this->recorded = true;
        if(OSData::SD_usable && OSData::SD.exists(this->path.c_str())) RecentFiles::Touch(this->path.c_str());
    }

    //動画は動画アプリへ置き換わる(「戻る」で元の画面へ戻る)
#if PICO_APP_VIDEO
    if(OSData::SD_usable && HasExtension(this->path.c_str(), ".pvid")){
        SceneFunctions::Change(new VideoScene(this->path.c_str()));
        return;
    }
#endif
    //ファームの更新ファイルは「本体の更新」へ置き換わる(確かめてから書き込むかを聞く)
    if(OSData::SD_usable && HasExtension(this->path.c_str(), ".pfw")){
        SceneFunctions::Change(new FirmwareScene(this->path.c_str()));
        return;
    }
    //Shift_JIS のテキスト(青空文庫の配布物)はここでは文字化けするので、読書アプリへ置き換わる
#if PICO_APP_READER
    if(OSData::SD_usable && HasExtension(this->path.c_str(), ".txt")){
        ReaderFile f;
        Aozora::Info info;
        if(f.open(this->path.c_str())){
            Aozora::Analyze(f, info);
            f.close();
            if(info.enc == Aozora::Encoding::ShiftJis){
                SceneFunctions::Change(new ReaderScene(this->path.c_str()));
                return;
            }
        }
    }
#endif
    this->media = Media::None;
    if(OSData::SD_usable){
        if(HasExtension(this->path.c_str(), ".wav"))      this->media = Media::Wav;
        else if(HasExtension(this->path.c_str(), ".mml")) this->media = Media::Mml;
    }
    //WAVは44100Hzで鳴らす。切り替えは鳴っているものを止めるので、鳴らす前に頼んでおく
    if(this->media == Media::Wav) SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateHigh);
    this->media_h = 0;
    this->media_started = this->media_ended = false;
    this->last_shown_sec = -1;

    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    const int status_x = back_box.x + back_box.w + MARGIN;

    const char* p = this->path.c_str();
    //エディタはテキスト(PICO_APP_TEXTEDITOR)を外した版には出さない
    const bool editable = PICO_APP_TEXTEDITOR && OSData::SD_usable && !IsImagePath(p) && this->media != Media::Wav;

    //右端の「編集」(テキスト系だけ)。状態欄はその手前までにする
    int status_right = content.x + content.w - MARGIN;
    if(editable){
        this->edit_button = new Button(0, content.y + MARGIN, "編集");
        this->edit_button->setFontSize(FontFn::Small);
        this->edit_button->setH(20 + Button::kFrameExtra);
        this->edit_button->setX(content.x + content.w - MARGIN - this->edit_button->getLocalRect().w);
        this->edit_button->setOnPressEnd([this](){
            SceneFunctions::Push(new TextEditorScene(this->path.c_str()));
        });
        WidgetFunctions::Add(this->edit_button);
        status_right = this->edit_button->getLocalRect().x - MARGIN;
    }
    //.txt は「読む」で読書アプリ(ページ送り・しおり・続きから。青空文庫の Shift_JIS も読める)
    if(PICO_APP_READER && OSData::SD_usable && HasExtension(p, ".txt")){
        this->read_button = new Button(0, content.y + MARGIN, "読む");
        this->read_button->setFontSize(FontFn::Small);
        this->read_button->setH(20 + Button::kFrameExtra);
        this->read_button->setX(status_right - this->read_button->getLocalRect().w);
        this->read_button->setOnPressEnd([this](){
            SceneFunctions::Push(new ReaderScene(this->path.c_str()));
        });
        WidgetFunctions::Add(this->read_button);
        status_right = this->read_button->getLocalRect().x - MARGIN;
    }

    this->status_label = new Label<PICO_STR_L>(status_x, content.y + MARGIN, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(status_right - status_x);
    this->status_label->setMaxHeight(back_box.h);
    //ファイル名に**等が含まれても装飾として解釈しない
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    if(this->media != Media::None) this->buildMediaControls(content);
    const Rect body = this->bodyRect(back_box.h);
    const char* name = PICO_IO::filename(p);
    this->status_label->setText(name);

    if(!OSData::SD_usable){
        this->showText(body, "SDカードが使えません");
        return;
    }

    if(HasExtension(p, ".md") || HasExtension(p, ".markdown")){
        this->md_view = new MarkdownView(body.x, body.y, body.w, body.h);
        this->md_view->setBodyFontSize(FontFn::Text);
        WidgetFunctions::Add(this->md_view);
        if(!this->md_view->load(p)){
            this->md_view->setVisible(false);
            this->showText(body, "このファイルを開けませんでした");
        }
    }else if(this->media == Media::Wav){
        //WAVは見せる中身が無いので、ファイル名だけを真ん中に
        {
            FixedString<PICO_STR_L> msg;
            msg.append(name);
            this->showText(body, msg.c_str());
        }
        this->startMedia();
    }else if(IsImagePath(p)){
        this->image_view = new ImageView(body.x, body.y, body.w, body.h);
        WidgetFunctions::Add(this->image_view);
        if(!this->image_view->load(p)){
            this->image_view->setVisible(false);
            this->showText(body, "この画像を開けませんでした");
        }else{
            //元の大きさ。縮めて見せていれば「(1/4)」を足す
            char buf[PICO_STR_L];
            const int div = this->image_view->getScaleDiv();
            if(div > 1){
                snprintf(buf, sizeof(buf), "%s %dx%d(1/%d)", name,
                    this->image_view->getSourceW(), this->image_view->getSourceH(), div);
            }else{
                snprintf(buf, sizeof(buf), "%s %dx%d", name,
                    this->image_view->getSourceW(), this->image_view->getSourceH());
            }
            this->status_label->setText(buf);
        }
    }else{
        bool truncated = false;
        if(!this->loadPlainText(body, truncated)){
            this->showText(body, "このファイルを開けませんでした");
        }else if(truncated){
            char buf[PICO_STR_L];
            snprintf(buf, sizeof(buf), "%s(途中まで)", name);
            this->status_label->setText(buf);
            LOG_SYS_WARN("FileViewer: %s は大きすぎるため途中までしか表示しません", p);
        }
        //MMLは中身を見せたまま鳴らす
        if(this->media == Media::Mml) this->startMedia();
    }
    this->refreshMedia(true);
}

void FileViewerScene::onExit(){
    this->stopMedia();
    this->seek_bar = nullptr;
    this->media_buttons = nullptr;
    this->time_label = nullptr;
    this->back_button  = nullptr;
    this->edit_button  = nullptr;
    this->read_button  = nullptr;
    this->status_label = nullptr;
    this->md_view      = nullptr;
    this->text_view    = nullptr;
    this->image_view   = nullptr;
}

// ================================================================ 音の再生

uint32_t FileViewerScene::preferredSampleRate() const {
    return HasExtension(this->path.c_str(), ".wav") ? SoundFunctions::kSampleRateHigh : 0;
}

void FileViewerScene::buildMediaControls(const Rect& content){
    constexpr int kBarH = 12;
    const int btn_h = MediaButtons::kHeight;
    const int time_h = FontFn::GetFontSize(FontFn::Small);
    // 下から: ボタンの行 → 時間の行(バー+時刻)
    const int ctrl_y = content.y + content.h - MARGIN - btn_h;
    const int time_y = ctrl_y - MARGIN - time_h;
    this->media_h = (content.y + content.h) - time_y + MARGIN;

    this->time_label = new Label<PICO_STR_S>(0, time_y, "0:00 / 0:00");
    this->time_label->setFontSize(FontFn::Small);
    this->time_label->setDisableAutoTextDecoration(true);
    this->time_label->setX(content.x + content.w - MARGIN - this->time_label->getLocalRect().w);
    WidgetFunctions::Add(this->time_label);

    const int bar_x = content.x + MARGIN;
    const int bar_w = this->time_label->getLocalRect().x - MARGIN - bar_x;
    this->seek_bar = new SeekBar((int16_t)bar_x, (int16_t)(time_y + (time_h - kBarH) / 2),
                                 (int16_t)bar_w, (int16_t)kBarH);
    this->seek_bar->setSeekable(this->media == Media::Wav);
    this->seek_bar->setOnSeek([this](float r){
        if(this->media != Media::Wav) return;
        const uint32_t dur = SoundFunctions::WavDurationMs();
        this->seekMedia((uint32_t)(r * (float)dur));
    });
    WidgetFunctions::Add(this->seek_bar);

    this->media_buttons = new MediaButtons(content.x + MARGIN, ctrl_y, content.w - MARGIN * 2);
    this->media_buttons->setOnButton([this](int b){
        if(b == MediaButtons::kRew)       this->skipMedia(-(int32_t)kSkipMs);
        else if(b == MediaButtons::kFwd)  this->skipMedia((int32_t)kSkipMs);
        else                              this->toggleMedia();
    });
    WidgetFunctions::Add(this->media_buttons);
    this->last_playing_label = false;
}

bool FileViewerScene::startMedia(){
    this->media_ended = false;
    if(this->media == Media::Wav){
        const char* err = "";
        if(!SoundFunctions::WavPlay(this->path.c_str(), false, 100, &err)){
            ErrorFunctions::ShowFatal(err && *err ? err : "WAVとして読めません");
            this->media_started = false;
            return false;
        }
        SoundFunctions::MusicStop();
    }else{
        MmlResult r;
        if(!SoundFunctions::MusicPlayFile(this->path.c_str(), &r)){
            FixedString<PICO_STR_256B> msg;
            if(r.line > 0) msg.appendFormat("%d行%d列: ", r.line, r.col);
            msg.append(r.message.c_str());
            ErrorFunctions::ShowFatal(msg.c_str());
            this->media_started = false;
            return false;
        }
        SoundFunctions::WavStop();
    }
    this->media_started = true;
    this->last_shown_sec = -1;
    return true;
}

void FileViewerScene::stopMedia(){
    if(this->media == Media::Wav)      SoundFunctions::WavStop();
    else if(this->media == Media::Mml) SoundFunctions::MusicStop();
    this->media_started = false;
}

bool FileViewerScene::mediaActive() const {
    if(!this->media_started) return false;
    return this->media == Media::Wav ? SoundFunctions::WavPlaying() : SoundFunctions::MusicPlaying();
}

bool FileViewerScene::mediaPaused() const {
    return this->media == Media::Wav ? SoundFunctions::WavPaused() : SoundFunctions::MusicPaused();
}

uint32_t FileViewerScene::mediaPosMs() const {
    if(!this->media_started) return 0;
    return this->media == Media::Wav ? SoundFunctions::WavPositionMs() : SoundFunctions::MusicElapsedMs();
}

uint32_t FileViewerScene::mediaTotalMs(bool* loops) const {
    if(loops) *loops = false;
    if(this->media == Media::Wav) return SoundFunctions::WavDurationMs();
    return SoundFunctions::MusicTotalMs(loops);
}

void FileViewerScene::toggleMedia(){
    if(this->media == Media::None) return;
    if(!this->media_started || !this->mediaActive()){
        //終わっていた/鳴らせていなかった: 最初から
        this->startMedia();
    }else if(this->media == Media::Wav){
        SoundFunctions::WavPause(!SoundFunctions::WavPaused());
    }else{
        SoundFunctions::MusicPause(!SoundFunctions::MusicPaused());
    }
    this->refreshMedia(true);
}

void FileViewerScene::seekMedia(uint32_t ms){
    //飛べるのはWAVだけ(MMLの時間はシークできない)
    if(this->media != Media::Wav || !this->media_started) return;
    const uint32_t dur = SoundFunctions::WavDurationMs();
    if(ms >= dur) ms = dur > 0 ? dur - 1 : 0;
    const bool was_paused = SoundFunctions::WavPlaying() && SoundFunctions::WavPaused();
    if(SoundFunctions::WavSeekMs(ms)){
        //終わっていたものは飛んだ先から続けて鳴らす
        SoundFunctions::WavPause(was_paused);
        this->media_ended = false;
    }
    this->last_shown_sec = -1;
    this->refreshMedia(true);
}

void FileViewerScene::skipMedia(int32_t delta_ms){
    if(this->media != Media::Wav) return;
    const int64_t t = (int64_t)this->mediaPosMs() + delta_ms;
    this->seekMedia(t < 0 ? 0 : (uint32_t)t);
}

void FileViewerScene::refreshMedia(bool force){
    if(!this->media_buttons) return;
    const bool active = this->mediaActive();
    const bool show_pause = active && !this->mediaPaused();
    this->media_buttons->setShowPause(show_pause);
    this->last_playing_label = show_pause;
    this->media_buttons->setSkipEnabled(this->media == Media::Wav && this->media_started);

    bool loops = false;
    const uint32_t total = this->media_started ? this->mediaTotalMs(&loops) : 0;
    uint32_t pos = active || this->media_ended ? this->mediaPosMs() : 0;
    if(this->media_ended) pos = total;
    const int sec = (int)(pos / 1000);
    if(force || sec != this->last_shown_sec){
        this->last_shown_sec = sec;
        char a[12], b[12], text[28];
        snprintf(a, sizeof(a), "%u:%02u", (unsigned)(pos / 60000), (unsigned)((pos / 1000) % 60));
        if(loops) snprintf(b, sizeof(b), "loop");
        else      snprintf(b, sizeof(b), "%u:%02u", (unsigned)(total / 60000), (unsigned)((total / 1000) % 60));
        snprintf(text, sizeof(text), "%s / %s", a, b);
        this->time_label->setText(text);
    }
    //終わりの無いMMLは1周の中の位置で見せる(バーは進むだけ)
    float ratio = 0.0f;
    if(total > 0) ratio = (float)(pos > total ? total : pos) / (float)total;
    this->seek_bar->setRatio(ratio);
}

void FileViewerScene::onUpdate(){
    if(this->media == Media::None) return;
    //鳴らしていたのに鳴らなくなった = 最後まで鳴らし終えた
    if(this->media_started && !this->media_ended && !this->mediaActive()){
        this->media_ended = true;
    }
    this->refreshMedia(false);
}

bool FileViewerScene::onKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;
    if(this->media == Media::None) return false;
    if(ev.key == K::Escape){ SceneFunctions::Pop(); return true; }
    if(ev.key == K::Left && this->media == Media::Wav){ this->skipMedia(-(int32_t)kSkipMs); return true; }
    if(ev.key == K::Right && this->media == Media::Wav){ this->skipMedia((int32_t)kSkipMs); return true; }
    if(ev.isPlainChar() && (ev.cp == ' ' || ev.cp == 'k')){ this->toggleMedia(); return true; }
    return false;
}
