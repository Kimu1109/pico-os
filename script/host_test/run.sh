#!/bin/sh
# ホストテストを回す入口。実コード(src/)をPCのg++ + ASan/UBSanで動かす。
# テストの一覧・グループ・リンクのしかたは script/host_test/CMakeLists.txt、説明は README.md。
#
# 使い方: sh script/host_test/run.sh [-j N] [グループ名...]
#   グループ名を省略すると全グループ。--list でグループ名の一覧。
#   -j N … 同時にビルド/実行する数(既定: CPU数)
# ビルドの結果は script/host_test/build/(HOST_TEST_BUILD で変えられる)に残り、2回目からは差分だけ作る。
#
# 確保回数やピーク使用量の計測は run_mem.sh、本物のソケットを使う結合テストは run_net.sh の担当。
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR="$ROOT/script/host_test"
BUILD=${HOST_TEST_BUILD:-$DIR/build}

# グループの一覧は CMakeLists.txt の HOST_TEST_GROUPS(configure せずに --list できるよう直接読む)
ALL_GROUPS=$(sed -n 's/^set(HOST_TEST_GROUPS \(.*\))$/\1/p' "$DIR/CMakeLists.txt")
if [ -z "$ALL_GROUPS" ]; then
    echo "CMakeLists.txt に set(HOST_TEST_GROUPS ...) の行が見つかりません" >&2
    exit 2
fi

JOBS=$(nproc 2>/dev/null || echo 2)
SELECTED=""
while [ $# -gt 0 ]; do
    case "$1" in
        --list) echo $ALL_GROUPS; exit 0 ;;
        -j) JOBS=$2; shift ;;
        -j*) JOBS=${1#-j} ;;
        *) case " $ALL_GROUPS " in
               *" $1 "*) SELECTED="$SELECTED $1" ;;
               *) echo "不明なグループ: $1 (候補: $ALL_GROUPS)" >&2; exit 2 ;;
           esac ;;
    esac
    shift
done

# 「run.shが一生終わらない」事故の対策。テスト1本の打ち切り(60秒)は CTest の TIMEOUT、
# ビルドはここで全体を区切る。終了コードは「cmd || rc=$?」で取る(if ! cmd では
# $? に cmd の本当の終了コードが乗らないため)
BUILD_TIMEOUT_SEC=1800
run_step() {
    rc=0
    timeout -k 10 "$BUILD_TIMEOUT_SEC" "$@" || rc=$?
    if [ "$rc" -ne 0 ]; then
        [ "$rc" -eq 124 ] && echo "[FATAL] ${BUILD_TIMEOUT_SEC}秒を超えて終わりませんでした: $*" >&2
        exit "$rc"
    fi
}

GEN=""
if [ ! -f "$BUILD/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
    GEN="-G Ninja"
fi
run_step cmake -S "$DIR" -B "$BUILD" $GEN -DCMAKE_BUILD_TYPE=Debug

TARGETS=""
LABELS=""
for g in $SELECTED; do
    TARGETS="$TARGETS group_$g"
    LABELS="${LABELS:+$LABELS|}$g"
done
if [ -n "$TARGETS" ]; then
    run_step cmake --build "$BUILD" -j "$JOBS" --target $TARGETS
else
    run_step cmake --build "$BUILD" -j "$JOBS"
fi

if [ -n "$LABELS" ]; then
    ctest --test-dir "$BUILD" -j "$JOBS" --output-on-failure -L "^($LABELS)\$"
else
    ctest --test-dir "$BUILD" -j "$JOBS" --output-on-failure
fi
