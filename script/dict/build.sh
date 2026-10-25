#!/bin/sh
# 辞書の材料(script/dict/src/)から、SDへ置くファイルを script/dict/out/ に作る。
# out/ はSDのルートと同じ並びなので、中身(sys/)をそのままSDのルートへ写せばよい。
#   out/sys/ime/skk_body.tsv  skk_index.tsv           … IME(かな漢字変換)
#   out/sys/dict/en-ja-and-ja-en.tsv  dict_index.tsv  … 辞書アプリ(前方一致)
#   out/sys/dict/dict_suffixes.tsv  dict_suffix_index.tsv … 辞書アプリ(部分一致。約27MB、無くてもよい)
# 使い方: sh script/dict/build.sh [ime|dict|suffix ...](省略で全部)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
SRC="$DIR/src"
OUT="${PICOOS_DICT_OUT:-$DIR/out}"
PY="${PYTHON:-python3}"
[ $# -eq 0 ] && set -- ime dict suffix
mkdir -p "$OUT/sys/ime" "$OUT/sys/dict"
for what in "$@"; do
    case "$what" in
    ime)
        # --block-size は src/ime/IME_Dict.hpp の IME_INDEX_BLOCK_LINES と揃える
        "$PY" "$DIR/convert_skk_dict.py" "$SRC/SKK-JISYO.ML" \
            --exclude-chars-file "$SRC/tofu-chars.txt" --block-size 320 \
            --body "$OUT/sys/ime/skk_body.tsv" --index "$OUT/sys/ime/skk_index.tsv" ;;
    dict)
        cp "$SRC/en-ja-and-ja-en.tsv" "$OUT/sys/dict/en-ja-and-ja-en.tsv"
        "$PY" "$DIR/build_dict_index.py" "$SRC/en-ja-and-ja-en.tsv" \
            --index "$OUT/sys/dict/dict_index.tsv" ;;
    suffix)
        "$PY" "$DIR/build_dict_suffix_index.py" "$SRC/en-ja-and-ja-en.tsv" \
            --out "$OUT/sys/dict/dict_suffixes.tsv" \
            --build-index --index-out "$OUT/sys/dict/dict_suffix_index.tsv" ;;
    *) echo "知らない種類: $what (ime / dict / suffix)" >&2; exit 2 ;;
    esac
done
# 辞書のライセンス(再配布のとき辞書と一緒に置く決まり)
cp "$DIR/LICENSES.md" "$OUT/sys/ime/LICENSES.md"
cp "$DIR/LICENSES.md" "$OUT/sys/dict/LICENSES.md"
echo "[dict] $OUT/sys/ を SD のルートへ写してください" >&2
