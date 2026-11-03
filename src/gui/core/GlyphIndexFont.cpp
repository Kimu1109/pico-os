#include "gui/core/GlyphIndexFont.hpp"

#if !defined(PICOOS_LGFX_STUB)

#include <algorithm>
#include "OS_Data.hpp"
#include "config/Ram_Func.hpp"
#include <lgfx/Fonts/IPA/lgfx_font_japan.h>

// updateFontMetric()/drawChar() は LovyanGFX 1.2.28 の lgfx_fonts.cpp(U8g2font)の写し
// (FreeBSDライセンス、著作権者 lovyan03。THIRD_PARTY_LICENSES.md)。違うのは getGlyph() を glyph() にしたところだけ。
// 字を描く道(drawChar とその下請け)は実機ではRAMに置く(config/Ram_Func.hpp)。字のデータ(フラッシュ)を読みながら回るので、
// フラッシュに置くとその読みで自分のコードがXIPのキャッシュから追い出される

namespace {
    // lgfx_fonts.cpp の u8g2_font_decode_t の写し(あちらは .cpp の中にあって外から使えない)
    struct U8g2Decode {
        explicit U8g2Decode(const uint8_t* ptr) : decode_ptr(ptr), decode_bit_pos(0) {}

        const uint8_t* decode_ptr;
        uint8_t decode_bit_pos;

        PICO_RAM_FUNC uint_fast8_t get_unsigned_bits(uint_fast8_t cnt) {
            uint_fast8_t bit_pos = this->decode_bit_pos;
            uint_fast8_t val = (*this->decode_ptr) >> bit_pos;
            auto bit_pos_plus_cnt = bit_pos + cnt;
            if (bit_pos_plus_cnt >= 8) {
                bit_pos_plus_cnt -= 8;
                val |= (*(++this->decode_ptr)) << (8 - bit_pos);
            }
            this->decode_bit_pos = bit_pos_plus_cnt;
            return val & ((1U << cnt) - 1);
        }

        PICO_RAM_FUNC int_fast8_t get_signed_bits(uint_fast8_t cnt) {
            return (int_fast8_t)get_unsigned_bits(cnt) - (1 << (cnt - 1));
        }
    };

    GlyphIndexFont font16(lgfx_font_japan_gothic_p_16);
    GlyphIndexFont font24(lgfx_font_japan_gothic_p_24);

    // 4bppの1行の [x0, x1) を色 nib(0〜15)で塗る。偶数のxが上位4ビット(LovyanGFXの4bppのスプライトと同じ並び)
    // no-tree-loop-distribute-patterns: 下のループをコンパイラが memset の呼び出しへ戻さないように
    PICO_RAM_FUNC __attribute__((optimize("no-tree-loop-distribute-patterns")))
    void FillNibbles(uint8_t* row, int32_t x0, int32_t x1, uint8_t nib) {
        if (x0 & 1) {
            uint8_t& b = row[x0 >> 1];
            b = (uint8_t)((b & 0xF0) | nib);
            x0++;
        }
        // 字の線は数バイトと短いので memset は呼ばない(実機の memset はフラッシュにあり、RAMに置いたこの道から毎回出ていくことになる)
        const int32_t pairs = (x1 - x0) >> 1;
        if (pairs > 0) {
            uint8_t* p = row + (x0 >> 1);
            const uint8_t both = (uint8_t)(nib * 0x11);
            for (int32_t i = 0; i < pairs; i++) p[i] = both;
            x0 += pairs * 2;
        }
        if (x0 < x1) {
            uint8_t& b = row[x0 >> 1];
            b = (uint8_t)((b & 0x0F) | (nib << 4));
        }
    }

    // 画面の合成先(OSData::frame。4bpp・回転なし)へ直接描けるか
    PICO_RAM_FUNC LGFX_Sprite* DirectTarget(lgfx::v1::LGFXBase* gfx) {
        LGFX_Sprite* f = OSData::frame;
        if (!f || static_cast<lgfx::v1::LGFXBase*>(f) != gfx) return nullptr;
        if (f->getColorDepth() != lgfx::v1::palette_4bit || f->getRotation() != 0) return nullptr;
        if (!f->getBuffer()) return nullptr;
        return f;
    }
}

