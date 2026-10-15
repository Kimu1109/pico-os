# アプリストアの配布物(APP_REPO v1)

pico-os のアプリストア(ランチャの「ストア」)が、Web からLuaアプリを取ってきて `/lua/apps/` へ入れるための取り決め。
**静的なファイルだけで成り立つ**ので、GitHub Pages・Raspberry Pi の nginx・`python3 -m http.server` のどれでも同じように配れる。
配る側の道具は `script/build_app_repo.py`、本体側は `src/store/`(`App_Repo` が読み取り、`App_Store` が通信とインストール)と `StoreScene`。

## 置き方

配布元のURL(既定 `https://kimu1109.github.io/pico-os/apps/`)の下に:

```
catalog.tsv              一覧(1行1アプリ)
catalog.sig              catalog.tsv の Ed25519 署名
<id>/<version>/files.tsv そのアプリのファイル一覧
<id>/<version>/<path>    アプリのファイル(main.lua, app.cfg, ...)
```

- 文字コードはUTF-8、改行はLF(CRLFも読む)。列はタブ区切り。空行と `#` で始まる行は読み飛ばす。
- **知らない列は無視する**(前方互換)。列を足すときは行の後ろへ。

### catalog.tsv

```
pico-os-apps<TAB>1<TAB><serial>
<id><TAB><version><TAB><dir><TAB><name><TAB><bytes><TAB><files_sha256><TAB><perms><TAB><description>
...
```

| 列 | 中身 |
|---|---|
| 1行目 | `pico-os-apps`・形式の番号 `1`・**serial**(32bitの符号なし整数。作るたびに増やす。既定はUNIX時刻) |
| id | 英小文字・数字・`-`・`_`、23文字まで。アプリを見分ける名前(app.cfg の `id` と同じ) |
| version | 数字と `.`(`1.2.10`)。数として左から比べる |
| dir | `/lua/apps/` の下のディレクトリ名(日本語可。区切り・FATで使えない文字・先頭の`.`・末尾の空白と`.`は不可) |
| name | 表示名(47バイトまで表示) |
| bytes | 全部のファイルの合計バイト数(4MiBまで) |
| files_sha256 | `files.tsv` のSHA-256(16進64桁) |
| perms | 使う権限。`network` `notify` `sd`(= `permission_sd_outside_app_dir`) `background`(= `permission_background`)のカンマ区切り、無ければ `-`。**知らない名前があればその行を読まない** |
| description | 説明(190バイトまで表示) |

### catalog.sig

```
ed25519 <公開鍵 16進64桁> <署名 16進128桁>
```

署名するのは **`"pico-os-apps-v1\0"`(16バイト) + SHA-256(catalog.tsv)(32バイト)** の48バイト(Ed25519、RFC 8032)。

### files.tsv

```
<path><TAB><bytes><TAB><sha256>
```

- path: アプリのディレクトリからの相対パス。区切りは `/`、各区切りは `[A-Za-z0-9._-]` で `.` から始めない、4段・95文字まで。
- **大文字小文字を区別しない昇順**に並べる(SDのFATでは大小が同じ名前になるので、重なりを並びで弾く)。
- `main.lua` と `app.cfg` を必ず含む。64ファイル・1ファイル1MiB・合計4MiB・files.tsv自体8KiBまで。

### app.cfg

いつもの app.cfg(`LuaAppScanner`)に、配るときは `id` が要る。`version` はカタログと同じ値。

```
name=テトリス
id=tetris
version=2.0.0
icon=icon.pimg
permission_network=false
```

## 本体が確かめること

1. **一覧**: catalog.tsv と catalog.sig を取り、署名の鍵が信頼できるか(下)・署名が合うかを見る。合えば `/sys/store/catalog.tsv` へ保存。
   **同じURLで前に見た serial より小さい一覧は断る**(古い署名済みの一覧へ戻されるのを防ぐ。`/sys/store/state.cfg`)。
