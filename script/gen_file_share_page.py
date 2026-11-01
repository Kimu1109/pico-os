#!/usr/bin/env python3
"""Wi-Fi転送のページ(src/net/File_Share_Page.html)を gzip して src/net/File_Share_Page.cpp を作る。

    python3 script/gen_file_share_page.py          … 書き出す
    python3 script/gen_file_share_page.py --check  … .cpp の中身を戻したものが .html と食い違えば終了コード1(CIで回す)

ページは gzip のままフラッシュに置き、`Content-Encoding: gzip` で返す(ブラウザが戻す)。
生の文字列で持つと約12.8KB、gzip で約5.2KB(2026-11-01)。
--check は gzip の出力そのものではなく「戻した中身」を比べる(zlib の版で圧縮の結果が変わっても落ちない)。
ページの決まり(SDの名前を innerHTML へ入れない・キーの見出しを送る)もここで確かめる。
.cpp は生成物なので直接書き換えないこと。標準ライブラリだけで動く。
"""
import gzip
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(ROOT, "src", "net", "File_Share_Page.html")
OUT = os.path.join(ROOT, "src", "net", "File_Share_Page.cpp")

HEAD = """// 生成物: script/gen_file_share_page.py が src/net/File_Share_Page.html から作る。直接書き換えないこと
// (直したら python3 script/gen_file_share_page.py。CI の「層の決まり」が --check で食い違いを見る)
#include "net/File_Share.hpp"

// Wi-Fi転送のページ(ブラウザで開く1枚)。取り決めは net/File_Share.hpp の先頭。中身は .html の方を見ること。
// 中身はSDの名前を表示するだけなので、名前は必ず textContent で入れる(innerHTML へ入れない)。
// キーはクッキー pk に持ち、書き換える要求には X-Pico-Key 見出しでも送る。
// アップロードは1つずつ順に送る(本体は同時に1本しか相手をしない)。進み具合が要るので fetch ではなく XHR。
// フォルダごと(選ぶ・落とす)は中のファイルを「フォルダ/中/名前」で ?mkdirs=1 を付けて送り、本体が親のフォルダを作る。
// 表示の言語はブラウザの言語で決める(ja 以外は英語。本体の言語設定は見ない: 開く人はPCの前にいる)。
// 文は日本語の原文を T() で包み、英語は EN の表から引く(本体が返す誤りの文も同じ表で訳す)。
// 静的なところは data-t の付いた要素の文字を書き換える(textContent。innerHTML は使わない)。
//
// フラッシュを節約するため gzip のまま持ち、Content-Encoding: gzip で返す(元は約12.8KB)。
namespace FileShare {

"""


def read_html():
    with open(SRC, encoding="utf-8") as f:
        html = f.read()
    if "innerHTML" in html:
        sys.exit("File_Share_Page.html: innerHTML を使っています(SDの名前は textContent で入れること)")
    if "X-Pico-Key" not in html:
        sys.exit("File_Share_Page.html: 書き換える要求に X-Pico-Key の見出しを付けていません")
    return html


def generate(html):
    gz = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
    out = [HEAD, f"// 元の大きさ {len(html.encode('utf-8'))}B\n", "const uint8_t kPageGz[] = {\n"]
    for i in range(0, len(gz), 16):
        out.append("    " + ", ".join(f"0x{b:02x}" for b in gz[i:i + 16]) + ",\n")
    out.append("};\n\nsize_t PageGzLength(){ return sizeof(kPageGz); }\n\n}\n")
    return "".join(out)


def decode_cpp(text):
    """生成した .cpp の配列を読み戻して gzip を戻す"""
    start = text.index("kPageGz[] = {") + len("kPageGz[] = {")
    end = text.index("};", start)
    data = bytes(int(tok, 16) for tok in text[start:end].replace("\n", " ").split(",") if tok.strip())
    return gzip.decompress(data).decode("utf-8")


def main():
    html = read_html()
    if "--check" in sys.argv:
        try:
            with open(OUT, encoding="utf-8") as f:
                ok = decode_cpp(f.read()) == html
        except (OSError, ValueError, EOFError) as e:
            print(f"src/net/File_Share_Page.cpp を読めません: {e}")
            ok = False
        if not ok:
            print("src/net/File_Share_Page.cpp が src/net/File_Share_Page.html と食い違っています。"
                  "python3 script/gen_file_share_page.py で作り直してください")
            return 1
        print("File_Share_Page.cpp は File_Share_Page.html と一致しています")
        return 0
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(generate(html))
    print("wrote", os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
