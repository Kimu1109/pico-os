#pragma once

#include <SdFat.h>
#include <cstring>
#include <cstdlib>

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

// ============================================================================
// 設定ファイル(key=value形式)パーサー
//
// 仕様概要(詳細は仕様書を参照):
//   - セクションなし、1行1設定項目
//   - コメントは行頭の '#'(前後の空白はスキップして判定)
//   - 文字コードはUTF-8(BOMなし)、改行はLF/CRLF両対応
//   - キー重複時は「後勝ち」(コールバックが呼ばれた順に上書きすることで実現)
//   - 値の型変換(bool/int/float)はパース時ではなく、取得時に失敗を返す
//
// 動的メモリ確保を避けるため、1行ずつ固定バッファへ読み込んで処理する。
// ============================================================================

namespace PICO_Config
{
    // --------------------------------------------------------------------
    // バッファ長(用途: Wi-Fi SSID/パスワード, app.iniの住所的キー名など)
    // 不足した場合はここを拡張する。
    // --------------------------------------------------------------------
    constexpr size_t kConfigMaxLineLen = 192;  // SDから読み込む1行分の生バッファ
    constexpr size_t kConfigMaxKeyLen = 64;    // 住所的記法のキー名を想定
    constexpr size_t kConfigMaxValueLen = 128; // パスワードやパスに余裕を持たせる

    // --------------------------------------------------------------------
    // 1行の解析結果
    // --------------------------------------------------------------------
    enum class ConfigLineResult
    {
        Entry,           // 有効な key=value の行
        CommentOrEmpty,  // コメント行、または空行(エラーではない)
        ErrorNoEquals,   // '=' が見つからない
        ErrorEmptyKey,   // キー部分が空
        ErrorKeyTooLong, // キーが kConfigMaxKeyLen を超える
        ErrorValueTooLong, // 値が kConfigMaxValueLen を超える
    };

