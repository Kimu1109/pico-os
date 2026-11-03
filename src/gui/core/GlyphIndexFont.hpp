#pragma once
#include "LovyanGFX.hpp"

// 日本語フォント(lgfxJapanGothicP_16/24)を、字の探し方だけ速くして使う(U8g2GlyphIndex.hpp)。
// docs/design/performance.md「日本語フォントの字の探し方」。
//
// フォントのデータは LovyanGFX のものをそのまま使い、U8g2font を継承して仮想関数の
// updateFontMetric()/drawChar() だけを差し替える(LovyanGFX 自体には手を入れない)。
// 中身は LovyanGFX 1.2.28 の同じ関数の写しで、違うのは getGlyph() を索引で引くところだけ。
// **LovyanGFX の版を上げたら、lgfx_fonts.cpp の U8g2font::drawChar()/updateFontMetric() と見比べること。**
//
// 画面の文字は FontFn::GetSmall()/GetNormal()(=ここの2つ)を通して選ぶ。&lgfxJapanGothicP_* を直接
// setFont() しても描けるが、遅い元の探し方になる。
//
// ホストテストのスタブ(stubs/LovyanGFX.h)には IFont が無いので、そのときは元のフォントの名前を返すだけ。

#if defined(PICOOS_LGFX_STUB)

namespace GlyphIndexFonts {
    inline const lgfx::v1::U8g2font* Small()  { return &lgfxJapanGothicP_16; }
    inline const lgfx::v1::U8g2font* Normal() { return &lgfxJapanGothicP_24; }
}

#else

#include "gui/core/U8g2GlyphIndex.hpp"

class GlyphIndexFont : public lgfx::v1::U8g2font {
public:
    explicit constexpr GlyphIndexFont(const uint8_t* u8g2_font) : lgfx::v1::U8g2font(u8g2_font), index_(u8g2_font) {}

    bool updateFontMetric(lgfx::v1::FontMetrics* metrics, uint16_t uniCode) const override;
    size_t drawChar(lgfx::v1::LGFXBase* gfx, int32_t x, int32_t y, uint16_t c,
                    const lgfx::v1::TextStyle* style, lgfx::v1::FontMetrics* metrics, int32_t& filled_x) const override;

    // 字のデータの位置(U8g2font::getGlyph() と同じ値)
    const uint8_t* glyph(uint16_t encoding) const { return index_.find(encoding); }
    U8g2GlyphIndex& index() const { return index_; }

private:
    // 描画は const のまま呼ばれるので、控え・索引は mutable
    mutable U8g2GlyphIndex index_;
};

namespace GlyphIndexFonts {
    const GlyphIndexFont* Small();   // 16px
    const GlyphIndexFont* Normal();  // 24px
}

#endif
