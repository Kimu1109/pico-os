#include "gui/core/JpFont_Ext.hpp"
#include "OS_Data.hpp"
#include "config/Ram_Func.hpp"
#include "system/I18n_Functions.hpp"

namespace {
    // SdFat の FsFile を開いたままにして読む
    class SdReader final : public ExtGlyphStore::Reader {
    public:
        PICO_RAM_FUNC uint32_t generation() override {
            return OSData::SD_usable ? OSData::SD_mount_gen : 0;
        }
        bool open(const char* path, uint32_t& size) override {
            close();
            file_ = OSData::SD.open(path, O_RDONLY);
            if (!file_) return false;
            size = (uint32_t)file_.fileSize();
            return true;
        }
        bool readAt(uint32_t pos, void* dst, uint32_t len) override {
            if (!file_ || !OSData::SD_usable) return false;
            if (!file_.seekSet(pos)) return false;
            return file_.read(dst, len) == (int)len;
        }
        void close() override {
            if (file_) file_.close();
        }
    private:
        FsFile file_;
    };

    SdReader reader16;
    SdReader reader24;

    constexpr int kBytes16 = JpFontData::kExt16.max_record;
    constexpr int kBytes24 = JpFontData::kExt24.max_record;

    uint8_t slots16[JpFontExt::kSlots16 * kBytes16];
    uint16_t codes16[JpFontExt::kSlots16];
    uint8_t flags16[JpFontExt::kSlots16];
    uint8_t slots24[JpFontExt::kSlots24 * kBytes24];
    uint16_t codes24[JpFontExt::kSlots24];
    uint8_t flags24[JpFontExt::kSlots24];

    constexpr ExtGlyphStore::Expect Expect(const JpFontData::Ext& e) {
        return ExtGlyphStore::Expect{ e.sd_path, e.px, e.id, e.bytes };
    }

}

// constexpr のコンストラクタなので定数で初期化される(GlyphIndexFont のグローバルが静的な初期化の順番に依らず指せる)
ExtGlyphStore JpFontExt::store16(Expect(JpFontData::kExt16), &reader16, slots16, codes16, flags16, JpFontExt::kSlots16, kBytes16);
ExtGlyphStore JpFontExt::store24(Expect(JpFontData::kExt24), &reader24, slots24, codes24, flags24, JpFontExt::kSlots24, kBytes24);

using JpFontExt::store16;
using JpFontExt::store24;

const JpFontData::Ext& JpFontExt::Info(int px) {
    return px == 24 ? JpFontData::kExt24 : JpFontData::kExt16;
}

ExtGlyphStore& JpFontExt::Store(int px) {
    return px == 24 ? store24 : store16;
}

void JpFontExt::Reload() {
    store16.reload();
    store24.reload();
}

bool JpFontExt::Ready() {
    store16.poll();
    store24.poll();
    return store16.state() == ExtGlyphStore::State::Ready && store24.state() == ExtGlyphStore::State::Ready;
}

const char* JpFontExt::StateText(ExtGlyphStore::State s) {
    using S = ExtGlyphStore::State;
    switch (s) {
    // 設定の画面の1行(「追加の漢字: 〜」)に収まる短さにする
    case S::NotOpened: return TR("未確認");
    case S::NoSd:      return TR("SDなし");
    case S::Missing:   return TR("なし");
    case S::Bad:       return TR("壊れている");
    case S::WrongFont: return TR("版違い");
    case S::ReadError: return TR("読めない");
    case S::Ready:     return TR("あり");
    }
    return "";
}
