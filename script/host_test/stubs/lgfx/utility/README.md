# lgfx_tjpgd(ホストテスト用の写し)

LovyanGFX 1.2.28(`platformio.ini` の `lib_deps` と同じ版)の `src/lgfx/utility/lgfx_tjpgd.{c,h}` をそのまま写したもの。
元は ChaN 氏の TJpgDec(著作権表示とライセンスはファイルの先頭のまま)。

実機・PC/Webビルドは LovyanGFX に入っているものを使う。ホストテストは LovyanGFX を持たない(`stubs/LovyanGFX.h`)ので、
`src/gui/icons/Jpeg_Codec.cpp` の `#include <lgfx/utility/lgfx_tjpgd.h>` をここで受ける。
LovyanGFX の版を上げたら写し直すこと。
