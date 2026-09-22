#!/bin/sh
# picoos を ~/.local/bin へシンボリックリンクし、PATHが通っていれば
# どこからでも `picoos` コマンドとして使えるようにする。
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
DEST_DIR="$HOME/.local/bin"
mkdir -p "$DEST_DIR"
ln -sf "$HERE/picoos" "$DEST_DIR/picoos"
echo "リンクしました: $DEST_DIR/picoos -> $HERE/picoos"

case ":$PATH:" in
  *":$DEST_DIR:"*) ;;
  *) echo "注意: $DEST_DIR がPATHに入っていません。~/.bashrc 等へ以下を追加してください:"
     echo "  export PATH=\"\$HOME/.local/bin:\$PATH\"" ;;
esac
