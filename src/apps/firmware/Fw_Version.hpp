#pragma once

#include <cstdint>

// 今動いているファームの版。実機ビルドでは script/pio_firmware.py がビルドのたびに
// $BUILD_DIR/pico_build_info/Pico_Build_Info.h を書き(このファイルの.cppだけが読む)、
// 通し番号(git rev-list --count HEAD)・短いコミットのハッシュ(手元の変更があれば末尾に'+')・
// ビルドした日時・ビルドの構成の名前(platformio.ini の env。"rpipico2w" / "rpipico2w_lite" 等)が入る。
// PC/Webビルド・ホストテストは通し番号0・"dev"・構成は""。
//
// 同じ値を Marker() の文字列としてファームの中にも置き、script/firmware_tool.py が
// firmware.bin から探して更新ファイルの見出しへ写す(見出しと中身の版が必ず一致するように)。
namespace FwVersion {
    uint32_t Serial();
    const char* Git();
    const char* Date();   // "2026-10-16 12:34"(UTC)。分からなければ""
    // ビルドの構成(platformio.ini の env の名前)。Webの版の一覧から、同じ構成のものだけを選ぶのに使う。分からなければ""
    const char* Env();
    // "#123 (abcdef1)" のような表示用
    const char* Text();
    // "PICOOS-BUILD;serial=...;git=...;date=...;board=rpipico2w;env=...;"(env= は2026-10-29から)
    const char* Marker();
}
