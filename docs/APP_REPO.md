# アプリストアの配布物(APP_REPO 形式2)

pico-os のアプリストア(ランチャの「ストア」)が、Web からLuaアプリを取ってきて `/lua/apps/` へ入れるための取り決め。
**静的なファイルだけで成り立つ**ので、GitHub Pages・Raspberry Pi の nginx・`python3 -m http.server` のどれでも同じように配れる。
配る側の道具は `script/build_app_repo.py`、本体側は `src/apps/store/`(`App_Repo` が読み取り、`App_Store` が通信、`App_Install` がインストール、
`App_Package` がSDに置いたパッケージから入れる)と `StoreScene`。同じ場所にブラウザで見る一覧のページ(`index.html`)と、
SDに置けば入るパッケージ(下の「パッケージ(.papp)」)も置く。
誰でも投稿できるようにする運用(投稿用リポジトリ)は `docs/design/lua-apps.md`「アプリストアの投稿」。

## 鍵は2種類

| 鍵 | 持つ人 | 署名するもの | 本体が信頼する根拠 |
|---|---|---|---|
| カタログ鍵 | 配る人(投稿用リポジトリでは、そのCIのEnvironmentのSecret) | catalog.tsv | ファームに焼き込んだ鍵か `/sys/store.cfg` の `key`(下の「信頼する鍵」) |
| 作者の鍵 | アプリの作者の手元 | そのアプリの files.tsv | 初めて入れたときにカタログの `author_key` を覚え、更新では同じ鍵だけを受け付ける |

アプリを書き換えるには作者の鍵が要る。**カタログ鍵が漏れても、もう入っているアプリを偽の更新で書き換えることはできない**
(カタログ鍵だけでできるのは、新しいアプリを一覧に出すこと・取り下げの印を付けることまで)。
pico-os の公式アプリは、手元の同じ鍵で両方を署名する。

## 置き方

