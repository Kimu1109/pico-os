# pc/sdcard

PCビルドがSDカードとして読むディレクトリ。実機のSDに置くファイルを
そのまま同じ構成で置けば、PCでも同じパスで読める。

IMEの辞書(`sys/ime/skk_body.tsv` / `sys/ime/skk_index.tsv`)は大きいためリポジトリには
含めていない。日本語入力をPCで試す場合は、実機のSDから同じ場所へコピーするか、
`script/convert_skk_dict.py` で生成すること。無くても起動はする(変換候補が出ないだけ)。

別の場所を使いたい場合は環境変数で差し替えられる:

    PICOOS_SD_ROOT=/path/to/sd ./pc/build/picoos_pc
