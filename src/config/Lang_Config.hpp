#pragma once

// ビルド時に選ぶ表示言語(1=載せる・既定、0=載せない)。ハードウェア機能(Hardware_Config.hpp)と同じ形で、
// platformio.ini の env の build_flags で -DPICO_LANG_EN=0 等を足す(例は env:rpipico2w_ja / env:rpipico2w_en)。
// PCビルドは cmake -DPICOOS_LANG_EN=OFF 等(pc/CMakeLists.txt)。設計は docs/design/i18n.md「ビルド時に選ぶ言語」。
//
// - 日本語だけ(PICO_LANG_EN=0): 英語の訳の表(I18n_Table_En.cpp。約44KB)がフラッシュに入らない
// - 英語だけ(PICO_LANG_JA=0): TR("…") が日本語の原文を参照しなくなり、原文(約48KB)がフラッシュに入らない
//   (訳の表から引くだけになる。訳の無い原文は script/i18n.py が日本語のまま表に入れる)。
//   TR_NOOP() の原文はアプリの登録名等の識別子でもあるので残る。フォント・かな漢字変換は言語と関係なく残る
//
// どちらも載せたとき(既定)は今までどおり設定で切り替えられる。載せた言語が1つのときは設定の選択肢もそれだけになる。
//
// この値を `#if` で読むファイルは、必ずこのヘッダを自分で include すること(script/check_layers.py が見張る)。

#ifndef PICO_LANG_JA
    #define PICO_LANG_JA 1
#endif

#ifndef PICO_LANG_EN
    #define PICO_LANG_EN 1
#endif

static_assert(PICO_LANG_JA == 0 || PICO_LANG_JA == 1, "PICO_LANG_JA は 0 か 1");
static_assert(PICO_LANG_EN == 0 || PICO_LANG_EN == 1, "PICO_LANG_EN は 0 か 1");
static_assert(PICO_LANG_JA || PICO_LANG_EN, "表示言語を1つは載せること(PICO_LANG_JA / PICO_LANG_EN)");
