#include "gui/core/U8g2GlyphIndex.hpp"
#include "config/Ram_Func.hpp"
#include "gui/core/ExtGlyphStore.hpp"

// u8g2のフォントの形(LovyanGFX の lgfx_fonts.cpp・u8g2 の u8g2_font.c と同じ読み方):
//   ヘッダ23B: [17..18] 'A'からの字の位置 / [19..20] 'a'からの字の位置 / [21..22] 0x100以上の部の位置
//   0〜255の字: [文字コード1B][この字の大きさ1B][データ…]。大きさ0で終わり
//   0x100以上の部: 先頭に表 [次の区画までの距離2B][この区画の最後の文字コード2B]…(最後の文字コードが0xFFFFで終わり)、
//                 その後に字 [文字コード2B][この字の大きさ1B][データ…]。文字コード0で終わり
// (フラッシュはそのまま読める番地にあるので pgm_read_byte は使わない)
// 引く道(find/findAscii/findUnicode)は実機ではRAMに置く(config/Ram_Func.hpp。字のデータの読みでキャッシュから追い出されないように)。
// 索引を作る・元の探し方は1回きり/滅多に通らないのでフラッシュのまま

namespace {
    constexpr int kHeaderSize = 23;

    inline uint16_t Read16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

    inline int CacheSlot(uint16_t encoding) {
        // 連続した文字(ひらがな・カタカナ)が別の場所へ入るよう、下の桁をそのまま使う
        return encoding & (U8g2GlyphIndex::kCacheSlots - 1);
    }
}

PICO_RAM_FUNC const uint8_t* U8g2GlyphIndex::find(uint16_t encoding) {
    if (ext_) {
        // SDを抜いた・置いた・開き直したら、「無い」「SDにある」と覚えたものを捨てる
        const uint32_t epoch = ext_->poll();
        if (epoch != ext_epoch_) {
            ext_epoch_ = epoch;
            clearCache();
        }
    }
    const int slot = CacheSlot(encoding);
    const uint32_t cached = cache_pos_[slot];
    if (cached != kEmpty && cache_code_[slot] == encoding) {
        if (cached == kInExt) return ext_->find(encoding);
        return cached == kNotFound ? nullptr : font_ + cached;
    }
    const uint8_t* g = (encoding <= 255) ? findAscii(encoding) : findUnicode(encoding);
    cache_code_[slot] = encoding;
    if (!g && ext_ && MayBeInExt(encoding)) {
        g = ext_->find(encoding);
        cache_pos_[slot] = g ? kInExt : kNotFound;
        return g;
    }
    cache_pos_[slot] = g ? (uint32_t)(g - font_) : kNotFound;
    return g;
}

void U8g2GlyphIndex::clearCache() {
    for (int i = 0; i < kCacheSlots; i++) cache_pos_[i] = kEmpty;
}

PICO_RAM_FUNC const uint8_t* U8g2GlyphIndex::findAscii(uint16_t encoding) const {
    const uint8_t* font = font_ + kHeaderSize;
    if (encoding >= 'a')      font += Read16(font_ + 19);
    else if (encoding >= 'A') font += Read16(font_ + 17);
    for (; font[1]; font += font[1]) {
        if (font[0] == encoding) return font + 2;
    }
    return nullptr;
}

const uint8_t* U8g2GlyphIndex::unicodeGlyphs() const {
    const uint8_t* lut = font_ + kHeaderSize + Read16(font_ + 21);
    // 表の最初の項目の距離は「表の先頭 → 字の並びの先頭」
    return lut + Read16(lut);
}

const uint8_t* U8g2GlyphIndex::findUnicodeLinear(uint16_t encoding) const {
    // LovyanGFX の U8g2font::getGlyph() と同じ
    const uint8_t* font = font_ + kHeaderSize + Read16(font_ + 21);
    const uint8_t* lut = font;
    uint16_t e;
    do {
        font += Read16(lut);
        e = Read16(lut + 2);
        lut += 4;
    } while (e < encoding);
    for (; 0 != (e = Read16(font)); font += font[2]) {
        if (e == encoding) return font + 3;
    }
    return nullptr;
}

void U8g2GlyphIndex::buildIndex() {
    const uint8_t* first = unicodeGlyphs();
    // 1回目: 字の数を数え、文字コード順かを確かめる
    uint32_t count = 0;
    uint16_t prev = 0;
    bool sorted_ok = true;
    for (const uint8_t* p = first; ; p += p[2]) {
        const uint16_t e = Read16(p);
        if (e == 0) break;
        if (count > 0 && e <= prev) sorted_ok = false;
        prev = e;
        count++;
    }
    if (!sorted_ok || count == 0) {
        index_state_ = IndexState::Unsorted;
        return;
    }
    // 2回目: stride 字おきに区切りの先頭を覚える
    const uint32_t stride = (count + kIndexSlots - 1) / kIndexSlots;
    uint32_t i = 0;
    int n = 0;
    for (const uint8_t* p = first; ; p += p[2], i++) {
        const uint16_t e = Read16(p);
        if (e == 0) break;
        if (i % stride == 0 && n < kIndexSlots) {
            index_code_[n] = e;
            index_pos_[n] = (uint32_t)(p - font_);
            n++;
        }
    }
    index_count_ = (uint16_t)n;
    index_stride_ = (uint16_t)stride;
    index_state_ = IndexState::Ready;
}

PICO_RAM_FUNC const uint8_t* U8g2GlyphIndex::findUnicode(uint16_t encoding) {
    if (index_state_ == IndexState::NotBuilt) buildIndex();
    if (index_state_ != IndexState::Ready) return findUnicodeLinear(encoding);

    // index_code_[k] <= encoding となる最後の k
    int lo = 0, hi = index_count_ - 1;
    if (encoding < index_code_[0]) return nullptr;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (index_code_[mid] <= encoding) lo = mid;
        else hi = mid - 1;
    }
    // 区切りの中を順に見る。文字コード順なので、超えたら無い
    const uint8_t* p = font_ + index_pos_[lo];
    for (uint16_t e; 0 != (e = Read16(p)); p += p[2]) {
        if (e == encoding) return p + 3;
        if (e > encoding) break;
    }
    return nullptr;
}
