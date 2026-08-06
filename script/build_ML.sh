#!/bin/bash

python convert_skk_dict.py dic/SKK-JISYO.ML  --encoding utf-8 --exclude-chars-file to-fu-chars.txt --block-size 320
