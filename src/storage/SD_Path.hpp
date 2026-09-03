#pragma once

namespace PICO_Path {
    namespace DIR {
        constexpr const char* SYS = "/sys/";
        constexpr const char* SYS_IME = "/sys/ime/";
    };
    namespace FILE {
        constexpr const char* SYS_LOG_TXT = "/sys/log.txt";

        constexpr const char* TMP_TOFU_TXT = "/tmp/tofu-chars.txt";

        namespace CFG {
            constexpr const char* SYS_NETWORK_CFG = "/sys/network.cfg";
            constexpr const char* SYS_USER_CFG = "/sys/user.cfg";
        }

        namespace IME {            
            constexpr const char* IME_SKK_BODY = "/sys/ime/skk_body.tsv";
            constexpr const char* IME_SKK_INDEX = "/sys/ime/skk_index.tsv";   
        }
    };

    /**
     * パスを結合する
     *
     * 例:
     *   join(buf, "/test1", "test2")  -> "/test1/test2"
     *   join(buf, "/test1/", "test2") -> "/test1/test2"
     *   join(buf, "/test1", "/test2") -> "/test1/test2"
     *   join(buf, "/", "test2")       -> "/test2"
     *
     * 入力と出力に同じバッファを指定可能。
     */
    template <size_t N>
    inline bool join(char (&buffer)[N], const char* base, const char* name)
    {
        char temp[N];

        size_t baseLen = strlen(base);
        size_t nameStart = 0;

        // base末尾の '/' を除去
        while (baseLen > 1 && base[baseLen - 1] == '/')
            --baseLen;

        // name先頭の '/' を除去
        while (name[nameStart] == '/')
            ++nameStart;

        size_t nameLen = strlen(name + nameStart);

        // "/" + name
        size_t totalLen;

        if (baseLen == 1 && base[0] == '/')
            totalLen = 1 + nameLen;
        else
            totalLen = baseLen + 1 + nameLen;

        // '\0' の分も含めてチェック
        if (totalLen + 1 > N)
            return false;

        if (baseLen == 1 && base[0] == '/')
        {
            temp[0] = '/';
            memcpy(temp + 1, name + nameStart, nameLen);
            temp[totalLen] = '\0';
        }
        else
        {
            memcpy(temp, base, baseLen);
            temp[baseLen] = '/';
            memcpy(temp + baseLen + 1, name + nameStart, nameLen);
            temp[totalLen] = '\0';
        }

        memcpy(buffer, temp, totalLen + 1);

        return true;
    }


    /**
     * 親ディレクトリを取得する
     *
     * 例:
     *   parent("/test1/test2/test3") -> "/test1/test2"
     *   parent("/test1/test2")       -> "/test1"
     *   parent("/test1")             -> "/"
     *   parent("/")                  -> "/"
     *
     * 入力と出力に同じバッファを指定可能。
     */
    template <size_t N>
    inline bool parent(char (&buffer)[N], const char* path)
    {
        char temp[N];

        size_t len = strlen(path);

        // ルートならそのまま
        if (len == 0 || (len == 1 && path[0] == '/'))
        {
            if (N < 2)
                return false;

            temp[0] = '/';
            temp[1] = '\0';

            memcpy(buffer, temp, 2);
            return true;
        }

        // 末尾の '/' を除去
        while (len > 1 && path[len - 1] == '/')
            --len;

        // 最後の '/' を探す
        while (len > 1 && path[len - 1] != '/')
            --len;

        // "/" 直下の場合
        if (len == 1)
        {
            temp[0] = '/';
            temp[1] = '\0';

            memcpy(buffer, temp, 2);
            return true;
        }

        // 最後の '/' 自体を除去
        --len;

        if (len + 1 > N)
            return false;

        memcpy(temp, path, len);
        temp[len] = '\0';

        memcpy(buffer, temp, len + 1);

        return true;
    }

    /**
     * パスから最後のファイル・フォルダ名を取得する
     *
     * 例:
     *   filename("/test1/test2/test3") -> "test3"
     *   filename("/test1/test2.txt")   -> "test2.txt"
     *   filename("/test1")             -> "test1"
     *   filename("/")                  -> ""
     *
     * 戻り値は path 内を指すため、path が有効な間だけ使用可能。
     */
    inline const char* filename(const char* path)
    {
        size_t len = strlen(path);

        // 末尾の '/' を除去して考える
        while (len > 1 && path[len - 1] == '/')
            --len;

        // 最後の '/' を探す
        while (len > 0 && path[len - 1] != '/')
            --len;

        return path + len;
    }
};