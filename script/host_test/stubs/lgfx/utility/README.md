# lgfx_tjpgd / lgfx_pngle / lgfx_miniz(ホストテスト用の写し)

LovyanGFX 1.2.28(`platformio.ini` の `lib_deps` と同じ版)の `src/lgfx/utility/` から、次をそのまま写したもの
(著作権表示とライセンスはファイルの先頭のまま):

- `lgfx_tjpgd.{c,h}` … ChaN 氏の TJpgDec(JPEG)
- `lgfx_pngle.{c,h}` … kikuchan 氏の pngle(PNG。MIT)
- `lgfx_miniz.{c,h}` … miniz 1.15(pngle が展開に使う。パブリックドメイン)
- `pgmspace.h` … pngle が読む LovyanGFX のヘッダ(BSD)

実機・PC/Webビルドは LovyanGFX に入っているものを使う。ホストテストは LovyanGFX を持たない(`stubs/LovyanGFX.h`)ので、
`src/gui/icons/Jpeg_Codec.cpp` / `Png_Codec.cpp` の `#include <lgfx/utility/...>` をここで受ける。
LovyanGFX の版を上げたら写し直すこと。
