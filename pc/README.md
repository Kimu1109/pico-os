# pico-os を PC で動かす

実機(RP2350)に書き込まずに、PC上のウィンドウで pico-os をそのまま動かすためのビルド。
デバッガもプロファイラも使えるので、GUI周りの試行錯誤はこちらが速い。

**`src/` のコードは実機とまったく同じものを使う。** 実機のライブラリだけを
`pc/compat/` の代替ヘッダへ差し替えている(`script/host_test/stubs` と同じ考え方)。

## 必要なもの

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

## ビルドと実行

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
  CMakeLists.txt            ビルド定義
  main_pc.cpp               エントリポイント(Panel_sdl::main から setup()/loop() を回す)
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
