# pico-os の追加の漢字フォント(jpgothic16.pkf / jpgothic24.pkf)

pico-os の本体(ファーム)には、かな・記号・英数字と常用漢字(+数字)だけを入れてあり、それ以外の漢字
(1,407字)をこのファイルから読みます。SDカードの `/sys/fonts/` に置きます(本体の「設定」→「他」→「追加の漢字」の[取得]で
Webから取ると、ここに置かれます)。無くても本体は動き、常用漢字の外の字が □(豆腐)になるだけです。

## 元のフォントとライセンス

- 元は [IPAexゴシック](https://moji.or.jp/ipafont/)(IPA)。[LovyanGFX](https://github.com/lovyan03/LovyanGFX) が
  u8g2 の形のビットマップ(`src/lgfx/Fonts/IPA/lgfx_font_japan.c` の `lgfx_font_japan_gothic_p_16` / `_24`)にしたものを、
  pico-os の `script/font/split_font.py` が本体用とこのファイルに分けた(字の絵は1バイトも変えていない)。
- これらは **IPAフォントライセンスv1.0** の「派生プログラム」で、同じライセンスで配る(`IPA_Font_License_Agreement_v1.0.txt`)。
  派生プログラムなので、元のフォントの名前は使っていない(第3条1項(4))。
- 作り方(第3条1項(1)(b)にあたるもの): `script/font/split_font.py` と `script/font/base_kanji.txt`
  (https://github.com/Kimu1109/pico-os)。

## 元のフォントへ戻す方法(第3条1項(2))

1. 元のフォントは https://moji.or.jp/ipafont/ から、u8g2 の形は LovyanGFX の上の配列から得られる。
2. pico-os の本体を元のフォントへ戻すには、`src/gui/core/GlyphIndexFont.cpp` の `font16` / `font24` を
   `kJpGothicBase16` / `kJpGothicBase24` から `lgfx_font_japan_gothic_p_16` / `_24` に替えてビルドする
   (このファイルは要らなくなる)。

## ファイルの形

`script/font/split_font.py` の冒頭の「.pkf の形」。見出しの印が本体と合わないファイルは使われない(□になる)。
