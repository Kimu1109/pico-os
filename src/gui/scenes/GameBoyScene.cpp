#include "gui/scenes/GameBoyScene.hpp"
#include "functions/Power_Functions.hpp"

#include "gui/widgets/apps/GameBoyView.hpp"
#include "gui/widgets/apps/GameBoyPad.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "functions/Battery_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "gb/Gb_PadMap.hpp"
#include "gb/Gb_Rom_Flash.hpp"
#include "storage/Flash_Area.hpp"
#include "functions/Config_Functions.hpp"
#include "sound/Music_Service.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include "Arduino.h"

#include <cstdio>
#include <cstring>

// 1回のonUpdate()で進める最大フレーム数。これより遅れたぶんは捨てる
static constexpr int kMaxFramesPerUpdate = 2;
// Push()から戻ったとき等、間が空きすぎた差分は無かったことにする
static constexpr uint32_t kMaxElapsedMs = 100;
// 速さのログを出す間隔
static constexpr uint32_t kStatIntervalMs = 5000;

GameBoyScene::~GameBoyScene(){
    delete this->flash_job;
}

void GameBoyScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->view = new GameBoyView(content.x, content.y, &this->emu);
    WidgetFunctions::Add(this->view);

    this->pad = new GameBoyPad(content.x, content.y + GameBoyView::kViewH, content.w);
    this->pad->setOnMenu([this](){ this->openMenu(); });
    this->pad->setOnBack([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->pad);

    this->pending = Pending::None;
    this->pending_wait_frames = 0;
    this->crash_reported = false;
    this->acc_us = 0;
    this->last_ms = millis();
    this->banner_until_ms = 0;
    this->was_sleeping = false;
    this->last_low_battery_save_ms = this->last_ms;

    if(this->rom_path.empty()){
        this->view->setMessage("ROMを選んでください");
        this->pending = Pending::OpenPicker;
        this->pending_wait_frames = 1;
    }else{
        //上に載った画面から戻ってきた。離れたときの自動の枠から黙って続ける
        this->resume_silently = true;
        this->loadRom(this->rom_path.c_str());
        this->resume_silently = false;
    }
}

void GameBoyScene::onExit(){
    this->stopFlash();
    this->autoSave("画面を離れる");
    // セーブ(.sav)の書き出しはunload()の中で行われる
    this->emu.unload();

    this->view = nullptr;
    this->pad = nullptr;
    this->picker = nullptr; //ダイアログ本体はシーンの破棄でまとめて消える
    this->menu = nullptr;
    this->pending = Pending::None;
}

void GameBoyScene::autoSave(const char* why){
    if(!this->emu.loaded() || this->emu.crashed()) return;
    const GbEmu::StateError e = this->emu.saveState(GbEmu::kAutoSlot);
    if(e != GbEmu::StateError::None){
        LOG_APP_WARN("GameBoy: 自動の途中保存(%s)に失敗しました: %s", why, GbEmu::stateErrorToStr(e));
    }
    this->emu.writeSave();
}

void GameBoyScene::showBanner(const char* text){
    if(!this->view) return;
    this->view->setBanner(text);
    this->banner_until_ms = millis() + kBannerMs;
    if(this->banner_until_ms == 0) this->banner_until_ms = 1;
}

void GameBoyScene::openMenu(){
    if(this->menu || this->picker || this->flash_job || this->pending != Pending::None) return;

    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Choice, "メニュー", "", "キャンセル");
    if(!d) return;
    // 項目の番号 → すること(出す項目が状況で変わるため)
    enum : uint8_t { kSave, kLoad, kRom };
    uint8_t actions[3];
    int n = 0;
    if(this->emu.loaded() && !this->emu.crashed()){
        d->addChoice("途中保存");
        actions[n++] = kSave;
    }
    if(this->emu.loaded() && this->emu.hasState(GbEmu::kManualSlot)){
        d->addChoice("途中から再開");
        actions[n++] = kLoad;
    }
    d->addChoice("ROMを選ぶ");
    actions[n++] = kRom;
    d->layout();

    this->menu = d;
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClosed([this, d, actions, n](bool is_ok){
        const int i = is_ok ? d->getSelectedIndex() : -1;
        if(i >= 0 && i < n){
            switch(actions[i]){
                case kSave: this->pending = Pending::SaveManual; break;
                case kLoad: this->pending = Pending::LoadManual; break;
                default:    this->pending = Pending::OpenPicker; break;
            }
            this->pending_wait_frames = 1;
        }
        this->menu = nullptr;
        WidgetFunctions::DestroyLater(d);
    });
}

