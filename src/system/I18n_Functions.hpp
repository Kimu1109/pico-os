#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "config/Lang_Config.hpp"   // PICO_LANG_EN / PICO_LANG_JA(ビルド時に選ぶ言語)

// 表示言語(i18n)。設計は docs/design/i18n.md。
//
// **言語はビルド時に1つだけ選ぶ**(config/Lang_Config.hpp。既定は日本語、-DPICO_LANG_EN=1 で英語)。実行時には切り替えない。
// **日本語の原文そのものをキーにする**(gettext と同じ考え方)。画面に出す文字列を TR() で包むだけ。
//
//   label->setText(TR("設定"));
//   snprintf(buf, sizeof(buf), TR("バックグラウンド: %d件"), n);   // 書式の %d も訳の側で同じ並びに保つ
//
// - 日本語の版: TR() は原文そのもの(表もハッシュの引き当ても無い)
// - 英語の版: キーは原文の FNV-1a(32bit。コンパイル時に数える)で、実行時は二分探索1回だけ。
//   原文のリテラルは参照しないのでフラッシュに入らない。訳の無い原文は表に日本語のまま入る
// - 訳は i18n/en.po(人が書く)から script/codegen/i18n.py が I18n_Table_En.cpp(生成物)を作る。
//   TR()/TR_NOOP() を足したら `python3 script/codegen/i18n.py update`(CIの `--check` が食い違いを見張る)
// - 返す文字列はフラッシュに置いた定数。返したポインタはずっと有効(コピーせず持ってよい)
namespace I18nFunctions {
    enum class Lang : uint8_t { Ja = 0, En, Count };

    // このビルドの言語
    constexpr Lang kLang = PICO_LANG_EN ? Lang::En : Lang::Ja;
    constexpr Lang Current(){ return kLang; }

    // 原文のハッシュ(FNV-1a 32bit。UTF-8のバイト列のまま)。script/codegen/i18n.py と同じ計算
    constexpr uint32_t Hash(const char* s){
        uint32_t h = 2166136261u;
        for(; *s; ++s){
            h ^= (uint8_t)*s;
            h *= 16777619u;
        }
        return h;
    }

    // 訳の表の1行(生成物。hash の昇順に並ぶ)
    struct Entry {
        uint32_t    hash;
        const char* text;
    };

    // "ja" / "en"(Lua の pico.lang()・app.cfg の name_<言語>・ストアの訳の列に使う)
    const char* Code(Lang lang);

#if PICO_LANG_EN
    // TR() の中身。表から引くだけで原文を見ない。表に無いハッシュ(表が古い。CIの --check が落とす)は ""
    const char* LookupHash(uint32_t hash);
    // 原文は -Wformat に書式を見せるためだけに受け取り、使わない。必ずインライン化させて、
    // 呼び出し側に原文のリテラルが残らない(フラッシュに入らない)ようにする
    __attribute__((always_inline, format_arg(2)))
    inline const char* LookupNoSrc(uint32_t hash, const char*){ return LookupHash(hash); }

    // 実行時にしか決まらない原文の訳(アプリの登録名・TR_NOOP() で並べた表の項目)。表に無ければ src のまま。
    // 毎回ハッシュを数えるので、描画のたびに長い文字列へ呼ぶのは避ける
    const char* Tr(const char* src) __attribute__((format_arg(1)));

    // 生成物(I18n_Table_En.cpp)
    extern const Entry kTableEn[];
    extern const size_t kTableEnCount;
#else
    // TR() の中身。原文をそのまま返す(hash は TR() の引数がリテラルか確かめるために数えるだけ)
    __attribute__((always_inline, format_arg(2)))
    inline const char* Src(uint32_t, const char* src){ return src; }

    __attribute__((format_arg(1)))
    inline const char* Tr(const char* src){ return src; }
#endif
}

// 画面に出す日本語の文字列を包む。引数は文字列リテラルだけ(script/codegen/i18n.py が拾う。日本語の版でもハッシュを数えて確かめる)
#if PICO_LANG_EN
#define TR(s) (::I18nFunctions::LookupNoSrc(::std::integral_constant<uint32_t, ::I18nFunctions::Hash(s)>::value, (s)))
#else
#define TR(s) (::I18nFunctions::Src(::std::integral_constant<uint32_t, ::I18nFunctions::Hash(s)>::value, (s)))
#endif
// 表に並べておき、使う所で I18nFunctions::Tr() する文字列の印(script/codegen/i18n.py が拾うだけで、値は原文のまま)
#define TR_NOOP(s) (s)
