// src/util/FixedString.hpp
//
// 固定長・ヒープ非使用の文字列クラス。
// - 内部は char buf_[N] のみ(new/delete不使用、断片化しない)
// - String相当の操作(結合・部分削除・UTF-8文字数カウント・切り出し等)を安全に提供
// - 既存の char[33]/char[128] 等の固定長バッファ運用をそのまま置き換えられる
//
// 依存関係について:
//   UTF-8の文字列操作機能(文字数・オフセット・切り出し・削除・置換等)は
//   本クラスが担当し、functions/UTF8_Functions.hpp にはエンコード/デコードのみを置く。
//   FixedString.hpp 側は UTF8_Functions.hpp に依存してはいけない(循環include回避)。
//   最下層のリードバイト判定ロジック(util/Utf8Byte.hpp)のみを利用する。
//
// 使い方の基本方針:
//   - assign/append は「切り詰めが発生したか」をboolで返すので、
//     呼び出し側は戻り値を無視しないこと(黙って切り詰めるとバグの温床になる)
//   - UTF-8境界を跨いで文字が欠けることがないよう、バイト単位の操作は
//     すべて継続バイト(10xxxxxx)を巻き戻すガードを入れてある

#pragma once
#include <Arduino.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include "util/Utf8Byte.hpp"

template<size_t N>
class FixedString {
    static_assert(N >= 1, "FixedString: N must be at least 1 (for null terminator)");

public:
    FixedString() { buf_[0] = '\0'; }
    explicit FixedString(const char* src) { assign(src); }

    // ============ 長さキャッシュについて ============
    // length()はホットパス(1文字ずつのappendループ等)で毎回呼ばれるため、
    // 呼び出しの都度 strlen(buf_) していると蓄積済みバイト数に比例して
    // コストが増え、1文字ずつのappendループ全体がO(n^2)になってしまう。
    // そのため現在のバイト長をlen_としてメンバに保持し、バッファを変更する
    // 全メソッド(append/assign/insert/remove系)で追従更新する。
    // 「len_はbuf_の実際のNUL終端位置と常に一致する」という不変条件を破らないこと。

    // ============ static UTF-8 ヘルパー ============

    // 生バッファに対するUTF-8「文字数」(バイト数ではない)を数える
    static int charCount(const char* str) {
        if (!str) return 0;
        int count = 0;
        for (size_t i = 0; str[i] != '\0'; ) {
            i += Utf8CharBytesFromLeadByte(static_cast<uint8_t>(str[i]));
            count++;
        }
        return count;
    }

    // 生バッファに対する文字インデックス(0=先頭)からのバイトオフセット計算
    // charIndex <= 0 なら 0、文字数を超える場合は末尾バイトオフセット(strlen)を返す
    static int byteOffsetOfChar(const char* str, int charIndex) {
        if (!str || charIndex <= 0) return 0;
        int count = 0;
        int i = 0;
        while (str[i] != '\0') {
            if (count == charIndex) return i;
            i += Utf8CharBytesFromLeadByte(static_cast<uint8_t>(str[i]));
            count++;
        }
        return i;
    }

    // 生バッファから末尾の1文字を切り出す(UTF-8考慮)
    static FixedString<5> lastChar(const char* str) {
        FixedString<5> result;
        if (!str) return result;
        int totalBytes = static_cast<int>(strlen(str));
        if (totalBytes == 0) return result;

        int lastCharStart = totalBytes - 1;
        while (lastCharStart > 0 && ((static_cast<uint8_t>(str[lastCharStart]) & 0xC0) == 0x80)) {
            lastCharStart--;
        }
        result.append(str + lastCharStart);
        return result;
    }

    // 生バッファから先頭の1文字を切り出す(UTF-8考慮)
    static FixedString<5> firstChar(const char* str) {
        FixedString<5> result;
        if (!str || str[0] == '\0') return result;

        int charLen = Utf8CharBytesFromLeadByte(static_cast<uint8_t>(str[0]));
        int totalLen = static_cast<int>(strlen(str));
        if (charLen > totalLen) charLen = totalLen;

        result.appendUtf8Char(str, charLen);
        return result;
    }

    // ============ 代入 ============

    // 全置換。Nを超える分は切り詰め、切り詰めが発生した場合はfalseを返す
    bool assign(const char* src) {
        clear();
        return append(src);
    }

    // 別サイズのFixedString同士でも代入できるようにするオーバーロード
    template<size_t M>
    bool assign(const FixedString<M>& other) {
        return assign(other.c_str());
    }

    // ============ 追記 ============

