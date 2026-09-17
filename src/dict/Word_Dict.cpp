//
// Word_Dict.cpp
//
#include "Word_Dict.hpp"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "OS_Data.hpp"

namespace {
    // 検索語のASCII英字だけを小文字化する。検索用語句側(英単語)も
    // 小文字化して格納されている前提のため(日本語の読みはひらがなの
    // ままなのでUTF-8の非ASCIIバイトはtolower()の対象にならず無害)。
    void NormalizeQueryInto(const char* src, char* dst, size_t dstCap) {
        size_t i = 0;
        if (src) {
            for (; src[i] != '\0' && i + 1 < dstCap; i++) {
                dst[i] = (char)tolower((unsigned char)src[i]);
            }
        }
        dst[i] = '\0';
    }
}

bool WordDictionary::begin(const char* dictPath, const char* indexPath) {
    sd_ = &OSData::SD;
    strncpy(dictPath_, dictPath, sizeof(dictPath_) - 1);
    dictPath_[sizeof(dictPath_) - 1] = '\0';

    loadIndex(indexPath); // 無くても(0件でも)全体走査だけで動くので失敗を無視する

    if (dictFile_) {
        dictFile_.close();
    }
    dictFile_ = sd_->open(dictPath_, O_RDONLY);
    if (!dictFile_) {
        return false;
    }
    fileSize_ = (uint32_t)dictFile_.fileSize();
    return true;
}

bool WordDictionary::loadIndex(const char* indexPath) {
    FsFile idxFile = sd_->open(indexPath, O_RDONLY);
    if (!idxFile) {
        indexCount_ = 0;
        return false;
    }

    indexCount_ = 0;
    char line[160];

    while (indexCount_ < kMaxIndexEntries) {
        int len = idxFile.fgets(line, sizeof(line));
        if (len <= 0) break; // EOF

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        const char* keyStr = line;
        const char* offsetStr = tab + 1;

        IndexEntry& entry = index_[indexCount_];
        strncpy(entry.key, keyStr, sizeof(entry.key) - 1);
        entry.key[sizeof(entry.key) - 1] = '\0';
        entry.offset = (uint32_t)strtoul(offsetStr, nullptr, 10);

        indexCount_++;
    }

    idxFile.close();
    return indexCount_ > 0;
}

