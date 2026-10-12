#include "functions/UsbDrive_Functions.hpp"
#include "functions/SD_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "storage/Msc_Sector_Bridge.hpp"
#include "lua/LuaAppScanner.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdlib>

namespace {
    MscSectorBridge bridge;
    volatile bool active = false;
    volatile bool ejected = false;
    uint32_t capacity_sectors = 0;

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
        LuaAppScanner::Scan();
        LuaAppScanner::PruneMissing();
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
}
#else
// ===== 実機: arduino-pico のUSBへマスストレージの口を足す(libraries/FatFSUSB と同じ作り) =====
#include <USB.h>
#include <tusb-msc.h>
#include <class/msc/msc.h>
#include <device/usbd.h>

namespace {
    uint8_t usb_itf = 0, ep_in = 0, ep_out = 0;
    // registerInterface() は記述子の場所を覚えるので、関数を抜けても残る所に置く
    uint8_t msc_desc[TUD_MSC_DESC_LEN];

    bool UsbAttach(){
        USB.disconnect();
        ep_in = USB.registerEndpointIn();
        ep_out = USB.registerEndpointOut();
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
        USB.unregisterEndpointOut(ep_out);
        USB.unregisterEndpointIn(ep_in);
        USB.connect();
    }
    bool SimHost(){ return false; }
}

extern "C" uint8_t tud_msc_get_maxlun_cb(void){ return 1; }

extern "C" void tud_msc_inquiry_cb(uint8_t, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]){
    memcpy(vendor_id, "pico-os ", 8);
    memcpy(product_id, "SD Card         ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun){
    if(active && !ejected) return true;
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);   //媒体が無い
    return false;
}

extern "C" void tud_msc_capacity_cb(uint8_t, uint32_t* block_count, uint16_t* block_size){
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
        case StartError::None:          return "成功";
        case StartError::NoSd:          return "SDカードが使えません";
        case StartError::AlreadyActive: return "すでにUSBドライブになっています";
        case StartError::UsbFailed:     return "USBの準備に失敗しました";
    }
    return "不明な誤り";
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
    ejected = false;
    active = true;
    if(!UsbAttach()){
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