    // 末尾にconst char*を追記する。
    // 容量超過でUTF-8マルチバイト文字の途中バイトで切れそうな場合は
    // 文字の開始バイトまで巻き戻してから切り詰める(文字を欠けさせない)。
    bool append(const char* src) {
        if (!src) return true;
        size_t curLen = len_;
        size_t room = (curLen < N - 1) ? (N - 1 - curLen) : 0;
        size_t srcLen = strlen(src);
        size_t addLen = (srcLen < room) ? srcLen : room;

        // 継続バイト(10xxxxxx)の途中で切れていたら開始バイトまで巻き戻す
        while (addLen > 0 && ((static_cast<uint8_t>(src[addLen]) & 0xC0) == 0x80)) {
            addLen--;
        }

        memcpy(buf_ + curLen, src, addLen);
        buf_[curLen + addLen] = '\0';
        len_ = curLen + addLen;
        return addLen == srcLen;
    }

    // 別サイズのFixedString同士でも結合できるようにするオーバーロード
    template<size_t M>
    bool append(const FixedString<M>& other) {
        return append(other.c_str());
    }

    // 1文字(ASCII)を追記する
    bool append(char c) {
        const char tmp[2] = { c, '\0' };
        return append(tmp);
    }

    // srcの先頭lenバイトだけを追記する(範囲指定でのsubstring切り出しに使う)。
    // src は len バイト読めれば十分で、NUL終端されていなくてもよい
    // (addLen == len、つまり切り詰めが発生しない場合は src[len] を読まない)。
    // 容量超過で切り詰めが発生した場合のみ、UTF-8継続バイトの途中で
    // 終わらないよう開始バイトまで巻き戻す。
    bool append(const char* src, size_t len) {
        if (!src) return true;
        size_t curLen = len_;
        size_t room = (curLen < N - 1) ? (N - 1 - curLen) : 0;
        size_t addLen = (len < room) ? len : room;

        if (addLen < len) {
            while (addLen > 0 && ((static_cast<uint8_t>(src[addLen]) & 0xC0) == 0x80)) {
                addLen--;
            }
        }

        memcpy(buf_ + curLen, src, addLen);
        buf_[curLen + addLen] = '\0';
        len_ = curLen + addLen;
        return addLen == len;
    }

    // 全置換版(srcの先頭lenバイトだけを使う)
    bool assign(const char* src, size_t len) {
        clear();
        return append(src, len);
    }

    // printf書式で末尾に追記する(vsnprintfでbuf_の残り容量へ直接書き込むため、
    // sprintf用の一時バッファを呼び出し側で用意する必要がない)。
    // 容量不足で切り詰められた場合はfalseを返す(errorまたは収まりきらない場合)。
    bool appendFormatV(const char* fmt, va_list args) {
        size_t curLen = len_;
        if (curLen >= N - 1) return false;
        int written = vsnprintf(buf_ + curLen, N - curLen, fmt, args);
        if (written < 0) {
            buf_[curLen] = '\0';
            return false;
        }
        size_t avail = N - curLen; // NUL終端分込みの残り容量
        bool ok = (size_t)written < avail;
        // vsnprintfは切り詰め時、収まりきらなかった分もNUL込みで書き込まないため、
        // 実際に書き込まれたバイト数はok時はwritten、切り詰め時はavail-1(NUL手前まで)
        len_ = curLen + (ok ? (size_t)written : (avail - 1));
        return ok;
    }

    bool appendFormat(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        bool ok = appendFormatV(fmt, args);
        va_end(args);
        return ok;
    }

    // UTF-8を1文字単位で安全に追記する(KeyboardNum/KeyboardEng等、
    // カーソル位置への1文字ずつの入力を想定)。
    // 容量が足りない場合は1文字も追記せずfalseを返す(文字が半端に入るのを防ぐ)。
    bool appendUtf8Char(const char* utf8Bytes, int byteLen) {
        if (!utf8Bytes || byteLen <= 0) return true;
        size_t curLen = len_;
        if (curLen + static_cast<size_t>(byteLen) >= N) return false;
        memcpy(buf_ + curLen, utf8Bytes, byteLen);
        buf_[curLen + byteLen] = '\0';
        len_ = curLen + static_cast<size_t>(byteLen);
        return true;
    }

    // ============ 削除・置換 ============

    // カーソル位置(UTF-8文字インデックス、0始まり)の1文字を削除する。
    // 範囲外指定の場合は何もせずfalseを返す。
    bool removeCharAt(int charIndex) {
        if (charIndex < 0) return false;

        int totalBytes = static_cast<int>(length());
        int byteStart = byteOffsetOfChar(charIndex);
        if (byteStart >= totalBytes) return false;

        int byteEnd = byteOffsetOfChar(charIndex + 1);
        memmove(buf_ + byteStart, buf_ + byteEnd, totalBytes - byteEnd + 1); // +1で終端\0も込みで移動
        len_ -= static_cast<size_t>(byteEnd - byteStart);
        return true;
    }

