#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// 内蔵フラッシュのLittleFSの領域(platformio.ini の board_build.filesystem_size)を、
// ファイルシステムを通さず生で読み書きする口。今の利用者はゲームボーイのROMの置き場(gb/Gb_Rom_Flash)。
//
// この領域は本来ファームの更新(firmware/Fw_Store)の置き場だが、そちらは更新の間だけ使い、起動時に
// 片付けて空にする。ゲームボーイのROMはそこを間借りする:
//   - 生のデータを置いている間は、領域の先頭4バイトを kRawMagic にする(holdsRawData())。
//     FirmwareFunctions::Setup() はこれを見たらLittleFSをマウントしない(マウントに失敗すると
//     フォーマットされ、置いたROMが消えるため)
//   - ファームの更新を始めるとLittleFSがフォーマットされてROMは消える。次に遊ぶときSDから書き直す
//   - arduino-picoのOTAのローダーは起動のたびにLittleFSを読み取りだけでマウントし、失敗すれば
//     何もせず本体を起動する(framework-arduinopico/ota/ota.c の has_ota())。生のデータは無害
//
// 実機: 読むのはXIP(data()はフラッシュをそのまま指す)。消す/書く間はフラッシュを読めないので、
//       LittleFSと同じく割り込みを止めて2コア目を休ませる(rp2040.idleOtherCore())。
//       その間は2コア目の音も止まるので、呼ぶ側は先に音を止めておくこと。
//       64KBの消去は1回で百数十ms〜(最悪1秒程度)割り込みが止まる。
// PC:   ファームの更新の置き場と同じディレクトリ(FwStorePc::dir)の gb_flash.bin を mmap して真似る。
//       消すと0xFF、書くとNORフラッシュと同じく今の値とのANDになる(消さずに書くバグが見える)。
// Web:  メモリの中だけ(再読み込みで消える)。
class FlashArea {
    public:
        constexpr static uint32_t kSectorBytes = 4096;
        constexpr static uint32_t kBlockBytes = 65536;
        constexpr static uint32_t kPageBytes = 256;
        // 生のデータを置いている印(LittleFSのスーパーブロックは先頭が版の番号なのでぶつからない)
        constexpr static char kRawMagic[4] = { 'P', 'R', 'A', 'W' };

        virtual ~FlashArea() = default;

        // 領域の大きさ(4KBの倍数)
        virtual uint32_t size() const = 0;
        // 領域の先頭のフラッシュの番地(64KBの消去が使えるかを決めるため)
        virtual uint32_t baseAddress() const = 0;
        // 読み出し。実機はXIPの番地そのもの
        virtual const uint8_t* data() const = 0;
        // off/len とも kSectorBytes の倍数。64KB境界に揃った64KBは1回のブロック消去になる
        virtual bool erase(uint32_t off, uint32_t len) = 0;
        // off/len とも kPageBytes の倍数。消してある所へだけ書く
        virtual bool program(uint32_t off, const uint8_t* p, uint32_t len) = 0;

        bool holdsRawData() const {
            return this->size() >= kSectorBytes && memcmp(this->data(), kRawMagic, sizeof(kRawMagic)) == 0;
        }
};

// 実機/PCの領域(Flash_Area.cpp)。領域が無ければnullptr
FlashArea* DefaultFlashArea();
