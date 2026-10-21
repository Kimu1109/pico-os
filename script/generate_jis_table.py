#!/usr/bin/env python3
"""generate_jis_table.py

メールアプリ(src/apps/mail/)が日本語の古い文字コード(ISO-2022-JP / Shift_JIS / EUC-JP)を
UTF-8へ直すための表 src/apps/mail/Mail_Jis_Data.hpp を作る。標準ライブラリだけで動く。

表は JIS X 0208 の区点(1〜94区 × 1〜94点)→ Unicode の 94×94 個の uint16_t(約17KB、フラッシュに置く)。
対応は Pythonの cp932(Windowsの Shift_JIS)の写像を使う:
  - 13区のNEC特殊文字(①②… ㈱ 等。日本語のメールでよく使われる)も入る
  - 〜 は U+FF5E(全角チルダ)、- は U+FF0D 等、Windowsで書かれたメールと同じ字形になる
  - 89〜92区のNEC選定IBM拡張も入る。IBM拡張(Shift_JISの0xFA〜0xFC)は表の外なので 〓 になる
対応の無い区点は 0 にする(読む側が 〓 に置き換える)。

使い方: python3 script/generate_jis_table.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "src" / "apps" / "mail" / "Mail_Jis_Data.hpp"


def sjis_bytes(row, cell):
    """区点(1始まり)→ Shift_JIS の2バイト"""
    s1 = ((row + 1) >> 1) + (0x80 if row <= 62 else 0xC0)
    if row % 2 == 1:
        s2 = cell + 0x3F
        if s2 >= 0x7F:
            s2 += 1
    else:
        s2 = cell + 0x9E
    return bytes([s1, s2])


def main():
    table = []
    for row in range(1, 95):
        for cell in range(1, 95):
            try:
                ch = sjis_bytes(row, cell).decode("cp932")
            except UnicodeDecodeError:
                ch = ""
            code = ord(ch) if len(ch) == 1 and ord(ch) <= 0xFFFF else 0
            table.append(code)

    lines = [
        "#pragma once",
        "",
        "// script/generate_jis_table.py が作る。手で直さないこと。",
        "// JIS X 0208 の区点 → Unicode(cp932の写像)。添字は (区-1)*94 + (点-1)。0 は対応なし",
        "",
        "#include <cstdint>",
        "",
        "namespace MailJisData {",
        "    constexpr int kRows = 94;",
        "    constexpr int kCells = 94;",
        "    static const uint16_t kTable[kRows * kCells] = {",
    ]
    for i in range(0, len(table), 12):
        chunk = table[i:i + 12]
        lines.append("        " + ",".join("0x%04X" % v for v in chunk) + ",")
    lines += ["    };", "}", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print("生成しました: %s (%d 区点、対応あり %d)" % (OUT, len(table), sum(1 for v in table if v)))


if __name__ == "__main__":
    main()
