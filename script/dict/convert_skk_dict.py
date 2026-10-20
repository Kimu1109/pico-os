#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
convert_skk_dict.py

SKK-JISYO.M (SKK形式の辞書ファイル) を pico-os IME用の
skk_body.tsv / skk_index.tsv に変換するスクリプト。

使い方:
    python3 convert_skk_dict.py SKK-JISYO.M
    python3 convert_skk_dict.py SKK-JISYO.M --encoding utf-8
    python3 convert_skk_dict.py SKK-JISYO.M --body skk_body.tsv --index skk_index.tsv --block-size 256

出力ファイルの仕様 (pico-os側 ime_dict.h / ime_dict.cpp と対応):

  skk_body.tsv:
      1行 = 1エントリ。「よみ\t候補1\t候補2\t...\n」形式。
      改行はLF固定(CRLF不可)。よみのUnicodeコードポイント昇順
      (= UTF-8バイト列昇順と等価)でソート済み。
      Pico側の strcmp() による比較順序と一致する。

  skk_index.tsv:
      --block-size 行ごとの先頭エントリについて
      「よみ\tバイトオフセット\n」を出力。
      バイトオフセットは skk_body.tsv 内でのその行の開始位置
      (UTF-8エンコード後のバイト数、書き込み開始からの累積)。

注意:
  - SKK-JISYOは伝統的にEUC-JPエンコードで配布されることが多いが、
    近年はUTF-8版も配布されている。読み込みに失敗する場合は
    --encoding オプションで指定し直すこと (euc-jp / utf-8 など)。
  - 元ファイル中の ";; okuri-ari entries." のようなセクション分け・
    コメントはすべて無視し、有効なエントリだけをこちらで再ソートする。
    okuri-ari領域が歴史的経緯で逆順格納されていても問題ない。
