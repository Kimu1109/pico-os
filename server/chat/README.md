# pico-chat — pico-os 用の自前チャットサーバ

pico-os のチャットアプリと、ブラウザ(スマホ・PC)から使う、友達数人向けの小さなチャットサーバ。
Raspberry Pi で動かして、Let's Encrypt の証明書で HTTPS 化して外から使う想定。

- **Python 3 の標準ライブラリだけ**で動く(`pip install` 不要)。発言は SQLite の1ファイルに保存する
- 部屋は2種類: **オープンチャット**(誰でも検索して参加できる)と **プライベートチャット**(検索に出ず、
  参加している人が発行する**参加コード**でだけ入れる。コードは30分で無効)
- 部屋ごとに役割(オーナー/管理者/参加者)があり、追い出し・追放・管理者の任命などができる(下の「部屋の種類と権限」)
- 未読の数は人ごと・部屋ごとにサーバが覚える(Pico と Web で共通)
- Web の画面(`web/index.html`)も同じサーバが配る
- 通信の仕様はリポジトリ直下の [`CHAT_PROTOCOL.md`](../../CHAT_PROTOCOL.md)

| ファイル | 中身 |
|---|---|
| `chat_server.py` | サーバ本体と管理コマンド(ユーザー/部屋の追加など) |
| `web/index.html` | Web クライアント(1ファイル) |
| `pico-chat.service` | systemd のユニット |
| `certbot-deploy-hook.sh` | 証明書を取った/更新したときに、サーバが読める場所へ写すスクリプト |

## まず手元で試す(平文HTTP)

```sh
cd server/chat
python3 chat_server.py --db /tmp/chat.db adduser alice --display ありす   # パスワードを聞かれる
python3 chat_server.py --db /tmp/chat.db addroom 雑談 --owner alice   # オープンチャット(Webからも作れる)
python3 chat_server.py --db /tmp/chat.db serve --port 8080
# → ブラウザで http://localhost:8080/ を開いて alice でログイン
```

`adduser` の最後に出るのが pico-os 用のトークン。PCビルドの pico-os で試すなら
`pc/sdcard/sys/chat.cfg` に次のように書く(`--port 8080` のサーバへ繋がる):

```
server = http://127.0.0.1:8080
token = (adduserで出た43文字)
```

## Raspberry Pi に置いて外から使う(HTTPS)

### 0. 用意するもの

