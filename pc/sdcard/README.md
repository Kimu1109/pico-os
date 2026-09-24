# pc/sdcard

PCビルドがSDカードとして読むディレクトリ。実機のSDに置くファイルを
そのまま同じ構成で置けば、PCでも同じパスで読める。

IMEの辞書(`sys/ime/skk_body.tsv` / `sys/ime/skk_index.tsv`)は大きいためリポジトリには
含めていない。日本語入力をPCで試す場合は、実機のSDから同じ場所へコピーするか、
`script/convert_skk_dict.py` で生成すること。無くても起動はする(変換候補が出ないだけ)。

`calendar/sample.ics` はカレンダーアプリの動作サンプル。毎週/隔週/毎月最終金曜/毎年の
繰り返し、1回だけ時間を変えた回、UTCで書いた日をまたぐ予定などを含む。
`calendar/family.ics` は2つ目のカレンダーの例(複数のカレンダーを重ねると色分けされる)。
`calendar/` 直下の `*.ics` は全部読まれるので、手元の .ics(Googleカレンダーの
「iCal形式の非公開URL」から落としたもの等)を置けばそれも重ねて表示される。

別の場所を使いたい場合は環境変数で差し替えられる:

    PICOOS_SD_ROOT=/path/to/sd ./pc/build/picoos_pc

`gb/dmg-acid2.gb` はゲームボーイエミュ(GameBoyScene)の動作確認用のテストROM。
画面の描画(背景/ウィンドウ/スプライトの重なり順や反転)が正しければ笑顔の顔が出る。
Matt Currie氏の [dmg-acid2](https://github.com/mattcurrie/dmg-acid2)(v1.0のリリース)を
そのまま置いたもので、ライセンスは以下のMIT。市販のゲームのROMはリポジトリに含めない
(持っているカートリッジから吸い出したものを、手元の `gb/` に置いて試すこと)。

```
MIT License

Copyright (c) 2020 Matt Currie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
