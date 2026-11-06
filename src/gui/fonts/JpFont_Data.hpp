#pragma once
// 生成物。手で直さず script/codegen/font/split_font.py で作り直すこと(docs/design/performance.md「漢字の一部をSDへ」)。
// フラッシュに残した日本語フォントと、SDに置く追加フォント(/sys/fonts/jpgothic<px>.pkf)をつなぐ定数。
#include <cstdint>

extern const uint8_t kJpGothicBase16[103309];
extern const uint8_t kJpGothicBase24[174059];

namespace JpFontData {
    struct Ext {
        uint8_t px;                 // 字の高さ
        uint32_t id;                // .pkf の見出しの印(合わなければ使わない)
        uint32_t bytes;             // .pkf の大きさ
        uint16_t glyphs;            // .pkf の字の数
        uint16_t max_record;        // .pkf の1字の記録の最大
        uint8_t sha256[32];         // .pkf のSHA-256(Webから落としたものを確かめる)
        const char* sd_path;        // SDの置き場所
        const char* web_name;       // 配布元(web-pages.yml の /fonts/)での名前
    };
    // 16px: フラッシュ 103308B(2830字。元は 161079B)、SD 58517B(1407字)
    constexpr Ext kExt16 = { 16, 0x0653683bu, 58517u, 1407, 56,
        { 0x41, 0xec, 0x51, 0xb0, 0x60, 0x79, 0x58, 0x37, 0x17, 0x7b, 0x74, 0x9e, 0xd0, 0x19, 0x15, 0x4e, 0x9f, 0x8f, 0x9d, 0xb6, 0x42, 0xb5, 0xae, 0x57, 0xa5, 0xbe, 0xe3, 0x5c, 0x19, 0x07, 0x6e, 0xee },
        "/sys/fonts/jpgothic16.pkf", "jpgothic16-0653683b.pkf" };
    // 24px: フラッシュ 174058B(2830字。元は 276933B)、SD 103621B(1407字)
    constexpr Ext kExt24 = { 24, 0xb86be544u, 103621u, 1407, 114,
        { 0x94, 0x57, 0x37, 0xae, 0x85, 0x67, 0x77, 0x8c, 0x1a, 0x58, 0x97, 0x09, 0x6e, 0xf6, 0xb8, 0x97, 0x08, 0xa8, 0x0a, 0x8b, 0x37, 0xba, 0xcd, 0x13, 0xdc, 0x47, 0x07, 0xa0, 0xa9, 0xca, 0x46, 0x04 },
        "/sys/fonts/jpgothic24.pkf", "jpgothic24-b86be544.pkf" };
}
