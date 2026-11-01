#pragma once

#include <cstddef>
#include <cstdint>

// 内蔵フラッシュの末尾4KB(arduino-picoのリンカスクリプトが EEPROM 用に取っている _EEPROM_start の1セクタ)を
// 生で読み書きする口。今の利用者は液晶・SDの設定(hal/HwConfig_Functions)だけ。
//
// この領域はファーム本体とも、ファームの更新の置き場(LittleFS。storage/Flash_Area)とも重ならない
// (platformio.ini の「ファームの上限は 4MB - 4KB(EEPROM) - filesystem_size」)。arduino-picoのEEPROMライブラリは
// 使っていない(begin()で4KBまでをヒープへ写すため)。
//
// 実機: 読むのはXIP(Data()はフラッシュをそのまま指す)。書くときは4KBを消してから256バイトずつ書く。
//       その間(数十ms)は割り込みを止めて2コア目を休ませる(storage/Flash_Area と同じ。音が一瞬途切れる)。
// PC:   ファームの更新の置き場と同じディレクトリ(FwStorePc::dir)の eeprom.bin。Webはメモリの中だけ。
namespace EepromArea {
    constexpr size_t kBytes = 4096;
    constexpr size_t kPageBytes = 256;

    // 先頭(kBytes バイト読める)。領域が無ければ nullptr
    const uint8_t* Data();
    // 領域を消して p[0..len) を書く(len は kBytes まで。残りは0xFF)
    bool Write(const uint8_t* p, size_t len);
    // 領域を消すだけ(0xFF)
    bool Erase();
}
