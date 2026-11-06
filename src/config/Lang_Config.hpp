#pragma once

// 表示言語はビルド時に1つだけ選ぶ(実行時には切り替えない)。設計は docs/design/i18n.md「ビルド時に選ぶ言語」。
//
//   PICO_LANG_EN=0(既定): 日本語の版。TR("…") は原文そのもの(表を引かない)。英語の訳の表はフラッシュに入らない
//   PICO_LANG_EN=1        : 英語の版。TR("…") は日本語の原文を参照せず訳の表から引く(原文はフラッシュに入らない)。
//                           訳の無い原文は script/codegen/i18n.py が日本語のまま表に入れる
//
// buildcfg/ の版の build_flags で -DPICO_LANG_EN=1 を足す(部品は buildcfg/options.ini の opt_lang_en、例は env:rpipico2w_en)。
// PCビルドは cmake -DPICOOS_LANG_EN=ON(pc/CMakeLists.txt)。
// TR_NOOP() の原文(アプリの登録名等の識別子)・フォント・かな漢字変換は言語と関係なく残る。
//
// PICO_LANG_JA はここで決める(外から渡さない)。どちらかが必ず1。
// この値を `#if` で読むファイルは、必ずこのヘッダを自分で include すること(script/check/check_layers.py が見張る)。

#ifdef PICO_LANG_JA
    #error "PICO_LANG_JA は外から渡さない。英語の版は -DPICO_LANG_EN=1"
#endif

#ifndef PICO_LANG_EN
    #define PICO_LANG_EN 0
#endif

static_assert(PICO_LANG_EN == 0 || PICO_LANG_EN == 1, "PICO_LANG_EN は 0 か 1");

#if PICO_LANG_EN
    #define PICO_LANG_JA 0
#else
    #define PICO_LANG_JA 1
#endif
