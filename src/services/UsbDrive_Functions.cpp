#include "services/UsbDrive_Functions.hpp"
#include "hal/SD_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "storage/Msc_Sector_Bridge.hpp"
#include "OS_Data.hpp"
#include "system/I18n_Functions.hpp"

#include <Arduino.h>
#include <cstdlib>

void (*UsbDriveFunctions::sd_remounted)() = nullptr;

namespace {
    MscSectorBridge bridge;
    volatile bool active = false;
    volatile bool ejected = false;
    uint32_t capacity_sectors = 0;

    // 診断の数(USBのコールバックの中で増える)
    volatile uint32_t n_inquiry = 0, n_ready = 0, n_capacity = 0, n_other = 0;
    volatile uint8_t last_other = 0;

    bool CardRead(uint32_t lba, uint8_t* dst){
        SdCard* card = OSData::SD.card();
        return card && card->readSector(lba, dst);
    }
    bool CardWrite(uint32_t lba, const uint8_t* src){
        SdCard* card = OSData::SD.card();
        return card && card->writeSector(lba, src);
    }

    // SDをpico-osから切り離す(ログ・WAV)
    void DetachSd(){
        SoundFunctions::WavStop();
        LogFunctions::Suspend();
        OSData::SD_usable = false;
    }

    // SDをpico-osへ戻す
    void AttachSd(){
        if(!PICO_SD::Remount()){
            LOG_SYS_FAIL("USBドライブ: SDをマウントし直せません(抜いて差し直すか、再起動してください)");
            return;
        }
        LogFunctions::Resume();
        //PCでアプリを足した/消したかもしれない
        if(UsbDriveFunctions::sd_remounted) UsbDriveFunctions::sd_remounted();
    }
}

#if defined(PICOOS_PC)
// ===== PC/Web: 本物のUSBは無い。状態だけ真似る =====
namespace {
    bool UsbAttach(){ return true; }
    void UsbDetach(){}
    bool SimHost(){
        const char* v = getenv("PICOOS_USB_DRIVE_HOST");
        return v && v[0] == '1';
    }
    void FillUsbDiag(UsbDriveFunctions::Diag& d){
        d.usb_connected = d.usb_mounted = SimHost();
    }
}
#else
// ===== 実機: arduino-pico のUSBへマスストレージの口を足す(libraries/FatFSUSB と同じ作り) =====
#include <USB.h>
#include <tusb-msc.h>
#include <class/msc/msc.h>
#include <device/usbd.h>
#include <tusb.h>
#include <hardware/irq.h>

namespace {
    uint8_t usb_itf = 0, ep_in = 0, ep_out = 0;
    // registerInterface() は記述子の場所を覚えるので、関数を抜けても残る所に置く
    uint8_t msc_desc[TUD_MSC_DESC_LEN];

    bool UsbAttach(){
        USB.disconnect();
        ep_in = USB.registerEndpointIn();
        ep_out = USB.registerEndpointOut();
        if(ep_in == 0 || ep_out == 0){
            //エンドポイントが尽きた。口を足さずにつなぎ直すだけにする
            USB.connect();
            return false;
        }
        const uint8_t desc[] = { TUD_MSC_DESCRIPTOR(1 /* 番号は差し替えられる */, 0, ep_out, ep_in, CFG_TUD_MSC_EP_BUFSIZE) };
        memcpy(msc_desc, desc, sizeof(msc_desc));
        usb_itf = USB.registerInterface(1, USBClass::simpleInterface, msc_desc, sizeof(msc_desc), 2, 0);
        USB.connect();
        return true;
    }
    void UsbDetach(){
        //disconnect()はUSBの処理(割り込み)の鍵を取るので、ここから先コールバックは来ない
        USB.disconnect();
        USB.unregisterInterface(usb_itf);
        // arduino-pico の unregisterEndpointIn/Out は引数の扱いが逆になっている
        // (In は 1<<ep、Out は 1<<(ep-0x80) で戻す)。素直に渡すと何も戻らず、
        // 始める/終わるを繰り返すたびにエンドポイントが減っていき、いずれ尽きる。
        // FatFSUSB も同じ呼び方なので、ここで合わせて渡す。
        USB.unregisterEndpointOut(ep_out | 0x80);
        USB.unregisterEndpointIn(ep_in & 0x7F);
        USB.connect();
    }
    bool SimHost(){ return false; }
    void FillUsbDiag(UsbDriveFunctions::Diag& d){
        d.usb_connected = tud_connected();
        d.usb_mounted = tud_mounted();
        d.ep_in = ep_in;
        d.ep_out = ep_out;
    }
}

// arduino-pico は tud_task() を1msごとのタイマーでしか回さない。マスストレージは64バイトの受け渡しごとに
// tud_task() が要るので、そのままだと 64バイト/ms ≒ 64KB/秒で頭打ちになる(実機の Linux で dd が 66KB/秒、
// ls が固まったように見えた)。USBの出来事が積まれたら(USBの割り込みの中で呼ばれる)すぐに USB の処理の
// 割り込みを起こして、次の1msを待たずに続きを進める。貸し出している間だけ。
// TinyUSB(libpico の usbd.c)の弱いシンボルを上書きする。
extern "C" void tud_event_hook_cb(uint8_t, uint32_t, bool){
    if(active) irq_set_pending(USB.usbTaskIRQ);
}

