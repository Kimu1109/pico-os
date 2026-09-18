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

    // UTF-8文字列(NUL終端)の文字数を数える。kMinSubstringQueryCharsとの
    // 比較にのみ使うので、不正なバイト列でもクラッシュしなければよい
    // (Utf8CharBytesFromLeadByteが不正バイトを1文字として扱うフォールバックを持つ)。
    int Utf8CharCountOf(const char* s) {
        int n = 0;
        const uint8_t* p = (const uint8_t*)s;
        while (*p) {
            p += Utf8CharBytesFromLeadByte(*p);
            n++;
        }
        return n;
    }
}

bool WordDictionary::begin(const char* dictPath, const char* indexPath,
                            const char* suffixDictPath, const char* suffixIndexPath) {
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

    // サフィックスインデックスは無くても(壊れていても)部分一致が
    // 全体走査(State::Scanning)へ自動フォールバックするだけなので、
    // begin()自体は失敗にしない(dictPath/indexPathと同じ方針)。
    suffixAvailable_ = false;
    if (suffixFile_) {
        suffixFile_.close();
    }
    if (suffixDictPath && suffixIndexPath) {
        suffixFile_ = sd_->open(suffixDictPath, O_RDONLY);
        if (suffixFile_ && loadSuffixIndex(suffixIndexPath)) {
            suffixFileSize_ = (uint32_t)suffixFile_.fileSize();
            suffixAvailable_ = true;
        } else if (suffixFile_) {
            suffixFile_.close();
        }
    }

    return true;
}

int WordDictionary::LoadIndexEntries(SdFat* sd, const char* indexPath,
                                      IndexEntry* arr, int maxCount) {
    FsFile idxFile = sd->open(indexPath, O_RDONLY);
    if (!idxFile) return 0;

    int count = 0;
    char line[160];

    while (count < maxCount) {
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

        IndexEntry& entry = arr[count];
        strncpy(entry.key, keyStr, sizeof(entry.key) - 1);
        entry.key[sizeof(entry.key) - 1] = '\0';
        entry.offset = (uint32_t)strtoul(offsetStr, nullptr, 10);

        count++;
    }

    idxFile.close();
    return count;
}

bool WordDictionary::loadIndex(const char* indexPath) {
    indexCount_ = LoadIndexEntries(sd_, indexPath, index_, kMaxIndexEntries);
    return indexCount_ > 0;
}

bool WordDictionary::loadSuffixIndex(const char* indexPath) {
    suffixIndexCount_ = LoadIndexEntries(sd_, indexPath, suffixIndex_, kMaxSuffixIndexEntries);
    return suffixIndexCount_ > 0;
}

int WordDictionary::findBlockStart(const IndexEntry* arr, int count, const char* key) {
    int lo = 0, hi = count - 1, result = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(arr[mid].key, key);
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

int WordDictionary::readSuffixLine(char* buf, int bufSize, char** suffix, uint32_t* origOffset) {
    int len = suffixFile_.fgets(buf, bufSize);
    if (len <= 0) return len;

    // readLine()と同じガード: 収まらない行は改行まで読み捨てて次行とずれないようにする。
    // 接尾辞は本体の検索用語句由来なので極端に長い行は基本無いはずだが念のため踏襲する。
    if (buf[len - 1] != '\n') {
        char discard[128];
        int dlen;
        do {
            dlen = suffixFile_.fgets(discard, sizeof(discard));
        } while (dlen > 0 && discard[dlen - 1] != '\n');
    }

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    char* tab = strchr(buf, '\t');
    if (!tab) {
        *suffix = buf;
        *origOffset = 0;
        return len;
    }
    *tab = '\0';
    *suffix = buf;
    *origOffset = (uint32_t)strtoul(tab + 1, nullptr, 10);
    return len;
}

void WordDictionary::addHit(const char* col2, const char* col3) {
    if (hitCount_ >= kMaxHits) return;
    WordDictHit& h = hits_[hitCount_];
    h.term.assign(col2 ? col2 : "");
    h.desc.assign(col3 ? col3 : "");
    hitCount_++;
}

bool WordDictionary::isOffsetAlreadyAdded(uint32_t originalOffset) const {
    for (int i = 0; i < addedOffsetsCount_; i++) {
        if (addedOffsets_[i] == originalOffset) return true;
    }
    return false;
}

void WordDictionary::rememberAddedOffset(uint32_t originalOffset) {
    if (addedOffsetsCount_ < kMaxHits) {
        addedOffsets_[addedOffsetsCount_++] = originalOffset;
    }
}

void WordDictionary::search(const char* query) {
    hitCount_ = 0;
    scanOffset_ = 0;
    prefixRangeStart_ = 0;
    prefixRangeEnd_ = 0;
    skippedPrefixRange_ = false;
    suffixInGroup_ = false;
    addedOffsetsCount_ = 0;

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
    int blockIdx = (indexCount_ > 0) ? findBlockStart(index_, indexCount_, query_) : -1;
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
        // 前方一致だけで枠が埋まった。部分一致を探すまでもない
        state_ = State::Done;
        return;
    }

    // ---- 残り(語の途中に含む一致=部分一致)を拾う ----
    // クエリが短すぎる(1文字)と一致数が多すぎて絞り込みにならないため、
    // そもそも試みない(前方一致の結果のみで終える)。
    if (Utf8CharCountOf(query_) < kMinSubstringQueryChars) {
        state_ = State::Done;
        return;
    }

    if (suffixAvailable_) {
        // サフィックス索引がある: 前方一致と同じブロックインデックス+
        // 二分探索の技法で該当ブロックへ飛び、State::ScanningSuffixとして
        // update()で少しずつ確認する(1ブロックの行数が本体索引より多く、
        // 同期で終わらせるとフレームが止まりかねないため)。
        int sBlockIdx = (suffixIndexCount_ > 0)
            ? findBlockStart(suffixIndex_, suffixIndexCount_, query_) : -1;
        uint32_t sStart = (sBlockIdx >= 0) ? suffixIndex_[sBlockIdx].offset : 0;
        uint32_t sEnd = (sBlockIdx + 1 < suffixIndexCount_)
            ? suffixIndex_[sBlockIdx + 1].offset : suffixFileSize_;

        if (suffixFile_.seek(sStart)) {
            suffixBlockStart_ = sStart;
            suffixBlockEnd_ = sEnd;
            suffixScanOffset_ = sStart;
            suffixInGroup_ = false;
            state_ = State::ScanningSuffix;
            return;
        }
        // seekに失敗した場合のみ下の全体走査へフォールバックする
    }

    // ---- サフィックス索引が無い/開けない場合のみ、全体走査で拾う ----
    if (!dictFile_.seek(0)) {
        state_ = State::Done;
        return;
    }
    scanOffset_ = 0;
    state_ = State::Scanning;
}