void GameBoyScene::askResume(){
    if(this->menu) return;
    PickerDialog* d = new PickerDialog(PickerDialog::Mode::Choice, "続きから遊びますか", "", "");
    if(!d) return;
    d->addChoice("前回の続きから");
    d->addChoice("最初から");
    d->layout();
    this->menu = d;
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    d->setOnClosed([this, d](bool is_ok){
        if(is_ok && d->getSelectedIndex() == 0){
            this->pending = Pending::ResumeAuto;
            this->pending_wait_frames = 1;
        }
        this->menu = nullptr;
        this->last_ms = millis();   //聞いている間の時間を取り返そうとしない
        WidgetFunctions::DestroyLater(d);
    });
}

void GameBoyScene::openPicker(){
    if(this->picker) return;

    // 前に選んだROMの場所から。まだなら /gb/(無ければSDのルート)
    FixedString<PICO_PATH_LEN> start;
    if(!this->rom_path.empty()) PICO_IO::parent(start, this->rom_path.c_str());
    if(start.empty()){
        if(OSData::SD_usable && OSData::SD.exists(PICO_Path::DIR::GB_ROMS)){
            //末尾の"/"を落とす(FileExplorerはフォルダ名を末尾の要素から取るため)
            start.assign(PICO_Path::DIR::GB_ROMS, strlen(PICO_Path::DIR::GB_ROMS) - 1);
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

void GameBoyScene::loadRom(const char* path){
    // 先に今のROMを片付ける(自動の枠とセーブも書き出される)。読み込みに失敗してもROM無しの状態になる
    if(!this->resume_silently) this->autoSave("ROMを替える");
    this->emu.unload();
    this->crash_reported = false;

    this->stopFlash();

    //音は2コア目の音源チップ(GbApu)で鳴らす
    this->emu.setAudioSink(SoundFunctions::GbAudio());
    const RomMode mode = this->romMode();
    if(mode == RomMode::Always){
        this->startFlash(path);
        return;
    }
    const GbEmu::LoadError err = this->emu.load(path);
    if(mode == RomMode::Auto && DefaultFlashArea()
       && (err == GbEmu::LoadError::TooLarge || err == GbEmu::LoadError::OutOfMemory)){
        // RAMに載らない。内蔵フラッシュへ書いてそこから読む
        LOG_APP_MSG("GameBoy: %s はRAMに載らないので内蔵フラッシュから読みます(%s)", path, GbEmu::loadErrorToStr(err));
        this->startFlash(path);
        return;
    }
    if(err != GbEmu::LoadError::None){
        LOG_APP_FAIL("GameBoy: %s を開けません: %s", path, GbEmu::loadErrorToStr(err));
        this->view->setMessage("ROMを選んでください");
        this->view->invalidate();
        ErrorFunctions::ShowFatal(GbEmu::loadErrorToStr(err));
        return;
    }
    this->afterLoad(path);
}

GameBoyScene::RomMode GameBoyScene::romMode() const {
    RomMode mode = RomMode::Auto;
    if(!OSData::SD_usable || !OSData::SD.exists(PICO_Path::FILE::CFG::SYS_GB_CFG)) return mode;
    PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_GB_CFG, [&](const char* key, const char* value){
        if(strcmp(key, "rom-in-flash") != 0) return;
        if(strcmp(value, "always") == 0) mode = RomMode::Always;
        else if(strcmp(value, "never") == 0) mode = RomMode::Never;
        else mode = RomMode::Auto;
    });
    return mode;
}

bool GameBoyScene::startFlash(const char* path){
    this->stopFlash();
    this->flash_job = new GbRomFlash(DefaultFlashArea());
    if(!this->flash_job){
        ErrorFunctions::ShowFatal("メモリが足りません");
        return false;
    }
    // 消す/書く間は2コア目が止まる(音がぶつぶつ切れる)ので、先に止めておく
    SoundFunctions::WavStop();
    SoundFunctions::MusicStop();
    SoundFunctions::StopAll();
    MusicService::Stop();

    this->flash_path.assign(path);
    this->flash_resume_silently = this->resume_silently;
    this->flash_last_percent = 0;
    const GbRomFlash::State st = this->flash_job->begin(path);
    if(st == GbRomFlash::State::Writing){
        this->view->setMessage("ROMを内蔵フラッシュへ書いています 0%");
        this->view->invalidate();
        return true;
    }
    this->stepFlash(); // Done(同じROMが置いてある)/ Failed をその場で片付ける
    return st == GbRomFlash::State::Done;
}

void GameBoyScene::stepFlash(){
    if(!this->flash_job) return;
    this->flash_job->step(kFlashStepMs);
    const GbRomFlash::State st = this->flash_job->state();
    if(st == GbRomFlash::State::Writing){
        const uint32_t total = this->flash_job->total();
        const uint32_t pct = total ? (uint32_t)((uint64_t)this->flash_job->written() * 100 / total) : 0;
        if(pct != this->flash_last_percent){
            this->flash_last_percent = pct;
            char text[64];
            snprintf(text, sizeof(text), "ROMを内蔵フラッシュへ書いています %lu%%", (unsigned long)pct);
            this->view->setMessage(text);
        }
        return;
    }

    if(st == GbRomFlash::State::Failed){
        FixedString<PICO_STR_LL> msg(this->flash_job->message());
        this->stopFlash();
        LOG_APP_FAIL("GameBoy: %s を開けません: %s", this->flash_path.c_str(), msg.c_str());
        this->view->setMessage("ROMを選んでください");
        this->view->invalidate();
        ErrorFunctions::ShowFatal(msg.c_str());
        return;
    }
    if(st != GbRomFlash::State::Done){ this->stopFlash(); return; }

    // エミュはフラッシュの中のROMを直接読む(このオブジェクトはもう要らない)
    const uint8_t* image = this->flash_job->image();
    const uint32_t size = this->flash_job->imageSize();
    this->stopFlash();
    const GbEmu::LoadError err = this->emu.load(this->flash_path.c_str(), image, size);
    if(err != GbEmu::LoadError::None){
        LOG_APP_FAIL("GameBoy: %s を開けません: %s", this->flash_path.c_str(), GbEmu::loadErrorToStr(err));
        this->view->setMessage("ROMを選んでください");
        this->view->invalidate();
        ErrorFunctions::ShowFatal(GbEmu::loadErrorToStr(err));
        return;
    }
    const bool saved_silently = this->resume_silently;
    this->resume_silently = this->flash_resume_silently;
    FixedString<PICO_PATH_LEN> path(this->flash_path.c_str());
    this->afterLoad(path.c_str());
    this->resume_silently = saved_silently;
    this->last_ms = millis(); //書いていた時間を取り返そうとしない
}

void GameBoyScene::stopFlash(){
    delete this->flash_job;
    this->flash_job = nullptr;
}

void GameBoyScene::afterLoad(const char* path){
    this->rom_path.assign(path);
    this->view->setMessage("");
    this->view->invalidate();
    this->acc_us = 0;
    this->last_ms = millis();
    this->stat_start_ms = this->last_ms;
    this->stat_frames = 0;
    this->stat_dropped = 0;

    if(this->emu.hasState(GbEmu::kAutoSlot)){
        if(this->resume_silently){
            const GbEmu::StateError e = this->emu.loadState(GbEmu::kAutoSlot);
            if(e != GbEmu::StateError::None){
                LOG_APP_WARN("GameBoy: 自動の枠から続けられません: %s", GbEmu::stateErrorToStr(e));
            }
            this->view->invalidate();
        }else{
            this->pending = Pending::AskResume;
            this->pending_wait_frames = 1;
        }
    }
}

void GameBoyScene::onUpdate(){
    //エミュは操作が無くても動き続ける(スリープさせない)
    PowerFunctions::KeepAwake();
    const uint32_t now = millis();
    uint32_t elapsed = now - this->last_ms;
    this->last_ms = now;
    if(elapsed > kMaxElapsedMs) elapsed = kMaxElapsedMs;

    // 内蔵フラッシュへROMを書いている間はエミュを動かさない
    if(this->flash_job){
        this->stepFlash();
        return;
    }

    if(this->pending != Pending::None){
        if(this->pending_wait_frames > 0){
            this->pending_wait_frames--;
            return;
        }
        const Pending p = this->pending;
        this->pending = Pending::None;
        if(p == Pending::OpenPicker){
            this->openPicker();
        }else if(p == Pending::Load){
            this->loadRom(this->pending_path.c_str());
        }else if(p == Pending::AskResume){
            this->askResume();
        }else if(p == Pending::ResumeAuto || p == Pending::LoadManual){
            const int slot = (p == Pending::ResumeAuto) ? GbEmu::kAutoSlot : GbEmu::kManualSlot;
            const GbEmu::StateError e = this->emu.loadState(slot);
            if(e == GbEmu::StateError::None){
                this->view->invalidate();
                this->showBanner(p == Pending::ResumeAuto ? "前回の続きから" : "途中から再開しました");
            }else{
                ErrorFunctions::ShowFatal(GbEmu::stateErrorToStr(e));
            }
            this->last_ms = millis();
        }else if(p == Pending::SaveManual){
            const GbEmu::StateError e = this->emu.saveState(GbEmu::kManualSlot);
            if(e == GbEmu::StateError::None){
                this->showBanner("途中保存しました");
            }else{
                ErrorFunctions::ShowFatal(GbEmu::stateErrorToStr(e));
            }
            this->last_ms = millis();   //書き込みの時間を取り返そうとしない
        }else if(p == Pending::Cancelled && this->emu.loaded()){
            //ROMの選び直しをやめた。今のゲームを続ける
        }else if(!this->emu.loaded()){
            // 最初のROM選択をやめた = アプリを開くのをやめた
            SceneFunctions::Pop();
        }
        return;
    }

    if(this->banner_until_ms != 0 && (int32_t)(now - this->banner_until_ms) >= 0){
        this->banner_until_ms = 0;
        this->view->setBanner(nullptr);
    }

    // 電源メニューでスリープへ入った: そのまま電池が切れても続きから遊べるよう、入った時点で書く
    const bool sleeping = PowerFunctions::IsSleeping();
    if(sleeping && !this->was_sleeping) this->autoSave("スリープ");
    this->was_sleeping = sleeping;
    if(sleeping) return;

    // ROM選択中・メニューを開いている間は止めておく
    if(this->picker || this->menu || !this->emu.loaded()) return;

    if(this->emu.crashed()){
        if(!this->crash_reported){
            this->crash_reported = true;
            FixedString<PICO_STR_L> msg("エミュが停止しました: ");
            msg.append(this->emu.crashMessage());
            ErrorFunctions::ShowFatal(msg.c_str());
        }
        return;
    }

    // 外部コントローラーのHOMEは「戻る」(画面の「戻る」と同じ。セーブはonExit()で書かれる)
    if(PadFunctions::Pressed(PadFunctions::Home)){
        SceneFunctions::Pop();
        return;
    }
    // ZLはメニュー(ゲームのボタンには割り当てていない)
    if(PadFunctions::Pressed(PadFunctions::ZL)){
        this->openMenu();
        return;
    }

    // 画面の操作パッドと外部コントローラーを重ねる。
    // タッチは1点しか取れないので、同時押し(十字キー+A等)は外部コントローラーでしかできない
    this->emu.setButtons(this->pad->getPressed() | GbPadMap::ToGb(PadFunctions::Buttons()));

    this->acc_us += elapsed * 1000u;
    int frames = 0;
    while(this->acc_us >= GbEmu::kFrameUs && frames < kMaxFramesPerUpdate){
        this->emu.runFrame();
        this->acc_us -= GbEmu::kFrameUs;
        frames++;
    }
    if(this->acc_us >= GbEmu::kFrameUs){
        //追いつけないぶんは捨てる
        this->stat_dropped += this->acc_us / GbEmu::kFrameUs;
        this->acc_us = 0;
    }

    if(frames > 0) this->view->onFrame();

    // ゲームの中のセーブ(.sav)は、書き込みが落ち着いたら書き出す
    if(this->emu.flushSaveIfIdle(now)) this->last_ms = millis();

    // 電池が少ない間は、いつ切れてもよいように自動の枠へときどき書く
    if(BatteryFunctions::HasSample() && !BatteryFunctions::IsExternallyPowered()
        && BatteryFunctions::GetPercent() <= kLowBatteryPercent
        && now - this->last_low_battery_save_ms >= kLowBatteryAutoSaveMs){
        this->last_low_battery_save_ms = now;
        this->autoSave("電池が少ない");
        this->last_ms = millis();
    }

    this->stat_frames += (uint32_t)frames;
    if(now - this->stat_start_ms >= kStatIntervalMs){
        LOG_APP_DEBUG("GameBoy: %lums で %luフレーム実行 / %luフレーム捨てた(実機は59.7フレーム/秒が目標)",
            (unsigned long)(now - this->stat_start_ms),
            (unsigned long)this->stat_frames, (unsigned long)this->stat_dropped);
        this->stat_start_ms = now;
        this->stat_frames = 0;
        this->stat_dropped = 0;
    }
}
