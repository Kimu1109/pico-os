#pragma once

#include "Arduino.h"
#include "util/FixedString.hpp"

namespace PICO_IO {
    /**
     * パスを結合する(生char配列版・従来実装)
     *
     * 例:
     *   join(buffer, "/test1", "test2")  -> "/test1/test2"
     *   join(buffer, "/test1/", "test2") -> "/test1/test2"
     *   join(buffer, "/test1", "/test2") -> "/test1/test2"
     *   join(buffer, "/", "test2")       -> "/test2"
     *
     * 入力(base)と出力(buffer)に同じ実体を指定可能(内部で一時バッファ経由するため)。
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
     * パスを結合する(FixedString版)
     *
     * 実装はchar配列版へ委譲し、結果をbuffer.assign()で書き戻す。
     * baseにbuffer自身(の.c_str())を渡すエイリアス呼び出しも従来通り安全。
     */
    template <size_t N>
    inline bool join(FixedString<N>& buffer, const char* base, const char* name)
    {
        char temp[N];
        if (!join(temp, base, name)) return false;
        return buffer.assign(temp);
    }

    template <size_t N, size_t M>
    inline bool join(FixedString<N>& buffer, const FixedString<M>& base, const char* name)
    {
        return join(buffer, base.c_str(), name);
    }

    /**
     * 親ディレクトリを取得する(生char配列版・従来実装)
     *
     * 例:
     *   parent(buffer, "/test1/test2/test3") -> "/test1/test2"
     *   parent(buffer, "/test1/test2")       -> "/test1"
     *   parent(buffer, "/test1")             -> "/"
     *   parent(buffer, "/")                  -> "/"
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
     * 親ディレクトリを取得する(FixedString版)
     */
    template <size_t N>
    inline bool parent(FixedString<N>& buffer, const char* path)
    {
        char temp[N];
        if (!parent(temp, path)) return false;
        return buffer.assign(temp);
    }

    template <size_t N>
    inline bool parent(FixedString<N>& buffer, const FixedString<N>& path)
    {
        return parent(buffer, path.c_str());
    }

    /**
     * パスから最後のファイル・フォルダ名を取得する(生char*版・従来実装)
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

    /**
     * パスから最後のファイル・フォルダ名を取得する(FixedString版)
     *
     * 戻り値は path の内部バッファを指すため、path が有効な間だけ使用可能。
     */
    template <size_t N>
    inline const char* filename(const FixedString<N>& path)
    {
        return filename(path.c_str());
    }

    // 再帰的にファイル/フォルダを削除する。path は内部で256バイトのFixedStringを
    // 組み立てながら再帰するため、パス階層の深さに実質的な制約はあるが、
    // 通常のSDカード運用では十分な余裕がある。
    bool removeRecursive(const char* path);

    template <size_t N>
    inline bool removeRecursive(const FixedString<N>& path)
    {
        return removeRecursive(path.c_str());
    }
}
