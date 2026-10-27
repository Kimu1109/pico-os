#include "gui/scenes/RecorderScene.hpp"
#include "gui/scenes/FileViewerScene.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "hal/Mic_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
    constexpr uint32_t kRates[] = {16000, 24000, 48000};
    constexpr int kGains[] = {0, 6, 12, 18, 24, 30, 36};

    uint32_t ReadLe32(const uint8_t* p){
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    // 録ったWAVの長さ(ms)。読めなければ0
    uint32_t WavDurationMs(const char* path){
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if(!f) return 0;
        uint8_t h[44];
        const int n = f.read(h, sizeof(h));
        const uint32_t size = (uint32_t)f.size();
        f.close();
        if(n != (int)sizeof(h) || memcmp(h, "RIFF", 4) != 0) return 0;
        const uint32_t rate = ReadLe32(h + 24);
        const uint32_t block = (uint32_t)h[32] | ((uint32_t)h[33] << 8);
        if(rate == 0 || block == 0) return 0;
        uint32_t data = ReadLe32(h + 40);
        //書きかけ(見出しの大きさがまだ0)ならファイルの大きさから
        if(data == 0 && size > 44) data = size - 44;
        return (uint32_t)((uint64_t)data / block * 1000 / rate);
    }

    void FormatTime(char* out, size_t cap, uint32_t ms, bool tenths){
        const uint32_t s = ms / 1000;
        if(tenths) snprintf(out, cap, "%lu:%02lu.%lu", (unsigned long)(s / 60), (unsigned long)(s % 60), (unsigned long)(ms % 1000 / 100));
        else snprintf(out, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
}

bool RecorderScene::keepForeground() const { return MicFunctions::IsRecording(); }

void RecorderScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int x0 = content.x + MARGIN;
    const int w = content.w - MARGIN * 2;
    int y = content.y + 4;

    auto* back = new Button(x0, y, "戻る");
    back->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(back);
    const int row_h = back->getLocalRect().h;

    auto* title = new Label<PICO_STR_S>(back->getLocalRect().x + back->getLocalRect().w + 8, y + 4, kAppName);
    WidgetFunctions::Add(title);

    //周波数と増幅は小さい字で(大きい字だと上の段に収まらない)
    constexpr int kSmallW = 52;
    this->gain_button = new Button(0, y, "+36dB");
    this->gain_button->setFontSize(FontFn::Small);
    this->gain_button->setW(kSmallW);
    this->gain_button->setH(row_h);
    this->gain_button->setX(content.x + content.w - MARGIN - kSmallW);
    this->gain_button->setOnPressEnd([this](){ this->cycleGain(); });
    WidgetFunctions::Add(this->gain_button);

    this->rate_button = new Button(0, y, "48kHz");
    this->rate_button->setFontSize(FontFn::Small);
    this->rate_button->setW(kSmallW);
    this->rate_button->setH(row_h);
    this->rate_button->setX(this->gain_button->getLocalRect().x - 4 - kSmallW);
    this->rate_button->setOnPressEnd([this](){ this->cycleRate(); });
    WidgetFunctions::Add(this->rate_button);
    y += row_h + 6;

    this->time_label = new Label<PICO_STR_S>(x0, y, "0:00.0");
    this->time_label->setFontSize(FontFn::Big);
    WidgetFunctions::Add(this->time_label);
    y += 36;

    this->status_label = new Label<PICO_STR_LL>(x0, y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(w);
    this->status_label->setMaxHeight(34);
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);
    y += 36;

    this->meter = new ProgressBar(x0, y, w, 12);
    this->meter->setColor(PICO_GREEN);
    WidgetFunctions::Add(this->meter);
    y += 18;

    this->rec_button = new Button(x0, y, "録音");
    this->rec_button->setW(w);
    this->rec_button->setH(40);
    this->rec_button->setOnPressEnd([this](){ this->toggleRecording(); });
    WidgetFunctions::Add(this->rec_button);
    y += 46;

    const int bottom_h = row_h;
    const int list_h = content.y + content.h - MARGIN - bottom_h - 4 - y;
    this->list = new ScrollList(x0, y, w, list_h > 40 ? list_h : 40);
    this->list->setFontSize(FontFn::Small);
    this->list->setEnableIcon(true);
    this->list->setOnSelectItem([this](int, bool already_selected){
        if(already_selected) this->openSelected();
        this->refreshButtons();
    });
    WidgetFunctions::Add(this->list);

    this->delete_button = new Button(x0, content.y + content.h - MARGIN - bottom_h, "削除");
    this->delete_button->setOnPressEnd([this](){ this->deleteSelected(); });
    WidgetFunctions::Add(this->delete_button);

    auto* open_button = new Button(this->delete_button->getLocalRect().x + this->delete_button->getLocalRect().w + 6,
                                   content.y + content.h - MARGIN - bottom_h, "再生");
    open_button->setOnPressEnd([this](){ this->openSelected(); });
    WidgetFunctions::Add(open_button);

    this->was_recording = MicFunctions::IsRecording();
    this->confirming = false;
    this->meter_level = 0;
    this->shown_sec = 0xFFFFFFFFu;
    MicFunctions::SetMonitor(true);
    MicFunctions::TakePeak();
    this->reloadList();
    this->refreshButtons();
    this->refreshStatus();
}

void RecorderScene::onExit(){
    //画面を離れたら録音を止める(後片付けと保存は MicFunctions::Update() が続ける)
    if(MicFunctions::IsRecording()) MicFunctions::StopRecording();
    MicFunctions::SetMonitor(false);
    this->time_label = nullptr;
    this->status_label = nullptr;
    this->meter = nullptr;
    this->rec_button = nullptr;
    this->rate_button = nullptr;
    this->gain_button = nullptr;
    this->delete_button = nullptr;
    this->list = nullptr;
}

void RecorderScene::onUpdate(){
    const bool recording = MicFunctions::IsRecording();
    if(recording) PowerFunctions::KeepAwake();
    //止めて保存し終えたら、ボタンと一覧を戻す
    if(this->was_recording && !recording){
        this->reloadList();
        this->refreshButtons();
        const MicFunctions::StopReason r = MicFunctions::LastStopReason();
        if(r != MicFunctions::StopReason::User && r != MicFunctions::StopReason::None){
            char msg[160];
            snprintf(msg, sizeof(msg), "録音を途中で止めました: %s\n(%s までは保存されています)",
                MicFunctions::StopReasonToStr(r), MicFunctions::RecordingPath());
            ErrorFunctions::ShowFatal(msg);
        }
    }
    this->was_recording = recording;

    const uint32_t now = millis();
    if(now - this->last_meter_ms >= kMeterMs){
        this->last_meter_ms = now;
        this->updateMeter(now);
    }
    if(now - this->last_status_ms >= kStatusMs){
        this->last_status_ms = now;
        this->refreshStatus();
    }
}

bool RecorderScene::onKey(const KeyInputFunctions::Event& ev){
    using KeyInputFunctions::Key;
    if(ev.key == Key::PlayPause || (ev.key == Key::Char && !ev.ctrl() && (ev.cp == ' ' || ev.cp == 'r'))){
        this->toggleRecording();
        return true;
    }
    if(ev.key == Key::Escape){
        SceneFunctions::Pop();
        return true;
    }
    return false;
}

void RecorderScene::toggleRecording(){
    if(MicFunctions::IsFinishing()) return;
    if(MicFunctions::IsRecording()){
        MicFunctions::StopRecording();
    }else{
        const MicFunctions::RecError e = MicFunctions::StartRecording();
        if(e != MicFunctions::RecError::None){
            ErrorFunctions::ShowFatal(MicFunctions::RecErrorToStr(e));
            return;
        }
        this->shown_sec = 0xFFFFFFFFu;
    }
    this->refreshButtons();
    this->refreshStatus();
}

void RecorderScene::cycleRate(){
    if(MicFunctions::IsRecording()) return;
    const uint32_t cur = MicFunctions::SampleRate();
    const int n = (int)(sizeof(kRates) / sizeof(kRates[0]));
    int i = 0;
    while(i < n && kRates[i] != cur) i++;
    MicFunctions::SetSampleRate(kRates[(i + 1) % n]);
    this->refreshButtons();
}

void RecorderScene::cycleGain(){
    const int cur = MicFunctions::GainDb();
    const int n = (int)(sizeof(kGains) / sizeof(kGains[0]));
    int next = kGains[0];
    for(int i = 0; i < n; i++){
        if(kGains[i] > cur){ next = kGains[i]; break; }
    }
    MicFunctions::SetGainDb(next);
    this->refreshButtons();
}

void RecorderScene::refreshButtons(){
    if(!this->rec_button) return;
    const bool recording = MicFunctions::IsRecording();
    this->rec_button->setText(MicFunctions::IsFinishing() ? "保存中…" : recording ? "停止" : "録音");
    this->rec_button->setTextColor(recording ? PICO_RED : PICO_FORECOLOR);
    char buf[16];
    snprintf(buf, sizeof(buf), "%lukHz", (unsigned long)(MicFunctions::SampleRate() / 1000));
    this->rate_button->setText(buf);
    this->rate_button->setEnabled(!recording);
    snprintf(buf, sizeof(buf), "+%ddB", MicFunctions::GainDb());
    this->gain_button->setText(buf);
    const bool has_sel = this->list && this->list->getSelectedIndex() >= 0 && this->list->getSelectedIndex() < this->file_count;
    this->delete_button->setEnabled(!recording && has_sel);
}

void RecorderScene::refreshStatus(){
    if(!this->status_label) return;
    const bool recording = MicFunctions::IsRecording();

    //時間(0.1秒が変わったときだけ書き換える)
    const uint32_t ms = recording ? MicFunctions::RecordedMs() : 0;
    if(ms / 100 != this->shown_sec){
        this->shown_sec = ms / 100;
        char t[16];
        FormatTime(t, sizeof(t), ms, true);
        this->time_label->setText(t);
        this->time_label->setTextColor(recording ? PICO_RED : PICO_FORECOLOR);
    }

    char buf[160];
    int8_t color = PICO_DARKGREY;
    const MicFunctions::Input in = MicFunctions::InputState();
    if(in == MicFunctions::Input::Failed){
        snprintf(buf, sizeof(buf), "マイクのI2Sを開始できませんでした");
        color = PICO_RED;
    }else if(in == MicFunctions::Input::Running && !MicFunctions::HasSignal()){
        snprintf(buf, sizeof(buf), "マイクの信号がありません(配線を確認)");
        color = PICO_RED;
    }else if(recording){
        const char* name = strrchr(MicFunctions::RecordingPath(), '/');
        name = name ? name + 1 : MicFunctions::RecordingPath();
        const uint32_t dropped = MicFunctions::DroppedSamples();
        if(dropped > 0){
            const uint32_t dms = (uint32_t)((uint64_t)dropped * 1000 / MicFunctions::SampleRate());
            snprintf(buf, sizeof(buf), "録音中 %s\n取りこぼし %lums(SDが遅い?)", name, (unsigned long)dms);
            color = PICO_RED;
        }else{
            snprintf(buf, sizeof(buf), "録音中 %s", name);
            color = PICO_FORECOLOR;
        }
    }else if(in == MicFunctions::Input::Starting){
        snprintf(buf, sizeof(buf), "マイクを準備しています…");
    }else{
        snprintf(buf, sizeof(buf), "待機中(Space / r で録音)");
    }
    this->status_label->setText(buf);
    this->status_label->setTextColor(color);
}

void RecorderScene::updateMeter(uint32_t){
    if(!this->meter) return;
    const int32_t peak = MicFunctions::TakePeak();
    //-60〜0dBFS を 0〜100 へ。落ちるときはゆっくり(1回3%)
    float level = 0;
    if(peak > 0){
        const float db = 20.0f * log10f((float)peak / 32768.0f);
        level = (db + 60.0f) / 60.0f * 100.0f;
        if(level < 0) level = 0;
        if(level > 100) level = 100;
    }
    if(level < this->meter_level - 3.0f) level = this->meter_level - 3.0f;
    if((int)level == (int)this->meter_level) { this->meter_level = level; return; }
    this->meter_level = level;
    this->meter->setValue(level);
    //-1dBFS より上は割れかけ(赤)、-12dBFS より上は黄
    this->meter->setColor(peak >= 29205 ? PICO_RED : (peak >= 8231 ? PICO_YELLOW : PICO_GREEN));
}

void RecorderScene::reloadList(){
    if(!this->list) return;
    this->list->clear();
    this->file_count = 0;
    if(!OSData::SD_usable) return;

    char dir_path[PICO_PATH_LEN + 1];
    snprintf(dir_path, sizeof(dir_path), "%s", PICO_Path::DIR::RECORDINGS);
    FsFile dir = OSData::SD.open(dir_path, O_RDONLY);
    if(!dir) return;
    FsFile file;
    while(file.openNext(&dir, O_RDONLY)){
        char name[64];
        const bool is_dir = file.isDirectory();
        const bool got = file.getName(name, sizeof(name));
        file.close();
        if(is_dir || !got) continue;
        const size_t len = strlen(name);
        if(len < 5 || strcasecmp(name + len - 4, ".wav") != 0) continue;
        if(len >= PICO_STR_S) continue;
        //新しい順(名前の大きい順)に差し込む。多すぎれば古いものから落とす
        int pos = 0;
        while(pos < this->file_count && strcmp(this->files[pos].c_str(), name) > 0) pos++;
        if(pos >= kMaxFiles) continue;
        const int last = this->file_count < kMaxFiles ? this->file_count : kMaxFiles - 1;
        for(int i = last; i > pos; i--) this->files[i] = this->files[i - 1];
        this->files[pos].assign(name);
        if(this->file_count < kMaxFiles) this->file_count++;
    }
    dir.close();

    for(int i = 0; i < this->file_count; i++){
        char path[PICO_PATH_LEN + 1];
        snprintf(path, sizeof(path), "%s%s", PICO_Path::DIR::RECORDINGS, this->files[i].c_str());
        char t[16];
        FormatTime(t, sizeof(t), WavDurationMs(path), false);
        ScrollListTools::Item item;
        item.icon = IconID::Microphone;
        char text[64];
        const size_t base_len = this->files[i].length() - 4;    // ".wav" を落とす
        snprintf(text, sizeof(text), "%.*s  %s", (int)base_len, this->files[i].c_str(), t);
        item.text.assign(text);
        this->list->add(item);
    }
}

void RecorderScene::openSelected(){
    if(MicFunctions::IsRecording() || !this->list) return;
    const int i = this->list->getSelectedIndex();
    if(i < 0 || i >= this->file_count) return;
    char path[PICO_PATH_LEN + 1];
    snprintf(path, sizeof(path), "%s%s", PICO_Path::DIR::RECORDINGS, this->files[i].c_str());
    SceneFunctions::Push(new FileViewerScene(path));
}

void RecorderScene::deleteSelected(){
    if(MicFunctions::IsRecording() || this->confirming || !this->list) return;
    const int i = this->list->getSelectedIndex();
    if(i < 0 || i >= this->file_count) return;
    char msg[96];
    snprintf(msg, sizeof(msg), "%s を削除しますか?", this->files[i].c_str());
    auto* dialog = new MsgDialog(msg, "キャンセル", "削除");
    if(!dialog) return;
    this->confirming = true;
    this->confirming_name = this->files[i];
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_ok){
        this->confirming = false;
        WidgetFunctions::DestroyLater(dialog);
        if(!is_ok) return;
        char path[PICO_PATH_LEN + 1];
        snprintf(path, sizeof(path), "%s%s", PICO_Path::DIR::RECORDINGS, this->confirming_name.c_str());
        if(!OSData::SD.remove(path)) ErrorFunctions::ShowFatal("削除できませんでした");
        this->reloadList();
        this->refreshButtons();
    });
}
