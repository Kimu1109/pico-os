#!/usr/bin/env python3
"""alac_test の ALAC(.m4a)を ffmpeg(libavcodec のALACエンコーダ)で作り、alac_test_data.hpp へ書き出す。

    python3 script/host_test/gen_alac_test_data.py

gen_flac_test_data.py と同じ考え方: 正解は ffmpeg 自身の展開結果(16bitに直したサンプル列)のFNV-1a 64ビットのハッシュで、
AlacDecoder を元の周波数のまま(周波数の変換を通さず)読んだ結果とビット単位で一致するかを見る。24bitは「四捨五入して16bit」にして数える。
本体は data/alac/<名前>.m4a(バイナリ)。moov が mdat の後ろにあるもの(ffmpeg の既定)と前にあるもの(faststart)の両方を作る。
"""
import pathlib
import struct
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "alac_test_data.hpp"
DATA = HERE / "data" / "alac"


def music_stereo(rate, dur):
    return ["-f", "lavfi", "-i",
            f"aevalsrc=exprs='0.4*sin(2*PI*440*t)+0.1*sin(2*PI*3000*t)|0.32*sin(2*PI*440*t)+0.2*sin(2*PI*660*t)':s={rate}:d={dur}"]


def music_mono(rate, dur, freq=1000):
    return ["-f", "lavfi", "-i", f"aevalsrc=exprs='0.5*sin(2*PI*{freq}*t)':s={rate}:d={dur}"]


# (変数名, 説明, 入力側の引数, 出力側の引数, 元のビット数)
CASES = [
    ("kAlacStereo16", "44.1kHz ステレオ16bit 0.3秒(既定。moov が後ろ)", music_stereo(44100, 0.3),
     ["-c:a", "alac", "-sample_fmt", "s16p"], 16),
    ("kAlacStereo16Fast", "44.1kHz ステレオ16bit 0.3秒(faststart。moov が前)", music_stereo(44100, 0.3),
     ["-c:a", "alac", "-sample_fmt", "s16p", "-movflags", "+faststart"], 16),
    ("kAlacStereo16Order", "44.1kHz ステレオ16bit 0.3秒(予測の次数1〜30)", music_stereo(44100, 0.3),
     ["-c:a", "alac", "-sample_fmt", "s16p", "-min_prediction_order", "1", "-max_prediction_order", "30"], 16),
    ("kAlacMono", "22.05kHz モノラル16bit 0.2秒", music_mono(22050, 0.2),
     ["-c:a", "alac", "-sample_fmt", "s16p"], 16),
    ("kAlacStereo24", "48kHz ステレオ24bit 0.2秒(下位8bitを別に持つ)", music_stereo(48000, 0.2),
     ["-c:a", "alac", "-sample_fmt", "s32p", "-bits_per_raw_sample", "24"], 24),
    ("kAlacStereo24At96k", "96kHz ステレオ24bit 0.1秒(ハイレゾ)", music_stereo(96000, 0.1),
     ["-c:a", "alac", "-sample_fmt", "s32p", "-bits_per_raw_sample", "24"], 24),
    ("kAlacSilence", "44.1kHz ステレオ16bit 0.2秒の無音",
     ["-f", "lavfi", "-i", "anullsrc=r=44100:cl=stereo", "-t", "0.2"],
     ["-c:a", "alac", "-sample_fmt", "s16p"], 16),
    ("kAlacNoise", "44.1kHz ステレオ16bit 0.1秒の全振幅の雑音(生のまま持つ=エスケープ)",
     ["-f", "lavfi", "-i", "anoisesrc=r=44100:a=1.0:c=white:seed=1:d=0.1,pan=stereo|c0=c0|c1=c0"],
     ["-c:a", "alac", "-sample_fmt", "s16p"], 16),
]

TAG_CASE = ("kAlacTagged", "22.05kHz モノラル16bit 0.2秒・iTunes形式のタグ(曲名・アーティスト・アルバム・曲番号)付き",
            music_mono(22050, 0.2),
            ["-c:a", "alac", "-sample_fmt", "s16p", "-metadata", "title=テスト曲", "-metadata", "artist=Some Band",
             "-metadata", "album=First Album", "-metadata", "track=3/12"], 16)

# AACの.m4a(ALACではないので断る確認用。正解のハッシュは無い)
AAC_CASE = ("kAacOnly", "44.1kHz ステレオ AAC 0.2秒(ALACではない)", music_stereo(44100, 0.2), ["-c:a", "aac", "-b:a", "32k"])

SEEK_CASE = ("kAlacChirp", "22.05kHz モノラル16bit 1.2秒・周波数が上がるチャープ(シークの確認用)",
             ["-f", "lavfi", "-i", "aevalsrc=exprs='0.5*sin(2*PI*(200+300*t)*t)':s=22050:d=1.2"],
             ["-c:a", "alac", "-sample_fmt", "s16p"], 16)


# ---- MP4の組み直し(複数のチャンク・stsc の複数の組・チャンクの間の詰め物を持つファイルを作る。ffmpegは1チャンクにまとめて書く) ----
CONTAINERS = (b"moov", b"trak", b"mdia", b"minf", b"stbl", b"udta")


def parse_atoms(d, start, end):
    out = []
    p = start
    while p + 8 <= end:
        size, typ = struct.unpack(">I4s", d[p:p + 8])
        if size == 0:
            size = end - p
        body = d[p + 8:p + size]
        if typ in CONTAINERS:
            out.append([typ, parse_atoms(d, p + 8, p + size)])
        else:
            out.append([typ, body])
        p += size
    return out


def build_atoms(nodes):
    out = b""
    for typ, body in nodes:
        payload = build_atoms(body) if isinstance(body, list) else body
        out += struct.pack(">I4s", 8 + len(payload), typ) + payload
    return out


