#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "config/Lang_Config.hpp"   // PICO_LANG_JA / PICO_LANG_EN(ビルド時に選ぶ言語)

// 表示言語の切り替え(i18n)。設計は docs/design/i18n.md。
//
// **日本語の原文そのものをキーにする**(gettext と同じ考え方)。画面に出す文字列を TR() で包むだけで、
// 訳が無い言語・訳し漏れの文字列は日本語のまま出る。
//
//   label->setText(TR("設定"));
//   snprintf(buf, sizeof(buf), TR("バックグラウンド: %d件"), n);   // 書式の %d も訳の側で同じ並びに保つ
//
// - キーは原文の FNV-1a(32bit)。TR() はコンパイル時に計算するので、実行時は二分探索1回だけ
//   (日本語のときは表を見ずに原文を返す)
// - 訳は i18n/en.po(人が書く)から script/i18n.py が I18n_Table_En.cpp(生成物)を作る。
//   TR()/TR_NOOP() を足したら `python3 script/i18n.py update`(CIの `--check` が食い違いを見張る)
// - 訳の文字列はフラッシュに置いた定数。返したポインタはずっと有効(コピーせず持ってよい)
// - 文字列は作ったときの言語で決まる。切り替えた後は画面を作り直すまで前の言語のまま(SetLanguage() 参照)
// - 載せる言語はビルド時に選べる(config/Lang_Config.hpp)。英語だけの版の TR() は原文を参照しない
namespace I18nFunctions {
    // 値は言語を外した版でも変えない(AppEntry::local_names の添え字等に使う)
    enum class Lang : uint8_t { Ja = 0, En, Count };

    // このビルドに載せた言語か
    constexpr bool Available(Lang lang){
        return (lang == Lang::Ja && PICO_LANG_JA) || (lang == Lang::En && PICO_LANG_EN);
    }
    // 載せた言語の数(1 なら切り替えられない)
    constexpr int kAvailableCount = PICO_LANG_JA + PICO_LANG_EN;
    // 何も選ばれていないとき・載せていない言語を選ばれたときの言語
    constexpr Lang kDefaultLang = PICO_LANG_JA ? Lang::Ja : Lang::En;

    // 原文のハッシュ(FNV-1a 32bit。UTF-8のバイト列のまま)。script/i18n.py と同じ計算
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

    // 原文 src(そのハッシュが hash)の、今の言語での文字列。訳が無ければ src。
    // format_arg: TR() の結果を printf 系へ渡したとき、原文の書式で -Wformat が引数を確かめる
    const char* Lookup(uint32_t hash, const char* src) __attribute__((format_arg(2)));

#if !PICO_LANG_JA
    // 英語だけの版の TR()。表から引くだけで原文を見ない(訳の無い原文も script/i18n.py が表に入れてある)。
    // 表に無いハッシュ(表が古い。CIの --check が落とす)は ""
    const char* LookupHash(uint32_t hash);
    // 原文は -Wformat に書式を見せるためだけに受け取り、使わない。必ずインライン化させて、
    // 呼び出し側に原文のリテラルが残らない(フラッシュに入らない)ようにする
    __attribute__((always_inline, format_arg(2)))
    inline const char* LookupNoSrc(uint32_t hash, const char*){ return LookupHash(hash); }
#endif

    // 実行時にしか決まらない原文の訳(アプリの登録名・TR_NOOP() で並べた表の項目)。
    // 毎回ハッシュを数えるので、描画のたびに長い文字列へ呼ぶのは避ける
    const char* Tr(const char* src) __attribute__((format_arg(1)));

    // /sys/user.cfg の language を読む。最初のシーン・ステータスバーを作るより前に呼ぶ
    void Setup();

    Lang Current();
    // 切り替える。save なら user.cfg へ書く。
    // 出ている画面の文字列は変わらないので、呼び出し側が SceneFunctions::Reload() で作り直す
    // (常駐のウィジェット(キーボード等)は再起動まで前の言語)
    void SetLanguage(Lang lang, bool save);

    // "ja" / "en"(user.cfg・PCの --lang / Webの ?lang= に書く値)
    const char* Code(Lang lang);
    // "ja" "en" "en-US" のような値から。知らない値なら false
    bool FromCode(const char* code, Lang& out);
    // 言語の名前をその言語で("日本語" / "English")。言語の選択肢に出す
    const char* NativeName(Lang lang);

    // 今の言語(Current() で読む。書き換えは SetLanguage()/Setup() から)
    inline Lang current_lang = kDefaultLang;

    // PCビルドが setup() より前に差す値。override_code(--lang / ?lang=)は user.cfg より優先し、
    // default_code(Webではブラウザの言語)は user.cfg に language が無いときだけ使う。どちらも保存はしない
    inline const char* override_code = nullptr;
    inline const char* default_code = nullptr;

#if PICO_LANG_EN
    // 生成物(I18n_Table_En.cpp)
    extern const Entry kTableEn[];
    extern const size_t kTableEnCount;
#endif
}

// 画面に出す日本語の文字列を包む。引数は文字列リテラルだけ(script/i18n.py が拾う)
#if PICO_LANG_JA
#define TR(s) (::I18nFunctions::Lookup(::std::integral_constant<uint32_t, ::I18nFunctions::Hash(s)>::value, (s)))
#else
#define TR(s) (::I18nFunctions::LookupNoSrc(::std::integral_constant<uint32_t, ::I18nFunctions::Hash(s)>::value, (s)))
#endif
// 表に並べておき、使う所で I18nFunctions::Tr() する文字列の印(script/i18n.py が拾うだけで、値は原文のまま)
#define TR_NOOP(s) (s)
