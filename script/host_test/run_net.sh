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
# チャット(chat_net_test)は server/chat/chat_server.py を使い捨てのDBで、平文とHTTPS
# (上と同じ使い捨ての証明書)の2つ立てて確かめる。
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
cleanup(){
    kill "$SERVER_PID" "$BARE_PID" $TLS_PID $CHAT_PIDS 2>/dev/null || true
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
python3 "$CHAT" --db "$CHAT_DB" addroom 雑談 > /dev/null
python3 "$CHAT" --db "$CHAT_DB" addroom 連絡 > /dev/null

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
"$OUT/chat_net_test" "$CHAT_PORT" "$CHAT_TLS_PORT" "$OUT/ca.pem" "$ALICE" "$BOB"
