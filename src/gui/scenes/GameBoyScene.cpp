#include "gui/scenes/GameBoyScene.hpp"

#include "gui/widgets/apps/GameBoyView.hpp"
#include "gui/widgets/apps/GameBoyPad.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "gb/Gb_PadMap.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include "Arduino.h"

#include <cstring>

// 1回のonUpdate()で進める最大フレーム数。これより遅れたぶんは捨てる
static constexpr int kMaxFramesPerUpdate = 2;
// Push()から戻ったとき等、間が空きすぎた差分は無かったことにする
static constexpr uint32_t kMaxElapsedMs = 100;
// 速さのログを出す間隔
static constexpr uint32_t kStatIntervalMs = 5000;

void GameBoyScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->view = new GameBoyView(content.x, content.y, &this->emu);
    WidgetFunctions::Add(this->view);

    this->pad = new GameBoyPad(content.x, content.y + GameBoyView::kViewH, content.w);
    this->pad->setOnRom([this](){ this->openPicker(); });
    this->pad->setOnBack([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->pad);

    this->pending = Pending::None;
    this->pending_wait_frames = 0;
    this->crash_reported = false;
    this->acc_us = 0;
    this->last_ms = millis();

    if(this->rom_path.empty()){
        this->view->setMessage("ROMを選んでください");
        this->pending = Pending::OpenPicker;
        this->pending_wait_frames = 1;
    }else{
        this->loadRom(this->rom_path.c_str());
    }
}

void GameBoyScene::onExit(){
    // セーブの書き出しもunload()の中で行われる
    this->emu.unload();

    this->view = nullptr;
    this->pad = nullptr;
    this->picker = nullptr; //ダイアログ本体はシーンの破棄でまとめて消える
    this->pending = Pending::None;
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
    // 先に今のROMを片付ける(セーブも書き出される)。読み込みに失敗してもROM無しの状態になる
    this->emu.unload();
    this->crash_reported = false;

    const GbEmu::LoadError err = this->emu.load(path);
    if(err != GbEmu::LoadError::None){
        LOG_APP_FAIL("GameBoy: %s を開けません: %s", path, GbEmu::loadErrorToStr(err));
        this->view->setMessage("ROMを選んでください");
        this->view->invalidate();
        ErrorFunctions::ShowFatal(GbEmu::loadErrorToStr(err));
        return;
    }

    this->rom_path.assign(path);
    this->view->setMessage("");
    this->view->invalidate();
    this->acc_us = 0;
    this->last_ms = millis();
    this->stat_start_ms = this->last_ms;
    this->stat_frames = 0;
    this->stat_dropped = 0;
}

void GameBoyScene::onUpdate(){
    const uint32_t now = millis();
    uint32_t elapsed = now - this->last_ms;
    this->last_ms = now;
    if(elapsed > kMaxElapsedMs) elapsed = kMaxElapsedMs;

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
        }else if(!this->emu.loaded()){
            // 最初のROM選択をやめた = アプリを開くのをやめた
            SceneFunctions::Pop();
        }
        return;
    }

    // ROM選択中は止めておく
    if(this->picker || !this->emu.loaded()) return;

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
