#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_dict_suffix_index.py

script/en-ja-and-ja-en.tsv (検索用語句でソート済みの単語辞書) から、
「語の途中に含む」一致(部分一致)を高速に引くためのサフィックスコーパスを作る。

Word_Dict.hpp の前方一致は「1列目(検索用語句)でソート済み」という性質を
使い、ブロックインデックス+二分探索で該当ブロックへ飛べる。語の途中に
含む一致はこのソート順では見つけられないため、これまでは全体走査
(update()でフレーム分割しながら1行ずつstrstr())に頼っていた
(実データ27万行、SPI 10MHzだと数十秒〜数分かかりうる)。

このスクリプトは「各検索用語句の、先頭以外から始まる全ての接尾辞」を
列挙し、接尾辞のバイト順にソートし直したコーパスを作る。すると
「語の途中に含む」検索は「あるエントリの接尾辞に対する前方一致」に
帰着でき、前方一致と全く同じブロックインデックス+二分探索の技法が
そのまま使い回せる(--build-indexを指定すると build_dict_index.py を
このコーパスに対して再実行し、インデックスまで作る。ロジックの二重化を
避けるため、コーパス生成だけをこのスクリプトが担当する)。

出力 (dict_suffixes.tsv):
    接尾辞(バイト列) \t 元エントリの行頭バイトオフセット(入力ファイル内) \n
    接尾辞のバイト順でソート済み。先頭(位置0)の接尾辞=検索用語句そのものは
    含めない(前方一致パスが既にカバーしているため、コーパスを1/平均語長
    ぶん減らせる)。UTF-8の継続バイト(10xxxxxx)の位置からは接尾辞を
    起こさない(文字の途中で切らない)。

使い方:
    python3 build_dict_suffix_index.py en-ja-and-ja-en.tsv
    python3 build_dict_suffix_index.py en-ja-and-ja-en.tsv \
        --build-index --index-block-size 8000
"""

import argparse
import subprocess
import sys
from pathlib import Path

# Word_Dict.hpp の kMaxSuffixIndexEntries と揃える(静的配列の上限)。
# 本体の索引(kMaxIndexEntries=400)より少なくしてあるのは、サフィックス
# コーパスは本体よりずっと行数が多い分をblock-size側で吸収し、RAMを
# 余分に食わせないため。
DEFAULT_MAX_INDEX_ENTRIES = 200


def is_utf8_continuation_byte(b: int) -> bool:
    return (b & 0xC0) == 0x80


def gen_suffix_offsets(term: bytes):
    """termの、UTF-8文字境界かつ位置0以外から始まる接尾辞の開始バイト位置を返す"""
    for i in range(1, len(term)):
        if not is_utf8_continuation_byte(term[i]):
            yield i


def parse_args():
    p = argparse.ArgumentParser(
        description="辞書TSV(検索用語句でソート済み)から部分一致検索用のサフィックスコーパスを作る"
    )
    p.add_argument("input", help="入力の辞書TSV (例: en-ja-and-ja-en.tsv)")
    p.add_argument("--out", default="dict_suffixes.tsv", help="出力: サフィックスコーパスtsv (default: dict_suffixes.tsv)")
    p.add_argument(
        "--build-index", action="store_true",
        help="生成したコーパスに対して build_dict_index.py も実行し、ブロックインデックスまで作る",
    )
    p.add_argument("--index-out", default="dict_suffix_index.tsv", help="--build-index時の出力先 (default: dict_suffix_index.tsv)")
    p.add_argument(
        "--index-block-size", type=int, default=None,
        help="--build-index時のブロックサイズ。未指定なら、コーパスの行数から "
             "--max-index-entries へ収まるよう自動算出する",
    )
    p.add_argument("--key-bytes", type=int, default=48, help="build_dict_index.py と同じ切り詰め長 (default: 48)")
    p.add_argument(
        "--max-index-entries", type=int, default=DEFAULT_MAX_INDEX_ENTRIES,
        help=f"Word_Dict.hpp の kMaxSuffixIndexEntries と一致させること (default: {DEFAULT_MAX_INDEX_ENTRIES})",
    )
    return p.parse_args()


def main():
    args = parse_args()

    entries = []  # (suffix_bytes, original_offset)
    entry_count = 0
    offset = 0

    with open(args.input, "rb") as f:
        for line in f:
            tab = line.find(b"\t")
            if tab < 0:
                print(f"[error] タブ区切りでない行があります(offset={offset}): {line[:80]!r}",
                      file=sys.stderr)
                sys.exit(1)
            term = line[:tab]

            for i in gen_suffix_offsets(term):
                entries.append((term[i:], offset))

            offset += len(line)
            entry_count += 1

    print(f"[info] {entry_count}行から接尾辞{len(entries)}件を生成。ソート中…", file=sys.stderr)
    # Pythonのbytes比較はバイト値の大小そのもの(strcmp相当)なので、
    # Word_Dict.cpp側のstrcmp/strncmpによる二分探索と順序が一致する。
    entries.sort(key=lambda e: e[0])

    out_path = Path(args.out)
    with open(out_path, "wb") as outf:
        for suffix, orig_offset in entries:
            outf.write(suffix + b"\t" + str(orig_offset).encode("ascii") + b"\n")

    print(f"[info] 出力完了: {out_path} ({len(entries)}行)", file=sys.stderr)

    # 目安のブロックサイズ: 400/342(本体索引の実充填率 約85%)に倣い、
    # 枠の85%程度を使う想定で逆算する(壊れたインデックスにはならないが、
    # 枠ぎりぎりだと次に辞書データが増えたときすぐ溢れるため余裕を持たせる)。
    suggested_block_size = max(1, -(-len(entries) // int(args.max_index_entries * 0.85)))
    block_size = args.index_block_size if args.index_block_size is not None else suggested_block_size
    if args.index_block_size is not None and args.index_block_size != suggested_block_size:
        print(
            f"[info] 参考: {args.max_index_entries}ブロック枠に余裕を持って収めるなら "
            f"--index-block-size {suggested_block_size} 目安(指定値={args.index_block_size}を使用)",
            file=sys.stderr,
        )
    else:
        print(f"[info] block-size自動算出: {block_size}", file=sys.stderr)

    if args.build_index:
        script_dir = Path(__file__).resolve().parent
        cmd = [
            sys.executable, str(script_dir / "build_dict_index.py"),
            str(out_path),
            "--index", args.index_out,
            "--block-size", str(block_size),
            "--key-bytes", str(args.key_bytes),
            "--max-index-entries", str(args.max_index_entries),
        ]
        print(f"[info] インデックス生成: {' '.join(cmd)}", file=sys.stderr)
        subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
