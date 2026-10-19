#!/bin/sh
# 以前の「グループごとに別プロセスで並列に回す」入口。今は run.sh が CTest で並列に回すので、
# そのまま run.sh へ渡す(同じグループの中は1本ずつ、別のグループとは並列)。
#
# 使い方: sh script/host_test/run_parallel.sh [-j N] [グループ名...]
exec sh "$(cd "$(dirname "$0")" && pwd)/run.sh" "$@"
