#include "gui/core/ExtGlyphStore.hpp"
#include "config/Ram_Func.hpp"

#include <cstring>

// 見出しの読み方は script/codegen/font/split_font.py の「.pkf の形」(数値はリトルエンディアン、字の並びの中だけu8g2と同じビッグエンディアン)。
// 控えに当たったとき(find の前半)と poll() は毎フレーム通るのでRAMに置く。SDを読む道は滅多に通らないのでフラッシュのまま

namespace {
    inline uint16_t Le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    inline uint32_t Le32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}

void ExtGlyphStore::setState(State s) {
    state_ = s;
    epoch_++;
}

void ExtGlyphStore::clearSlots() {
    for (int i = 0; i < slot_count_; i++) flags_[i] = kSlotEmpty;
}

void ExtGlyphStore::reload() {
    // 開いていたファイルはすぐ閉じる(置き換える前に呼ばれる)。次の poll() で開き直す
    if (reader_) reader_->close();
    clearSlots();
    generation_ = 0xFFFFFFFFu;
    setState(State::NotOpened);
}

// 字を引くたびに通る(U8g2GlyphIndex::find())ので、変わっていないときの道はRAMに置く
PICO_RAM_FUNC uint32_t ExtGlyphStore::poll() {
    const uint32_t gen = reader_ ? reader_->generation() : 0;
    if (gen == generation_) return epoch_;
    return changed(gen);
}

uint32_t ExtGlyphStore::changed(uint32_t gen) {
    generation_ = gen;
    clearSlots();
    if (reader_) reader_->close();
    if (gen == 0) {
        setState(State::NoSd);
        return epoch_;
    }
    openFile();
    return epoch_;
}

bool ExtGlyphStore::read(uint32_t pos, void* dst, uint32_t len) {
    reads_++;
    return reader_->readAt(pos, dst, len);
}

void ExtGlyphStore::openFile() {
    glyph_count_ = 0;
    index_count_ = 0;
    uint32_t size = 0;
    if (!reader_->open(expect_.path, size)) {
        setState(State::Missing);
        return;
    }
    uint8_t h[kHeaderSize];
    if (size < (uint32_t)kHeaderSize || !read(0, h, kHeaderSize) || memcmp(h, "PKF1", 4) != 0 || h[4] != 1) {
        reader_->close();
        setState(State::Bad);
        return;
    }
    const uint8_t px = h[5];
    const uint16_t n_index = Le16(h + 6);
    const uint32_t id = Le32(h + 8);
    const uint32_t n_glyph = Le32(h + 12);
    const uint16_t max_record = Le16(h + 16);
    const uint32_t data_pos = Le32(h + 20);
    const uint32_t data_bytes = Le32(h + 24);
    if (px != expect_.px || id != expect_.id) {
        reader_->close();
        setState(State::WrongFont);
        return;
    }
    // 大きさは決まり(JpFont_Data.hpp)と同じはず。違えば途中で切れている・書き換わっている
    if (size != expect_.bytes || n_index > kMaxIndex || n_glyph > 0xFFFF || max_record > slot_bytes_ ||
        data_pos != (uint32_t)kHeaderSize + 6u * n_index || data_bytes < 2 ||
        (uint64_t)data_pos + data_bytes != size || (n_glyph > 0 && n_index == 0)) {
        reader_->close();
        setState(State::Bad);
        return;
    }
    uint8_t e[6];
    for (int i = 0; i < n_index; i++) {
        if (!read((uint32_t)kHeaderSize + 6u * i, e, 6)) {
            reader_->close();
            setState(State::ReadError);
            return;
        }
        index_code_[i] = Le16(e);
        index_pos_[i] = Le32(e + 2);
        const bool ordered = i == 0 || (index_code_[i] > index_code_[i - 1] && index_pos_[i] > index_pos_[i - 1]);
        if (!ordered || index_pos_[i] >= data_bytes - 2) {
            reader_->close();
            setState(State::Bad);
            return;
        }
    }
    index_count_ = n_index;
    glyph_count_ = (uint16_t)n_glyph;
    data_pos_ = data_pos;
    data_bytes_ = data_bytes;
    setState(State::Ready);
}

PICO_RAM_FUNC const uint8_t* ExtGlyphStore::find(uint16_t code) {
    if (state_ != State::Ready) return nullptr;
    const int slot = code & (slot_count_ - 1);
    if (flags_[slot] != kSlotEmpty && codes_[slot] == code) {
        return flags_[slot] == kSlotHas ? slots_ + slot * slot_bytes_ + 3 : nullptr;
    }
    return load(code, slot);
}

const uint8_t* ExtGlyphStore::load(uint16_t code, int slot) {
    misses_++;
    const auto remember_none = [&]() -> const uint8_t* {
        codes_[slot] = code;
        flags_[slot] = kSlotNone;
        return nullptr;
    };
    if (index_count_ == 0 || code < index_code_[0]) return remember_none();
    // index_code_[k] <= code となる最後の k
    int lo = 0, hi = index_count_ - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (index_code_[mid] <= code) lo = mid;
        else hi = mid - 1;
    }
    uint32_t pos = index_pos_[lo];
    const uint32_t end = (lo + 1 < index_count_) ? index_pos_[lo + 1] : data_bytes_ - 2;
    uint8_t head[3];
    while (pos < end) {
        if (!read(data_pos_ + pos, head, 3)) break;
        const uint16_t c = (uint16_t)((head[0] << 8) | head[1]);
        const uint8_t size = head[2];
        if (c == 0 || size < 3 || pos + size > data_bytes_ - 2) {
            reader_->close();
            setState(State::Bad);
            return nullptr;
        }
        if (c > code) return remember_none();
        if (c == code) {
            if (size > slot_bytes_) {
                reader_->close();
                setState(State::Bad);
                return nullptr;
            }
            uint8_t* dst = slots_ + slot * slot_bytes_;
            // 控えを上書きする前に印を消す(読みに失敗したとき、半端な中身を当たりとして返さない)
            flags_[slot] = kSlotEmpty;
            if (!read(data_pos_ + pos, dst, size)) break;
            codes_[slot] = code;
            flags_[slot] = kSlotHas;
            return dst + 3;
        }
        pos += size;
    }
    if (pos >= end) return remember_none();
    // 読めなかった(カードを抜いた等)。次にマウントし直すまで使わない
    reader_->close();
    clearSlots();
    setState(State::ReadError);
    return nullptr;
}
