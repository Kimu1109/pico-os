# pico-os を PC / ブラウザで動かす

実機(RP2350)に書き込まずに、pico-os をそのまま動かすためのビルド。出口が2つある。

| | 出力 | 用途 |
|---|---|---|
| **ネイティブ** | SDL2のウィンドウ(`picoos_pc`) | 普段の開発。デバッガもプロファイラも使える |
| **Web** | WebAssembly(`index.html` + `.wasm`) | ブラウザで動かす。URLを渡すだけで他人にも触ってもらえる |

**`src/` のコードは実機ともWebとも同じものを使う。** 実機のライブラリだけを
`pc/compat/` の代替ヘッダへ差し替えている(`script/host_test/stubs` と同じ考え方)。
ネイティブとWebの違いは**ループの回し方(`main_pc.cpp`)とビルド設定だけ**で、
`compat/` の中身もほぼ共通。

## 必要なもの(ネイティブ)

- CMake 3.16 以降
- C++17 が通るコンパイラ(g++ / clang++)
- SDL2 の開発パッケージ
- OpenSSL の開発パッケージ(HTTPS用。実機はarduino-pico同梱のBearSSLを使うので、PCだけの依存)

```sh
# Debian / Ubuntu
sudo apt-get install build-essential cmake libsdl2-dev libssl-dev

# macOS (Homebrew)
brew install cmake sdl2 openssl
```

LovyanGFX は CMake が自動で取得する。**版は `platformio.ini` の `lib_deps` を読んで決める**ので、
実機ビルドとPCビルドで必ず同じ版になる(上げるときは `platformio.ini` の1行だけ直せばよい)。

`platformio.ini` 側が `^1.2.26` のような範囲指定だと「実際に落ちてくる版」が確定せず
PC側と食い違うため、その場合は configure がその旨を出して止まる。完全固定にすること。

手元にソースがあるならそれを使わせてもよい。取得するタグだけを差し替えることもできる:

```sh
cmake -S pc -B pc/build -DLOVYANGFX_DIR=/path/to/LovyanGFX
cmake -S pc -B pc/build -DLOVYANGFX_TAG=1.2.29
```

## ビルドと実行(ネイティブ)

```sh
cmake -S pc -B pc/build
cmake --build pc/build -j

./pc/build/picoos_pc
```

240x320 のウィンドウが開き、実機と同じ起動シーケンス(GFX → SD → Log → Touch →
Task → Network → Keyboard → IME → Time → Test → App)が走る。
**マウスの左ドラッグがタッチになる。**

### ヘッドレスで画面を確認する

CIや画面のないマシンでは、Nフレーム回してPPMへ書き出して終了できる。

```sh
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc --shot shot.ppm 40
```

## ブラウザで動かす (WebAssembly)

### 必要なもの

[Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) だけ。
SDL2はemscriptenのportsが持っているので、`libsdl2-dev` は要らない。

```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh        # このシェルで emcc / emcmake が使えるようになる
```

### ビルドと配信

```sh
emcmake cmake -S pc -B pc/build-web -DCMAKE_BUILD_TYPE=Release
cmake --build pc/build-web -j

emrun --no_browser --port 8080 pc/build-web    # → http://localhost:8080/index.html
# emrunが無ければ: python3 -m http.server -d pc/build-web 8080
```

**`file://` で開いても動かない。** `.wasm` を `fetch` で読むので、必ずHTTPで配信すること。

出力は `pc/build-web/` に4つ。丸ごと静的ホスティングへ置けばそのまま公開できる
(GitHub Pages等。サーバ側の設定もCORSも要らない)。

| ファイル | 中身 |
|---|---|
| `index.html` | ページの外枠。`pc/web/shell.html` から作られる |
| `index.js` | emscriptenのグルーコード |
| `index.wasm` | pico-os本体(既定で約1.7MB、`Release`で約1.3MB) |
| `index.data` | `pc/sdcard/` を固めたもの(仮想FSへ展開される) |

### ページでできること

- **マウスの左ドラッグがタッチ**(ネイティブと同じ)。スマホの指タッチも効く。
- `Serial` の出力がページ内のログ欄とブラウザのコンソールの両方に出る。
- 「画面をPNGで保存」ボタンで今の画面を落とせる(不具合の報告用)。
- Wi-Fiの状態はボタン(＝URLのクエリ)で差し替える。下記参照。
- 見た目や道具立てを足したいときは `pc/web/shell.html` を書き換える。

