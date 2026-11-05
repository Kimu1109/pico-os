// alac_test の正解。script/host_test/gen_alac_test_data.py が作る(手で直さない)。ALAC本体は data/alac/<名前>.m4a
// 〜Hash は ffmpeg の展開結果(16bit・左右交互)のFNV-1a 64ビット。〜Frames は1chあたりのサンプル数
#pragma once

// kAlacStereo16.m4a: 44.1kHz ステレオ16bit 0.3秒(既定。moov が後ろ)(20865バイト)
static const unsigned kAlacStereo16Frames = 13230;
static const unsigned long long kAlacStereo16Hash = 0x028A3CC0280F5DE5ULL;

// kAlacStereo16Fast.m4a: 44.1kHz ステレオ16bit 0.3秒(faststart。moov が前)(20865バイト)
static const unsigned kAlacStereo16FastFrames = 13230;
static const unsigned long long kAlacStereo16FastHash = 0x028A3CC0280F5DE5ULL;

// kAlacStereo16Order.m4a: 44.1kHz ステレオ16bit 0.3秒(予測の次数1〜30)(19942バイト)
static const unsigned kAlacStereo16OrderFrames = 13230;
static const unsigned long long kAlacStereo16OrderHash = 0x028A3CC0280F5DE5ULL;

// kAlacMono.m4a: 22.05kHz モノラル16bit 0.2秒(5206バイト)
static const unsigned kAlacMonoFrames = 4410;
static const unsigned long long kAlacMonoHash = 0x64A6E9E417FB5B7DULL;

// kAlacStereo24.m4a: 48kHz ステレオ24bit 0.2秒(下位8bitを別に持つ)(34009バイト)
static const unsigned kAlacStereo24Frames = 9600;
static const unsigned long long kAlacStereo24Hash = 0xBD9FDDFA1361F785ULL;

// kAlacSilence.m4a: 44.1kHz ステレオ16bit 0.2秒の無音(847バイト)
static const unsigned kAlacSilenceFrames = 8820;
static const unsigned long long kAlacSilenceHash = 0x03972ECB9416BB45ULL;

// kAlacNoise.m4a: 44.1kHz ステレオ16bit 0.1秒の全振幅の雑音(生のまま持つ=エスケープ)(11784バイト)
static const unsigned kAlacNoiseFrames = 4410;
static const unsigned long long kAlacNoiseHash = 0x388637F8F5B3C779ULL;

// kAlacChirp.m4a: 22.05kHz モノラル16bit 1.2秒・周波数が上がるチャープ(シークの確認用)(23490バイト)
static const unsigned kAlacChirpFrames = 26460;
static const unsigned long long kAlacChirpHash = 0x00F8D6D074120435ULL;

// kAlacTagged.m4a: 22.05kHz モノラル16bit 0.2秒・iTunes形式のタグ(曲名・アーティスト・アルバム・曲番号)付き(5342バイト)
static const unsigned kAlacTaggedFrames = 4410;
static const unsigned long long kAlacTaggedHash = 0x64A6E9E417FB5B7DULL;

// kAlacChunks.m4a: チャープを 1・2・1・3 フレームのチャンクへ組み直したもの(stscが複数の組・チャンクの間に詰め物)(23594バイト)
static const unsigned kAlacChunksFrames = 26460;
static const unsigned long long kAlacChunksHash = 0x00F8D6D074120435ULL;

// kAacOnly.m4a: 44.1kHz ステレオ AAC 0.2秒(ALACではない)(正解のハッシュは無い)