"""

import argparse
import sys


def parse_args():
    p = argparse.ArgumentParser(
        description="SKK-JISYO を pico-os 用 skk_body.tsv / skk_index.tsv に変換する"
    )
    p.add_argument("input", help="入力のSKK-JISYOファイル (例: SKK-JISYO.M)")
    p.add_argument("--body", default="skk_body.tsv", help="出力: 辞書本体tsv (default: skk_body.tsv)")
    p.add_argument("--index", default="skk_index.tsv", help="出力: インデックスtsv (default: skk_index.tsv)")
    p.add_argument(
        "--block-size",
        type=int,
        default=256,
        help="インデックスのブロック行数。ime_dict.h の IME_INDEX_BLOCK_LINES と必ず一致させること (default: 256)",
    )
    p.add_argument(
        "--encoding",
        default="euc-jp",
        help="入力ファイルの文字コード (default: euc-jp)。UTF-8版のSKK-JISYOなら utf-8 を指定",
    )
    p.add_argument(
        "--exclude-chars-file",
        default=None,
        help="Pico側の font_coverage_check で生成した missing_chars.txt のパス。"
             "指定すると、当該文字を含む候補を除外する(候補が全滅したエントリは丸ごと削除)",
    )
    return p.parse_args()


def load_excluded_chars(path):
    """
    missing_chars.txt (font_coverage_check.cpp の出力) を読み込み、
    除外対象の文字の集合(1文字ずつのset)を返す。

    想定フォーマット(1行1文字):
        U+8C46<TAB>豆
        U+10000 (BMP範囲外のため未対応扱い)   <- 2列目が無い行
    """
    excluded = set()
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) >= 2 and parts[1]:
                # 2列目に実際の文字が入っていればそれを使う(確実)
                for ch in parts[1]:
                    excluded.add(ch)
            elif parts[0].startswith("U+"):
                # 2列目が無い行(BMP範囲外の注記など)はコード側から復元
                code_str = parts[0][2:].split(" ", 1)[0]
                try:
                    code = int(code_str, 16)
                    excluded.add(chr(code))
                except ValueError:
                    continue
    return excluded


def filter_entries_by_font(entries, excluded_chars):
    """
    excluded_charsに含まれる文字を持つ候補を除去する。
    候補が1つも残らなくなったエントリは丸ごと削除する。
    戻り値: (フィルタ後のentries, 削除した候補数, 削除したエントリ数)
    """
    if not excluded_chars:
        return entries, 0, 0

    filtered = []
    removed_candidates = 0
    removed_entries = 0

    for yomi, candidates in entries:
        kept = [c for c in candidates if not any(ch in excluded_chars for ch in c)]
        removed_candidates += (len(candidates) - len(kept))
        if kept:
            filtered.append((yomi, kept))
        else:
            removed_entries += 1

    return filtered, removed_candidates, removed_entries


def read_text(path, encoding):
    """
    指定エンコーディングで読み込む。失敗したら euc-jp / utf-8 を
    自動的に試す (よくある取り違えのフォールバック)。
    """
    tried = []
    candidates = [encoding] + [e for e in ("euc-jp", "utf-8", "cp932") if e != encoding]

    for enc in candidates:
        try:
            with open(path, "r", encoding=enc, errors="strict") as f:
                text = f.read()
            if enc != encoding:
                print(f"[warn] --encoding {encoding} での読み込みに失敗したため "
                      f"{enc} で読み込みました。実際のファイルの文字コードを確認してください。",
                      file=sys.stderr)
            return text
        except UnicodeDecodeError:
            tried.append(enc)
            continue

    print(f"[error] どのエンコーディングでも読み込めませんでした (試行: {tried})", file=sys.stderr)
    sys.exit(1)


def load_entries(path, encoding):
    """
    SKK-JISYOファイルを読み込み、(yomi, [candidates...]) のリストを返す。
    ";;" で始まるコメント行はファイル中どこにあってもスキップする。
    """
    text = read_text(path, encoding)

    entries = []
    skipped_comment = 0
    skipped_malformed = 0

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip("\r")
        if not line:
            continue
        if line.startswith(";;"):
            skipped_comment += 1
            continue

        # SKK-JISYOの1行の形式: よみ /候補1/候補2/.../
        # よみと候補群は半角スペース区切り
        parts = line.split(" ", 1)
        if len(parts) != 2:
            skipped_malformed += 1
            continue
        yomi, cand_field = parts
        cand_field = cand_field.strip()

        if not cand_field.startswith("/"):
            skipped_malformed += 1
            continue

        # 前後の "/" を落として "/" で分割
        raw_cands = cand_field.strip("/").split("/")
        candidates = []
        for c in raw_cands:
            if not c:
                continue
            # 「候補;注釈」形式の注釈部分を除去 (unannotated版でも念のため防御)
            if ";" in c:
                c = c.split(";", 1)[0]
            if c:
                candidates.append(c)

        if not yomi or not candidates:
            skipped_malformed += 1
            continue

        entries.append((yomi, candidates))

    print(
        f"[info] 読み込み完了: {len(entries)}件 "
        f"(コメント行スキップ: {skipped_comment}, 不正/空行スキップ: {skipped_malformed})",
        file=sys.stderr,
    )
    return entries


def write_outputs(entries, body_path, index_path, block_size):
    # よみのUnicodeコードポイント順でソート (= UTF-8バイト列順と等価)
    entries.sort(key=lambda e: e[0])

    index_entries = []
    offset = 0

    with open(body_path, "wb") as bf:
        for i, (yomi, candidates) in enumerate(entries):
            line = yomi + "\t" + "\t".join(candidates) + "\n"
            line_bytes = line.encode("utf-8")

            if i % block_size == 0:
                index_entries.append((yomi, offset))

            bf.write(line_bytes)
            offset += len(line_bytes)

    with open(index_path, "w", encoding="utf-8", newline="\n") as idxf:
        for yomi, off in index_entries:
            idxf.write(f"{yomi}\t{off}\n")

    print(
        f"[info] 出力完了: {body_path} ({len(entries)}行, {offset}バイト), "
        f"{index_path} ({len(index_entries)}ブロック, block-size={block_size})",
        file=sys.stderr,
    )

    # ime_dict.h の設定値と食い違っていないか簡易チェック
    if len(index_entries) > 64:
        print(
            f"[warn] インデックスエントリ数({len(index_entries)})が "
            f"ime_dict.h の IME_MAX_INDEX_ENTRIES(デフォルト64)を超える可能性があります。"
            f"block-size を大きくするか、IME_MAX_INDEX_ENTRIES を増やしてください。",
            file=sys.stderr,
        )


def main():
    args = parse_args()
    entries = load_entries(args.input, args.encoding)
    if not entries:
        print(
            "[error] 有効なエントリが1件も読み込めませんでした。"
            " --encoding オプションを確認してください。",
            file=sys.stderr,
        )
        sys.exit(1)

    if args.exclude_chars_file:
        excluded = load_excluded_chars(args.exclude_chars_file)
        entries, removed_cand, removed_entries = filter_entries_by_font(entries, excluded)
        print(
            f"[info] フォント非対応文字によるフィルタ適用: "
            f"除外文字数={len(excluded)}, 削除候補数={removed_cand}, "
            f"全滅により削除したエントリ数={removed_entries}",
            file=sys.stderr,
        )

    write_outputs(entries, args.body, args.index, args.block_size)


if __name__ == "__main__":
    main()