### 設定(環境変数の代わりにURLのクエリ)

ブラウザには環境変数が無いので、`main_pc.cpp` が起動時にURLのクエリを `setenv()` する。
**`compat/` 側は何も変わらない**(いつもどおり `getenv` を読むだけ)。

```
index.html?wifi=disconnected&rssi=-85
```

| クエリ | 対応する環境変数 |
|---|---|
| `wifi` | `PICOOS_WIFI_STATE` (`auto` / `connected` / `disconnected` / `ssid-not-found` / `failed`) |
| `rssi` | `PICOOS_WIFI_RSSI` |
| `ssid` | `PICOOS_WIFI_SSID` |
| `scan` | `PICOOS_WIFI_SCAN` |
| `render` | `PICOOS_RENDER_DRIVER` (`gl` を指定するとGPU描画。既定はソフトウェア描画。下記) |
| `spi_wait` | `PICOOS_SPI_WAIT` (`on` / `off`。液晶転送の待ち。Webの既定は`off`。下記「実機と違うところ」) |
| `sound` | `PICOOS_SOUND_STATE` (`auto` / `connected` / `disconnected`。音声のアンプの有無。下記「実機と違うところ」) |

`PICOOS_` で始まるキーはそのまま環境変数名として扱われるので、
将来増えたものは表に足さなくても `?PICOOS_XXX=...` で渡せる。

### ネイティブとの違い

| 項目 | Webでの扱い |
|---|---|
| ループ | `emscripten_set_main_loop()`。ブラウザのメインスレッドは止められないので、`Panel_sdl::main()`(別スレッド)は使わず1フレームずつ刻む |
| SDカード | `pc/sdcard/` を**ビルド時に**`index.data`へ焼き込む。**中身を変えたら再ビルドが必要**で、アプリ側からの書き込みはメモリ上だけ(リロードで消える) |
| 描画 | **SDLのソフトウェアレンダラ(canvas 2D)に固定**(下記)。GPU描画は `?render=gl` で試せる。どちらでも実測60fps |
| Wi-Fi | 疎通判定は `navigator.onLine`(ソケットが無いため)。固定したいときは `?wifi=...` |
| **Markdownブラウザのオンライン機能** | **使えない。** ブラウザには生のTCPソケットが無いので、`browser-home` を設定したりツールバーの「更新」「検索」を押してもサーバへ繋がらない。同梱のサンプル(`/tmp/doc.md`)を読む分にはそのまま動く |
| 液晶転送の待ち | 既定で**無効**(メインスレッドを空回りで止めることになるため)。`?spi_wait=on` で有効にできる |
| `delay()` | 何もせず即座に戻る(待つとタブが固まるため)。`src/` は使っていない |
| スレッド | 無し。`Panel_sdl` のデバッガ検出スレッドも起動しないが、動作に影響は無い |
| 速度 | 実測60fps(メインループ自体の負荷は数ms/120フレーム)。240x320を2倍で出す程度ではGPUを使っても差が出ない |

IMEの辞書(`sys/ime/skk_*.tsv`)を `pc/sdcard/` へ置くと、**そのサイズがそのまま
`index.data` に乗る**(初回ロードで全部ダウンロードされる)。Webで配る際は要注意。

### Webで画面が出ない場合(canvasの大きさ)

**SDLがウィンドウを作る間だけ、`main_pc.cpp` がcanvasのCSS上の大きさを1.5pxに固定する。**
`pinCanvasCssSizeForProbe()` / `releaseCanvasCssPin()` がそれで、**消さないこと**。

emscriptenのSDLは `Emscripten_CreateWindow()` で毎回こうする:

1. canvasの属性を **1x1** にする
2. CSS上の大きさ(`getBoundingClientRect`)を測る
3. **`floor(実測値) != 1` なら「CSSが大きさを決めている」(external_size)と見なし、
   実測値をそのまま画面の大きさに採用する**

CSSが何も指定していなければ 2. は 1x1 を返す……はずだが、**ページズームや端数の都合で
`0.9999998` のように1をわずかに下回る値が返ることがある**。すると `floor` で0になり、
「CSSが0を指定している」と解釈されて **canvasもSDLのウィンドウも 0x0 で作られる**。
こうなるとソフトウェア描画が `createImageData(0, 0)` で例外を投げ、**メインループが
1フレーム目で止まる**。画面は出ないのにC++のログだけ普通に出るので原因が見えにくい。