2. **インストール**: files.tsv を取ってカタログの `files_sha256` と照合 → 行の形・並び・数・合計・必須ファイルを確かめる →
   各ファイルを取って大きさとSHA-256を照合 → 取ったapp.cfgの `id`・`version` がカタログと同じで、**権限がカタログの perms を超えない**ことを確かめる →
   `/sys/store/staging/` から `/lua/apps/<dir>/` へ1つずつ移し、登録簿(ランチャ)へ入れる。
   - 入れる前にカタログの権限を確認のダイアログで見せる。
   - 更新では、前の版の一覧(`/sys/store/installed/<id>.tsv`)にあって新しい版に無いファイルだけを消す。**アプリが自分で書いたファイル(セーブ等)は残る**。
   - 同じ名前のディレクトリに id の無いアプリ(手で入れたもの)があれば、上書きの確認を出す。別の id のアプリがあれば入れない。
3. **削除**: `/lua/apps/<dir>/` を丸ごと消し、登録簿から外す。

途中で失敗・中止したら置き場を消し、入っているものには触らない(移している最中に電源が切れた場合だけ新旧が混ざる。入れ直せば揃う)。

## 信頼する鍵

署名の鍵は次のどちらかに一致すれば信頼する。どちらにも無ければ一覧を読まない。

- ファームに焼き込んだ鍵: `src/store/App_Repo_Keys.hpp` の `kBuiltin`
- SDの `/sys/store.cfg` の `key = <16進64桁>`(4行まで)

SDへ書ける人はアプリを `/lua/apps/` に直接置けるので、SDの鍵を信頼しても守りは弱くならない。
**秘密鍵は手元のPCにだけ置き、リポジトリにも配布元のサーバにも置かない。** サーバが乗っ取られても、偽のアプリは署名が合わずに入らない。

## /sys/store.cfg

```
url = https://kimu1109.github.io/pico-os/apps/
key = 0123...(公開鍵)
```

どちらも無くてよい(url の既定は上)。url はストアの [設定] でも変えられる。

## 配り方

```sh
# 鍵を作る(1回だけ)。表示された公開鍵を store.cfg か App_Repo_Keys.hpp へ
python3 script/build_app_repo.py keygen --key ~/.pico-os/app_repo.key

# Raspberry Pi 等へ置く: 配布物をまるごと作って rsync
python3 script/build_app_repo.py build --key ~/.pico-os/app_repo.key --out /tmp/apps --clean
rsync -a --delete /tmp/apps/ pi:/var/www/pico-os/apps/

# GitHub Pages: 署名した目録だけをコミットする(アプリの中身はワークフローが写す)
python3 script/build_app_repo.py sign --key ~/.pico-os/app_repo.key --out store
git add store && git commit -m "ストアの目録を更新"
```

- 配るのは `pc/sdcard/lua/apps/*/app.cfg` に `id` があるアプリだけ。gitの管理下ならgitが管理しているファイルだけを配る(遊んでいる間にできたセーブ等を配らない)。
- **GitHub Pages では、アプリを変えたら `sign` をやり直してコミットすること。** 署名の後で中身が変わったアプリは、ワークフローが警告を出して配らない(本体では一覧に出るが、入れようとすると404で止まる)。
- GitHub Pages は `Cache-Control: max-age=600` なので、公開してから最大10分ほど古い一覧が返ることがある。
- 静的なサーバならどこでもよい。Raspberry Pi 5 + Let's Encrypt なら nginx で `root` を置くだけ(ISRG Root X1/X2 は焼き込みのルートにある)。

## 制限値

| 値 | 上限 |
|---|---|
| カタログのアプリ | 40件(それより後ろは読まない) |
| catalog.tsv | 32KiB、1行510バイト |
| catalog.sig | 256バイト |
| files.tsv | 8KiB、64行 |
| 1ファイル / 1アプリ | 1MiB / 4MiB |
| パス | 95文字・4段 |

## 今は無いもの

- 依存関係・本体の版の条件(`min_os` 等)。必要になったら catalog.tsv に列を足す。
- 差分の更新(更新は全部のファイルを取り直す)。
- 投稿・検索・ダウンロード数(動的なサーバが要る。Raspberry Pi に置くときの次の段)。
- アイコンをインストール前に一覧で見せること(今はアイコンの絵はインストール後にランチャで出る)。
