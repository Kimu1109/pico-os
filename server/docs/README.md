# pico-docs — pico-os の Markdown ブラウザ用のドキュメントサーバ

pico-os の Markdown ブラウザ(`MarkdownScene`)が文書を読み・検索する相手。
本体は参照実装の [`script/tools/reference_server.py`](../../script/tools/reference_server.py)(Python 3 の標準ライブラリだけ)で、
ここにはそれを **手元(127.0.0.1)で動かし、Apache でベースパスを変えて外へ公開する** ための設定を置く。
チャットサーバ(`server/chat/README.md`「Apache の後ろに置いて外から使う」)と同じ形。

- 通信の仕様はリポジトリ直下の [`docs/spec/PROTOCOL.md`](../../docs/spec/PROTOCOL.md)
- 文書は `.md` をディレクトリに置くだけ。画像は `.pimg`(`script/tools/generate_pimg.py` で変換)

| ファイル | 中身 |
|---|---|
| `pico-docs.service` | systemd のユニット(127.0.0.1:8090、`--base-path /docs`) |
| `apache-pico-docs.conf` | Apache 2.4 の設定の例(`/docs/` の下と `/.well-known/pico-os` をサーバへ転送する) |

```
pico-os ──https──> Apache(443番、証明書) ──http──> reference_server.py(127.0.0.1:8090)
        https://example.com/docs/            /docs/ の下と /.well-known/pico-os だけ転送
```

## まず手元で試す

```sh
python3 script/tools/reference_server.py --root examples --port 8090 --base-path /docs --home /doc.md
curl http://127.0.0.1:8090/.well-known/pico-os     # home / search / manifest が /docs/... で返る
curl -i http://127.0.0.1:8090/docs/                # home(/docs/doc.md)へ転送される
```

`--check` で文書がクライアントの上限(8KB・128ブロック等)に収まっているかを調べられる。

## Apache の後ろに置いて外から使う

ドメイン名とポート開放(443番)・証明書の取り方は、チャットサーバの `server/chat/README.md` と同じ。
既に Apache で https のサイトを出しているなら、そこへ足すだけでよい。

### 1. サーバを手元で起動する

```sh
sudo useradd --system --home /srv/pico-docs --shell /usr/sbin/nologin pico-docs
sudo install -d -o root -g root -m 755 /opt/pico-docs /srv/pico-docs
sudo install -m 644 script/tools/reference_server.py /opt/pico-docs/
sudo cp -r ~/my-docs/. /srv/pico-docs/            # 配る文書(.md と .pimg)。pico-docs ユーザーが読めればよい

sudo cp server/docs/pico-docs.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now pico-docs
journalctl -u pico-docs -f
```

- `--host 127.0.0.1` なので、サーバへは同じ機械の Apache からしか届かない
- `--base-path /docs` で、`/docs/` の下に置かれているものとして動く。**サーバが返すパス**
  (サーバ情報の `home`・`search`・`manifest`、検索結果とマニフェストの `path`、`/docs/` の転送先)**は全部 `/docs` 付きになる**
  (pico-os はそれをそのまま GET するため)。Apache がパスの頭を外して渡しても動く
- `--home` は文書ルートの中のパスで書く(`/index.md`。`/docs` は付けない)
- `--trust-proxy` は、ログに出す送り元を `X-Forwarded-For` の一番右(Apache が見た相手)にするだけ(このサーバに認証は無い)
- 文書を足したり直したりしたら置き換えるだけでよい(要求のたびに更新時刻を見て索引を作り直す。再起動は要らない)

### 2. Apache で公開する

```sh
sudo a2enmod proxy proxy_http headers
```

`server/docs/apache-pico-docs.conf` の「ここから」〜「ここまで」を https の `<VirtualHost *:443>` の中へ貼り、

```sh
sudo apachectl configtest && sudo systemctl reload apache2
curl https://xxxx.duckdns.org/.well-known/pico-os   # home が /docs/index.md なら繋がっている
```

- **`/.well-known/pico-os` はホストの頭(`/docs` の下ではない)にも転送する。** Markdown ブラウザはホストごとに
  ここを見に来て検索・マニフェストの場所を知る(`docs/spec/PROTOCOL.md`「サーバ情報」)。転送しないと文書は読めるが検索ができない
  - そのため **1つのホストに置けるドキュメントサーバは1つだけ**(`/.well-known/pico-os` が指す先)
- **圧縮しない**(`no-gzip`)。pico-os は gzip を解けず、mod_deflate が付ける `ETag` の `-gzip` で条件付きGETも外れる
- チャットサーバと同じ VirtualHost に並べてよい(`/chat/` と `/docs/` はぶつからない)

### 3. pico-os で開く

Markdown ブラウザのアドレスに**文書のURL**(`https://xxxx.duckdns.org/docs/index.md`)を入れる。
以後は[ホーム]で home の文書へ、検索もそのサーバの中で引ける。

- `https://xxxx.duckdns.org/docs/` のような**ディレクトリのURLは pico-os では開けない**(ルートの `/` と同じで、
  プロトコルが既定の文書名を決めていないため。`apps/browser/Doc_Fetch.cpp`)。ブラウザ(PC・スマホ)なら home へ転送される
- 文書の中のリンクは**相対パスで書く**(`[次へ](setup/wifi.md)`、`![図](img/a.pimg)`)。
  `/setup/wifi.md` のような `/` 始まりのリンクはホストの頭から引かれるので、`/docs` の下にならない
- pico-os は Let's Encrypt 等の主なルート証明書を最初から信頼している。HTTPS は時計が合う(NTP同期)まで繋がらない