    // --------------------------------------------------------------------
    // 1行を解析して key / value を抽出する。
    //
    // 注意: line は破壊的に扱われないが、内部で先頭/末尾の空白を
    //       スキップするためのポインタ操作のみ行う(呼び出し側のバッファは変更しない)。
    //
    // outKey / outValue には、成功時(Entry)のみ有効な文字列が書き込まれる。
    // --------------------------------------------------------------------
    inline ConfigLineResult ParseLine(
        const char *line,
        char *outKey, size_t keyCap,
        char *outValue, size_t valueCap)
    {
        // 先頭の空白・タブをスキップ
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;

        // 空行 or コメント行(行頭 '#'。前の空白はスキップ済み)
        if (*p == '\0' || *p == '\r' || *p == '\n' || *p == '#')
        {
            return ConfigLineResult::CommentOrEmpty;
        }

        // '=' を検索(最初の1つのみを区切りとして使う。値の中の'='はそのまま値に含まれる)
        const char *eq = strchr(p, '=');
        if (eq == nullptr)
        {
            return ConfigLineResult::ErrorNoEquals;
        }

        // --- キー部分の抽出(前後の空白をトリム) ---
        const char *keyStart = p;
        const char *keyEnd = eq; // exclusive
        while (keyEnd > keyStart && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t'))
            --keyEnd;

        size_t keyLen = (size_t)(keyEnd - keyStart);
        if (keyLen == 0)
        {
            return ConfigLineResult::ErrorEmptyKey;
        }
        if (keyLen >= keyCap)
        {
            return ConfigLineResult::ErrorKeyTooLong;
        }
        memcpy(outKey, keyStart, keyLen);
        outKey[keyLen] = '\0';

        // --- 値部分の抽出(前後の空白・改行をトリム) ---
        const char *valueStart = eq + 1;
        while (*valueStart == ' ' || *valueStart == '\t')
            ++valueStart;

        const char *valueEnd = valueStart + strlen(valueStart);
        while (valueEnd > valueStart)
        {
            char c = valueEnd[-1];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                --valueEnd;
            else
                break;
        }

        size_t valueLen = (size_t)(valueEnd - valueStart);
        if (valueLen >= valueCap)
        {
            return ConfigLineResult::ErrorValueTooLong;
        }
        memcpy(outValue, valueStart, valueLen);
        outValue[valueLen] = '\0';

        return ConfigLineResult::Entry;
    }

    // --------------------------------------------------------------------
    // デフォルトのエラーハンドラ(何もしない)
    // --------------------------------------------------------------------
    inline void DefaultConfigErrorHandler(int /*lineNo*/, const char * /*rawLine*/, ConfigLineResult /*result*/) {}

    // --------------------------------------------------------------------
    // 設定ファイルをSDから読み込み、1行ずつ解析してコールバックへ渡す。
    //
    // OnEntry: void(const char* key, const char* value) を満たす呼び出し可能オブジェクト。
    //          同じキーが複数回出現した場合、呼ばれる順序はファイル中の出現順のため、
    //          呼び出し側が単純に構造体メンバへ代入するだけで「後勝ち」が実現される。
    //
    // OnError: void(int lineNo, const char* rawLine, ConfigLineResult result) を満たす
    //          呼び出し可能オブジェクト。省略時は無視される。
    //
    // 戻り値: ファイルを開けなかった場合のみ false。行単位のエラーは OnError 経由で通知される。
    // --------------------------------------------------------------------
    template <typename OnEntry, typename OnError>
    inline bool ParseFile(const char *path, OnEntry &&onEntry, OnError &&onError)
    {
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if (!f)
        {
            LOG_SYS_FAIL("Couldn't open config file: %s", path);
            return false;
        }

        char lineBuf[kConfigMaxLineLen];
        char keyBuf[kConfigMaxKeyLen];
        char valueBuf[kConfigMaxValueLen];

        int lineNo = 0;
        while (true)
        {
            // fgetsは改行文字を含めてバッファへ格納する(末尾の\r\nはParseLine側でトリムする)。
            // 1行が kConfigMaxLineLen を超える場合、残りは次のfgets呼び出しで
            // 別の"行"として読み込まれてしまう点に注意(既知の制約)。
            int n = f.fgets(lineBuf, sizeof(lineBuf));
            if (n <= 0)
            {
                break; // EOF またはエラー
            }
            ++lineNo;

            ConfigLineResult result = ParseLine(lineBuf, keyBuf, sizeof(keyBuf), valueBuf, sizeof(valueBuf));

            switch (result)
            {
            case ConfigLineResult::Entry:
                onEntry(keyBuf, valueBuf);
                break;
            case ConfigLineResult::CommentOrEmpty:
                break;
            default:
                onError(lineNo, lineBuf, result);
                break;
            }
        }

        f.close();
        return true;
    }

    // OnErrorを省略した場合のオーバーロード(行単位のエラーは無視される)
    template <typename OnEntry>
    inline bool ParseFile(const char *path, OnEntry &&onEntry)
    {
        return ParseFile(path, onEntry, DefaultConfigErrorHandler);
    }

    // --------------------------------------------------------------------
    // 値の型変換ヘルパー(15章: 「取得API呼び出し時にパース失敗ならエラーを返す」の実装)
    // すべて成功時 true / 失敗時 false を返し、out引数には成功時のみ書き込む。
    // --------------------------------------------------------------------
    namespace ConfigValue
    {
        // 真偽値: "true" / "false" の完全一致のみ受理(大文字・小文字を区別)
        inline bool AsBool(const char *value, bool &out)
        {
            if (strcmp(value, "true") == 0)
            {
                out = true;
                return true;
            }
            if (strcmp(value, "false") == 0)
            {
                out = false;
                return true;
            }
            return false;
        }

        // 整数: 10進数のみ。"0xFF"のような16進表記は endptr が末尾に到達しないため自動的に拒否される。
        inline bool AsInt(const char *value, int &out)
        {
            if (value[0] == '\0')
                return false;

            char *endPtr = nullptr;
            long v = strtol(value, &endPtr, 10);
            if (endPtr == value || *endPtr != '\0')
                return false;

            out = (int)v;
            return true;
        }

        // 小数: 指数表記("1.0e-5"等)は仕様上禁止のため、変換前に 'e'/'E' の有無を検査して弾く。
        inline bool AsFloat(const char *value, float &out)
        {
            if (value[0] == '\0')
                return false;

            for (const char *p = value; *p != '\0'; ++p)
            {
                if (*p == 'e' || *p == 'E')
                    return false;
            }

            char *endPtr = nullptr;
            double v = strtod(value, &endPtr);
            if (endPtr == value || *endPtr != '\0')
                return false;

            out = (float)v;
            return true;
        }
    }
}
