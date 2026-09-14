#pragma once

#include "Arduino.h"
#include "util/FixedString.hpp"
#include "consts.hpp"

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

    /**
     * パスを正規化する
     *
     * "docs/./a/../b.md" -> "/docs/b.md" のように "." と ".." を畳み、
     * 連続するスラッシュを1つにまとめる。結果は必ず "/" で始まる。
     *
     * ルートを超える ".." は捨てる(ブラウザと同じ安全側の扱い。
     * "/../../etc" のような参照でSDのルート外へ出させないため)。
     *
     * セグメント数が上限を超える場合や、bufferへ収まらない場合はfalseを返す。
     */
    inline bool normalize(FixedString<PICO_PATH_LEN>& buffer, const char* path)
    {
        if (!path) return false;

        // 畳んだ後に残るセグメントの最大数。パス長の上限(255B)に対して十分な数で、
        // 1セグメント1文字("/a/b/c...")でも足りる範囲に収めてある
        constexpr int kMaxSegments = 32;

        const char* segStart[kMaxSegments];
        int segLen[kMaxSegments];
        int segCount = 0;

        const char* p = path;
        while (*p)
        {
            // 連続するスラッシュはまとめて読み飛ばす
            while (*p == '/') p++;
            if (!*p) break;

            const char* start = p;
            while (*p && *p != '/') p++;
            const int len = (int)(p - start);

            // "." は現在位置なので捨てる
            if (len == 1 && start[0] == '.') continue;

            // ".." は1つ戻る。戻る先が無い(ルート)場合は捨てる
            if (len == 2 && start[0] == '.' && start[1] == '.')
            {
                if (segCount > 0) segCount--;
                continue;
            }

            if (segCount >= kMaxSegments) return false;

            segStart[segCount] = start;
            segLen[segCount] = len;
            segCount++;
        }

        buffer.clear();

        // 全部消えた場合はルート
        if (segCount == 0) return buffer.assign("/");

        for (int i = 0; i < segCount; i++)
        {
            if (!buffer.append("/")) return false;
            if (!buffer.append(segStart[i], (size_t)segLen[i])) return false;
        }

        return true;
    }

    /**
     * 文書base_docから見た参照refを絶対パスへ解決する(一般的なブラウザと同じ規則)
     *
     *   resolve(out, "/docs/pico/intro.md", "gpio.md")     -> "/docs/pico/gpio.md"
     *   resolve(out, "/docs/pico/intro.md", "../setup.md") -> "/docs/setup.md"
     *   resolve(out, "/docs/pico/intro.md", "/index.md")   -> "/index.md"
     *
     * refが "/" 始まりならルート基準、それ以外はbase_docのあるディレクトリ基準。
     * base_docにはディレクトリではなく「文書自身のパス」を渡すこと。
     */
    inline bool resolve(FixedString<PICO_PATH_LEN>& buffer, const char* base_doc, const char* ref)
    {
        if (!ref || ref[0] == '\0') return false;

        // ルート基準の参照はそのまま畳むだけでよい
        if (ref[0] == '/') return normalize(buffer, ref);

        // 相対参照は「base_docが置かれているディレクトリ」を基準にする。
        // base_docは文書自身のパスなので、まず親ディレクトリを取る
        char dir[PICO_PATH_LEN];
        if (!parent(dir, (base_doc && base_doc[0] != '\0') ? base_doc : "/")) return false;

        // join()は ".." をそのまま繋ぐだけなので、この時点では "/docs/pico/../a.md" の
        // ような形になっている。畳むのは下のnormalize()の仕事
        char joined[PICO_PATH_LEN];
        if (!join(joined, dir, ref)) return false;

        return normalize(buffer, joined);
    }

    template <size_t N>
    inline bool removeRecursive(const FixedString<N>& path)
    {
        return removeRecursive(path.c_str());
    }
}
