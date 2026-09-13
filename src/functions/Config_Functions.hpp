#pragma once

#include <SdFat.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "consts.hpp"
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
    // 書き込み
    //
    // 読み込みと同じく1行ずつ処理し、一時ファイルへ書き出してから差し替える。
    // 元ファイルを直接書き換えないので、途中で電源が落ちても設定が壊れない。
    // (removeとrenameの間で落ちた場合は .tmp が残る。その場合は設定が
    //  前回値のまま残るだけで、読み込み側に影響はない)
    // --------------------------------------------------------------------
    namespace Detail
    {
        // 元の行をそのまま書き戻す。末尾に改行が無い行(ファイル最終行)には補う。
        // 補わないと、この後ろにエントリを追記したときに連結してしまう
        inline bool WriteRawLine(FsFile &f, const char *line)
        {
            const size_t len = strlen(line);
            if (len > 0 && f.write(line, len) != len) return false;
            if (len == 0 || line[len - 1] != '\n')
            {
                if (f.write("\n", 1) != 1) return false;
            }
            return true;
        }

        inline bool WriteEntry(FsFile &f, const char *key, const char *value)
        {
            char buf[kConfigMaxKeyLen + kConfigMaxValueLen + 4];
            const int n = snprintf(buf, sizeof(buf), "%s=%s\n", key, value);
            if (n <= 0 || n >= (int)sizeof(buf)) return false;
            return f.write(buf, (size_t)n) == (size_t)n;
        }
    }

    // --------------------------------------------------------------------
    // keyの値を書き換える。コメント行と行順はそのまま保たれる。
    //
    // - keyがファイルに無ければ末尾へ追記する
    // - ファイル自体が無ければ新規作成する
    // - 同じkeyが複数行ある場合は最初の1行を書き換え、以降の重複行は削除する
    //   (読み込み側が「後勝ち」なので、残すと書き換えたはずの値が上書きされてしまう)
    //
    // 戻り値: 書き換えが完了したら true
    // --------------------------------------------------------------------
    inline bool SetValue(const char *path, const char *key, const char *value)
    {
        if (!path || !key || !value) return false;
        if (key[0] == '\0')
        {
            LOG_SYS_WARN("Config SetValue: キーが空です (%s)", path);
            return false;
        }
        if (strlen(key) >= kConfigMaxKeyLen || strlen(value) >= kConfigMaxValueLen)
        {
            LOG_SYS_WARN("Config SetValue: キーか値が長すぎます (%s: %s)", path, key);
            return false;
        }

        char tmpPath[PICO_PATH_LEN];
        const int pathLen = snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
        if (pathLen <= 0 || pathLen >= (int)sizeof(tmpPath))
        {
            LOG_SYS_WARN("Config SetValue: パスが長すぎます (%s)", path);
            return false;
        }

        FsFile dst = OSData::SD.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
        if (!dst)
        {
            LOG_SYS_FAIL("Config SetValue: 一時ファイルを作成できません (%s)", tmpPath);
            return false;
        }

        bool ok = true;
        bool replaced = false;

        FsFile src = OSData::SD.open(path, O_RDONLY);
        if (src)
        {
            char lineBuf[kConfigMaxLineLen];
            char keyBuf[kConfigMaxKeyLen];
            char valueBuf[kConfigMaxValueLen];

            while (ok)
            {
                const int n = src.fgets(lineBuf, sizeof(lineBuf));
                if (n <= 0) break;

                const ConfigLineResult result =
                    ParseLine(lineBuf, keyBuf, sizeof(keyBuf), valueBuf, sizeof(valueBuf));

                if (result == ConfigLineResult::Entry && strcmp(keyBuf, key) == 0)
                {
                    if (!replaced)
                    {
                        ok = Detail::WriteEntry(dst, key, value);
                        replaced = true;
                    }
                    continue; // 2件目以降の重複行は落とす
                }

                ok = Detail::WriteRawLine(dst, lineBuf);
            }
            src.close();
        }

        if (ok && !replaced) ok = Detail::WriteEntry(dst, key, value);
        dst.close();

        if (!ok)
        {
            LOG_SYS_FAIL("Config SetValue: 書き込みに失敗しました (%s)", path);
            OSData::SD.remove(tmpPath);
            return false;
        }

        if (OSData::SD.exists(path) && !OSData::SD.remove(path))
        {
            LOG_SYS_FAIL("Config SetValue: 元ファイルを削除できません (%s)", path);
            OSData::SD.remove(tmpPath);
            return false;
        }
        if (!OSData::SD.rename(tmpPath, path))
        {
            LOG_SYS_FAIL("Config SetValue: 一時ファイルを差し替えられません (%s)", path);
            return false;
        }

        return true;
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

        // ------------------------------------------------------------
        // 書き込み用の逆変換。SetValue()へ渡す文字列を作る。
        // AsXxx()で読み戻せる表記になっていること(往復できること)が条件
        // ------------------------------------------------------------
        inline const char *FromBool(bool value)
        {
            return value ? "true" : "false";
        }

        inline bool FromInt(int value, char *out, size_t cap)
        {
            if (!out || cap == 0) return false;
            const int n = snprintf(out, cap, "%d", value);
            return n > 0 && (size_t)n < cap;
        }

        // 小数は既定で3桁。AsFloat()はstrtofなのでそのまま読み戻せる
        inline bool FromFloat(float value, char *out, size_t cap, int decimals = 3)
        {
            if (!out || cap == 0) return false;
            const int n = snprintf(out, cap, "%.*f", decimals, (double)value);
            return n > 0 && (size_t)n < cap;
        }
    }
}
