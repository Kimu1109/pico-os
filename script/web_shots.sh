#!/bin/sh
# GitHub Pages のトップ(pc/web/landing.html)に載せる画面写真を、PCビルドの --shot で撮る。
#
#   sh script/web_shots.sh <picoos_pc> <出力先ディレクトリ>
#   例: sh script/web_shots.sh pc/build/picoos_pc _site/shots
#
# .github/workflows/web-pages.yml が公開のたびに呼ぶ(写真はリポジトリに置かず、常に今の画面を撮る)。
# 手元で見るときは出力先を pc/web/ と同じ階層の shots/ にすれば landing.html から読める。
#
# アプリは --app で名前を指して開き、撮る画面まではアプリが --scene で自動で進む
# (ホーム画面の並びに依存しない。画面の名前はアプリごと。pc/README.md「起動時に開くアプリ」)。
# ランチャのページ(apps)だけはホーム画面をタップで送る(ページ送り「>」が等倍で(215,305))。
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

# shot <名前> <撮るフレーム> <拡大率> [--app ... --scene ... --tap ...]
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
shot clock    250 1 --app 時計
shot calc     250 1 --app 電卓
shot markdown 250 1 --app Markdown
shot music    250 1 --app ミュージック
# shellcheck disable=SC2046
shot apps     200 1 $(next_pages 4)
# テトリス: タイトルを飛ばして始め、少し落としてから
shot tetris   500 1 --app テトリス --scene play
# ブロック: ワールドAへ「自然」を作って入り、チャンクを読み終わるまで回す
shot blocks  1000 1 --app ブロック --scene nature
shot zombie   600 1 --app ゾンビTD

ls -l "$out"
