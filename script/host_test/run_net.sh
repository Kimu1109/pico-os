#!/bin/sh
# HttpGet の結合テスト。**実際にソケットで通信する**ので run.sh とは分けてある。
#
# script/reference_server.py を一時的に立ち上げ、pc/compat の WiFiClient から
# 本当に取得できるかを確かめる。SDL2もSDカードも要らない。
#
# run.sh との違い:
#   run.sh     … stubs/ を使い、ネットワークもSDも無い状態で純粋なロジックを見る
#   run_net.sh … pc/compat/ を使い、本物のソケットでプロトコルの往復を見る
#
# HTTPS(calendar_sync_test)は、ここで使い捨てのCAとサーバ証明書を openssl コマンドで作り、
# script/host_test/tls_test_server.py を立てて確かめる(外のサーバへは行かない)。
#
# TODO(todoist_net_test)は script/host_test/todoist_fake_server.py(Todoist API v1 の偽物)を平文・HTTPS
# (上と同じ使い捨ての証明書)・ページを小さくしたものの3つ立てて確かめる。本物の api.todoist.com へは行かない。
#
# チャット(chat_net_test)は server/chat/chat_server.py を使い捨てのDBで、平文とHTTPS
# (上と同じ使い捨ての証明書)の2つ立てて確かめる。サーバ単体の権限まわり(chat_server_test.py)も
# ここで回す(同じプロセスの中で 127.0.0.1 の空きポートにサーバを立てる)。
#
# SSH(ssh_net_test)は OpenSSH の sshd を使い捨てのホスト鍵・利用者の鍵・設定で 127.0.0.1 に立てて
# 確かめる(公開鍵認証。sshd が無い環境では飛ばす。Debian/Ubuntuなら apt install openssh-server)。
# パスワード/keyboard-interactive認証は母艦に利用者を作る必要があるので自動では回さない
# (ssh_net_test.cpp の SSH_TEST_PW_* を参照)。
#
# 使い方: sh script/host_test/run_net.sh
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d)
PORT=${PICOOS_TEST_PORT:-8137}
TLS_PORT=$((PORT + 2))
# discoveryを持たないサーバ(素の静的ファイルサーバ)の再現用。
# クライアントが正しく縮退するかを見るために立てる
BARE_PORT=$((PORT + 1))

# SDは stubs/ のメモリ上のもの(何が書かれたかをそのまま検査できる)、
# TCPクライアントは stubs/WiFi.h 経由で pc/compat の本物のソケット実装を使う
g++ -std=gnu++17 -g -fsanitize=address,undefined \
    -I"$ROOT/script/host_test/stubs" -I"$ROOT/src" \
    "$ROOT/script/host_test/net_test.cpp" \
    "$ROOT/src/task/Http_Get.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    "$ROOT/src/net/Doc_Fetch.cpp" \
    "$ROOT/src/net/Discovery.cpp" \
    "$ROOT/src/net/Doc_Search.cpp" \
    "$ROOT/src/net/Manifest.cpp" \
    "$ROOT/src/storage/Doc_Cache.cpp" \
    "$ROOT/src/storage/SD_IO.cpp" \
    -o "$OUT/net_test" -lssl -lcrypto

echo "参照実装サーバを起動します (port $PORT)"
python3 "$ROOT/script/reference_server.py" \
    --root "$ROOT/examples" --port "$PORT" --home /doc.md \
    > "$OUT/server.log" 2>&1 &
SERVER_PID=$!

python3 "$ROOT/script/reference_server.py" \
    --root "$ROOT/examples" --port "$BARE_PORT" --no-discovery \
    > "$OUT/server_bare.log" 2>&1 &
BARE_PID=$!

# サーバの起動を待つ(最大5秒)
i=0
while [ $i -lt 50 ]; do
    if grep -q "終了" "$OUT/server.log" 2>/dev/null; then break; fi
    i=$((i + 1))
    sleep 0.1
done

