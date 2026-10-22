#!/bin/sh
# GitHub Pages のトップ(pc/web/landing.html)に載せる画面写真を、PCビルドの --shot で撮る。
#
#   sh script/web_shots.sh <picoos_pc> <出力先ディレクトリ>
#   例: sh script/web_shots.sh pc/build/picoos_pc _site/shots
#
# .github/workflows/web-pages.yml が公開のたびに呼ぶ(写真はリポジトリに置かず、常に今の画面を撮る)。
# 手元で見るときは出力先を pc/web/ と同じ階層の shots/ にすれば landing.html から読める。
#
# タップの座標は等倍(240x320)の画面上の位置で、ホーム画面の並びに依存する
# (2列3段: 列x=60/178・段y=55/140/225、ページ送り「>」が(215,305))。
# アプリの並びが変わって別の画面が写ったら、ここの座標を直すこと。
# SDの中身(pc/sdcard/)は一時ディレクトリへ写して使う(ブロックのワールド等の書き込みでリポジトリを汚さない)。
set -eu

if [ $# -ne 2 ]; then
    echo "使い方: sh script/web_shots.sh <picoos_pc> <出力先ディレクトリ>" >&2
    exit 2
fi
bin=$1
out=$2
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

export SDL_VIDEODRIVER=offscreen

# shot <名前> <撮るフレーム> <拡大率> [--tap ...]
shot() {
    name=$1; frames=$2; scale=$3; shift 3
    rm -rf "$work/sd"
    cp -R "$root/pc/sdcard" "$work/sd"
    rc=0
    PICOOS_SD_ROOT="$work/sd" "$bin" "$@" --shot "$work/$name.ppm" "$frames" > "$work/$name.log" 2>&1 || rc=$?
    if [ "$rc" -ne 0 ] || [ ! -f "$work/$name.ppm" ]; then
        echo "撮れなかった: $name (終了コード $rc)" >&2
        tail -n 20 "$work/$name.log" >&2
        exit 1
    fi
    python3 "$root/script/ppm2png.py" "$work/$name.ppm" "$out/$name.png" "$scale"
}

# ホーム画面を n ページ送る --tap の列(20フレームおき)
next_pages() {
    i=1
    while [ "$i" -le "$1" ]; do
        printf ' --tap 215,305@%d:3' $((i * 20))
        i=$((i + 1))
    done
}

# 時刻は333msごとにしか更新しないので、200フレーム以上回してから撮る(pc/README.md)
shot home     250 2
shot clock    250 1 --tap 178,55@30:5
shot calc     250 1 --tap 60,140@30:5
shot markdown 250 1 --tap 60,55@30:5
# shellcheck disable=SC2046
shot music    300 1 $(next_pages 2) --tap 178,55@70:5
# shellcheck disable=SC2046
shot apps     200 1 $(next_pages 4)
# テトリスは「タップで開始」を押して少し落としてから
# shellcheck disable=SC2046
shot tetris   700 1 $(next_pages 4) --tap 178,140@110:5 --tap 60,145@250:5
# ブロック: ワールドA →「自然」を選び、作り終わるまで回す
# shellcheck disable=SC2046
shot blocks  1200 1 $(next_pages 4) --tap 60,140@110:5 --tap 120,90@200:5 --tap 60,133@260:5
# shellcheck disable=SC2046
shot zombie   700 1 $(next_pages 4) --tap 178,55@110:5

ls -l "$out"