    // 末尾1文字を削除する(バックスペース用)
    bool removeLastChar() {
        int totalBytes = static_cast<int>(length());
        if (totalBytes == 0) return false;

        // 末尾から手前へ、継続バイト(10xxxxxx)の間は巻き戻して文字の開始位置を探す
        int lastCharStart = totalBytes - 1;
        while (lastCharStart > 0 && ((static_cast<uint8_t>(buf_[lastCharStart]) & 0xC0) == 0x80)) {
            lastCharStart--;
        }
        buf_[lastCharStart] = '\0';
        len_ = static_cast<size_t>(lastCharStart);
        return true;
    }

    // 末尾1文字を別の文字列に置換する(in-place)
    template<size_t M>
    bool replaceLastChar(const FixedString<M>& newText) {
        removeLastChar();
        return append(newText);
    }

    bool replaceLastChar(const char* newText) {
        removeLastChar();
        return append(newText);
    }

    // ============ 挿入 ============

    // バイトオフセット位置に文字列を挿入する
    bool insert(size_t byteOffset, const char* src) {
        if (!src) return true;
        size_t curLen = len_;
        if (byteOffset > curLen) byteOffset = curLen;
        size_t srcLen = strlen(src);
        size_t room = (curLen < N - 1) ? (N - 1 - curLen) : 0;
        size_t addLen = (srcLen < room) ? srcLen : room;

        while (addLen > 0 && ((static_cast<uint8_t>(src[addLen]) & 0xC0) == 0x80)) {
            addLen--;
        }

        memmove(buf_ + byteOffset + addLen, buf_ + byteOffset, curLen - byteOffset + 1);
        memcpy(buf_ + byteOffset, src, addLen);
        len_ = curLen + addLen;
        return addLen == srcLen;
    }

    template<size_t M>
    bool insert(size_t byteOffset, const FixedString<M>& other) {
        return insert(byteOffset, other.c_str());
    }

    // 文字インデックス(0始まり)位置に文字列を挿入する
    bool insertAtChar(int charIndex, const char* src) {
        return insert(static_cast<size_t>(byteOffsetOfChar(charIndex)), src);
    }

    template<size_t M>
    bool insertAtChar(int charIndex, const FixedString<M>& other) {
        return insertAtChar(charIndex, other.c_str());
    }

    // ============ 演算子 ============

    FixedString& operator=(const char* src) {
        assign(src);
        return *this;
    }
    template<size_t M>
    FixedString& operator=(const FixedString<M>& other) {
        assign(other.c_str());
        return *this;
    }
    FixedString& operator+=(const char* src) {
        append(src);
        return *this;
    }
    template<size_t M>
    FixedString& operator+=(const FixedString<M>& other) {
        append(other.c_str());
        return *this;
    }

    void clear() { buf_[0] = '\0'; len_ = 0; }

    // ============ 参照・切り出し ============

    const char* c_str() const { return buf_; }
    bool empty() const { return buf_[0] == '\0'; }

    size_t length() const { return len_; } // バイト数(キャッシュ済みなのでO(1))

    // 文字インデックスではなくバイトインデックスでの1バイト参照(範囲外は'\0')
    char operator[](size_t byteIndex) const {
        return (byteIndex < length()) ? buf_[byteIndex] : '\0';
    }

    // 文字cをfromIndex(バイト位置)以降から探し、見つかったバイト位置を返す(無ければ-1)
    int indexOf(char c, int fromIndex = 0) const {
        int len = static_cast<int>(length());
        if (fromIndex < 0) fromIndex = 0;
        for (int i = fromIndex; i < len; i++) {
            if (buf_[i] == c) return i;
        }
        return -1;
    }

    // UTF-8文字数(バイト数ではない)
    int charCount() const { return charCount(buf_); }

    // 文字インデックス(0始まり)から対応するバイトオフセットを返す
    int byteOffsetOfChar(int charIndex) const { return byteOffsetOfChar(buf_, charIndex); }

    // 文字列末尾の1文字を切り出す(UTF-8考慮)
    FixedString<5> lastChar() const { return lastChar(buf_); }

    // 文字列先頭の1文字を切り出す(UTF-8考慮)
    FixedString<5> firstChar() const { return firstChar(buf_); }

    // ============ 判定 ============

    // アルファベット1文字(ASCII)かどうかの判定
    bool isAsciiAlpha() const {
        return length() == 1 && isalpha(static_cast<unsigned char>(buf_[0]));
    }

    static constexpr size_t capacity() { return N - 1; } // 格納可能な最大バイト数(終端\0除く)

    // ============ 比較 ============

    bool operator==(const char* other) const {
        if (!other) return empty();
        return strcmp(buf_, other) == 0;
    }
    bool operator!=(const char* other) const { return !(*this == other); }

    template<size_t M>
    bool operator==(const FixedString<M>& other) const { return strcmp(buf_, other.c_str()) == 0; }
    template<size_t M>
    bool operator!=(const FixedString<M>& other) const { return strcmp(buf_, other.c_str()) != 0; }

private:
    char buf_[N];
    size_t len_ = 0; // buf_の現在のバイト長(strlen(buf_)と常に一致するキャッシュ)
};
