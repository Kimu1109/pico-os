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

```sh
# Debian / Ubuntu
sudo apt-get install build-essential cmake libsdl2-dev

# macOS (Homebrew)
brew install cmake sdl2
```

LovyanGFX は CMake が自動で取得する(`platformio.ini` の `^1.2.26` に合わせて 1.2.28)。
手元にソースがあるならそれを使わせてもよい:

```sh
cmake -S pc -B pc/build -DLOVYANGFX_DIR=/path/to/LovyanGFX
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

`PICOOS_` で始まるキーはそのまま環境変数名として扱われるので、
将来増えたものは表に足さなくても `?PICOOS_XXX=...` で渡せる。

### ネイティブとの違い

| 項目 | Webでの扱い |
|---|---|
| ループ | `emscripten_set_main_loop()`。ブラウザのメインスレッドは止められないので、`Panel_sdl::main()`(別スレッド)は使わず1フレームずつ刻む |
| SDカード | `pc/sdcard/` を**ビルド時に**`index.data`へ焼き込む。**中身を変えたら再ビルドが必要**で、アプリ側からの書き込みはメモリ上だけ(リロードで消える) |
| Wi-Fi | 疎通判定は `navigator.onLine`(ソケットが無いため)。固定したいときは `?wifi=...` |
| `delay()` | 何もせず即座に戻る(待つとタブが固まるため)。`src/` は使っていない |
| スレッド | 無し。`Panel_sdl` のデバッガ検出スレッドも起動しないが、動作に影響は無い |
| 速度 | 実測60fps(メインループ自体の負荷は数ms/120フレーム)。描画はWebGL経由 |

IMEの辞書(`sys/ime/skk_*.tsv`)を `pc/sdcard/` へ置くと、**そのサイズがそのまま
`index.data` に乗る**(初回ロードで全部ダウンロードされる)。Webで配る際は要注意。

### ブラウザでC++をデバッグする

`-DCMAKE_BUILD_TYPE=Debug` でビルドするとDWARF情報が `.wasm` に入り、Chromeの
[C/C++ DevTools Support (DWARF)](https://chromewebstore.google.com/detail/cc++-devtools-support-dwarf/pdcpmagijalfljmkmjngeonclgbbannb)
拡張を入れればDevTools上でC++のソースのままブレークポイントを張れる。
ビルドは遅く `.wasm` も大きくなるので、普段は `Release` でよい。

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
| タッチ | SDLのマウス。座標はSDL側でパネル座標へ戻されるので拡大率の影響を受けない |
| SDカード | `pc/sdcard/` を実ファイルシステムとして読む |
| Wi-Fi | 母艦の疎通を見て接続/切断を返す。設定で任意の状態に固定もできる(下記) |
| NTP / 時刻 | 同期しない。**必要ない** — PCの時計をそのまま使うので最初から正しい時刻が出る |
| GPIO / SPI | 何もしない空実装 |

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
    functions/Touch_Functions_PC.hpp マウスをタッチとして読む
  sdcard/                   SDカードとして読まれるディレクトリ
```

`src/config/LGFX_Config.hpp` と `src/functions/Touch_Functions.hpp` の2つだけは、
`#if defined(PICOOS_PC)` で `pc/compat/` 側へ切り替える分岐を持っている。
ハードウェアそのものを触る箇所なので、インクルードパスの差し替えでは吸収できないため。
