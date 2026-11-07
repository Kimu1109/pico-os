#!/usr/bin/env python3
"""flac_test の FLAC を ffmpeg(libavcodec のFLACエンコーダ)で作り、flac_test_data.hpp へ書き出す。

    python3 script/host_test/gen_flac_test_data.py

正解は ffmpeg 自身の展開結果(16bitに直したサンプル列)のFNV-1a 64ビットのハッシュ。FlacDecoder を元の周波数のまま
(周波数の変換を通さず)読んだ結果とビット単位で一致するかを見る。24bitは「四捨五入して16bit」(FlacDecoder と同じ丸め)にして数える。
エンコーダの版でバイト列が変わっても、ハッシュは展開結果なので変わらない(作り直してもテストは通る)。
"""
import pathlib
import struct
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "flac_test_data.hpp"
DATA = HERE / "data" / "flac"       # 〜.flac の本体(バイナリ。ヘッダには正解のハッシュだけ置く)

# 左右が相関した、音楽らしい(ごく小さい雑音入りの)信号。副フレームの種類(固定・LPC・差分・中央/差)を満遍なく通す
def music_stereo(rate, dur):
    return ["-f", "lavfi", "-i",
            f"aevalsrc=exprs='0.4*sin(2*PI*440*t)+0.1*sin(2*PI*3000*t)|0.32*sin(2*PI*440*t)+0.2*sin(2*PI*660*t)':s={rate}:d={dur}"]

def music_mono(rate, dur, freq=1000):
    return ["-f", "lavfi", "-i", f"aevalsrc=exprs='0.5*sin(2*PI*{freq}*t)':s={rate}:d={dur}"]

# (変数名, 説明, 入力側の引数, 出力側の引数, 元のビット数)
CASES = [
    ("kFlacStereo16", "44.1kHz ステレオ16bit 0.3秒・level 8(LPC・差分の各種)", music_stereo(44100, 0.3),
     ["-c:a", "flac", "-compression_level", "8", "-sample_fmt", "s16"], 16),
    ("kFlacStereo16Fixed", "44.1kHz ステレオ16bit 0.15秒・level 0(固定予測)", music_stereo(44100, 0.15),
     ["-c:a", "flac", "-compression_level", "0", "-sample_fmt", "s16"], 16),
    ("kFlacMono200", "22.05kHz モノラル16bit 0.2秒・ブロック長200(8bitの長さ指定)・level 5", music_mono(22050, 0.2),
     ["-c:a", "flac", "-compression_level", "5", "-frame_size", "200", "-sample_fmt", "s16"], 16),
    ("kFlacMono1000", "22.05kHz モノラル16bit 0.2秒・ブロック長1000(16bitの長さ指定)・level 12", music_mono(22050, 0.2),
     ["-c:a", "flac", "-compression_level", "12", "-frame_size", "1000", "-sample_fmt", "s16"], 16),
    ("kFlacStereo24", "48kHz ステレオ24bit 0.1秒・level 12", music_stereo(48000, 0.1),
     ["-c:a", "flac", "-compression_level", "12", "-sample_fmt", "s32", "-bits_per_raw_sample", "24"], 24),
    # 96kHz(ハイレゾ)。ffmpegの既定は96kHzでブロック長8192(FlacDecoder::kMaxBlock を超える)なので、`flac` コマンドと同じ4096にする
    ("kFlacStereo24At96k", "96kHz ステレオ24bit 0.1秒・ブロック長4096・level 8", music_stereo(96000, 0.1),
     ["-c:a", "flac", "-compression_level", "8", "-frame_size", "4096", "-sample_fmt", "s32", "-bits_per_raw_sample", "24"], 24),
    ("kFlacSilence", "44.1kHz ステレオ16bit 0.2秒の無音(定数の副フレーム)",
     ["-f", "lavfi", "-i", "anullsrc=r=44100:cl=stereo", "-t", "0.2"],
     ["-c:a", "flac", "-sample_fmt", "s16"], 16),
    ("kFlacNoise", "44.1kHz ステレオ16bit 0.04秒の全振幅の雑音(生の副フレーム)",
     ["-f", "lavfi", "-i", "anoisesrc=r=44100:a=1.0:c=white:seed=1:d=0.04,pan=stereo|c0=c0|c1=c0"],
     ["-c:a", "flac", "-sample_fmt", "s16"], 16),
]

# 長い曲(シークの確認用。信号は位置ごとに違う周波数のチャープ)
SEEK_CASE = ("kFlacChirp", "22.05kHz モノラル16bit 1秒・周波数が上がるチャープ・ブロック長1152",
             ["-f", "lavfi", "-i", "aevalsrc=exprs='0.5*sin(2*PI*(200+300*t)*t)':s=22050:d=1"],
             ["-c:a", "flac", "-compression_level", "5", "-frame_size", "1152", "-sample_fmt", "s16"], 16)


class Samples(list):
    """16bitに丸めた展開結果。24bitの曲は wide に24bitのまま(24bitの出力の確かめ用)"""
    wide = None


def fnv64_wide(samples):
    """24bitの値(32bitの符号付きとして)のFNV-1a 64ビット"""
    h = 0xCBF29CE484222325
    for s in samples:
        h = ((h ^ (s & 0xFFFFFFFF)) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def fnv64(samples):
    h = 0xCBF29CE484222325
    for s in samples:
        h = ((h ^ (s & 0xFFFF)) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def run_ffmpeg(args, path):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *args, str(path)], check=True)


