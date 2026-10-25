#pragma once

// ビルド時に選べるハードウェア機能(1=載せる・既定、0=載せない)。
// platformio.ini の env の build_flags で -DPICO_HW_SOUND=0 等を足す(例は env:rpipico2w_minimal)。
// PCビルドは cmake -DPICOOS_HW_SOUND=OFF 等(pc/CMakeLists.txt)。
//
// 載せない機能は、実装(hal/Xxx_Functions.cpp)の代わりに空の実装(hal/Xxx_Functions_Off.cpp)が入る。
// 呼び出しの形は変わらず「つながっていない」と答えるだけなので、呼び出し側は分岐しなくてよい。
// 本体のRAM(I2Sの作業場所・音源の状態・コマンドの列)とフラッシュ(音源・WAV・MMLの読み取り等)が丸ごと外れ、
// 音声もマイクも載せないときは2コア目も起こさない(そのスタック8KBも要らない。main.cpp の setup1/loop1)。
// 利用者に見える所(アプリの一覧・設定の音タブ・ステータスバー)も、載せない機能は出さない。
//
// **液晶・タッチ・SDは選べない**(無いとOSとして動かない。画面の種類と大きさは PICO_PANEL_* / SCREEN_WIDTH で選ぶ)。
//
// この値を `#if` で読むファイルは、必ずこのヘッダを自分で include すること
// (未定義のマクロは `#if` で黙って0になり、機能が消えたことに気づけない。script/check_layers.py が見張る)。

// 音声出力(I2S: MAX98357A / PCM5102A)と、その上の効果音・曲(MML)・WAV・ゲームボーイの音。
// 0 にすると、アプリの一覧から「ミュージック」を外し、設定の音タブは「音声出力なし」の表示だけになる
#ifndef PICO_HW_SOUND
    #define PICO_HW_SOUND 1
#endif

// マイク(I2S: SPH0645)と録音。0 にすると、アプリの一覧から「録音」を外す
#ifndef PICO_HW_MIC
    #define PICO_HW_MIC 1
#endif

// 電池(VSYSの電圧で残量を見る。PiCoLiPoSHIM等)。0 にすると、常にUSB給電として扱い
// (電池での音量の頭打ちも掛からない)、ステータスバーと設定に残量を出さない
#ifndef PICO_HW_BATTERY
    #define PICO_HW_BATTERY 1
#endif

static_assert(PICO_HW_SOUND == 0 || PICO_HW_SOUND == 1, "PICO_HW_SOUND は 0 か 1");
static_assert(PICO_HW_MIC == 0 || PICO_HW_MIC == 1, "PICO_HW_MIC は 0 か 1");
static_assert(PICO_HW_BATTERY == 0 || PICO_HW_BATTERY == 1, "PICO_HW_BATTERY は 0 か 1");

// 2コア目(音声専用)を使う機能が1つでもあるか
#define PICO_HW_CORE1 (PICO_HW_SOUND || PICO_HW_MIC)

// 実行時に読む口(`if constexpr` や Lua の pico.system へ渡す用)
namespace HardwareConfig {
    constexpr bool kSound   = PICO_HW_SOUND != 0;
    constexpr bool kMic     = PICO_HW_MIC != 0;
    constexpr bool kBattery = PICO_HW_BATTERY != 0;
}
