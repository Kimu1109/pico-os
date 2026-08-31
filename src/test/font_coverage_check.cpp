//
// font_coverage_check.cpp
//
#include "font_coverage_check.hpp"
#include "functions/Log_Functions.hpp"
#include <set>
#include <string.h>

// UTF-8バイト列から1文字分のUnicodeコードポイントを取り出す。
// s: UTF-8文字列(null終端)
// pos: 開始位置。呼び出し後、次の文字の開始位置に更新される。
// 戻り値: コードポイント。不正なバイト列の場合は0を返しposを1進める。
static uint32_t utf8DecodeOne(const char* s, size_t& pos) {
    uint8_t b0 = (uint8_t)s[pos];
    if (b0 == 0) return 0;

    if (b0 < 0x80) {
        pos += 1;
        return b0;
    } else if ((b0 & 0xE0) == 0xC0 && s[pos + 1]) {
        uint8_t b1 = (uint8_t)s[pos + 1];
        pos += 2;
        return ((uint32_t)(b0 & 0x1F) << 6) | (b1 & 0x3F);
    } else if ((b0 & 0xF0) == 0xE0 && s[pos + 1] && s[pos + 2]) {
        uint8_t b1 = (uint8_t)s[pos + 1];
        uint8_t b2 = (uint8_t)s[pos + 2];
        pos += 3;
        return ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (b2 & 0x3F);
    } else if ((b0 & 0xF8) == 0xF0 && s[pos + 1] && s[pos + 2] && s[pos + 3]) {
        uint8_t b1 = (uint8_t)s[pos + 1];
        uint8_t b2 = (uint8_t)s[pos + 2];
        uint8_t b3 = (uint8_t)s[pos + 3];
        pos += 4;
        return ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12)
             | ((uint32_t)(b2 & 0x3F) << 6) | (b3 & 0x3F);
    }

    // 不正なバイト列: 1バイトだけ進めてスキップ
    pos += 1;
    return 0;
}

// codepointをUTF-8バイト列(4バイト+null)に変換する
// (人間が結果ファイルを見て何の文字か分かるように)
static void utf8Encode(uint32_t cp, char out[5]) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = '\0';
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = '\0';
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = '\0';
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        out[4] = '\0';
    }
}

void checkFontCoverage(SdFat &sd, const lgfx::IFont &font,
                        const char* dictBodyPath, const char* outputPath) {
    LOG_SYS_MSG("Font coverage check starts!");

    FsFile dictFile = sd.open(dictBodyPath, O_RDONLY);
    if (!dictFile) {
        LOG_SYS_FAIL("[font_check] 辞書ファイルを開けませんでした: %s", dictBodyPath);
        return;
    }

    // 辞書中に出現する異なり文字を重複なく収集する
    // (漢字は数千種程度に収まる想定なので std::set で問題ない。
    //  これは一度だけ実行するツールなのでヒープ確保も許容する)
    std::set<uint32_t> seenCodepoints;
    char line[512];

    while (true) {
        int len = dictFile.fgets(line, sizeof(line));
        if (len <= 0) break;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        // 最初のタブ(よみ)より後ろの候補部分だけをチェック対象にする。
        // よみはひらがな/記号のみで、日本語フォントに含まれている前提。
        char* tab = strchr(line, '\t');
        const char* candStart = tab ? (tab + 1) : line;

        size_t pos = 0;
        while (candStart[pos] != '\0') {
            uint32_t cp = utf8DecodeOne(candStart, pos);
            if (cp == 0 || cp == '\t') continue;
            seenCodepoints.insert(cp);
        }
    }
    dictFile.close();

    LOG_SYS_OK("[font_check] 辞書中の異なり文字数: %u", (unsigned)seenCodepoints.size());

    FsFile outFile = sd.open(outputPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (!outFile) {
        LOG_SYS_FAIL("[font_check] 出力ファイルを開けませんでした: %s", outputPath);
        return;
    }

    int missingCount = 0;
    lgfx::FontMetrics metrics;
    for (uint32_t cp : seenCodepoints) {
        if (cp > 0xFFFF) {
            // u8g2フォントAPIは16bitコードまでしか扱えないため、
            // BMP範囲外の文字は無条件で「未対応」扱いにする
            outFile.printf("U+%06X (BMP範囲外のため未対応扱い)\n", (unsigned)cp);
            missingCount++;
            continue;
        }

        bool hasGlyph = font.updateFontMetric(&metrics, (uint16_t)cp);
        if (!hasGlyph) {
            char utf8buf[5];
            utf8Encode(cp, utf8buf);
            outFile.printf("U+%04X\t%s\n", (unsigned)cp, utf8buf);
            missingCount++;
        }
    }
    outFile.close();

    LOG_SYS_OK("[font_check] フォント未対応文字数: %d件 (詳細: %s)\n",
                  missingCount, outputPath);
}
