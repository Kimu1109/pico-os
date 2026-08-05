//
// ime_dict.cpp
//
#include "IME_Dict.hpp"
#include <string.h>
#include <stdlib.h>

ImeDictionary::ImeDictionary()
    : _sd(nullptr), _indexCount(0) {
    _dictPath[0] = '\0';
}

bool ImeDictionary::begin(SdFat &sd, const char* dictPath, const char* indexPath) {
    _sd = &sd;
    strncpy(_dictPath, dictPath, sizeof(_dictPath) - 1);
    _dictPath[sizeof(_dictPath) - 1] = '\0';

    if (!loadIndex(indexPath)) {
        return false;
    }

    if (_dictFile) {
        _dictFile.close();
    }
    // 検索の度に開閉しない。begin()時に一度だけ開いてハンドルを保持する。
    _dictFile = _sd->open(_dictPath, O_RDONLY);
    return (bool)_dictFile;
}

bool ImeDictionary::loadIndex(const char* indexPath) {
    FsFile idxFile = _sd->open(indexPath, O_RDONLY);
    if (!idxFile) {
        return false;
    }

    _indexCount = 0;
    char line[IME_MAX_LINE_BYTES];

    while (_indexCount < IME_MAX_INDEX_ENTRIES) {
        int len = idxFile.fgets(line, sizeof(line));
        if (len <= 0) break; // EOF

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        // 形式: yomi \t byteOffset
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        const char* yomiStr = line;
        const char* offsetStr = tab + 1;

        ImeIndexEntry &entry = _index[_indexCount];
        strncpy(entry.yomi, yomiStr, sizeof(entry.yomi) - 1);
        entry.yomi[sizeof(entry.yomi) - 1] = '\0';
        entry.offset = (uint32_t)strtoul(offsetStr, nullptr, 10);

        _indexCount++;
    }

    idxFile.close();
    return _indexCount > 0;
}

