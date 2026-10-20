# 辞書の材料と作り方

IME(かな漢字変換)と辞書アプリが SD から読むファイルを、ここの材料から作る。
**作ったもの(`out/`)はリポジトリに入れない**(全部で約55MB。材料から何度でも同じものが作れる)。

```sh
sh script/dict/build.sh            # 全部(部分一致の索引は約27MBで、作るのに数秒)
sh script/dict/build.sh ime dict   # IME と辞書アプリの前方一致だけ
picoos dict build                  # 同じ(cli-tool/picoos)
```

`out/` はSDのルートと同じ並びなので、`out/sys/` をSDのルートへ写せばよい。

| 出力 | 使うもの | 材料 |
|---|---|---|
| `sys/ime/skk_body.tsv` `skk_index.tsv` | IME(`src/ime/IME_Dict.hpp`) | `src/SKK-JISYO.ML` + `src/tofu-chars.txt` |
| `sys/dict/en-ja-and-ja-en.tsv` `dict_index.tsv` | 辞書アプリの前方一致(`src/apps/dict/Word_Dict.hpp`) | `src/en-ja-and-ja-en.tsv` |
| `sys/dict/dict_suffixes.tsv` `dict_suffix_index.tsv` | 辞書アプリの部分一致(無くてもよい) | 同上 |

## 材料(`src/`)

- `SKK-JISYO.ML` / `SKK-JISYO.M` … SKK辞書(EUC-JP)。IMEは ML を使う。
- `tofu-chars.txt` … IMEのフォントで描けない文字(SKK辞書向けに `checkFontCoverage()` で作った)。
- `en-ja-and-ja-en.tsv` … 英和/和英の単語辞書(1列目でソート済み、豆腐の行は除いてある)。
- `dict_tofu_chars.txt` … 単語辞書向けの豆腐文字の一覧(`tofu-chars.txt` とは別物)。
  単語辞書を作り直すときは `filter_dict_tofu.py <素の辞書> src/dict_tofu_chars.txt --out src/en-ja-and-ja-en.tsv`。
  一覧の作り方は `filter_dict_tofu.py` の説明を参照。

## スクリプト

`convert_skk_dict.py`(SKK→IME用)/ `build_dict_index.py`(前方一致の索引)/
`build_dict_suffix_index.py`(部分一致のコーパスと索引)/ `filter_dict_tofu.py`(豆腐の行を除く)。
`build.sh` はこれらを決まった引数で呼ぶだけ。引数の数値(ブロックの行数等)は C++ 側の定数と揃える決まりがあるので、変えるときは各スクリプトの説明を読むこと。