実際に出た報告がこれで、症状はこうだった:

```
Uncaught IndexSizeError: Failed to execute 'createImageData' ... The source width is zero
[PAGE] フレーム数=1 / canvas=0x0 / 描画=software
```

1.5pxを入れておけば、端数が出ても実測値は**1以上2未満**に収まり `floor` は必ず1になる。
つまり 3. の判定を「CSSは大きさを決めていない」側へ確実に倒せる。
あわせて、**レイアウト前(実測値が0)の間はウィンドウを作らせずに待つ**
(最大60フレーム。`canvasBoxReady()`)。

**external_size側へ倒してはいけない。** 一度は「480x640と明示すれば0にならない」と考えて
そうしたが、それだとSDLはCSS上の大きさを画面の大きさとして採用し、**以後ウィンドウの
内部サイズとCSSの箱を同期しなくなる**。LovyanGFXのSDLパネルは「ウィンドウの大きさは自分が
決める」前提で拡大率(`_update_scaling`)とタッチ座標の換算を組み立てているため、
**初期表示の縦横比が崩れ、タップ位置もずれる**(こちらも報告が出た)。値が正しくても
モードが変わってしまうのが問題なので、**判定を通したら固定は外す**。

- ページ側(`pc/web/shell.html`)は **canvasに `max-width` などを普通に書いてよい**。
  固定が外れたあとに効くだけで、SDLはマウス座標をCSS上の大きさで割り戻すため、
  縮小表示してもタップはずれない。
- 起動時の自己チェックはCSS上の大きさも出す:
  `[WEB] 画面を用意しました: canvas 480x640 (CSS上は480.0x640.0 / 描画=software)`

手元での再現(Playwrightの `addInitScript`):

- **端数** … `getBoundingClientRect` が `width * (1 - 2e-7)` を返すようにする
- **レイアウト前** … 最初の十数回の `getBoundingClientRect` が0を返すようにする

どちらも修正前は1フレーム目で落ち、修正後は落ちない。縦横比とタップ位置は
**不具合が出る前のビルド(8c06644)と一致すること**を、dpr 1 / 1.25 / 2 × 窓幅2種で確かめる。

### Webの描画をソフトウェアに固定してある理由

**Webビルドは既定でSDLのソフトウェアレンダラ(canvas 2D)を使う。** GPU(WebGL)描画は
`?render=gl` を付けたときだけ。速度のためではなく、**GPU描画では「フレームは進んでいるのに
画面が出ない」状態になりうる**ため。

LovyanGFXの `sdl_create()` は
`SDL_CreateRenderer(..., SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)` を要求する。
SDLのGLES2レンダラは、ウィンドウに `SDL_WINDOW_OPENGL` が立っていないと
**`SDL_RecreateWindow()` でウィンドウを作り直す**。そしてemscriptenのSDLは、ウィンドウを
壊すときcanvasそのものは壊せないので **0x0へ縮める**
(`SDL_emscriptenvideo.c`: "We can't destroy the canvas, so resize it to zero instead")。

つまり**GPU描画では起動のたびにcanvasが必ず一度 0x0 を通る**。作り直しに失敗すると
canvasは **0x0のまま**になり、C++側は何事もなく回り続けるのでログも普通に出る。
これが「画面だけ出ない」の正体で、`?render=gl` で実際にcanvasが一度0x0になるのは
DevToolsでも確認できる。

- **作り直しが失敗する条件はこちらからは予測できない**: SDLが要求するEGL/WebGLサーフェスの
  属性が通らない、GPUがブロックリスト入り、WebGLコンテキスト数の上限、など。
- **捨てcanvasへ `getContext('webgl')` が通ることは何の保証にもならない。**
  以前はWebGLの有無で切り替えていたが、**「WebGLあり・canvas 0x0」という報告**が出た。
- ソフトウェア描画では `SW_CreateRenderer` が `SDL_WINDOW_OPENGL` を要求しないため
  **ウィンドウの作り直しが起きない**。canvasは作成時の1回だけ設定され、0x0を通らない。
- SDLはヒント(`SDL_HINT_RENDER_DRIVER`)で名指ししたドライバを `SDL_RENDERER_ACCELERATED` の
  要求と突き合わせずに使うので、**LovyanGFX側は無改造でよい**。