配布元のURL(既定 `https://kimu1109.github.io/pico-os-apps/`。投稿用リポジトリ [kimu1109/pico-os-apps](https://github.com/Kimu1109/pico-os-apps) の Pages。
2026-10-26 より前のファームの既定は `https://kimu1109.github.io/pico-os/apps/`)の下に:

```
catalog.tsv              一覧(1行1アプリ)
catalog.sig              catalog.tsv の署名(カタログ鍵)
<id>/<version>/files.tsv そのアプリのファイル一覧
<id>/<version>/files.sig files.tsv の署名(作者の鍵)
<id>/<version>/<path>    アプリのファイル(main.lua, app.cfg, ...)
index.html               ブラウザで見る一覧(build_app_repo.py site。本体は読まない)
<id>-<version>.papp      パッケージ(同上。下の「パッケージ(.papp)」)
<id>-<version>.png       一覧のページのアイコン(app.cfg の icon の .pimg から作る)
```

id に `.` は入らないので、`<id>/` のディレクトリと `.papp`・`.png` の名前は重ならない。

- 文字コードはUTF-8、改行はLF(CRLFも読む)。列はタブ区切り。空行と `#` で始まる行は読み飛ばす。
- **知らない列は無視する**(前方互換)。列を足すときは行の後ろへ。どの列にもタブは入れない。

### catalog.tsv

```
pico-os-apps<TAB>2<TAB><serial>
<id><TAB><version><TAB><dir><TAB><name><TAB><bytes><TAB><files_sha256><TAB><perms><TAB><description><TAB><author_key><TAB><author><TAB><status>
...
```

| 列 | 中身 |
|---|---|
| 1行目 | `pico-os-apps`・形式の番号 `2`・**serial**(32bitの符号なし整数。作るたびに増やす。既定はUNIX時刻) |
| id | 英小文字・数字・`-`・`_`、23文字まで。アプリを見分ける名前(app.cfg の `id` と同じ) |
| version | 数字と `.`(`1.2.10`)。数として左から比べる |
| dir | `/lua/apps/` の下のディレクトリ名(日本語可。区切り・FATで使えない文字・先頭の`.`・末尾の空白と`.`は不可) |
| name | 表示名(47バイトまで表示) |
| bytes | 全部のファイルの合計バイト数(4MiBまで) |
| files_sha256 | `files.tsv` のSHA-256(16進64桁) |
| perms | 使う権限。`network` `notify` `sd`(= `permission_sd_outside_app_dir`) `background`(= `permission_background`) `microphone`(= `permission_microphone`)のカンマ区切り、無ければ `-`。**知らない名前があればその行を読まない** |
| description | 説明(190バイトまで表示) |
| author_key | 作者の公開鍵(16進64桁。全部0は不可) |
| author | 表示用の作者名(投稿用リポジトリでは投稿したGitHubのアカウント名。47バイトまで表示) |
| status | `-`(普通) / `yanked`(取り下げ) / `blocked`(危険と判断したもの)。**知らない値は `yanked` と同じに扱う** |

- `yanked`: 新しく入れられない。入っていなければ一覧に出さない。入っているものはそのまま。
- `blocked`: 新しく入れられない。入っている端末では一覧の先頭に赤で並べ、状態の行で削除をすすめる。

### catalog.sig

```
ed25519 <公開鍵 16進64桁> <署名 16進128桁>
```

署名するのは **`"pico-os-apps-v2\0"`(16バイト) + SHA-256(catalog.tsv)(32バイト)** の48バイト(Ed25519、RFC 8032)。

### files.tsv

```
<path><TAB><bytes><TAB><sha256>
```

- path: アプリのディレクトリからの相対パス。区切りは `/`、各区切りは `[A-Za-z0-9._-]` で `.` から始めない、4段・95文字まで。
- **大文字小文字を区別しない昇順**に並べる(SDのFATでは大小が同じ名前になるので、重なりを並びで弾く)。
- `main.lua` と `app.cfg` を必ず含む。64ファイル・1ファイル1MiB・合計4MiB・files.tsv自体8KiBまで。

### files.sig

形は catalog.sig と同じ。公開鍵はカタログの `author_key` と同じでなければならない。
署名するのは **`"pico-os-app-v2\0\0"`(16バイト) + SHA-256(`<id><TAB><version><TAB><files_sha256 16進>` + LF)** の48バイト。
id と版も署名に入れるのは、あるアプリの署名を別のアプリや別の版に付け替えられないようにするため。

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
   **同じURL・同じカタログ鍵で前に見た serial より小さい一覧は断る**(古い署名済みの一覧へ戻されるのを防ぐ。`/sys/store/state.cfg` の `url`・`key`・`serial`)。
   鍵ごとに数えるのは、漏れた鍵で最大の serial を署名されても、鍵を替えれば数え直せるようにするため。形式1の一覧は読まない。
2. **インストール**: files.tsv を取ってカタログの `files_sha256` と照合 → files.sig を取り、公開鍵が `author_key` と同じか・署名が合うかを確かめる →
   行の形・並び・数・合計・必須ファイルを確かめる → 各ファイルを取って大きさとSHA-256を照合 →
   取ったapp.cfgの `id`・`version` がカタログと同じで、**権限がカタログの perms を超えない**ことを確かめる →
   `/sys/store/staging/` から `/lua/apps/<dir>/` へ1つずつ移し、登録簿(ランチャ)へ入れる。
   - 入れる前に、作者名とカタログの権限を確認のダイアログで見せる。
   - 入れたら `author_key` を `/sys/store/installed/<id>.key` に覚える。**更新で `author_key` が覚えた鍵と違えば入れない**
     (表示は「作者の鍵が変わりました」)。削除すると覚えた鍵も消えるので、入れ直せば新しい鍵で入る(利用者が自分で決めたことにする)。
     形式1で入れたもの(`<id>.key` が無い)は、次に入れたときに覚える。
   - **入っている版より古い版は入れない**(漏れたカタログ鍵で、作者が昔署名した穴のある版へ戻されるのを防ぐ)。同じ版の入れ直しはできる。
   - `status` が `-` でないものは入れない。
   - 更新では、前の版の一覧(`/sys/store/installed/<id>.tsv`)にあって新しい版に無いファイルだけを消す。**アプリが自分で書いたファイル(セーブ等)は残る**。
   - 同じ名前のディレクトリに id の無いアプリ(手で入れたもの)があれば、上書きの確認を出す。別の id のアプリがあれば入れない。
3. **削除**: `/lua/apps/<dir>/` を丸ごと消し、登録簿から外す。`installed/<id>.tsv`・`<id>.key` も消す。

途中で失敗・中止したら置き場を消し、入っているものには触らない(移している最中に電源が切れた場合だけ新旧が混ざる。入れ直せば揃う)。

## パッケージ(.papp)

1つのアプリを1ファイルにまとめたもの。Webのアプリストアのページ(配布元のURLの `index.html`)の DOWNLOAD で落とし、
**SDカードの直下に置くと、次の起動(とUSBドライブを終えたとき)に本体が入れる**(Wi-Fiが無くても入る)。
作るのは `build_app_repo.py site`(配布物の全部のアプリ)と `package`(手元のアプリ1つ)、読むのは `src/apps/store/App_Package`。

```
pico-os-app<TAB>1<TAB><files.tsv のバイト数>       1行目: 印・形式の番号・次に続く files.tsv の大きさ
<catalog.tsv の1行と同じもの>                       2行目: id・版・dir・名前・…・author_key・author・status
ed25519 <作者の公開鍵> <署名>                       3行目: files.sig と同じもの
<files.tsv>                                        ちょうど1行目のバイト数
<各ファイルの中身>                                  files.tsv の行の順に、区切りなしで続ける
```

- 名前は `<id>-<版>.papp`(本体は名前を見ない。直下の `*.papp` なら大小を問わず全部読む。ブラウザが `(1)` を足しても入る)。
- 改行はLF(1〜3行目はCRLFも読む)。知らない形式の番号は読まない。最後のファイルの後ろに余分なバイトがあれば断る。
- 本体が確かめること: ストアから入れるとき(上の「本体が確かめること」の2)と同じ。files.tsv のSHA-256が2行目の
  `files_sha256` と同じか → 3行目の鍵が `author_key` と同じで署名が合うか → 各ファイルの大きさとSHA-256 →
  app.cfg の id・版・権限(2行目の perms を超えない)→ 覚えた作者の鍵・古い版・同じ名前の別のidのアプリ。
  `status` が `-` でないものも入れない。
- **カタログの署名は無い**。SDへ置ける人はアプリを `/lua/apps/` に直接置けるので、守りは弱くならない。
  それでも作者の署名は確かめるので、途中で壊れたもの・書き換えたものは入らず、入れた後はストアと同じく
  覚えた作者の鍵でしか更新できない(ストアから入れたものをパッケージで更新することも、その逆もできる)。
- 同じ名前のディレクトリに id の無いアプリ(手で入れたもの)があれば、確かめずに上書きする(置いたこと自体を利用者の決定とする。
  手で置いた他のファイルは残る)。
- 入れたパッケージは消す。入れられなかったものは `<名前>.err` へ改名し(起動のたびに同じ失敗を繰り返さない)、
  理由を通知に出す。1回に入れるのは8個まで(残りは次の起動で)。

## 信頼する鍵

カタログ鍵は次のどちらかに一致すれば信頼する。どちらにも無ければ一覧を読まない。

- ファームに焼き込んだ鍵: `src/apps/store/App_Repo_Keys.hpp` の `kBuiltin`
- SDの `/sys/store.cfg` の `key = <16進64桁>`(4行まで)

SDへ書ける人はアプリを `/lua/apps/` に直接置けるので、SDの鍵を信頼しても守りは弱くならない。
**秘密鍵は手元のPCにだけ置き、リポジトリにも配布元のサーバにも置かない**(投稿用リポジトリのカタログ鍵だけは例外で、CIのSecretに置く。
その代わりに作者の鍵がある)。サーバが乗っ取られても、偽のアプリは署名が合わずに入らない。

## /sys/store.cfg

```
url = https://kimu1109.github.io/pico-os-apps/
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

# Webのページ(index.html)とパッケージ(<id>-<版>.papp)・アイコンのPNGを配布物に足す(秘密鍵は要らない)
python3 script/build_app_repo.py site --dir /tmp/apps

# 手元のアプリを1つパッケージにする(ストアを通さずに渡す。作者の鍵で署名する)
python3 script/build_app_repo.py package --key ~/.pico-os/author.key --app pc/sdcard/lua/apps/テトリス

# 投稿用リポジトリ(kimu1109/pico-os-apps)へ出す: 作者が apps/<id>/ に署名してPRを出す
# (カタログの署名と GitHub Pages への公開は、そのリポジトリのCIがする。手順はそのリポジトリの README.md)
python3 script/build_app_repo.py author-sign --key ~/.pico-os/author.key --app ../pico-os-apps/apps/tetris \
    --src pc/sdcard/lua/apps/テトリス
```

- `--key` がカタログ鍵、`--author-key` が作者の鍵(既定は `--key` と同じ)、`--author` が表示する作者名(既定 `kimu1109`)。
- 配るのは `pc/sdcard/lua/apps/*/app.cfg` に `id` があるアプリだけ。gitの管理下ならgitが管理しているファイルだけを配る(遊んでいる間にできたセーブ等を配らない)。
- **pico-os の `store/`(`sign` の出力)は移行中の古い目録**で、もう更新しない(既定のURLが `/pico-os/apps/` のままの古いファームのために、
  pico-os の Pages が配り続けている)。署名の後で中身が変わったアプリは、ワークフローが警告を出して配らない(本体では一覧に出るが、入れようとすると404で止まる)。
- `assemble`・`verify` は形式1の目録も通す(警告を出す)。形式2のファームは形式1を読まないので、`sign` をやり直すこと。
- GitHub Pages は `Cache-Control: max-age=600` なので、公開してから最大10分ほど古い一覧が返ることがある。
- 静的なサーバならどこでもよい。Raspberry Pi 5 + Let's Encrypt なら nginx で `root` を置くだけ(ISRG Root X1/X2 は焼き込みのルートにある)。

## 制限値

| 値 | 上限 |
|---|---|
| カタログのアプリ | 40件(それより後ろは読まない。一覧のページ分けを入れたら大きさだけで決める) |
| catalog.tsv | 32KiB(ページ分けの後は256KiBの予定)、1行640バイト |
| catalog.sig / files.sig | 256バイト |
| files.tsv | 8KiB、64行 |
| 1ファイル / 1アプリ | 1MiB / 4MiB |
| パス | 95文字・4段 |

## 今は無いもの

- **一覧のページ分けと検索**(投稿を受け付けると40件を超える。catalog.tsv をSDに置いたまま1ページずつ読み、
  名前・説明・作者の部分一致で探す予定。`docs/design/lua-apps.md`「アプリストアの投稿」の順番の3)。
- 作者の鍵の引き継ぎ(古い鍵で新しい鍵に署名して、「作者の鍵が変わりました」を出さずに替える)。
- 依存関係・本体の版の条件(`min_os` 等)。必要になったら catalog.tsv に列を足す。
- 差分の更新(更新は全部のファイルを取り直す)。
- ダウンロード数・評価(動的なサーバが要る)。
- 本体のストアでアイコンをインストール前に一覧で見せること(今はインストール後にランチャで出る。Webのページでは見せる)。
