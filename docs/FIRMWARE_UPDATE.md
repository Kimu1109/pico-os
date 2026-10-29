# ファームの更新(Web / SD / Wi-Fi)

USBでつながなくても、Webの版の一覧から選ぶか、SDカードに置いたファイルか、PCからのWi-Fi経由でファームを書き換えられる。
ランチャの「**本体の更新**」から行う(ファイルアプリで `.pfw` を開いても同じ画面へ来る)。

- 書き換えそのものは arduino-pico のOTAのローダー(どのファームにも入っている、フラッシュの先頭の約12KB)が、
  次の起動のときに行う。本体は更新ファイルの中身を内蔵フラッシュのLittleFS(更新専用の置き場)へ置いて再起動するだけ
- **自動のロールバックは無い**。新しいファームが起動しなくなったら、BOOTSELボタンを押しながらUSBにつなぎ、
  前の `firmware.uf2` を書き込んで戻す(手元に前の版の `.uf2` を残しておくこと。
  直近50本のナイトリービルドは https://github.com/Kimu1109/pico-os/releases にも残っている)
- 書き換えの途中で電源が落ちても、次の起動でローダーが続きから書き直す(指示は書き終えるまで残る)

## 最初に1回だけ: 署名の鍵

更新ファイルは Ed25519 で署名し、本体は**ファームに焼き込んだ公開鍵**で確かめる
(SDの設定ファイルの鍵は読まない。SDへはLuaアプリも書けるため)。

```sh
python3 script/firmware_tool.py keygen          # ~/.pico-os/firmware.key を作り、公開鍵の1行を表示する
```

1. 表示された `{ { 0x.., ... } },` の1行で `src/apps/firmware/Fw_Keys.hpp` の `kBuiltin` を置き換える
2. そのファームを**USBで1回**書き込む(以後はその鍵で署名した更新を受け付ける)
3. **ナイトリーに署名させるため**、`~/.pico-os/firmware.key` の中身(16進64桁の1行)を
   GitHubのリポジトリの Settings → Secrets and variables → Actions → New repository secret に
   名前 `FIRMWARE_SIGNING_KEY` で登録する(下の「Webから取る」)

秘密鍵は手元のPCとGitHubのSecretsにだけ置く。リポジトリには置かない。
別の場所に置くなら `--key` か環境変数 `PICOOS_FW_KEY` で指定する。
Secretsに置くのは利用者と決めた(2026-10-29。ナイトリーとWebからの更新を署名済みで回すため)。
鍵を渡すのは main で動くナイトリーと安定版のワークフローだけで、PRのビルド(`firmware-build.yml`)には渡さない。
鍵が漏れたら `keygen --force` で作り直し、`Fw_Keys.hpp` を差し替えたファームをUSBで書き込み、Secretsも替える。

## Webから取る

「本体の更新」→[**Webから**]で、GitHub Pages に置いた版の一覧
(`https://kimu1109.github.io/pico-os/firmware/index.tsv`)を取り、選んだ版を書き込む。Wi-Fiと時計(HTTPSの証明書の期限を見るため)が要る。

1. 一覧には、**今の本体と同じ構成**(`platformio.ini` の env。版の情報に入っている)で**署名のある**版だけが出る。
   `fw-allow-unsigned = true` のときは署名なしも「無署名」と付けて出る。今の版は「(今)」、古い版は「(古)」
2. 選ぶと SD の `/firmware/<ファイル名>.part` へ落としながら SHA-256 を取り、一覧の大きさ・SHA-256 と合えば `.part` を外す
3. あとは「SDから選ぶ」と同じ流れ(内容を確かめるダイアログ → 置き場へ写す → 再起動)。
   **置き場へ写し終えたら落としたファイルは消す**(失敗・取り消しのときは残るので、[SDから]で選び直せる)

一覧の置き場は SD の `/sys/firmware.cfg` で変えられる(自前の置き場・PCビルドでの確認用):

```
url = https://example.com/pico-os/firmware/
```

### 一覧を作る側(CI)

```
ナイトリー(nightly.yml)  ─┐   GitHubのリリース            web-pages.yml が写す         本体
安定版(firmware-stable.yml)┴─→ nightly-* / v<通し番号> ──→ Pages の /firmware/ ──────→ [Webから]
                               firmware.pfw(署名済み)     index.tsv + *.pfw
```

- **ナイトリー**は `firmware.pfw` を Secrets の `FIRMWARE_SIGNING_KEY` で署名してからリリースへ載せる
  (`script/firmware_tool.py sign`。署名は見出しだけに掛かり、見出しに中身のSHA-256が入っているので、ビルドした後から付けられる)。
  Secretsに鍵が無ければ署名なしのまま載せて警告を出す。鍵が `Fw_Keys.hpp` の公開鍵と違えば止まる
- **安定版**は Actions の「ファームの安定版を出す」を手動で実行し、安定版にするナイトリーのタグ(`nightly-<通し番号>-<短いハッシュ>`)を入れる。
  同じ添付を署名し直して `v<通し番号>` のリリースを作る(ナイトリーは50本を超えると消えるが、安定版は消さない)
- どちらも最後に Pages の公開(`web-pages.yml`)を動かす。公開のたびに `script/build_fw_index.py` が
  **新しい安定版5本とナイトリー3本**の `firmware.pfw` を落として `/firmware/` へ写し、`index.tsv` を作る。
  同じ版が安定版とナイトリーの両方にあれば安定版の1行にする。読めない・壊れた更新ファイルは飛ばす
- リリースから直接落とさないのは、リリースの添付が別のホストへの長いURLの転送になり、本体のURL(クエリ192バイト)に入らないため。
  Pagesはアプリストアと同じホスト(`kimu1109.github.io`)

### 一覧の形式(index.tsv)

```
# pico-os-fw-index 1
<通し番号>\t<git>\t<日時(UTC)>\t<stable|nightly>\t<構成>\t<署名 1|0>\t<バイト数>\t<SHA-256>\t<ファイル名>
```

- 新しい順。本体は上から16件まで読む。10列目より後ろは読み飛ばす(列を足しても古い本体が読める)。読めない行は飛ばす
- 構成は更新ファイルの中身(`firmware.bin`)の版の印 `env=` から取る。2026-10-29より前のファームには無いので `rpipico2w` とみなす
- バイト数とSHA-256は更新ファイル(`.pfw`)全体のもの。ファイル名は英数字と `. _ -` だけで `.pfw` で終わる
- **一覧そのものには署名しない**。書き込む前に、落とした更新ファイルの署名(焼き込みの鍵)をいつも通り確かめるため。
  偽の一覧でできるのは古い署名済みの版を勧めることくらいで、それは「古い版は本体で確認する」で止まる

## 更新ファイルを作る(SDに置く)

```sh
pio run -e rpipico2w -t fwpack       # .pio/build/rpipico2w/firmware.pfw ができる(鍵があれば署名される)
# または
python3 script/firmware_tool.py pack --bin .pio/build/rpipico2w/firmware.bin --out firmware.pfw
```

SDの `/firmware/` に置き(USBドライブ機能でPCから置ける)、「本体の更新」→[SDから選ぶ]、
またはファイルアプリでタップ。内容(版・大きさ・署名)を確かめるダイアログで[書き込む]を押すと、
置き場へ写して確かめ、再起動して書き換わる。起動すると「ファームを更新しました」と通知が出る。

## Wi-Fiで送る(開発向け)

1. 本体で「本体の更新」→[**受け取る**](IPアドレスとポート `8370` が出る)
2. PCで
   ```sh
   pio run -e rpipico2w -t fwpush --upload-port 192.168.1.23
   # または
   python3 script/firmware_tool.py push --host 192.168.1.23 --bin .pio/build/rpipico2w/firmware.bin
   ```
   IPアドレスの代わりに本体の名前(mDNS。既定 `picoos.local`、設定アプリのWi-Fiタブの[名前]で変えられる)も使える:
   `--upload-port picoos.local`(PCがmDNSを引ける場合。macOS・avahiの入ったLinux・Windows 10以降)
署名済みで今より新しい版なら、確認なしでそのまま書き込んで再起動する。古い版・同じ版・署名を確かめられない
ファイルは、本体で確認してから(PCは確認を待つ)。待ち受けるのは[受け取る]を押している間だけで、
画面を離れるとやめる。

## 署名なしの更新(開発中)

鍵を登録していない間や、署名せずに試したいときは、SDの `/sys/debug.cfg` に

```
fw-allow-unsigned = true
```

と書くと、署名の無い(確かめられない)更新ファイルも受け付ける。**その場合も毎回本体で確認する**
(Luaアプリが debug.cfg を書き換えても、本体の画面で押さない限り書き込まれない)。
CIのPRのビルド(Actionsの「実機ファームのビルド」の成果物 `firmware.pfw`)は署名なし。
ナイトリー・安定版のリリースに添付されるものは、Secretsに鍵があれば署名済み(上の「Webから取る」)。

## 断る場合

- 署名が合わない(壊れている・書き換えられている)/ 署名が無い・知らない鍵(`fw-allow-unsigned` が無いとき)
- 別の基板向け、見出しが壊れている、形式の版が違う
- **LittleFSの割り当て(`platformio.ini` の `board_build.filesystem_size`)が今のファームと違う** →
  割り当てを変えた版は**USBで書き込む**(置き場の位置が変わるため)
- 解いた大きさがアプリの領域(フラッシュの先頭〜LittleFSの手前)に収まらない、gzipが置き場に入らない
- 置き場へ書いて読み直した中身が見出しのSHA-256と合わない、gzipの末尾の大きさが見出しと違う
- 電池で動いていて残りが30%未満(USBにつなぐか充電する)

## フラッシュの割り当て(4MB)

| 範囲 | 中身 |
|---|---|
| `0x10000000`〜 | OTAのローダー(約12KB)+ ファーム(上限 約2.31MB) |
| `0x1024F000`〜`0x103FF000` | LittleFS 1728KB(更新の置き場) |
| `0x103FF000`〜 | EEPROM(4KB、未使用) |

更新ファイルの中身は `firmware.bin` を gzip したもの(今は約67%)。1728KB はファームの上限(約2.31MB)を
gzip したものが入る大きさとつり合うように決めた(2026-10-16のファームは約1.95MB → gzip 約1.37MB)。

## 形式(.pfw)

```
[見出し 128バイト][署名 64バイト][中身 = firmware.bin の gzip]
```

見出し(リトルエンディアン):

| 位置 | 大きさ | 中身 |
|---|---|---|
| 0 | 8 | `PICO-FW1` |
| 8 | 2 | 見出しの大きさ(128) |
| 10 | 2 | 形式の版(1) |
| 12 | 4 | 印(bit0 = 署名あり。他のビットは0) |
| 16 | 16 | 基板 `rpipico2w`(NULで埋める) |
| 32 | 4 | 通し番号(`git rev-list --count HEAD`) |
| 36 | 4 | ビルドの日時(UNIX時刻、HEADのコミットの日時) |
| 40 | 16 | 短いコミットのハッシュ(手元に変更があれば末尾に `+`) |
| 56 | 4 | 解いた大きさ(firmware.bin) |
| 60 | 4 | 中身(gzip)の大きさ |
| 64 | 4 | フラッシュの先頭(`0x10000000`) |
| 68 | 4 | LittleFSの始まり(firmware.bin の `0x2FF0`) |
| 72 | 4 | LittleFSの終わり(同 `0x2FF4`) |
| 76 | 32 | 中身のSHA-256 |
| 108 | 20 | 0 |

署名 = Ed25519(`"pico-os-fw-v1\0\0\0"`(16バイト)+ 見出し128バイト)。署名なしなら印のbit0を0、署名の欄を全部0にする。
版・コミット・日時は `firmware.bin` の中の `PICOOS-BUILD;serial=..;git=..;date=..;board=..;`
(`src/apps/firmware/Fw_Version.cpp`)から写すので、見出しと中身の版は必ず一致する。

### TCPの取り決め(ポート8370)

```
PC   → 見出し+署名(192バイト)
本体 → "OK" | "WAIT <理由>"(本体で確認中。続けて "OK" か "ERR ...") | "ERR <理由>"
PC   → "OK" を受け取ってから中身
本体 → "DONE"(約1.5秒後に再起動) | "ERR <理由>"
```
各行はUTF-8で改行(`\n`)まで。20秒何も届かなければ本体が切る。

## 版

ビルドのたびに `script/pio_firmware.py`(`platformio.ini` の `extra_scripts`)が通し番号・コミット・日時を
`$BUILD_DIR/pico_build_info/Pico_Build_Info.h` へ書く。版は「本体の更新」の画面・起動時のログ・シリアルの `hw info` で見られる。
ビルドの構成(env の名前)も書き、`PICOOS-BUILD;...;board=rpipico2w;env=<構成>;` としてファームに置く(Webの一覧が同じ構成の版を選ぶのに使う)。
通し番号は履歴の数なので、浅いクローン(`git clone --depth`)では小さくなる(CIは `fetch-depth: 0` で全部取る)。
PC/Webビルドは `#0 (dev)`。

## PC/Webビルドでの確認

置き場はSDのルートの隣の `ota_emu/`(`PICOOS_OTA_DIR` で変えられる)で真似る。選ぶ→確認→置き場へ書く→読み直す→
再起動、まで通るが、**本体は書き換わらない**(起動すると「PCビルドは本体を書き換えません」と出る)。
待ち受けのポートは `PICOOS_FW_PORT` で変えられる。Webビルドは待ち受けられない(生のソケットが無い)。
[Webから]は `/sys/firmware.cfg` の `url` を手元の `python3 -m http.server` へ向けて確かめられる。PCビルドの構成は
`rpipico2w` として扱う(`PICOOS_FW_ENV` で変えられる)。
