# 辞書データのライセンス

ここの辞書データは pico-os 本体とは別のライセンスで配布されている。
再配布(SDカードのイメージ、ファームと一緒の配布等)をするときは、このファイルを辞書と一緒に置くこと
(`build.sh` が `out/sys/ime/` と `out/sys/dict/` へ `LICENSES.md` として写す)。

## SKK辞書(`src/SKK-JISYO.M` / `src/SKK-JISYO.ML`)

- 配布元: SKK Development Team(https://github.com/skk-dev/dict)
- ライセンス: **GNU General Public License v2 以降**(GPL-2.0-or-later)。
  著作権表示と許諾文は各ファイルの先頭にそのまま残してあるので、**削らないこと**。
  GPLの全文: https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
- `src/SKK-JISYO.M` / `src/SKK-JISYO.ML` は配布元のファイルを**無改変**で置いている。
- IMEが読む `skk_body.tsv` / `skk_index.tsv`(`out/sys/ime/`)は、`SKK-JISYO.ML` を
  `convert_skk_dict.py` でUTF-8へ変換し、フォントで描けない文字(`tofu-chars.txt`)を除き、索引を付けた**改変物**。
  同じくGPL-2.0-or-laterで配布する。変換に使ったスクリプトはこのフォルダにあり、辞書の材料も `src/` にある。
- 完全な対応するソース(変換前の辞書)は `src/SKK-JISYO.ML`。
- 無保証。保証の否認はGPLの条文による。

## 英和/和英辞書(`src/en-ja-and-ja-en.tsv`)

- 材料: **JMdict / EDICT** をまぜて作った辞書(EDRDG: Electronic Dictionary Research and Development Group の成果物)。
- ライセンス: **Creative Commons 表示-継承 4.0 国際(CC BY-SA 4.0)**。
  - EDRDGライセンス: https://www.edrdg.org/edrdg/licence.html
  - CC BY-SA 4.0: https://creativecommons.org/licenses/by-sa/4.0/deed.ja
- 表示(クレジット): 「この辞書は Electronic Dictionary Research and Development Group の
  JMdict / EDICT ファイルを元にしており、同グループのライセンスに従って使用している。」
  (https://www.edrdg.org/)
- 改変: 項目の統合・並べ替え(1列目でソート)、フォントで描けない文字を含む行の削除(`filter_dict_tofu.py`)、
  前方一致/部分一致の索引の追加(`build_dict_index.py` / `build_dict_suffix_index.py`)を行っている。
- 派生物(`out/sys/dict/` の各ファイル)も、同じ CC BY-SA 4.0 で配布すること(継承)。

## pico-os 本体との関係

辞書は独立したデータファイルとしてSDから実行時に読むだけで、プログラム(ファーム)には埋め込まれていない。
そのため辞書のライセンスは辞書ファイルにだけ及び、pico-os のソースコードの扱いは変わらない。