const GlyphIndexFont* GlyphIndexFonts::Small()  { return &font16; }
const GlyphIndexFont* GlyphIndexFonts::Normal() { return &font24; }

PICO_RAM_FUNC bool GlyphIndexFont::updateFontMetric(lgfx::v1::FontMetrics* metrics, uint16_t uniCode) const {
    U8g2Decode decode(glyph(uniCode));
    if (decode.decode_ptr) {
        metrics->width     = decode.get_unsigned_bits(this->bits_per_char_width());
                             decode.get_unsigned_bits(this->bits_per_char_height());
        metrics->x_offset  = decode.get_signed_bits  (this->bits_per_char_x());
                             decode.get_signed_bits  (this->bits_per_char_y());
        metrics->x_advance = decode.get_signed_bits  (this->bits_per_delta_x());
        return true;
    }
    metrics->width = metrics->x_advance = this->max_char_width();
    metrics->x_offset = 0;
    return false;
}

PICO_RAM_FUNC size_t GlyphIndexFont::drawChar(lgfx::v1::LGFXBase* gfx, int32_t x, int32_t y, uint16_t uniCode,
                                const lgfx::v1::TextStyle* style, lgfx::v1::FontMetrics* metrics, int32_t& filled_x) const {
    int32_t sy = 65536 * style->size_y;
    y += (metrics->y_offset * sy) >> 16;
    U8g2Decode decode(glyph(uniCode));
    if (decode.decode_ptr == nullptr) return drawCharDummy(gfx, x, y, this->max_char_width(), metrics->height, style, filled_x);

    uint32_t w = decode.get_unsigned_bits(bits_per_char_width());
    uint32_t h = decode.get_unsigned_bits(bits_per_char_height());

    int32_t sx = 65536 * style->size_x;

    int32_t xoffset = (decode.get_signed_bits(bits_per_char_x()) * sx) >> 16;

    int32_t yoffset = -(int32_t)(decode.get_signed_bits(bits_per_char_y()) + h + metrics->y_offset);

    int32_t xAdvance = (decode.get_signed_bits(bits_per_delta_x()) * sx) >> 16;

    uint32_t colortbl[2] = {gfx->getColorConverter()->convert(style->back_rgb888), gfx->getColorConverter()->convert(style->fore_rgb888)};
    bool fillbg = (style->back_rgb888 != style->fore_rgb888);

    // ---- 速い道(ここだけ LovyanGFX に無い) ----
    // 画面の合成先(4bpp)へ、背景を塗らない・拡大なしで描くとき(画面の文字のほとんど)は、
    // 字の線の1本ごとに writeFillRect() を呼ばず(1文字あたり約20回)、クリップの内側だけを直接塗る。
    // 字の箱がクリップの外(dirty矩形の外の行・合成の外の回の空のクリップ)なら字のデータを展開もしない。
    // 塗る画素と色は元の道と同じ(PCビルドで全部の字を描き比べた。performance.md「日本語フォントの字の探し方」)
    if (!fillbg && sx == 65536 && sy == 65536) {
        if (LGFX_Sprite* f = DirectTarget(gfx)) {
            int32_t cx, cy, cw, ch;
            f->getClipRect(&cx, &cy, &cw, &ch);
            const int32_t gx = x + xoffset;
            const int32_t gy = y + yoffset;
            if (w == 0 || cw <= 0 || ch <= 0
                || gx >= cx + cw || gx + (int32_t)w <= cx || gy >= cy + ch || gy + (int32_t)h <= cy) {
                return xAdvance;
            }
            const int32_t cx1 = cx + cw, cy1 = cy + ch;
            const uint8_t nib = (uint8_t)(colortbl[1] & 0x0F);
            uint8_t* buf = static_cast<uint8_t*>(f->getBuffer());
            const int32_t stride = (int32_t)(f->bufferLength() / (uint32_t)f->height());
            uint32_t lx = 0;
            uint32_t ly = 0;
            do {
                uint32_t ab[2];
                ab[0] = decode.get_unsigned_bits(bits_per_0());
                ab[1] = decode.get_unsigned_bits(bits_per_1());
                bool i = 0;
                do {
                    uint32_t length = ab[i];
                    while (length) {
                        uint32_t len = (length > w - lx) ? w - lx : length;
                        length -= len;
                        if (i) {
                            const int32_t py = gy + (int32_t)ly;
                            if (py >= cy && py < cy1) {
                                const int32_t x0 = std::max<int32_t>(gx + (int32_t)lx, cx);
                                const int32_t x1 = std::min<int32_t>(gx + (int32_t)(lx + len), cx1);
                                if (x0 < x1) FillNibbles(buf + py * stride, x0, x1, nib);
                            }
                        }
                        lx += len;
                        if (lx == w) {
                            lx = 0;
                            ++ly;
                        }
                    }
                    i = !i;
                } while (i || decode.get_unsigned_bits(1) != 0);
            } while (ly < h);
            return xAdvance;
        }
    }

    int32_t left  = 0;
    int32_t right = 0;
    if (fillbg) {
        left  = std::max<int>(filled_x, x + (xoffset < 0 ? xoffset : 0));
        right = x + std::max<int>(((w * sx) >> 16) + xoffset, xAdvance);
        filled_x = right;
        gfx->setRawColor(colortbl[0]);
    }
    x += xoffset;
    gfx->startWrite();

    if (left < right) {
        if (yoffset > 0) {
            gfx->writeFillRect(left, y, right - left, (yoffset * sy) >> 16);
        }
        int32_t y0 = ((yoffset + h)   * sy) >> 16;
        int32_t y1 = (metrics->height * sy) >> 16;
        if (y0 < y1) {
            gfx->writeFillRect(left, y + y0, right - left, y1 - y0);
        }
    }

    if (w > 0) {
        if (left < right) {
            int32_t y0  = (  yoffset      * sy) >> 16;
            int32_t len = (((yoffset + h) * sy) >> 16) - y0;
            if (left < x) {
                gfx->writeFillRect(left, y + y0, x - left, len);
            }
            int32_t xwsx = x + ((w * sx) >> 16);
            if (xwsx < right) {
                gfx->writeFillRect(xwsx, y + y0, right - xwsx, len);
            }
        }
        left -= x;
        uint32_t ab[2];
        uint32_t lx = 0;
        uint32_t ly = 0;
        int32_t y0 = ((yoffset    ) * sy) >> 16;
        int32_t y1 = ((yoffset + 1) * sy) >> 16;
        do {
            ab[0] = decode.get_unsigned_bits(bits_per_0());
            ab[1] = decode.get_unsigned_bits(bits_per_1());
            bool i = 0;
            do {
                uint32_t length = ab[i];
                while (length) {
                    uint32_t len = (length > w - lx) ? w - lx : length;
                    length -= len;
                    if (i || fillbg) {
                        int32_t x0 = (lx * sx) >> 16;
                        if (!i && x0 < left) x0 = left;
                        int32_t x1 = ((lx + len) * sx) >> 16;
                        if (x0 < x1) {
                            gfx->setRawColor(colortbl[i]);
                            gfx->writeFillRect(x + x0, y + y0, x1 - x0, y1 - y0);
                        }
                    }
                    lx += len;
                    if (lx == w) {
                        lx = 0;
                        ++ly;
                        y0 = y1;
                        y1 = ((ly + yoffset + 1) * sy) >> 16;
                    }
                }
                i = !i;
            } while (i || decode.get_unsigned_bits(1) != 0);
        } while (ly < h);
    }
    gfx->endWrite();
    return xAdvance;
}

#endif