int WordDictionary::findBlockStart(const char* key) const {
    int lo = 0, hi = indexCount_ - 1, result = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(index_[mid].key, key);
        if (cmp <= 0) {
            result = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return result;
}

int WordDictionary::readLine(char* buf, int bufSize, char** col1, char** col2, char** col3) {
    int len = dictFile_.fgets(buf, bufSize);
    if (len <= 0) return len;

    // バッファに収まりきらなかった行(実データにまれに数KBのものがある)は、
    // 改行に達するまで読み捨てて次の行の開始位置とずれないようにする。
    // col1/col2は行の先頭にあるので通常はbuf内に収まっており、
    // 切り詰まるのは主にcol3(説明)側(colがバッファ長を超えて途中の
    // バイト列になる場合。下のタブ分割はそれでも壊れず動く)。
    if (buf[len - 1] != '\n') {
        char discard[128];
        int dlen;
        do {
            dlen = dictFile_.fgets(discard, sizeof(discard));
        } while (dlen > 0 && discard[dlen - 1] != '\n');
    }

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    char* tab1 = strchr(buf, '\t');
    if (!tab1) {
        *col1 = buf;
        *col2 = nullptr;
        *col3 = nullptr;
        return len;
    }
    *tab1 = '\0';
    *col1 = buf;

    char* tab2 = strchr(tab1 + 1, '\t');
    if (!tab2) {
        *col2 = tab1 + 1;
        *col3 = nullptr;
        return len;
    }
    *tab2 = '\0';
    *col2 = tab1 + 1;
    *col3 = tab2 + 1;
    return len;
}

void WordDictionary::addHit(const char* col2, const char* col3) {
    if (hitCount_ >= kMaxHits) return;
    WordDictHit& h = hits_[hitCount_];
    h.term.assign(col2 ? col2 : "");
    h.desc.assign(col3 ? col3 : "");
    hitCount_++;
}

void WordDictionary::search(const char* query) {
    hitCount_ = 0;
    scanOffset_ = 0;
    prefixRangeStart_ = 0;
    prefixRangeEnd_ = 0;
    skippedPrefixRange_ = false;

    NormalizeQueryInto(query, query_, sizeof(query_));
    queryLen_ = strlen(query_);

    if (queryLen_ == 0 || !dictFile_) {
        state_ = State::Done;
        return;
    }

    // ---- 前方一致ぶんを同期的に埋める ----
    // ソート済みファイルなので、前方一致するエントリは必ず1つの
    // 連続した範囲に収まる。二分探索で近いブロックまで飛んでから
    // 線形に確認する(IME_Dict.cppのlookup()と同じ発想)。
    int blockIdx = (indexCount_ > 0) ? findBlockStart(query_) : -1;
    uint32_t startOffset = (blockIdx >= 0) ? index_[blockIdx].offset : 0;

    if (!dictFile_.seek(startOffset)) {
        state_ = State::Done;
        return;
    }

    char line[PICO_STR_2KiB];
    bool inGroup = false;

    // ブロック1個ぶん(+安全マージン)を超えて回らないための保険。
    // 前方一致は kMaxHits に達した時点でも打ち切るため、実際にここまで
    // 回ることは通常無い。
    const int kMaxPrefixScanLines = 4000;

    for (int i = 0; i < kMaxPrefixScanLines && hitCount_ < kMaxHits; i++) {
        uint32_t lineStart = (uint32_t)dictFile_.position();
        char *col1, *col2, *col3;
        int len = readLine(line, sizeof(line), &col1, &col2, &col3);
        if (len <= 0) break; // EOF

        bool isPrefix = (strncmp(col1, query_, queryLen_) == 0);

        if (!isPrefix) {
            if (inGroup) {
                // ソート済みなので前方一致グループを追い越した=一致終了
                prefixRangeEnd_ = lineStart;
                break;
            }
            if (strcmp(col1, query_) < 0) {
                continue; // まだクエリに到達していない
            }
            // クエリより辞書順で後ろに来た = 前方一致は1件も無い
            break;
        }

        if (!inGroup) {
            inGroup = true;
            prefixRangeStart_ = lineStart;
        }
        prefixRangeEnd_ = (uint32_t)dictFile_.position();
        addHit(col2, col3);
    }

    if (hitCount_ >= kMaxHits) {
        // 前方一致だけで枠が埋まった。全体走査するまでもない
        state_ = State::Done;
        return;
    }

    // ---- 残り(語の途中に含む一致)をupdate()で少しずつ拾う ----
    if (!dictFile_.seek(0)) {
        state_ = State::Done;
        return;
    }
    scanOffset_ = 0;
    state_ = State::Scanning;
}

void WordDictionary::update() {
    if (state_ != State::Scanning) return;

    char line[PICO_STR_2KiB];

    for (int i = 0; i < kLinesPerUpdate && hitCount_ < kMaxHits; i++) {
        // 前方一致パスで既に確認済みの範囲([prefixRangeStart_, prefixRangeEnd_))
        // へ来たら、二重に数えないよう一気に読み飛ばす。1回のupdate()の
        // 途中で範囲の先頭へ達することもあるため、行を読む直前に毎回確認する。
        if (!skippedPrefixRange_ && prefixRangeStart_ < prefixRangeEnd_ &&
            scanOffset_ == prefixRangeStart_) {
            dictFile_.seek(prefixRangeEnd_);
            scanOffset_ = prefixRangeEnd_;
            skippedPrefixRange_ = true;
            if (scanOffset_ >= fileSize_) {
                state_ = State::Done;
                return;
            }
        }

        char *col1, *col2, *col3;
        int len = readLine(line, sizeof(line), &col1, &col2, &col3);
        if (len <= 0) {
            state_ = State::Done;
            scanOffset_ = fileSize_;
            return;
        }
        scanOffset_ = (uint32_t)dictFile_.position();

        if (col1 != nullptr && strstr(col1, query_) != nullptr) {
            addHit(col2, col3);
        }
    }

    if (hitCount_ >= kMaxHits) {
        state_ = State::Done;
    }
}

void WordDictionary::cancel() {
    if (state_ == State::Scanning) {
        state_ = State::Done;
    }
}

float WordDictionary::progress() const {
    if (state_ == State::Done) return 1.0f;
    if (fileSize_ == 0) return 1.0f;
    float p = (float)scanOffset_ / (float)fileSize_;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}