def encode(in_args, out_args, bits):
    with tempfile.TemporaryDirectory() as d:
        flac = pathlib.Path(d) / "out.flac"
        run_ffmpeg([*in_args, *out_args], flac)
        data = flac.read_bytes()
        raw = pathlib.Path(d) / "out.raw"
        fmt = "s16le" if bits == 16 else "s32le"
        run_ffmpeg(["-i", str(flac), "-f", fmt, "-acodec", "pcm_" + fmt], raw)
        pcm = raw.read_bytes()
    if bits == 16:
        samples = list(struct.unpack("<%dh" % (len(pcm) // 2), pcm))
    else:
        s32 = struct.unpack("<%di" % (len(pcm) // 4), pcm)
        samples = Samples()
        samples.wide = [v >> 8 for v in s32]
        for v in s32:
            x24 = v >> 8
            y = (x24 + 128) >> 8
            samples.append(max(-32768, min(32767, y)))
    return data, samples


def crc8(b):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def find_frames(data):
    """STREAMINFO の後の各フレームの (位置, 先頭の標本番号) を返す。固定ブロック長前提"""
    pos = 4
    while True:
        h = data[pos]
        ln = int.from_bytes(data[pos + 1:pos + 4], "big")
        if pos == 4:
            min_b, max_b = struct.unpack(">HH", data[pos + 4:pos + 8])
        pos += 4 + ln
        if h & 0x80:
            break
    start = pos
    frames = []
    i = start
    while i + 6 < len(data):
        if data[i] == 0xFF and data[i + 1] in (0xF8, 0xF9):
            # 見出しを数えてCRC-8が合えばフレーム
            bs_code = data[i + 2] >> 4
            sr_code = data[i + 2] & 15
            j = i + 4
            c = data[j]
            extra = 0 if c < 0x80 else (1 if c & 0xE0 == 0xC0 else 2 if c & 0xF0 == 0xE0 else 3 if c & 0xF8 == 0xF0 else 4 if c & 0xFC == 0xF8 else 5 if c & 0xFE == 0xFC else 6)
            num = c if extra == 0 else c & ((1 << (6 - extra)) - 1)
            for k in range(extra):
                num = (num << 6) | (data[j + 1 + k] & 0x3F)
            j += 1 + extra
            bs = {1: 192, 2: 576, 3: 1152, 4: 2304, 5: 4608}.get(bs_code, 256 << (bs_code - 8) if bs_code >= 8 else 0)
            if bs_code == 6:
                bs = data[j] + 1; j += 1
            elif bs_code == 7:
                bs = int.from_bytes(data[j:j + 2], "big") + 1; j += 2
            if sr_code == 12:
                j += 1
            elif sr_code in (13, 14):
                j += 2
            if crc8(data[i:j]) == data[j]:
                frames.append((i, num * min_b if min_b == max_b else num))
                i = j + 1
                continue
        i += 1
    return start, frames


def with_seektable(data, every=4):
    """STREAMINFO の直後に SEEKTABLE(every フレームごと + 1つ空き)を足す"""
    start, frames = find_frames(data)
    entries = b""
    for k in range(0, len(frames), every):
        pos, sample = frames[k]
        entries += struct.pack(">QQH", sample, pos - start, 1152)
    entries += struct.pack(">QQH", 0xFFFFFFFFFFFFFFFF, 0, 0)       # 空き(プレースホルダ)
    body = data[4:8 + 34]
    # STREAMINFO を「最後ではない」にして SEEKTABLE を続ける
    si = bytearray(body)
    si[0] &= 0x7F
    seek = bytes([0x80 | 3]) + len(entries).to_bytes(3, "big") + entries
    return data[:4] + bytes(si) + seek + data[8 + 34:]


def strip_metadata(data):
    """STREAMINFO だけ残す(ffmpeg が足す8KBの詰め物・タグを落として小さくする)"""
    pos = 4
    si = None
    while True:
        h = data[pos]
        ln = int.from_bytes(data[pos + 1:pos + 4], "big")
        if h & 0x7F == 0:
            si = bytearray(data[pos:pos + 4 + ln])
        pos += 4 + ln
        if h & 0x80:
            break
    si[0] |= 0x80
    return data[:4] + bytes(si) + data[pos:]


def dump(lines, name, desc, data, samples, extra=None):
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / f"{name}.flac").write_bytes(data)
    lines.append(f"// {name}.flac: {desc}({len(data)}バイト)")
    if samples is not None:
        lines.append(f"static const unsigned {name}Frames = {len(samples) // (2 if 'Stereo' in name or name in ('kFlacSilence', 'kFlacNoise') else 1)};")
        lines.append(f"static const unsigned long long {name}Hash = 0x{fnv64(samples):016X}ULL;")
        if getattr(samples, "wide", None) is not None:
            lines.append(f"static const unsigned long long {name}Hash24 = 0x{fnv64_wide(samples.wide):016X}ULL;   // 24bitのまま")
    lines.append("")


def main():
    lines = [
        "// flac_test の正解。script/host_test/gen_flac_test_data.py が作る(手で直さない)。FLAC本体は data/flac/<名前>.flac",
        "// 〜Hash は ffmpeg の展開結果(16bit・左右交互)のFNV-1a 64ビット。〜Frames は1chあたりのサンプル数",
        "#pragma once",
        "",
    ]
    for name, desc, in_args, out_args, bits in CASES:
        data, samples = encode(in_args, out_args, bits)
        if name != "kFlacStereo16":      # これだけ詰め物・タグ付きのまま(メタデータの読み飛ばしの確認)
            data = strip_metadata(data)
        dump(lines, name, desc, data, samples)
    name, desc, in_args, out_args, bits = SEEK_CASE
    data, samples = encode(in_args, out_args, bits)
    data = strip_metadata(data)
    dump(lines, name, desc, data, samples)
    # チャープの SEEKTABLE 付き(中身は同じ音)
    dump(lines, "kFlacChirpSeekTable", "上と同じ音に SEEKTABLE(4フレームごと)を足したもの", with_seektable(data), None)
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