void WordDictionary::update() {
    if (state_ == State::Scanning) {
        updateFullScan();
    } else if (state_ == State::ScanningSuffix) {
        updateSuffixScan();
    }
}

void WordDictionary::updateFullScan() {
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

void WordDictionary::updateSuffixScan() {
    char line[PICO_STR_2KiB]; // 接尾辞行は短いが、readLine()とバッファ長を揃えておく

    for (int i = 0; i < kLinesPerUpdate && hitCount_ < kMaxHits; i++) {
        char* suffix;
        uint32_t origOffset;
        int len = readSuffixLine(line, sizeof(line), &suffix, &origOffset);
        if (len <= 0) {
            // ブロックの終端(EOF、または次ブロックへ読み進んだだけの場合も
            // 含む)まで来た = このブロックにはもう無い
            state_ = State::Done;
            return;
        }
        suffixScanOffset_ = (uint32_t)suffixFile_.position();

        bool isMatch = (strncmp(suffix, query_, queryLen_) == 0);

        if (!isMatch) {
            if (suffixInGroup_) {
                // ソート済みコーパスなので一致グループを追い越した=一致終了
                state_ = State::Done;
                return;
            }
            if (strcmp(suffix, query_) < 0) {
                continue; // まだクエリに到達していない
            }
            // クエリより辞書順で後ろに来た = 部分一致は1件も無い
            state_ = State::Done;
            return;
        }
        suffixInGroup_ = true;

        // 前方一致で既に返した範囲は数えない(全体走査版と同じ考え方)
        if (origOffset >= prefixRangeStart_ && origOffset < prefixRangeEnd_) continue;
        // 同じエントリが複数の接尾辞位置で一致しても1回だけ数える
        if (isOffsetAlreadyAdded(origOffset)) continue;

        // 表示用語句/説明は元の辞書ファイル側から引く(suffixFile_と
        // dictFile_は別ハンドルなので、ここでdictFile_をseekしても
        // suffixFile_側の読み進み位置には影響しない)
        if (!dictFile_.seek(origOffset)) continue;
        char dline[PICO_STR_2KiB];
        char *col1, *col2, *col3;
        int dlen = readLine(dline, sizeof(dline), &col1, &col2, &col3);
        if (dlen <= 0) continue;

        addHit(col2, col3);
        rememberAddedOffset(origOffset);
    }

    if (hitCount_ >= kMaxHits) {
        state_ = State::Done;
    }
    // ここで打ち切らない: ブロック境界のチェックポイントは「先頭がquery以下」
    // というだけの目印で、一致グループがブロックをまたいで続くことがある
    // (例: query="an"に対しchecpoint_{N+1}="anna"のとき、block Nより後ろの
    // "another"も一致する)。前方一致と同じく、グループを追い越すか
    // (すでに一致グループへ入っていて非一致行に当たる)、クエリより
    // 辞書順で後ろに来るか、ファイル末尾に達するまで次フレームへ持ち越す。
}

void WordDictionary::cancel() {
    if (state_ == State::Scanning || state_ == State::ScanningSuffix) {
        state_ = State::Done;
    }
}

float WordDictionary::progress() const {
    if (state_ == State::Done) return 1.0f;

    if (state_ == State::ScanningSuffix) {
        // ブロックはファイル全体のごく一部なので、全体基準だと割合が
        // ほぼ動かずUIとして意味を成さない。このブロック内の進み具合を返す。
        if (suffixBlockEnd_ <= suffixBlockStart_) return 1.0f;
        float p = (float)(suffixScanOffset_ - suffixBlockStart_) /
                  (float)(suffixBlockEnd_ - suffixBlockStart_);
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        return p;
    }

    if (fileSize_ == 0) return 1.0f;
    float p = (float)scanOffset_ / (float)fileSize_;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}