TLS_PID=""
CHAT_PIDS=""
TODO_PIDS=""
SSHD_PID=""
cleanup(){
    kill "$SERVER_PID" "$BARE_PID" $TLS_PID $CHAT_PIDS $TODO_PIDS $SSHD_PID 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    wait "$BARE_PID" 2>/dev/null || true
    [ -n "$TLS_PID" ] && wait "$TLS_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo ""
echo "===== net_test ====="
"$OUT/net_test" "$PORT" "$BARE_PORT"

# ---- HTTPS: カレンダーの取得(CalendarSync) ----
g++ -std=gnu++17 -g -fsanitize=address,undefined \
    -I"$ROOT/script/host_test/stubs" -I"$ROOT/src" \
    "$ROOT/script/host_test/calendar_sync_test.cpp" \
    "$ROOT/src/calendar/Calendar_Sync.cpp" \
    "$ROOT/src/task/Http_Get.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    -o "$OUT/calendar_sync_test" -lssl -lcrypto

# 使い捨てのCAと、それで署名した localhost 用のサーバ証明書
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj "/CN=pico-os test CA" \
    -keyout "$OUT/ca.key" -out "$OUT/ca.pem" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" \
    -keyout "$OUT/server.key" -out "$OUT/server.csr" 2>/dev/null
printf "subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n" > "$OUT/server.ext"
openssl x509 -req -in "$OUT/server.csr" -CA "$OUT/ca.pem" -CAkey "$OUT/ca.key" -CAcreateserial \
    -days 2 -extfile "$OUT/server.ext" -out "$OUT/server.pem" 2>/dev/null

python3 "$ROOT/script/host_test/tls_test_server.py" \
    --port "$TLS_PORT" --cert "$OUT/server.pem" --key "$OUT/server.key" \
    > "$OUT/tls_server.log" 2>&1 &
TLS_PID=$!

i=0
while [ $i -lt 50 ]; do
    if grep -q "起動しました" "$OUT/tls_server.log" 2>/dev/null; then break; fi
    i=$((i + 1))
    sleep 0.1
done

echo ""
echo "===== calendar_sync_test ====="
"$OUT/calendar_sync_test" "$TLS_PORT" "$OUT/ca.pem"

# ---- チャット(ChatClient)。本物のチャットサーバを平文とHTTPSで立てる ----
CHAT_PORT=$((PORT + 3))
CHAT_TLS_PORT=$((PORT + 4))
CHAT="$ROOT/server/chat/chat_server.py"
CHAT_DB="$OUT/chat.db"

g++ -std=gnu++17 -g -fsanitize=address,undefined \
    -I"$ROOT/script/host_test/stubs" -I"$ROOT/src" \
    "$ROOT/script/host_test/chat_net_test.cpp" \
    "$ROOT/src/chat/Chat_Client.cpp" \
    "$ROOT/src/chat/Chat_Proto.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    -o "$OUT/chat_net_test" -lssl -lcrypto

ALICE=$(python3 "$CHAT" --db "$CHAT_DB" adduser alice --display ありす --password password1 | tail -1 | tr -d ' ')
BOB=$(python3 "$CHAT" --db "$CHAT_DB" adduser bob --display ぼぶ --password password2 | tail -1 | tr -d ' ')
# alice が作った部屋(オープン2つ・プライベート1つ・途中で消す1つ)。bob はテストの中で検索/参加コードから入る
python3 "$CHAT" --db "$CHAT_DB" addroom 雑談 --owner alice > /dev/null
python3 "$CHAT" --db "$CHAT_DB" addroom 連絡 --owner alice > /dev/null
python3 "$CHAT" --db "$CHAT_DB" addroom 秘密 --private --owner alice > /dev/null
python3 "$CHAT" --db "$CHAT_DB" addroom 消える部屋 --owner alice > /dev/null

# 無通信の接続を1秒で閉じさせる(使い回した接続が死んでいた場合の繋ぎ直しを確かめるため)
python3 "$CHAT" --db "$CHAT_DB" serve --host 127.0.0.1 --port "$CHAT_PORT" --idle-timeout 1 \
    > "$OUT/chat_server.log" 2>&1 &
CHAT_PIDS="$!"
python3 "$CHAT" --db "$CHAT_DB" serve --host 127.0.0.1 --port "$CHAT_TLS_PORT" \
    --tls-cert "$OUT/server.pem" --tls-key "$OUT/server.key" \
    > "$OUT/chat_tls_server.log" 2>&1 &
CHAT_PIDS="$CHAT_PIDS $!"

i=0
while [ $i -lt 50 ]; do
    if grep -q "起動しました" "$OUT/chat_server.log" 2>/dev/null \
       && grep -q "起動しました" "$OUT/chat_tls_server.log" 2>/dev/null; then break; fi
    i=$((i + 1))
    sleep 0.1
done

echo ""
echo "===== chat_net_test ====="
"$OUT/chat_net_test" "$CHAT_PORT" "$CHAT_TLS_PORT" "$OUT/ca.pem" "$ALICE" "$BOB" "$CHAT" "$CHAT_DB"

# ---- チャットサーバ単体(部屋の種類・参加コード・役割と権限・v1のDBからの移行) ----
echo ""
echo "===== chat_server_test ====="
python3 "$ROOT/script/host_test/chat_server_test.py"

# ---- TODO(TodoistClient)。Todoist API v1 の偽物を平文・HTTPS・ページ分けの3つ立てる ----
TODO_PORT=$((PORT + 5))
TODO_TLS_PORT=$((PORT + 6))
TODO_PAGED_PORT=$((PORT + 7))
TODO_FAKE="$ROOT/script/host_test/todoist_fake_server.py"
TODO_TOKEN=0123456789abcdef0123456789abcdef01234567

g++ -std=gnu++17 -g -fsanitize=address,undefined \
    -I"$ROOT/script/host_test/stubs" -I"$ROOT/src" \
    "$ROOT/script/host_test/todoist_net_test.cpp" \
    "$ROOT/src/todo/Todoist_Client.cpp" \
    "$ROOT/src/todo/Todoist_Proto.cpp" \
    "$ROOT/src/util/Json_Reader.cpp" \
    "$ROOT/src/task/Http_Request.cpp" \
    "$ROOT/src/net/Http_Transport.cpp" \
    "$ROOT/src/net/Http_Response.cpp" \
    -o "$OUT/todoist_net_test" -lssl -lcrypto

python3 "$TODO_FAKE" --port "$TODO_PORT" --token "$TODO_TOKEN" > "$OUT/todo_server.log" 2>&1 &
TODO_PIDS="$!"
python3 "$TODO_FAKE" --port "$TODO_TLS_PORT" --token "$TODO_TOKEN" \
    --cert "$OUT/server.pem" --key "$OUT/server.key" > "$OUT/todo_tls_server.log" 2>&1 &
TODO_PIDS="$TODO_PIDS $!"
python3 "$TODO_FAKE" --port "$TODO_PAGED_PORT" --token "$TODO_TOKEN" --page-size 4 --fillers 40 \
    > "$OUT/todo_paged_server.log" 2>&1 &
TODO_PIDS="$TODO_PIDS $!"

i=0
while [ $i -lt 50 ]; do
    if grep -q "起動しました" "$OUT/todo_server.log" 2>/dev/null \
       && grep -q "起動しました" "$OUT/todo_tls_server.log" 2>/dev/null \
       && grep -q "起動しました" "$OUT/todo_paged_server.log" 2>/dev/null; then break; fi
    i=$((i + 1))
    sleep 0.1
done

echo ""
echo "===== todoist_net_test ====="
"$OUT/todoist_net_test" "$TODO_PORT" "$TODO_TLS_PORT" "$OUT/ca.pem" "$TODO_TOKEN" "$TODO_PAGED_PORT"

# ---- SSH(SshClient)。本物の sshd を使い捨ての鍵と設定で立てる ----
SSHD=$(command -v sshd || ls /usr/sbin/sshd 2>/dev/null || true)
if [ -z "$SSHD" ] || ! command -v ssh-keygen > /dev/null; then
    echo ""
    echo "===== ssh_net_test ===== (sshd が無いので飛ばします)"
    exit 0
fi
SSH_PORT=$((PORT + 5))
SSH_DIR="$OUT/sshd"
mkdir -p "$SSH_DIR"
ssh-keygen -q -t ed25519 -N '' -f "$SSH_DIR/host_ed25519"
ssh-keygen -q -t ed25519 -N '' -f "$SSH_DIR/id_ed25519"
cp "$SSH_DIR/id_ed25519.pub" "$SSH_DIR/authorized_keys"
cat > "$SSH_DIR/sshd_config" <<EOF
Port $SSH_PORT
ListenAddress 127.0.0.1
HostKey $SSH_DIR/host_ed25519
AuthorizedKeysFile $SSH_DIR/authorized_keys
PidFile $SSH_DIR/sshd.pid
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
PermitRootLogin prohibit-password
StrictModes no
UsePAM no
EOF
# 特権分離のディレクトリ(root以外で動かす場合は既にあること)
[ -d /run/sshd ] || mkdir -p /run/sshd 2>/dev/null || true

gcc -c -O2 "$ROOT/lib/monocypher/src/monocypher.c" -o "$OUT/monocypher.o"
gcc -c -O2 -I"$ROOT/lib/monocypher/src" "$ROOT/lib/monocypher/src/monocypher-ed25519.c" -o "$OUT/monocypher-ed25519.o"
g++ -std=gnu++17 -g -fsanitize=address,undefined -DPICOOS_PC \
    -I"$ROOT/script/host_test/stubs" -I"$ROOT/src" -I"$ROOT/lib/monocypher/src" \
    "$ROOT/script/host_test/ssh_net_test.cpp" \
    "$ROOT/src/ssh/Ssh_Client.cpp" \
    "$ROOT/src/ssh/Ssh_Util.cpp" \
    "$ROOT/src/ssh/Vt_Terminal.cpp" \
    "$OUT/monocypher.o" "$OUT/monocypher-ed25519.o" \
    -o "$OUT/ssh_net_test"

"$SSHD" -D -e -f "$SSH_DIR/sshd_config" > "$SSH_DIR/sshd.log" 2>&1 &
SSHD_PID=$!
i=0
while [ $i -lt 50 ]; do
    if grep -q "listening" "$SSH_DIR/sshd.log" 2>/dev/null; then break; fi
    i=$((i + 1))
    sleep 0.1
done

echo ""
echo "===== ssh_net_test ====="
SSH_TEST_FINGERPRINT=$(ssh-keygen -l -f "$SSH_DIR/host_ed25519.pub" | cut -d' ' -f2) \
    "$OUT/ssh_net_test" "$SSH_PORT" "$SSH_DIR/id_ed25519" "$(id -un)" "$SSH_DIR/host_ed25519.pub"
