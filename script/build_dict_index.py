#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_dict_index.py

script/en-ja-and-ja-en.tsv (英和/和英統合の単語辞書。1行 =
「検索用語句<TAB>表示用語句<TAB>説明or訳」、検索用語句のバイト順で
ソート済み) から、部分一致検索用のブロックインデックスを作る。

convert_skk_dict.py と違って再ソートも再フォーマットもしない
(入力が既に目的の3列TSV形式・ソート済みのため)。やることは
「--block-size 行ごとに先頭行の検索用語句とバイトオフセットを
拾ってインデックスへ書き出す」だけ。

出力 (dict_index.tsv):
    検索用語句(切り詰めあり) \t バイトオフセット \n
    バイトオフセットは入力ファイル内でのその行の開始位置(バイト数)。
    Word_Dict.cpp の二分探索はこのインデックスで「前方一致の可能性がある
    ブロック」まで一気に飛び、そこから前方一致ぶんだけを線形走査する。
    ブロックに満たない部分一致(語の途中に含む場合)はこのインデックスでは
    見つけられないため、Word_Dict側が別途ファイル全体を走査して補う
    (インデックスは「速く返せる分だけ速く返す」ための最適化であって、
    無くても正しさは全体走査側が担保する)。

使い方:
    python3 build_dict_index.py en-ja-and-ja-en.tsv
    python3 build_dict_index.py en-ja-and-ja-en.tsv --index dict_index.tsv --block-size 800
"""

import argparse
import sys

# Word_Dict.hpp の kIndexKeyBytes と揃える(切り詰め長)。
# 大きすぎる検索用語句(まれ)は切り詰められるが、二分探索が多少不正確な
# ブロックへ飛ぶだけで、最終的な正しさは全体走査側が担保するので実害は無い。
DEFAULT_KEY_BYTES = 48

# Word_Dict.hpp の kMaxIndexEntries と揃える(静的配列の上限)。
DEFAULT_MAX_INDEX_ENTRIES = 400


def parse_args():
    p = argparse.ArgumentParser(
        description="辞書TSV(検索用語句でソート済み)からブロックインデックスを作る"
    )
    p.add_argument("input", help="入力の辞書TSV (例: en-ja-and-ja-en.tsv)")
    p.add_argument("--index", default="dict_index.tsv", help="出力: インデックスtsv (default: dict_index.tsv)")
    p.add_argument(
        "--block-size",
        type=int,
        default=800,
        help="インデックスの1エントリが表す行数。Word_Dict.hpp の "
             "kLinesPerIndexBlock 相当の値と対応(実際はデータ駆動で"
             "コンパイル定数とは独立だが、目安として合わせておくこと) (default: 800)",
    )
    p.add_argument(
        "--key-bytes",
        type=int,
        default=DEFAULT_KEY_BYTES,
        help=f"インデックスへ書く検索用語句の切り詰め長(バイト)。"
             f"Word_Dict.hpp の kIndexKeyBytes と一致させること (default: {DEFAULT_KEY_BYTES})",
    )
    p.add_argument(
        "--max-index-entries",
        type=int,
        default=DEFAULT_MAX_INDEX_ENTRIES,
        help=f"警告を出す閾値。Word_Dict.hpp の kMaxIndexEntries (default: {DEFAULT_MAX_INDEX_ENTRIES})",
    )
    return p.parse_args()


def main():
    args = parse_args()

    entry_count = 0
    index_entries = []  # (search_term_bytes, offset)
    prev_key = None
    offset = 0

    with open(args.input, "rb") as f:
        for line in f:
            tab = line.find(b"\t")
            if tab < 0:
                print(f"[error] タブ区切りでない行があります(offset={offset}): {line[:80]!r}",
                      file=sys.stderr)
                sys.exit(1)
            key = line[:tab]

            # 検索用語句がバイト順ソート済みであることを確認する。
            # 崩れていると二分探索(Word_Dict::findBlockStart)が誤った
            # ブロックへ飛び、見つかるはずの前方一致を取りこぼす。
            if prev_key is not None and key < prev_key:
                print(
                    f"[error] 検索用語句がバイト順にソートされていません "
                    f"(offset={offset}): {prev_key!r} の後に {key!r}",
                    file=sys.stderr,
                )
                sys.exit(1)
            prev_key = key

            if entry_count % args.block_size == 0:
                index_entries.append((key[: args.key_bytes], offset))

            offset += len(line)
            entry_count += 1

    with open(args.index, "wb") as idxf:
        for key, off in index_entries:
            idxf.write(key + b"\t" + str(off).encode("ascii") + b"\n")

    print(
        f"[info] 出力完了: {args.input} ({entry_count}行, {offset}バイト) から "
        f"{args.index} ({len(index_entries)}ブロック, block-size={args.block_size}) を生成",
        file=sys.stderr,
    )

    if len(index_entries) > args.max_index_entries:
        print(
            f"[warn] インデックスエントリ数({len(index_entries)})が "
            f"Word_Dict.hpp の kMaxIndexEntries(既定{args.max_index_entries})を"
            f"超える可能性があります。--block-size を大きくするか、"
            f"kMaxIndexEntries を増やしてください。",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()