- **ドメイン名**。家の回線のIPアドレスは変わるので、DDNS を使う
  (例: [DuckDNS](https://www.duckdns.org/) なら `xxxx.duckdns.org` が無料で持てる。Raspberry Pi に更新用の cron を入れる)
- **ルータのポート開放**: TCP の **80番と443番** を Raspberry Pi へ転送する
  - 80番は Let's Encrypt の確認(ACME http-01)に要る。証明書の更新(約60日ごと)でも使うので閉じないこと。
    80番へ来たそれ以外のアクセスは https へ転送する
  - **回線によってはポートを開けられない**(IPv6 IPoE の MAP-E/DS-Lite、CGNAT など)。
    その場合この手順はそのままでは使えない(Cloudflare Tunnel 等で外から入る道を別に作る必要がある)
- Raspberry Pi OS(Python 3.9 以上)

### 1. ファイルを置く

```sh
# サーバ専用のユーザー(ログインしない)
sudo useradd --system --home /var/lib/pico-chat --shell /usr/sbin/nologin pico-chat

sudo install -d -o root -g root -m 755 /opt/pico-chat /opt/pico-chat/web
sudo install -m 644 chat_server.py /opt/pico-chat/
sudo install -m 644 web/index.html /opt/pico-chat/web/
sudo install -m 755 certbot-deploy-hook.sh /opt/pico-chat/

sudo install -d -o pico-chat -g pico-chat -m 750 /var/lib/pico-chat /var/lib/pico-chat/acme
```

### 2. サーバを起動する

証明書はまだ無いが、先に起動してよい。**証明書が置かれるまでは80番(Let's Encrypt の確認用)だけで待ち、
置かれた時点で443番を開く。**

```sh
sudo cp pico-chat.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now pico-chat
journalctl -u pico-chat -f      # 「証明書がまだありません。置かれるまで待ちます」と出ていればよい
```

### 3. Let's Encrypt の証明書を取る

```sh
sudo apt install certbot
sudo certbot certonly --webroot -w /var/lib/pico-chat/acme \
    -d xxxx.duckdns.org \
    --deploy-hook /opt/pico-chat/certbot-deploy-hook.sh
```

- 取れると `certbot-deploy-hook.sh` が証明書を `/etc/pico-chat/tls/` へ写し、サーバが数秒で443番を開く
  (ログに「チャットサーバを起動しました (https://...)」)
- **更新は自動**。`apt install certbot` で入る systemd の timer が1日2回確認し、期限の30日前に取り直す。
  そのときも同じ `--webroot` と `--deploy-hook` が使われ、**サーバは再起動せずに新しい証明書へ切り替わる**
  (ファイルの更新時刻を見て、次の接続から読み直す)
- 更新の予行演習: `sudo certbot renew --dry-run`

### 4. ユーザーと部屋を作る

```sh
cd /var/lib/pico-chat
sudo -u pico-chat python3 /opt/pico-chat/chat_server.py --db chat.db adduser alice --display ありす
sudo -u pico-chat python3 /opt/pico-chat/chat_server.py --db chat.db adduser bob --display ぼぶ
sudo -u pico-chat python3 /opt/pico-chat/chat_server.py --db chat.db addroom 雑談 --owner alice
```

部屋は Web の「部屋を作る」からも作れる(作った人がオーナー)。友達にはログイン名とパスワードを伝え、
`https://xxxx.duckdns.org/` を開いてもらう。オープンチャットには「オープンチャットを探す」から、
プライベートチャットには参加者から聞いた参加コードを「参加コードで入る」に打って入る。

### 5. pico-os をつなぐ

Web にログインして **「pico-os の設定」** を押すと、次のような内容が出る。これを pico-os の
SD カードの **`/sys/chat.cfg`** として保存する(押すたびにトークンが発行し直され、前のものは使えなくなる)。

```
server = https://xxxx.duckdns.org
token = (43文字)
```

- pico-os は Let's Encrypt のルート証明書(ISRG Root X1/X2)を最初から信頼しているので、追加の設定は要らない
- HTTPS は **時計が合う(NTP同期)まで繋がらない**。Wi-Fi に繋いでしばらく待ってからアプリを開く
- 新着は3秒ごとに取りに行く。変えるなら `chat.cfg` に `poll-ms = 5000` のように書く

## 管理コマンド

`--db` は全コマンドで同じファイルを指すこと(環境変数 `PICO_CHAT_DB` でも指定できる)。

| コマンド | 内容 |
|---|---|
| `adduser <名前> [--display 表示名]` | ユーザーを作る(パスワードを聞かれる)。pico-os 用のトークンも表示する |
| `passwd <名前>` | パスワードを変える(ログイン中の Web は全てログアウトされる) |
| `token <名前>` | pico-os 用のトークンを発行し直して表示する(Web の「pico-os の設定」と同じ) |
| `deluser <名前>` | ログインとトークンを無効にする(過去の発言は残る) |
| `users` / `rooms` | 一覧(部屋は種類と人数も) |
| `addroom <名前> [--private] [--owner 名前]` | 部屋を作る。既定はオープンチャット。プライベートチャットは `--owner` 必須 |
| `delroom <部屋>` | 部屋を発言ごと消す |
| `members <部屋>` | 参加者と役割(追放中の人も) |
| `setrole <部屋> <名前> owner\|admin\|member` | 役割を決める(参加していなければ参加させる、追放も解く)。オーナーのいない部屋にオーナーを置くときなど |

`<部屋>` は id(`rooms` で出る `#3` か `3`)か、オープンチャットの名前。プライベートチャットは名前が重なり得るので id で指定する。

## 部屋の種類と権限

詳しくは [`CHAT_PROTOCOL.md`](../../CHAT_PROTOCOL.md) の「部屋の種類」「参加コード」「役割と権限」。

| | オープンチャット | プライベートチャット |
|---|---|---|
| 見つけ方 | 「オープンチャットを探す」(名前の一部で検索) | 検索には出ない |
| 入り方 | 検索結果の「参加」 | 参加者が「招待」で出したコードを「参加コードで入る」に打つ |

| 役割 | できること |
|---|---|
| 参加者 | 読む・書く・メンバーを見る・参加コードを出す・抜ける |
| 管理者 | + 自分より弱い人の追い出し(また入れる)・追放(解くまで入れない)・追放を解く・参加コードの全無効化 |
| オーナー(1人) | + 管理者の任命/解任・オーナーの譲渡・部屋の削除 |

- 参加コードは**30分で無効**。「1人だけ使えるコードにする」も選べる。サーバはコードのハッシュだけを持つ
- コードを外し続けると(10分に10回)しばらく受け付けない
- オーナーが抜けると管理者(いなければ一番古い参加者)が引き継ぐ。最後の1人が抜けた部屋は消える
- pico-os からできるのは「探して参加」「参加コードで入る」「招待(コードを出す)」まで。作る・抜ける・メンバーの管理は Web で

### v1 からの更新

前の版のDBをそのまま使える。**最初に起動したときに自動で移行**し、既存の部屋は全てオープンチャットになり、
その時点のユーザーは全員が全部の部屋の参加者になる(アップデートで部屋が見えなくならないように)。
既存の部屋にはオーナーがいないので、要るなら `setrole <部屋> <名前> owner` で置く。
移行の前に `sqlite3 chat.db ".backup backup.db"` でバックアップを取っておくと安心。

## 安全のために

- トークン・セッションは**ハッシュだけ**を保存する(DB が漏れてもそのまま使えない)。パスワードは PBKDF2
- ログインは同じ IP から10分に10回失敗すると断る
- Web の書き込みは Cookie + 独自ヘッダ(`X-Pico-Chat`)を必須にしている(他のサイトから送らせられない)
- **pico-os のトークンは SD に平文で置かれる**。SD を無くしたら Web の「pico-os の設定」で発行し直せば
  前のトークンは使えなくなる
- バックアップは `/var/lib/pico-chat/chat.db` を写すだけでよい(`sqlite3 chat.db ".backup backup.db"` が安全)

## 制限

| 項目 | 値 | 理由 |
|---|---|---|
| 発言1件 | 500バイト(日本語で約160文字) | pico-os 側の固定長バッファ |
| pico-os から送れる長さ | 約60文字 | オンスクリーンキーボードの入力欄の上限(191バイト) |
| 表示名・部屋名 | 45バイト(日本語で15文字) | 同上 |
| pico-os の部屋の一覧・検索結果 | 16件まで | RAM |
| pico-os が持つ発言 | 部屋ごとに新しい30件 | RAM(1件約580B) |
| 絵文字 | pico-os では表示されない | フォントが BMP の範囲しか持たない |
