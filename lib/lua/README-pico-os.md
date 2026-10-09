# lib/lua について

Lua 5.4.7 (https://www.lua.org/ftp/lua-5.4.7.tar.gz) の `src/` を、
`lua.c` / `luac.c`(スタンドアロンインタプリタ/コンパイラの`main()`を持つファイル)を
除いてそのまま置いたもの。ソースは無改造。

## なぜvendorしたか(LovyanGFXと違うやり方にした理由)

LovyanGFXはPlatformIOの`lib_deps`とpc/CMakeLists.txtの両方がgitタグから
FetchContentする方式で、`platformio.ini`を唯一の情報源にしている
(CLAUDE.md「ビルド構成」参照)。Luaでも最初はこれに揃えようとしたが、
Lua本体は`lua.c`/`luac.c`という`main()`持ちのファイルが`src/`直下に混在しており、
PlatformIOの自動ソース収集(Library Dependency Finder)は`src/`以下の`.c`を
問答無用で全部拾う。`lib_deps`に生のtarball/gitを指定すると、Arduinoフレームワーク
自身が提供する`main()`と衝突して実機ビルドがリンクエラーになる
(このリモート実行環境にはRP2350のPlatformIOボード定義が無く、実際に踏んで確認する
ことはできなかったが、`lua.c`がANSI Cの`int main(int argc, char **argv)`を
無条件で定義するため、Arduinoコアの`main()`と衝突するのは構成によらず起こる)。

この2ファイルだけを取り除いて`lib/lua/src/`へ置けば、PlatformIOの「プロジェクト
専用ライブラリ」(`lib/<name>/src/`)としてそのまま拾われ、上記の衝突を避けられる。
副産物として、pc/CMakeLists.txt側もネットワーク取得ではなくこの同じコピーを
参照するようにしたので、実機PCの両ビルドが**常に同一のソースを見る**
(LovyanGFXのように2箇所が別々に取得してタグを合わせる、という気を遣う必要が無い)。

## 版を上げる場合

1. https://www.lua.org/ftp/ から新しいtarballを取得
2. `src/`の中身を、`lua.c`と`luac.c`を除いて`lib/lua/src/`へ丸ごと置き換え
3. `library.json`の`version`を更新
4. `sh script/host_test/run.sh`とPCビルド(`pc/README.md`)で問題ないか確認

## LUA_32BITS(2026-10-09、唯一の改造)

`src/luaconf.h` の `LUA_32BITS` を `0` → `1` にした(32bitの整数と単精度の小数)。ソースの改造はこの1行だけ。
版を上げるときは、新しい `luaconf.h` でも同じ1行を書き換えること。

- 理由: RP2350(Cortex-M33)は単精度しかハードウェアで計算できず、倍精度の小数はソフトウェア、64bitの整数も2命令以上かかる。
  実機のゾンビTDで1フレームのLuaが5〜9ms(PCの約30倍)かかり、メモリも足りなかった。実機では値1つ(TValue)が16→8バイトになる。
- 実機・PC・ホストテストが同じ `luaconf.h` を読むので、どれも同じ振る舞いになる(ビルド設定では切り替えない)。
- Luaから見える違い: 整数は約±21億で回る(`0xffffffff` は `-1`)、小数の有効桁は約7桁、`tostring` は小数を7桁で書く。
  `pico.millis()` は約24.8日、`pico.micros()` は約36分で負へ回るが、引き算も同じく回るので差は正しい。
  エポック秒は2038年1月まで整数に収まる。詳しくは `lua-api-doc/content/reference/limits.md`「数の範囲」。