extern "C" uint8_t tud_msc_get_maxlun_cb(void){ return 1; }

extern "C" void tud_msc_inquiry_cb(uint8_t, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]){
    n_inquiry = n_inquiry + 1;
    memcpy(vendor_id, "pico-os ", 8);
    memcpy(product_id, "SD Card         ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun){
    n_ready = n_ready + 1;
    if(active && !ejected) return true;
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);   //媒体が無い
    return false;
}

extern "C" void tud_msc_capacity_cb(uint8_t, uint32_t* block_count, uint16_t* block_size){
    n_capacity = n_capacity + 1;
    *block_count = capacity_sectors;
    *block_size = MscSectorBridge::kSectorSize;
}

extern "C" int32_t tud_msc_read10_cb(uint8_t, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize){
    if(!active) return -1;
    return bridge.read(lba, offset, buffer, bufsize);
}

extern "C" bool tud_msc_is_writable_cb(uint8_t){ return true; }

extern "C" int32_t tud_msc_write10_cb(uint8_t, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize){
    if(!active) return -1;
    return bridge.write(lba, offset, buffer, bufsize);
}

extern "C" void tud_msc_write10_complete_cb(uint8_t){ bridge.flush(); }

extern "C" bool tud_msc_start_stop_cb(uint8_t, uint8_t, bool start, bool load_eject){
    if(load_eject){
        if(start){
            ejected = false;
        }else{
            bridge.flush();
            if(SdCard* card = OSData::SD.card()) card->syncDevice();
            ejected = true;
        }
    }
    return true;
}

extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void*, uint16_t){
    n_other = n_other + 1;
    last_other = scsi_cmd[0];
    switch(scsi_cmd[0]){
        case 0x1E:  //PREVENT/ALLOW MEDIUM REMOVAL
            return 0;
        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}
#endif

const char* UsbDriveFunctions::StartErrorToStr(StartError e){
    switch(e){
        case StartError::None:          return TR("成功");
        case StartError::NoSd:          return TR("SDカードが使えません");
        case StartError::AlreadyActive: return TR("すでにUSBドライブになっています");
        case StartError::UsbFailed:     return TR("USBの準備に失敗しました");
    }
    return TR("不明な誤り");
}

UsbDriveFunctions::StartError UsbDriveFunctions::Start(){
    if(active) return StartError::AlreadyActive;
    if(!OSData::SD_usable || !OSData::SD.card()) return StartError::NoSd;

    capacity_sectors = OSData::SD.card()->sectorCount();
    if(capacity_sectors == 0) return StartError::NoSd;

    LOG_SYS_MSG("USBドライブ: 始めます(%lu MiB)。この間pico-osはSDに触りません",
        (unsigned long)(capacity_sectors / 2048));
    DetachSd();
    bridge.reset(&CardRead, &CardWrite, capacity_sectors);
    n_inquiry = n_ready = n_capacity = n_other = 0;
    last_other = 0;
    ejected = false;
    active = true;
    if(!UsbAttach()){
        LOG_SYS_FAIL("USBドライブ: USBのエンドポイントが足りません");
        active = false;
        AttachSd();
        return StartError::UsbFailed;
    }
    return StartError::None;
}

void UsbDriveFunctions::Stop(){
    if(!active) return;
    UsbDetach();
    active = false;
    if(!bridge.flush()) LOG_SYS_WARN("USBドライブ: 最後の書き込みに失敗しました");
    if(SdCard* card = OSData::SD.card()) card->syncDevice();
    LOG_SYS_MSG("USBドライブ: 終わりました(読み %lu / 書き %lu セクタ)",
        (unsigned long)bridge.sectors_read, (unsigned long)bridge.sectors_written);
    AttachSd();
}

UsbDriveFunctions::State UsbDriveFunctions::GetState(){
    if(!active) return State::Off;
    if(ejected) return State::Ejected;
    if(bridge.sectors_read > 0 || bridge.sectors_written > 0 || SimHost()) return State::Mounted;
    return State::Waiting;
}

bool UsbDriveFunctions::IsActive(){ return active; }
uint32_t UsbDriveFunctions::SectorsRead(){ return bridge.sectors_read; }
uint32_t UsbDriveFunctions::SectorsWritten(){ return bridge.sectors_written; }
uint32_t UsbDriveFunctions::CapacityMiB(){ return capacity_sectors / 2048; }

UsbDriveFunctions::Diag UsbDriveFunctions::GetDiag(){
    Diag d;
    FillUsbDiag(d);
    d.msc_open = n_inquiry > 0 || n_ready > 0 || n_capacity > 0;
    d.inquiry = n_inquiry;
    d.ready = n_ready;
    d.capacity = n_capacity;
    d.other = n_other;
    d.last_other = last_other;
    d.errors = bridge.errors;
    return d;
}
