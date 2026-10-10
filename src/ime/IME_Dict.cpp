//
// ime_dict.cpp
//
#include "IME_Dict.hpp"
#include <string.h>
#include <stdlib.h>
#include "OS_Data.hpp"

ImeDictionary::ImeDictionary()
    : _sd(nullptr), _index(nullptr), _indexCount(0), _loadFailed(false) {
    _dictPath[0] = '\0';
    _indexPath[0] = '\0';
}

ImeDictionary::~ImeDictionary() {
    release();
}

bool ImeDictionary::begin(const char* dictPath, const char* indexPath) {
    release();
    _sd = &OSData::SD;
    if (strlen(dictPath) >= sizeof(_dictPath) || strlen(indexPath) >= sizeof(_indexPath)) return false;
    strcpy(_dictPath, dictPath);
    strcpy(_indexPath, indexPath);
    return true;
}

void ImeDictionary::release() {
    free(_index);
    _index = nullptr;
    _indexCount = 0;
    _loadFailed = false;
    if (_dictFile) _dictFile.close();
}

bool ImeDictionary::ensureLoaded() {
    if (_index && _dictFile) return true;
    if (_loadFailed || !_sd || _indexPath[0] == '\0') return false;
    free(_index); //片方だけ残っている場合に備えて読み直す
    _index = nullptr;
    _indexCount = 0;
    if (!loadIndex(_indexPath)) {
        _loadFailed = true;
        return false;
    }
    // 検索の度に開閉しない。開いている間はハンドルを保持する。
    _dictFile = _sd->open(_dictPath, O_RDONLY);
    if (!_dictFile) {
        free(_index);
        _index = nullptr;
        _indexCount = 0;
        _loadFailed = true;
        return false;
    }
    return true;
}

namespace {
    // 索引の1行(yomi \t byteOffset)を読む。空行・形式違いはfalse
    bool ReadIndexLine(FsFile& f, char* line, size_t cap, bool& eof, char*& yomi, const char*& offset) {
        int len = f.fgets(line, cap);
        if (len <= 0) { eof = true; return false; }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) return false;
        char* tab = strchr(line, '\t');
        if (!tab) return false;
        *tab = '\0';
        yomi = line;
        offset = tab + 1;
        return true;
    }
}

bool ImeDictionary::loadIndex(const char* indexPath) {
    FsFile idxFile = _sd->open(indexPath, O_RDONLY);
    if (!idxFile) {
        return false;
    }

    char line[IME_MAX_LINE_BYTES];
    char* yomiStr;
    const char* offsetStr;
    bool eof = false;

    // 1周目で件数を数え、その分だけ確保する
    int count = 0;
    while (!eof && count < IME_MAX_INDEX_ENTRIES) {
        if (ReadIndexLine(idxFile, line, sizeof(line), eof, yomiStr, offsetStr)) count++;
    }
    if (count == 0 || !idxFile.seekSet(0)) {
        idxFile.close();
        return false;
    }
    _index = static_cast<ImeIndexEntry*>(malloc(sizeof(ImeIndexEntry) * count));
    if (!_index) {
        idxFile.close();
        return false;
    }

    _indexCount = 0;
    eof = false;
    while (!eof && _indexCount < count) {
        if (!ReadIndexLine(idxFile, line, sizeof(line), eof, yomiStr, offsetStr)) continue;
        ImeIndexEntry &entry = _index[_indexCount];
        strncpy(entry.yomi, yomiStr, sizeof(entry.yomi) - 1);
        entry.yomi[sizeof(entry.yomi) - 1] = '\0';
        entry.offset = (uint32_t)strtoul(offsetStr, nullptr, 10);
        _indexCount++;
    }

    idxFile.close();
    if (_indexCount == 0) {
        free(_index);
        _index = nullptr;
        return false;
    }
    return true;
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
    if (!ensureLoaded()) return 0;
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
