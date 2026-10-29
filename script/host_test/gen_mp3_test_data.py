#!/usr/bin/env python3
"""mp3_test の MP3 を ffmpeg(libmp3lame)で作り、mp3_test_data.hpp へ書き出す。

    python3 script/host_test/gen_mp3_test_data.py

できあがりはエンコーダの版で少し変わるが、テストは周波数・振幅・長さの範囲で見るので作り直しても通る。
"""
import pathlib
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "mp3_test_data.hpp"

# (変数名, 説明, ffmpegの入力側の引数, 出力側の引数)
CASES = [
    ("kMp3CbrStereo",
     "44.1kHz ステレオ CBR 32kbps 1秒。左=440Hz(振幅0.5)・右=無音。LAMEのInfoフレーム(TOC付き)あり",
     ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=1",
      "-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono",
      "-filter_complex", "[0:a]volume=0.5[l];[l][1:a]amerge=inputs=2,atrim=duration=1[a]", "-map", "[a]"],
     ["-c:a", "libmp3lame", "-b:a", "32k", "-ac", "2", "-id3v2_version", "0", "-write_id3v1", "0"]),
    ("kMp3TaggedMono",
     "44.1kHz モノラル CBR 32kbps 1秒・1kHz(振幅0.5)。Xing/Info無し、頭にID3v2・終わりにID3v1",
     ["-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=44100:duration=1", "-af", "volume=0.5"],
     ["-c:a", "libmp3lame", "-b:a", "32k", "-ac", "1", "-write_xing", "0",
      "-id3v2_version", "3", "-write_id3v1", "1", "-metadata", "title=pico-os test"]),
    ("kMp3Vbr22k",
     "22.05kHz モノラル(MPEG-2)VBR 3秒。頭の1.5秒=440Hz・後の1.5秒=1500Hz。XingのTOCあり",
     ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=22050:duration=1.5",
      "-f", "lavfi", "-i", "sine=frequency=1500:sample_rate=22050:duration=1.5",
      "-filter_complex", "[0:a][1:a]concat=n=2:v=0:a=1,volume=0.5[a]", "-map", "[a]"],
     ["-c:a", "libmp3lame", "-q:a", "9", "-ac", "1", "-id3v2_version", "0", "-write_id3v1", "0"]),
]


def encode(in_args, out_args):
    with tempfile.TemporaryDirectory() as d:
        path = pathlib.Path(d) / "out.mp3"
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *in_args, *out_args, str(path)],
                       check=True)
        return path.read_bytes()


def main():
    lines = [
        "// mp3_test の MP3。script/host_test/gen_mp3_test_data.py が作る(手で直さない)",
        "#pragma once",
        "",
    ]
    for name, desc, in_args, out_args in CASES:
        data = encode(in_args, out_args)
        lines.append(f"// {desc}({len(data)}バイト)")
        lines.append(f"static const unsigned char {name}[] = {{")
        for i in range(0, len(data), 20):
            lines.append("    " + " ".join(f"0x{b:02x}," for b in data[i:i + 20]))
        lines.append("};")
        lines.append("")
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
