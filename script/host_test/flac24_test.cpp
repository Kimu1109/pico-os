// flac_test を24bitの音声出力を載せた版(PICO_HW_SOUND_24BIT=1)で作ったもの。
// 16bitで鳴らすとき(既定)は、標本の入れ物が32bitに・先読みの列が短く(12288サンプル)なっても今までどおり動くことを見る
// (24bitで鳴らすときの確かめは sound24_test)
#include "flac_test.cpp"
