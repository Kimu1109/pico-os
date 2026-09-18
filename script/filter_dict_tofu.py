#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
filter_dict_tofu.py

script/en-ja-and-ja-en.tsv から、フォントが描画できない文字(いわゆる豆腐。
script/tofu-chars.txt = font_coverage_check の出力)を1文字でも含む行を
丸ごと除いて書き出す。

convert_skk_dict.py の --exclude-chars-file と同じ入力形式
(「U+XXXX<TAB>該当文字」または、2列目が無い行は「U+XXXX」からコードポイントを
復元)を使うが、SKK側は「候補ごとに」フィルタして読みは残すのに対し、
こちらは検索用語句・表示用語句・説明のどこに豆腐が出ても検索結果として
見せたくないので、行単位で丸ごと落とす(部分的に伏せ字にする方法は無い)。

検索用語句(1列目)は既にバイト順にソートされているが、この処理は行を
間引くだけで残る行同士の順序は変えないので、ソートは崩れない
(build_dict_index.py が前提にする不変条件はそのまま保たれる)。

使い方:
    python3 filter_dict_tofu.py en-ja-and-ja-en.tsv tofu-chars.txt --out en-ja-and-ja-en.filtered.tsv
"""

import argparse
import sys


def parse_args():
    p = argparse.ArgumentParser(
        description="辞書TSVから豆腐化する文字を含む行を除く"
    )
    p.add_argument("input", help="入力の辞書TSV (例: en-ja-and-ja-en.tsv)")
    p.add_argument("tofu_chars_file", help="豆腐文字リスト (例: tofu-chars.txt)")
    p.add_argument("--out", required=True, help="出力先")
    return p.parse_args()


def load_tofu_chars(path):
    chars = set()
    with open(path, "r", encoding="utf-8") as f:
        for lineno, raw in enumerate(f, start=1):
            line = raw.rstrip("\n").rstrip("\r")
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) >= 2 and parts[1]:
                # 2列目に実際の文字が入っていればそれを使う(確実)
                for ch in parts[1]:
                    chars.add(ch)
            elif parts[0].startswith("U+"):
                # 2列目が無い行(BMP範囲外の注記など)はコード側から復元
                code_str = parts[0][2:].split(" ", 1)[0]
                try:
                    chars.add(chr(int(code_str, 16)))
                except ValueError:
                    print(f"[warn] {path}:{lineno}: U+コードを解釈できません: {line!r}",
                          file=sys.stderr)
            else:
                print(f"[warn] {path}:{lineno}: 形式を解釈できません: {line!r}", file=sys.stderr)
    return chars


def main():
    args = parse_args()
    tofu_chars = load_tofu_chars(args.tofu_chars_file)
    print(f"[info] 豆腐文字: {len(tofu_chars)}種", file=sys.stderr)

    kept = 0
    dropped = 0
    dropped_examples = []

    with open(args.input, "r", encoding="utf-8") as fin, \
         open(args.out, "w", encoding="utf-8", newline="\n") as fout:
        for line in fin:
            text = line.rstrip("\n").rstrip("\r")
            if not text:
                continue
            if any(ch in tofu_chars for ch in text):
                dropped += 1
                if len(dropped_examples) < 10:
                    dropped_examples.append(text.split("\t", 1)[0])
                continue
            fout.write(text + "\n")
            kept += 1

    print(f"[info] 残した行: {kept}, 除いた行: {dropped}", file=sys.stderr)
    if dropped_examples:
        print(f"[info] 除いた行の例(検索用語句のみ): {dropped_examples}", file=sys.stderr)


if __name__ == "__main__":
    main()