int ImeDictionary::findBlockStart(const char* key) {
    int lo = 0, hi = _indexCount - 1, result = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(_index[mid].yomi, key);
        if (cmp <= 0) {
            result = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return result;
}

int ImeDictionary::lookup(const char* key, char candidates[][IME_MAX_CAND_BYTES], int maxCandidates,
                            bool prefixMatch) {
    if (!_dictFile || _indexCount == 0) return 0;
    if (maxCandidates > IME_MAX_CANDIDATES) maxCandidates = IME_MAX_CANDIDATES;

    size_t keyLen = strlen(key);
    int blockIdx = findBlockStart(key);
    uint32_t startOffset = _index[blockIdx].offset;

    if (!_dictFile.seekSet(startOffset)) {
        return 0;
    }

    int found = 0;
    char line[IME_MAX_LINE_BYTES];

    // ソート済みファイルを前提に線形スキャン。
    // ブロック境界(256行)をまたいで一致が続く可能性があるため行数上限では
    // 打ち切らず、「読みがkeyの前方一致グループを追い越した」時点で打ち切る。
    // IME_MAX_SCAN_LINESは異常系(壊れたファイル等)での無限ループ防止の保険。
    for (int i = 0; i < IME_MAX_SCAN_LINES && found < maxCandidates; i++) {
        int len = _dictFile.fgets(line, sizeof(line));
        if (len <= 0) break; // EOF

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        const char* yomiStr = line;

        // yomiStrがkeyを前方一致で含むか(= yomiStrの先頭keyLenバイトがkeyと一致)
        bool isPrefix = (strncmp(yomiStr, key, keyLen) == 0);

        if (!isPrefix) {
            if (strcmp(yomiStr, key) < 0) {
                // まだキーに到達していない(ブロック先頭〜目的の行の間)
                continue;
            }
            // ソート済みなのでキーの前方一致グループを追い越した = 一致終了
            break;
        }

        // isPrefix == true。yomiStrの長さがkeyLenちょうどなら完全一致。
        bool isExact = (yomiStr[keyLen] == '\0');

        if (!prefixMatch && !isExact) {
            // 完全一致のみモード: 完全一致は前方一致グループの先頭側に
            // 集まる(短い文字列ほど辞書順で先)ため、ここに来た時点で
            // これ以降に完全一致が現れることはない。打ち切ってよい。
            break;
        }

        // 一致行。タブ区切りの候補を全部拾う。
        // (完全一致が前方一致より先に列挙されるため、候補配列の先頭側は
        //  自動的に完全一致優先になる)
        char* rest = tab + 1;
        char* saveptr = nullptr;
        char* tok = strtok_r(rest, "\t", &saveptr);
        while (tok != nullptr && found < maxCandidates) {
            strncpy(candidates[found], tok, IME_MAX_CAND_BYTES - 1);
            candidates[found][IME_MAX_CAND_BYTES - 1] = '\0';
            found++;
            tok = strtok_r(nullptr, "\t", &saveptr);
        }
    }

    return found;
}

// ---- buildOkuriKey() サンプル実装 ----
// 前述の通り、これはサンプルです。Lua側の既存テーブルで置き換え推奨。

struct KanaMarker {
    const char* kana; // UTF-8
    char marker;
};

// 送り仮名の先頭かな → 子音マーカー 対応表(代表例)
// ※ よw のような個別動詞の例外(酔う等)はこの汎用テーブルに含めず、
//    呼び出し側で個別ハンドリングする設計を推奨(元のLua実装踏襲)。
static const KanaMarker kKanaMarkerTable[] = {
    {"か", 'k'}, {"き", 'k'}, {"く", 'k'}, {"け", 'k'}, {"こ", 'k'},
    {"が", 'g'}, {"ぎ", 'g'}, {"ぐ", 'g'}, {"げ", 'g'}, {"ご", 'g'},
    {"さ", 's'}, {"し", 's'}, {"す", 's'}, {"せ", 's'}, {"そ", 's'},
    {"ざ", 'z'}, {"じ", 'z'}, {"ず", 'z'}, {"ぜ", 'z'}, {"ぞ", 'z'},
    {"た", 't'}, {"ち", 't'}, {"つ", 't'}, {"て", 't'}, {"と", 't'},
    {"だ", 'd'}, {"ぢ", 'd'}, {"づ", 'd'}, {"で", 'd'}, {"ど", 'd'},
    {"な", 'n'}, {"に", 'n'}, {"ぬ", 'n'}, {"ね", 'n'}, {"の", 'n'},
    {"は", 'h'}, {"ひ", 'h'}, {"ふ", 'h'}, {"へ", 'h'}, {"ほ", 'h'},
    {"ば", 'b'}, {"び", 'b'}, {"ぶ", 'b'}, {"べ", 'b'}, {"ぼ", 'b'},
    {"ぱ", 'p'}, {"ぴ", 'p'}, {"ぷ", 'p'}, {"ぺ", 'p'}, {"ぽ", 'p'},
    {"ま", 'm'}, {"み", 'm'}, {"む", 'm'}, {"め", 'm'}, {"も", 'm'},
    {"や", 'y'}, {"ゆ", 'y'}, {"よ", 'y'},
    {"ら", 'r'}, {"り", 'r'}, {"る", 'r'}, {"れ", 'r'}, {"ろ", 'r'},
    // う音便(五段動詞の「う」語尾)系の活用かなは全てwマーカーに集約
    {"わ", 'w'}, {"い", 'w'}, {"う", 'w'}, {"え", 'w'}, {"お", 'w'},
};
static const int kKanaMarkerCount = sizeof(kKanaMarkerTable) / sizeof(kKanaMarkerTable[0]);

bool ImeDictionary::buildOkuriKey(const char* stem, const char* okuriKanaUtf8,
                                    char* outKey, size_t outKeySize) {
    char marker = 0;
    for (int i = 0; i < kKanaMarkerCount; i++) {
        if (strcmp(kKanaMarkerTable[i].kana, okuriKanaUtf8) == 0) {
            marker = kKanaMarkerTable[i].marker;
            break;
        }
    }
    if (marker == 0) {
        return false; // 対応表にない文字、または個別例外(よw等)は呼び出し側で処理
    }

    size_t stemLen = strlen(stem);
    if (stemLen + 1 + 1 > outKeySize) { // stem + marker + null
        return false;
    }
    memcpy(outKey, stem, stemLen);
    outKey[stemLen] = marker;
    outKey[stemLen + 1] = '\0';
    return true;
}
