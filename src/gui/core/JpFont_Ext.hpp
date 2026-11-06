#pragma once
#include "gui/core/ExtGlyphStore.hpp"
#include "gui/fonts/JpFont_Data.hpp"

// SDに置く追加の漢字フォント(/sys/fonts/jpgothic16.pkf・jpgothic24.pkf)のつなぎ。docs/design/performance.md「漢字の一部をSDへ」。
//
// 日本語フォント(GlyphIndexFont の16px・24px)は、フラッシュに無い漢字(常用漢字の外)をここの ExtGlyphStore から引く。
// SDは SdFat の FsFile を開いたままにして読む。OSData::SD_usable が false の間(SDが無い・USBドライブ中)は読まず、
// OSData::SD_mount_gen が変わったら(マウントし直した)開き直す。
// ファイルが無い・合わないときは常用漢字の外の字が □ になるだけで、ほかは動く
// (**OSの画面の文字は常用漢字までで書く**。script/font/check_ui_chars.py)。
//
// 控えのRAM: 16px(本文)は128字ぶん × 56B、24px(見出し・部品)は32字ぶん × 114B、ほかに索引が1つ768B。
namespace JpFontExt {
    constexpr int kSlots16 = 128;
    constexpr int kSlots24 = 32;

    extern ExtGlyphStore store16;
    extern ExtGlyphStore store24;

    // px(16/24)の情報(置き場所・大きさ・SHA-256・配布元の名前)
    const JpFontData::Ext& Info(int px);
    ExtGlyphStore& Store(int px);

    // 両方を、次に字を引くときに開き直させる(Webから落として置いた後)
    void Reload();

    // 両方とも使えるか(設定の画面の表示用。まだ開いていなければ開いてみる)
    bool Ready();

    // 状態の短い説明(設定の画面の表示用)
    const char* StateText(ExtGlyphStore::State s);
}
