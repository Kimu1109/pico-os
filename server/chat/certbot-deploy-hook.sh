#!/bin/sh
# certbot が証明書を取った/更新したときに呼ばれる(--deploy-hook)。
#
# Let's Encrypt の証明書(/etc/letsencrypt/live/<ドメイン>/)は root しか読めないので、
# チャットサーバ(pico-chat ユーザー)が読める場所へ写す。
# サーバはファイルの更新に気づいて次の接続から新しい証明書を使うので、再起動は要らない。
#
# certbot は RENEWED_LINEAGE に /etc/letsencrypt/live/<ドメイン> を入れて呼ぶ。
set -eu

DEST=/etc/pico-chat/tls
SRC=${RENEWED_LINEAGE:?certbot から呼ばれていません}

install -d -m 750 -o root -g pico-chat "$DEST"
# 鍵を先に置き、証明書は最後に置く(サーバは両方の更新時刻を見て読み直す)
install -m 640 -o root -g pico-chat "$SRC/privkey.pem" "$DEST/privkey.pem.new"
install -m 644 -o root -g pico-chat "$SRC/fullchain.pem" "$DEST/fullchain.pem.new"
mv -f "$DEST/privkey.pem.new" "$DEST/privkey.pem"
mv -f "$DEST/fullchain.pem.new" "$DEST/fullchain.pem"

echo "pico-chat: 証明書を $DEST へ置きました"
