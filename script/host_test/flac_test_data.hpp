// flac_test の正解。script/host_test/gen_flac_test_data.py が作る(手で直さない)。FLAC本体は data/flac/<名前>.flac
// 〜Hash は ffmpeg の展開結果(16bit・左右交互)のFNV-1a 64ビット。〜Frames は1chあたりのサンプル数
#pragma once

// kFlacStereo16.flac: 44.1kHz ステレオ16bit 0.3秒・level 8(LPC・差分の各種)(16814バイト)
static const unsigned kFlacStereo16Frames = 13230;
static const unsigned long long kFlacStereo16Hash = 0x028A3CC0280F5DE5ULL;

// kFlacStereo16Fixed.flac: 44.1kHz ステレオ16bit 0.15秒・level 0(固定予測)(11808バイト)
static const unsigned kFlacStereo16FixedFrames = 6615;
static const unsigned long long kFlacStereo16FixedHash = 0xECC3F337F5B8EF8DULL;

// kFlacMono200.flac: 22.05kHz モノラル16bit 0.2秒・ブロック長200(8bitの長さ指定)・level 5(3660バイト)
static const unsigned kFlacMono200Frames = 4410;
static const unsigned long long kFlacMono200Hash = 0x64A6E9E417FB5B7DULL;

// kFlacMono1000.flac: 22.05kHz モノラル16bit 0.2秒・ブロック長1000(16bitの長さ指定)・level 12(1550バイト)
static const unsigned kFlacMono1000Frames = 4410;
static const unsigned long long kFlacMono1000Hash = 0x64A6E9E417FB5B7DULL;

// kFlacStereo24.flac: 48kHz ステレオ24bit 0.1秒・level 12(9715バイト)
static const unsigned kFlacStereo24Frames = 4800;
static const unsigned long long kFlacStereo24Hash = 0xC789445AC8ED8C75ULL;

// kFlacStereo24At96k.flac: 96kHz ステレオ24bit 0.1秒・ブロック長4096・level 8(20069バイト)
static const unsigned kFlacStereo24At96kFrames = 9600;
static const unsigned long long kFlacStereo24At96kHash = 0xF8DFC8F85BC5BA4DULL;

// kFlacSilence.flac: 44.1kHz ステレオ16bit 0.2秒の無音(定数の副フレーム)(72バイト)
static const unsigned kFlacSilenceFrames = 8820;
static const unsigned long long kFlacSilenceHash = 0x03972ECB9416BB45ULL;

// kFlacNoise.flac: 44.1kHz ステレオ16bit 0.04秒の全振幅の雑音(生の副フレーム)(3692バイト)
static const unsigned kFlacNoiseFrames = 1764;
static const unsigned long long kFlacNoiseHash = 0x7A4A936854FD1B71ULL;

// kFlacChirp.flac: 22.05kHz モノラル16bit 1秒・周波数が上がるチャープ・ブロック長1152(9879バイト)
static const unsigned kFlacChirpFrames = 22050;
static const unsigned long long kFlacChirpHash = 0x0FD96F0CD896956AULL;

// kFlacChirpSeekTable.flac: 上と同じ音に SEEKTABLE(4フレームごと)を足したもの(9991バイト)

