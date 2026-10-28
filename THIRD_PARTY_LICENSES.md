# 第三者のライセンス(Third-party licenses)

pico-os のために書いたファイルは [MIT ライセンス](LICENSE)(Copyright (c) 2026 Kimu1109)。
下の表のものは他者の著作物で、それぞれのライセンスに従う。全文は「ライセンス文」の列のファイルか配布元を見ること。

## リポジトリに同梱しているもの

### コード

| もの | 置き場所 | ライセンス | 著作権者 | ライセンス文 |
|---|---|---|---|---|
| Lua 5.4.7 | `lib/lua/` | MIT | Lua.org, PUC-Rio | `lib/lua/LICENSE` |
| Monocypher | `lib/monocypher/` | BSD-2-Clause または CC0-1.0 の選択 | Loup Vaillant ほか | `lib/monocypher/LICENCE.md` |
| Peanut-GB | `lib/peanut_gb/` | MIT | Mahyar Koshkouei | `lib/peanut_gb/LICENSE` |
| QR Code generator library | `lib/qrcodegen/` | MIT | Project Nayuki | `lib/qrcodegen/LICENSE` |
| TJpgDec(ホストテスト用の写し) | `script/host_test/stubs/lgfx/utility/lgfx_tjpgd.c` | ChaN のライセンス(著作権表示を残せば利用・改変・再配布自由) | ChaN | ファイル冒頭 |
| pngle(ホストテスト用の写し) | `script/host_test/stubs/lgfx/utility/lgfx_pngle.c` | MIT | kikuchan(LovyanGFX向けの改変は lovyan03) | ファイル冒頭 |
| miniz(ホストテスト用の写し) | `script/host_test/stubs/lgfx/utility/lgfx_miniz.c` | パブリックドメイン(Unlicense) | Rich Geldreich | ファイル冒頭と末尾 |
| LovyanGFX の `pgmspace.h`(ホストテスト用の写し。pngle が読む) | `script/host_test/stubs/lgfx/utility/pgmspace.h` | BSD | lovyan03 | ファイル冒頭(LovyanGFX の `license.txt`) |
| Blocks-TI-84(Luaアプリ「ブロック」の移植元と絵) | `pc/sdcard/lua/apps/ブロック/`・`script/assets/blocks_assets/` | MIT | The Science Elf | `script/assets/blocks_assets/LICENSE-Blocks-TI-84.txt` |

### データ

| もの | 置き場所 | ライセンス | 備考 |
|---|---|---|---|
| SKK辞書(SKK-JISYO.M / .ML) | `script/dict/src/` | **GPL-2.0-or-later** | IMEの辞書(`sys/ime/`)はこれの改変物で、同じGPLで配る |
| 英和・和英辞書(JMdict / EDICT由来) | `script/dict/src/en-ja-and-ja-en.tsv` | **CC BY-SA 4.0** | 辞書(`sys/dict/`)も同じライセンスで配る |
| Tabler Icons | `script/tabler_icons/` | MIT(Paweł Kuna) | `src/gui/icons/icons_data.h` の一部はこれから生成 |
| dmg-acid2(GBエミュの確認用ROM) | `pc/sdcard/gb/dmg-acid2.gb` | MIT(Copyright (c) 2020 Matt Currie) | 全文は `pc/sdcard/README.md` |
| 芥川龍之介「蜘蛛の糸」 | `pc/sdcard/books/` | パブリックドメイン(著作権の保護期間が満了) | |

辞書の詳しい条件は [`script/dict/LICENSES.md`](script/dict/LICENSES.md)。
辞書はファームに組み込まず、SDに置いて実行時に読むデータなので、pico-os のコード(MIT)には GPL / CC BY-SA は及ばない。
**辞書のファイルを配るとき(SDの中身を配る等)は、辞書のライセンスに従うこと。**

## ビルド時に取得するもの(リポジトリには無い)

実機のファーム(`firmware.bin` 等)とPC/Webビルドには、次のものが入る。**ファームを配るときはこれらのライセンス表示も添えること。**

| もの | 取得元 | ライセンス |
|---|---|---|
| LovyanGFX 1.2.28 | `platformio.ini` の `lib_deps` | FreeBSD / BSD / MIT(同梱の `license.txt`) |
| LovyanGFX の日本語フォント(`lgfxJapanGothicP_*`) | LovyanGFX 同梱 | **IPAフォントライセンス v1.0**(IPAフォントを変換したもの。`src/lgfx/Fonts/IPA/`) |
| XPT2046_Touchscreen v1.4 | `platformio.ini` の `lib_deps` | MIT(Paul Stoffregen) |
| Arduino-Pico コア | PlatformIO の `framework = arduino` | LGPL-2.1-or-later |
| Arduino-Pico に含まれるもの(Pico SDK・TinyUSB・lwIP・BearSSL・LittleFS・SdFat・CYW43 ドライバ等) | 同上 | 各ライブラリのライセンス(多くは BSD / MIT。CYW43 ドライバとWi-Fiチップのファームは独自のライセンス) |
| SDL2 / Emscripten(PC/Webビルドのみ) | `pc/` | zlib / MIT ほか |

ルート証明書(`src/net/Tls_Roots_Data.hpp`)は公開されている証明書を写したもの。