- 速度はどちらも実測60fps。`?render=gl` は比較用で、WebGLが無い環境で指定した場合は
  警告を出してソフトウェア描画へ戻す(確実に真っ黒になるため)。

起動直後に**canvasが本当に作られたかをC++側で1回確認**し、ログへ出す:

```
[WEB] 描画=ソフトウェア(canvas 2D)。GPU描画を試すなら ?render=gl
[WEB] 画面を用意しました: canvas 480x640 (描画=software)
```

0x0だった場合は `SDL_GetError()` ごと書き出すので、次に同じことが起きたときは
**どこで失敗したかがログに残る**。

ページ側にも見張りを入れてある。読み込み4秒後に**まだ1フレームも描かれていない**か
**canvasが未生成**なら、フレーム数・canvasの大きさ・描画ドライバ・WebGLの有無(参考値)・
UserAgentをログ欄へ書き出す。
「画面をPNGで保存」も、canvasが未生成なら**壊れたファイルを落とさずに理由を出す**。

### ブラウザでC++をデバッグする

`-DCMAKE_BUILD_TYPE=Debug` でビルドするとDWARF情報が `.wasm` に入り、Chromeの
[C/C++ DevTools Support (DWARF)](https://chromewebstore.google.com/detail/cc++-devtools-support-dwarf/pdcpmagijalfljmkmjngeonclgbbannb)
拡張を入れればDevTools上でC++のソースのままブレークポイントを張れる。
ビルドは遅く `.wasm` も大きくなるので、普段は `Release` でよい。

### GitHub Pages へ自動公開

`.github/workflows/web-pages.yml` が面倒を見る。

| きっかけ | すること |
|---|---|
| `main` へpush | Webビルド → **https://kimu1109.github.io/pico-os/ へ公開** |
| プルリクエスト | ビルドが通るかだけ確認(公開はしない) |
| 手動 | Actionsタブの「Run workflow」 |

- **初回だけリポジトリの Settings > Pages で Source を「GitHub Actions」にする。**
  ここは手作業が要る。ワークフローから自動で有効化することはできない
  (`GITHUB_TOKEN` にPagesサイトを作る権限が無く、
  `Create Pages site failed: Resource not accessible by integration` で落ちる)。
  設定前に走らせると公開ジョブだけが失敗する(ビルドと成果物のアップロードは成功する)。
- emsdkの版はワークフロー先頭の `EMSDK_VERSION` で固定している。
  **上げるときは手元で同じ版を通してから**にすること。
- emsdkは丸ごとキャッシュされる(SDL2のportsのビルド結果も同じ場所に溜まるため)。
  初回は数分かかるが、2回目以降は短い。
- **公開されているのがどのコミットか**は、ページのログの先頭に出る:
  `[WEB] pico-os build: 1a2b3c4`。手元のビルドは `dev` と出る
  (`-DPICOOS_WEB_REV=...` で変えられる)。
- 公開するのは `index.html` / `index.js` / `index.wasm` / `index.data` の4つだけ。
  ビルドディレクトリのCMakeの中間物は含めない。

## SDカード

`pc/sdcard/` を実機のSDカードとして読む。実機のSDに置くファイルを同じ構成で置けば、
同じパスで読める。別の場所を使いたければ環境変数で差し替えられる:

```sh
PICOOS_SD_ROOT=/path/to/sd ./pc/build/picoos_pc
```

IMEの辞書(`sys/ime/skk_*.tsv`)はサイズが大きいのでリポジトリには入れていない。
無くても起動する(変換候補が出ないだけ)。

## Wi-Fi

**母艦のWi-Fi設定は変更しない。** `ConnectWiFiAsync()` が来ても実際にSSIDへ繋ぎに行くことは
せず、次のどちらかで状態を決める。

1. **疎通判定(既定)** — 母艦にインターネットへの経路があるかを見て接続/切断を返す。
   UDPソケットを `connect()` するだけなので**パケットは一切飛ばない**
   (`connect(2)` は経路表を引くだけで、UDPにはハンドシェイクが無い)。
2. **設定ファイル/環境変数による上書き** — 「切断」「電波が弱い」「SSIDが見つからない」を
   狙って再現できる。UIの各状態を確認したいときはこちら。

設定は `pc/sdcard/sys/network.cfg` に `pc-` 始まりのキーで書く(実機のパーサは知らないキーを
無視するので、同じファイルを実機と共有しても害はない)。

| キー | 意味 |
|---|---|
| `pc-wifi-state` | `auto`(既定) / `connected` / `disconnected` / `ssid-not-found` / `failed` |
| `pc-wifi-rssi` | 電波強度(dBm)。ステータスバーのアイコンの本数がこれで決まる |
| `pc-wifi-ssid` | `WiFi.SSID()` が返す名前 |
| `pc-wifi-scan` | スキャンで返す一覧(`SSID:RSSI` のカンマ区切り) |

環境変数のほうが設定ファイルより優先される。一時的に切り替えたいときに便利:

```sh
PICOOS_WIFI_STATE=disconnected ./pc/build/picoos_pc    # 切断アイコンの確認
PICOOS_WIFI_RSSI=-85 ./pc/build/picoos_pc              # 電波1本の確認
```

なお `NetworkFunctions::Setup()` は **`wifi-ssid` と `wifi-password` が両方空でないとき**しか
接続を開始しない。PCでも同じなので、両方に何か入れておくこと(同梱の `network.cfg` は
埋めてある)。

## 実機と違うところ

| 項目 | PCでの扱い |
|---|---|
| 画面 | LovyanGFX の `Panel_sdl`。既定は2倍表示(`PICOOS_PC_SCALE`) |
| 液晶への転送時間 | **実機のSPI転送にかかる理論上の時間だけ待つ**(`Panel_sdl_SpiWait`)。SPIは75MHz相当(`TFT_MAX_SPEED`=80MHzを要求しても、RP2350のclk_peri 150MHzを2分周した75MHzが上限)、1ピクセル16bitで、**画面1枚で16.4ms**。4bpp→RGB565の変換にかかるCPU時間は含まないので、実機はこれより少し遅い。`PICOOS_SPI_WAIT=off` で無効にできる |
| タッチ | SDLのマウス。座標はSDL側でパネル座標へ戻されるので拡大率の影響を受けない |
| SDカード | `pc/sdcard/` を実ファイルシステムとして読む |
| Wi-Fi | 母艦の疎通を見て接続/切断を返す。設定で任意の状態に固定もできる(下記) |
| NTP / 時刻 | 同期しない。**必要ない** — PCの時計をそのまま使うので最初から正しい時刻が出る |
| 音声 | `pc/compat/I2S.h` がSDLの音声出力へ流す。アンプ(MAX98357A)の検出ピンは「音声デバイスを開けたら刺さっている」として答える。`/sys/sound.cfg` の `pc-sound-state`(`auto`/`connected`/`disconnected`)か `PICOOS_SOUND_STATE` で固定できる。ヘッドレスで音の中身を確かめるなら `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw`(22050Hz/16bit/ステレオの生データ)。Webはブラウザの自動再生の制限で、最初にクリック等をするまで鳴らない |
| GPIO / SPI | 何もしない空実装(`digitalRead()`は既定でHIGH。音声の検出ピンだけ上のとおり) |

## 構成

```
pc/
  CMakeLists.txt            ビルド定義(ネイティブ / Emscripten を分岐)
  main_pc.cpp               エントリポイント(ネイティブ=Panel_sdl::main / Web=emscripten_set_main_loop)
  web/
    shell.html              Webビルドのページの外枠(canvas + ログ + デバッグ用ボタン)
  compat/                   実機ライブラリの代替ヘッダ(src/ より先にインクルードされる)
    Arduino.h               millis/delay/GPIO/Serial
    SPI.h                   SPIClassRP2040 の空実装
    WiFi.h                  疎通判定ベースのWiFi + NTP(同期不要)
    SdFat.h                 実ファイルシステムを SdFat/FsFile として見せる
    XPT2046_Touchscreen.h   使わないが includeが通るように置いてある
    config/LGFX_Config_PC.hpp        SDLパネル設定
    config/Panel_sdl_SpiWait.hpp     液晶への書き込みに実機のSPI転送時間ぶんの待ちを入れるSDLパネル
    functions/Touch_Functions_PC.hpp マウスをタッチとして読む
  sdcard/                   SDカードとして読まれるディレクトリ
```

`src/config/LGFX_Config.hpp` と `src/functions/Touch_Functions.hpp` の2つだけは、
`#if defined(PICOOS_PC)` で `pc/compat/` 側へ切り替える分岐を持っている。
ハードウェアそのものを触る箇所なので、インクルードパスの差し替えでは吸収できないため。
