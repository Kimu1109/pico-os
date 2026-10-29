# lib/minimp3 について

[minimp3](https://github.com/lieff/minimp3)(lieff)の `minimp3.h`(コミット `ea99364`、2026-07-27。タグは無い)を `src/` へ置いたもの。
ライセンスは CC0-1.0(`LICENSE`。パブリックドメインへの献呈)。`minimp3_ex.h`(ファイル全体を読む・シークの補助)は使わないので置いていない。

MP3の再生(`src/sound/Mp3_Decoder.{hpp,cpp}`、`SoundFunctions::WavPlay()` が `.mp3` をこれで読む)が使う。

## なぜこれにしたか

- ヘッダ1本・依存無し・動的確保をしない。デコーダの状態は `mp3dec_t`(約6.7KB)だけで、呼び出し側が持つ
- CC0なのでファームへ組み込める(AACのFAAD2はGPLで組み込めない。`docs/design/sound.md`「MP3の再生」)
- 浮動小数で計算する。RP2350(Cortex-M33)は単精度のFPUがあるので、固定小数点の実装(Helix)でなくてよい

## 手を入れたところ(1か所)

`mp3dec_decode_frame()` は作業場所 `mp3dec_scratch_t`(約16KB)を**スタックに**置く。1コア目のスタックは arduino-pico の既定で4KB(`SCRATCH_Y`)しか無いので、
`MINIMP3_SCRATCH_PTR` が定義されていればその式が指す場所を使うようにした(定義しなければ元のまま)。
差分は `mp3dec_decode_frame()` の中だけで、`scratch.` → `scratch_p->`、`&scratch` → `scratch_p` の置き換えと宣言の数行。

実装は `src/minimp3_pico.c` で1回だけ作る(`MINIMP3_ONLY_MP3` で Layer I/II を外し、`MINIMP3_SCRATCH_PTR` を `minimp3_pico_scratch` へ)。
使う側は `minimp3_pico.h` を読み、`mp3dec_decode_frame()` を呼ぶ前に `minimp3_pico_scratch` へ
`minimp3_pico_scratch_bytes` 以上の場所を入れる(`Mp3Decoder` は自分のメンバの中に持つ)。

## 版を上げる場合

1. https://github.com/lieff/minimp3 の `minimp3.h` を取ってきて `src/minimp3.h` を置き換える
2. 上の「手を入れたところ」を当て直し、`library.json` の `version`・この文書のコミットを更新
3. `sh script/host_test/run.sh media`(mp3_test)を回し、PCビルドでミュージックアプリから `.mp3` を鳴らす
