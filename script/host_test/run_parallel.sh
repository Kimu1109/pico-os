#!/bin/sh
# run.shをグループごとに別プロセスで並列に回す(2026-10-05追加)。
#
# 使い方: sh script/host_test/run_parallel.sh [-j N] [グループ名...]
#   -j N … 同時に回す数(既定: CPU数、最大4)
# グループ名を省略すると全グループ。各グループの出力は終わってからまとめて表示する。
# 1つでも失敗すれば終了コードは非0(失敗したグループ名を最後に出す)。
DIR=$(cd "$(dirname "$0")" && pwd)
JOBS=$(nproc 2>/dev/null || echo 2)
[ "$JOBS" -gt 4 ] && JOBS=4
if [ "$1" = "-j" ]; then JOBS=$2; shift 2; fi
GROUPS_TO_RUN="$*"
[ -z "$GROUPS_TO_RUN" ] && GROUPS_TO_RUN=$(sh "$DIR/run.sh" --list)
LOGDIR=$(mktemp -d)
# 重いグループ(Lua)を先に始める
ORDERED=""
for g in $GROUPS_TO_RUN; do case "$g" in lua-*) ORDERED="$ORDERED $g" ;; esac; done
for g in $GROUPS_TO_RUN; do case "$g" in lua-*) ;; *) ORDERED="$ORDERED $g" ;; esac; done

echo $ORDERED | tr ' ' '\n' | xargs -P "$JOBS" -I{} sh -c \
    'sh "$0/run.sh" {} > "$1/{}.log" 2>&1; echo $? > "$1/{}.rc"' "$DIR" "$LOGDIR"

FAILED=""
for g in $GROUPS_TO_RUN; do
    echo "################ グループ: $g ################"
    cat "$LOGDIR/$g.log"
    rc=$(cat "$LOGDIR/$g.rc" 2>/dev/null || echo 1)
    [ "$rc" -ne 0 ] && FAILED="$FAILED $g"
done
rm -rf "$LOGDIR"
if [ -n "$FAILED" ]; then
    echo "[FAIL] 失敗したグループ:$FAILED" >&2
    exit 1
fi
echo "[OK] 全グループ成功"
