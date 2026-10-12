#pragma once
#include <cstdint>

// USBドライブ(USBマスストレージ)。PCとUSBでつなぐと、SDカードがそのままドライブとして見える。
// SDの抜き差しをせずに、Luaアプリ・曲・ROM・写真を母艦と行き来できる。
//
// ---- 切り替え式 ----
// SDを2者(pico-osとPC)が同時に書くとFATが壊れるので、ドライブの間は pico-os 側はSDに一切触らない:
//   Start(): ログを書き出してファイルを閉じる → WAVを止める → OSData::SD_usable=false →
//            USBにマスストレージの口を足す(つなぎ直しになる)
//   Stop():  USBの口を外す → 溜めた書き込みを書いてSDの書き込みを確定 → SDをマウントし直す
//            (PCが変えたファイルを読み直すため) → ログを開き直す → Luaアプリを走査し直す
// SDを使う処理はどれも SD_usable を見て止まる決まりなので、ほかの画面・サービスは「SDが無い」扱いになる。
// 画面は UsbDriveScene(ランチャの「USBドライブ」)が持ち、画面を離れると Stop() する。
//
// ---- USBシリアルは止めない ----
// arduino-pico のUSBは口を後から足せる(USB.registerInterface)ので、ログと pad_serial.py の
// USBシリアル(CDC)と同時に使える(複合デバイス)。ただし口を足す/外すときにUSBがつなぎ直しになり、
// シリアルが一瞬切れる(pad_serial.py は1秒ごとに開き直すので、そのうち戻る)。
//
// ---- 速さ ----
// USBはフルスピード(12Mbps)、1回の受け渡しは64バイト。SDとの間は MscSectorBridge が1セクタずつ溜める。
// SDの読み書きはUSBの処理(arduino-picoがタイマーの割り込みで回す tud_task)の中で同期に行う
// (arduino-picoの FatFSUSB と同じ作り)。実効は数百KB/秒の見込み(実機で未計測)。
//
// PC/Webビルドは本物のUSBが無いので、口を足さずに状態だけ真似る(PICOOS_USB_DRIVE_HOST=1 で
// 「PCにつながっている」ことにする)。SDを止めてマウントし直す流れは実機と同じに通る。
namespace UsbDriveFunctions {

    enum class State : uint8_t {
        Off,        // 使っていない
        Waiting,    // 口は出したが、PCがまだ読みに来ていない
        Mounted,    // PCが読み書きしている
        Ejected,    // PCで「取り出し」された(Stop()してよい)
    };

    enum class StartError : uint8_t { None, NoSd, AlreadyActive, UsbFailed };

    StartError Start();
    void Stop();

    State GetState();
    bool IsActive();

    // 画面の表示用
    uint32_t SectorsRead();
    uint32_t SectorsWritten();
    uint32_t CapacityMiB();

    const char* StartErrorToStr(StartError e);
}