def find_path(nodes, path):
    for n in nodes:
        if n[0] == path[0]:
            return n if len(path) == 1 else find_path(n[1], path[1:])
    raise KeyError(path)


def rechunk(data, pattern, gap=16):
    """各フレームを pattern(1チャンクあたりのフレーム数の並び。足りなければ最後を繰り返す)のチャンクへ分け、
    チャンクの間に gap バイトの詰め物を入れて組み直す。stsc は変わり目ごとの組、stco はチャンクごとの位置"""
    top = parse_atoms(data, 0, len(data))
    stbl = find_path(top, [b"moov", b"trak", b"mdia", b"minf", b"stbl"])[1]
    stsz = find_path(stbl, [b"stsz"])[1]
    count = struct.unpack(">I", stsz[8:12])[0]
    sizes = list(struct.unpack(">%dI" % count, stsz[12:12 + 4 * count]))
    old_off = struct.unpack(">I", find_path(stbl, [b"stco"])[1][8:12])[0]
    frames, p = [], old_off
    for sz in sizes:
        frames.append(data[p:p + sz])
        p += sz
    ftyp = [n for n in top if n[0] == b"ftyp"][0]
    head = build_atoms([ftyp])
    mdat_body = b""
    chunks, i, k = [], 0, 0
    while i < len(frames):
        n = pattern[min(k, len(pattern) - 1)]
        n = min(n, len(frames) - i)
        chunks.append((len(head) + 8 + len(mdat_body), n))
        for f in frames[i:i + n]:
            mdat_body += f
        mdat_body += bytes([0xEE]) * gap
        i += n
        k += 1
    entries, prev = [], None
    for idx, (_, n) in enumerate(chunks):
        if n != prev:
            entries.append((idx + 1, n, 1))
            prev = n
    stsc_body = struct.pack(">II", 0, len(entries)) + b"".join(struct.pack(">III", *e) for e in entries)
    stco_body = struct.pack(">II", 0, len(chunks)) + b"".join(struct.pack(">I", off) for off, _ in chunks)
    for n in stbl:
        if n[0] == b"stsc":
            n[1] = stsc_body
        elif n[0] == b"stco":
            n[1] = stco_body
    moov = [n for n in top if n[0] == b"moov"][0]
    return head + build_atoms([[b"mdat", mdat_body]]) + build_atoms([moov])


def fnv64(samples):
    h = 0xCBF29CE484222325
    for s in samples:
        h = ((h ^ (s & 0xFFFF)) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def run_ffmpeg(args, path):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *args, str(path)], check=True)


def encode(in_args, out_args, bits):
    with tempfile.TemporaryDirectory() as d:
        m4a = pathlib.Path(d) / "out.m4a"
        run_ffmpeg([*in_args, *out_args], m4a)
        data = m4a.read_bytes()
        raw = pathlib.Path(d) / "out.raw"
        fmt = "s16le" if bits == 16 else "s32le"
        run_ffmpeg(["-i", str(m4a), "-f", fmt, "-acodec", "pcm_" + fmt], raw)
        pcm = raw.read_bytes()
    if bits == 16:
        samples = list(struct.unpack("<%dh" % (len(pcm) // 2), pcm))
    else:
        samples = []
        for v in struct.unpack("<%di" % (len(pcm) // 4), pcm):
            y = ((v >> 8) + 128) >> 8
            samples.append(max(-32768, min(32767, y)))
    return data, samples


def dump(lines, name, desc, data, samples, stereo):
    DATA.mkdir(parents=True, exist_ok=True)
    (DATA / f"{name}.m4a").write_bytes(data)
    lines.append(f"// {name}.m4a: {desc}({len(data)}バイト)")
    lines.append(f"static const unsigned {name}Frames = {len(samples) // (2 if stereo else 1)};")
    lines.append(f"static const unsigned long long {name}Hash = 0x{fnv64(samples):016X}ULL;")
    lines.append("")


def main():
    lines = [
        "// alac_test の正解。script/host_test/gen_alac_test_data.py が作る(手で直さない)。ALAC本体は data/alac/<名前>.m4a",
        "// 〜Hash は ffmpeg の展開結果(16bit・左右交互)のFNV-1a 64ビット。〜Frames は1chあたりのサンプル数",
        "#pragma once",
        "",
    ]
    for name, desc, in_args, out_args, bits in CASES + [SEEK_CASE, TAG_CASE]:
        data, samples = encode(in_args, out_args, bits)
        stereo = "-ac" not in out_args and ("Stereo" in name or name in ("kAlacSilence", "kAlacNoise"))
        dump(lines, name, desc, data, samples, stereo)
    # チャープを 1・2・1・3 フレームのチャンクへ組み直したもの(中身の音は同じ。stsc は複数の組、チャンクの間に詰め物)
    chirp = (DATA / "kAlacChirp.m4a").read_bytes()
    chunks = rechunk(chirp, [1, 2, 1, 3])
    _, chirp_samples = encode(SEEK_CASE[2], SEEK_CASE[3], SEEK_CASE[4])
    dump(lines, "kAlacChunks", "チャープを 1・2・1・3 フレームのチャンクへ組み直したもの(stscが複数の組・チャンクの間に詰め物)", chunks, chirp_samples, False)
    name, desc, in_args, out_args = AAC_CASE
    with tempfile.TemporaryDirectory() as d:
        aac = pathlib.Path(d) / "out.m4a"
        run_ffmpeg([*in_args, *out_args], aac)
        DATA.mkdir(parents=True, exist_ok=True)
        (DATA / f"{name}.m4a").write_bytes(aac.read_bytes())
    lines.append(f"// {name}.m4a: {desc}(正解のハッシュは無い)")
    lines.append("")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
