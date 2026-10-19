#pragma once

#include <cstddef>

// Markdownの軽い走査。
//
// ブロックパーサ(MarkdownView::parseBlocks)を通さずに「この行は画像を参照して
// いるか」だけを知りたい場面のためのもの。Markdownブラウザが**表示する前に
// 画像を取りに行く**のに使う。
//
// なぜ表示前に要るのか: MarkdownView::layoutBlocks() は画像ファイルのヘッダを
// 読んでブロックの高さを決めている。表示の時点でファイルが無いとその画像は
// 高さ4pxとしてレイアウトされ、後から届いても再レイアウトが必要になる。
//
// **判定規則は parseBlocks() の画像ブロックと必ず合わせること。**
// ずれると「取ってきたのに表示されない」「表示されるのに取ってこない」が起きる。
// 現在の規則:
//   - 行頭が "![" で始まる
//   - 同じ行の中に "(" と ")" がこの順で現れる
//   - 括弧の中身が参照
//
// SDにもOSData にも依存しないので、単体でホストテストできる。
// 文書を1行ずつ読む側の処理は呼び出し側(MarkdownScene)が持つ。
namespace MdScan {

    // 1行から画像参照を取り出す。見つかれば true。
    // refOut は line 内を指す(コピーしない)。
    inline bool ImageRefInLine(const char* line, size_t len, const char*& refOut, size_t& refLenOut)
    {
        refOut = nullptr;
        refLenOut = 0;

        if (!line || len < 2) return false;
        if (line[0] != '!' || line[1] != '[') return false;

        const char* open = nullptr;
        const char* close = nullptr;
        for (size_t i = 0; i < len; i++) {
            if (!open && line[i] == '(') open = line + i;
            else if (open && line[i] == ')') { close = line + i; break; }
        }
        //"()" のように中身が空なら参照として扱わない
        if (!open || !close || close <= open + 1) return false;

        refOut = open + 1;
        refLenOut = (size_t)(close - open - 1);
        return true;
    }
}
