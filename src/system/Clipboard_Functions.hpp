#pragma once
#include <cstddef>
#include <cstdint>

// OS全体で1つのクリップボード(テキストだけ)。
//
// テキストエディタのコピー/切り取り/貼り付け、オンスクリーンキーボードを開いている入力欄での
// Ctrl+C / Ctrl+X / Ctrl+V(物理キーボード)、Luaの pico.clipboard_get/set が同じものを使う。
//
// - 中身は kMaxBytes(1KiB)まで。**入れた大きさだけをmallocで持ち**、空にしたら返す
//   (使わない間はRAMを1バイトも持たない。常駐の静的バッファにはしない)
// - 入れるときに '\r' と NUL を捨て(改行は'\n'だけにする)、上限で切るときはUTF-8の文字の途中で切らない
// - 再起動で消える(SDには書かない)
namespace ClipboardFunctions {
    constexpr size_t kMaxBytes = 1024;

    // 中身を差し替える。n==0なら空にする。
    // 上限で切った/確保できなかったらfalse(確保できなかったときは前の中身のまま)
    bool Set(const char* text, size_t n);
    bool Set(const char* text);
    // 今の中身(終端付き。空なら"")
    const char* Get();
    size_t Length();
    bool Empty();
    void Clear();
    // 中身が変わるたびに1つ増える
    uint32_t Revision();
}
