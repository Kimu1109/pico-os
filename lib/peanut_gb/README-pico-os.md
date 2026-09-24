# lib/peanut_gb について

[Peanut-GB](https://github.com/deltabeard/Peanut-GB)(MIT、C99のヘッダ1本のGame Boy(DMG)エミュ)を
コミット `d0bcca771c83a2638c93a9ae61f3d51f226dc905` の時点で `src/peanut_gb.h` へそのまま置いたもの。**無改造**。
`LICENSE` はヘッダ冒頭のライセンス文を書き出したもの。

## 使い方

実装の取り込み(`#include "peanut_gb.h"`)は **`src/gb/Gb_Emu.cpp` の1箇所だけ**で行う
(ヘッダに実装が入っているので、2箇所で取り込むと多重定義になる)。
他のファイルは `gb/Gb_Emu.hpp` の `GbEmu` だけを見る。

C++としてそのままコンパイルできる(g++ -std=gnu++17 で警告なし)ので、Lua本体のように
Cとしてビルドする設定は要らない。

## 選んだ理由

SUMMARY.md #9「最適なGBエミュを探せ」の結果。MIT・ヘッダ1本・ROMの読み出しがコールバック・
1行ずつ描画を渡す・RP2040でもフルスピード、の5点でpico-osへの載せやすさが一番高かった。
RP2350上の実例は Pico-GB(YouMakeTech)/ PicoCalc-GameBoy 等。

## 版を上げる場合

1. `https://raw.githubusercontent.com/deltabeard/Peanut-GB/<commit>/peanut_gb.h` を `src/peanut_gb.h` へ置き換え
2. この文書と `library.json` のコミットを書き換える
3. `sh script/host_test/run.sh`(`gb_emu_test`)とPCビルドで確認
