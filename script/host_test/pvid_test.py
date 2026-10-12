#!/usr/bin/env python3
"""script/generate_pvid.py の検証(VIDEO_FORMAT.md の .pvid)。

    python3 script/host_test/pvid_test.py

タイルの符号化の往復・書いたファイルを読み直した絵が「本体の画面に出ているはずの絵」と一致すること・
目録・差分の判定・色選び、ffmpeg があれば動画からの変換まで確かめる。numpy が無ければ飛ばす。
"""
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile

try:
    import numpy as np
except ImportError:
    print("  numpy が無いので pvid_test を飛ばします")
    sys.exit(0)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "script"))
import generate_pvid as gp  # noqa: E402

failures = 0
checks = 0


def check(cond, what):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print("  [NG] " + what)


rng = np.random.default_rng(7)

# ---------------------------------------------------------------- タイルの符号化
for colors, want_mode, want_len in ((1, gp.MODE_SOLID, 1), (2, gp.MODE_TWO, 10), (5, gp.MODE_RAW, 33)):
    for _ in range(30):
        pal = rng.choice(16, size=colors, replace=False)
        t = pal[rng.integers(colors, size=(8, 8))].astype(np.uint8)
        t.flat[: colors] = pal          # 全部の色が必ず出るように
        data, mode = gp.encode_tile(t)
        back, end = gp.decode_tile(data, 0)
        check(mode == want_mode, "%d色のタイルの形 (%d)" % (colors, mode))
        check(len(data) == want_len, "%d色のタイルの大きさ (%d)" % (colors, len(data)))
        check(end == len(data), "タイルの終わりの位置")
        check(np.array_equal(back, t), "%d色のタイルの往復" % colors)

# そのままの形: 左の画素が上位4bit
t = np.zeros((8, 8), np.uint8)
t[0, :] = [1, 2, 3, 4, 5, 6, 7, 8]
t[1:, :] = 9
data, mode = gp.encode_tile(t)
check(data[1:5] == bytes([0x12, 0x34, 0x56, 0x78]), "4bppの並び(左が上位4bit)")
# 2色の形: bit7 が左端、1 が c1
t = np.full((8, 8), 3, np.uint8)
t[0, 0] = 11
t[7, 7] = 11
data, mode = gp.encode_tile(t)
check(data[0] == (gp.MODE_TWO << 4 | 3) and data[1] == 11, "2色の見出し")
check(data[2] == 0x80 and data[9] == 0x01 and data[3:9] == b"\0" * 6, "2色の印の並び")

try:
    gp.decode_tile(bytes([0x70]), 0)
    check(False, "知らない形は断る")
except ValueError:
    pass


# ---------------------------------------------------------------- 書いて読み直す
def pal16():
    return gp.choose_palette(rng.integers(256, size=(3000, 3)).astype(np.uint8))


def encode_frames(frames, w, h, threshold=0, key_every=5, palette=None):
    palette = pal16() if palette is None else palette
    buf = io.BytesIO()
    enc = gp.Encoder(buf, w, h, 15, 1, palette, key_every, threshold)
    recons = []
    for fr in frames:
        enc.add(fr)
        recons.append(gp.from_tiles(enc.recon, w, h).copy())
    enc.finish()
    path = os.path.join(TMP, "t.pvid")
    with open(path, "wb") as f:
        f.write(buf.getvalue())
    return enc, gp.PvidFile(path), recons, palette


TMP = tempfile.mkdtemp(prefix="pvid_test_")
W, H = 48, 24
frames = []
base = rng.integers(16, size=(H, W)).astype(np.uint8)
for n in range(12):
    f = base.copy()
    f[(n * 2) % H: (n * 2) % H + 3, n * 3: n * 3 + 5] = n % 16      # 動く小さな四角
    if n == 6:
        f = rng.integers(16, size=(H, W)).astype(np.uint8)        # 場面が変わる
    frames.append(f)
frames.append(frames[-1].copy())                                   # 変化の無いフレーム

enc, v, recons, palette = encode_frames(frames, W, H)
check(v.w == W and v.h == H and v.fps_num == 15 and v.fps_den == 1, "見出しの大きさ・fps")
check(v.frame_count == len(frames), "フレーム数")
check(np.array_equal(v.palette, palette), "パレットの往復(RGB565)")
check(v.max_frame == enc.max_frame and v.max_frame > 0, "最大のフレームの大きさ")
decoded = list(v.frames())
check(len(decoded) == len(frames), "読み出したフレーム数")
for n, (img, key, _) in enumerate(decoded):
    check(np.array_equal(img, frames[n]), "threshold=0 なら元と同じ (%d)" % n)
    check(key == (n % 5 == 0), "キーフレームの印 (%d)" % n)
check(decoded[-1][2] == 1 + v.bitmap_bytes, "変化の無いフレームは印の表だけ")
check([n for n, _ in v.index] == [0, 5, 10], "目録のフレーム番号")
for n, off in v.index:
    (length,) = struct.unpack_from("<I", v.data, off)
    check(v.data[off + 4] & gp.FLAG_KEY, "目録の位置はキーフレームを指す (%d)" % n)
    # 目録の位置から読み始めても同じ絵になる(シーク)
    first = next(gp.PvidFile(os.path.join(TMP, "t.pvid")).frames(off))
    check(np.array_equal(first[0], frames[n]), "目録の位置から読める (%d)" % n)

# 差分を間引いても、読み出した絵は「本体の画面に出ているはずの絵」と一致する
f0 = rng.integers(16, size=(H, W)).astype(np.uint8)
seq = [f0]
for n in range(1, 8):
    f = seq[-1].copy()
    ys, xs = rng.integers(H, size=3), rng.integers(W, size=3)
    f[ys, xs] = rng.integers(16, size=3)
    seq.append(f)
enc, v, recons, _ = encode_frames(seq, W, H, threshold=6, key_every=100)
for n, (img, _, _) in enumerate(v.frames()):
    check(np.array_equal(img, recons[n]), "読み出し = 本体の画面 (%d)" % n)

# ---------------------------------------------------------------- 差分の判定
pal = np.array([gp.rgb565_to_rgb(gp.rgb565((i * 17, i * 17, i * 17))) for i in range(16)], np.uint8)
flat = np.full((H, W), 8, np.uint8)
near = flat.copy()
near[0, 0:3] = 9                    # 隣の色へ3画素(ディザの揺れ程度)
far = flat.copy()
far[10, 20] = 3                     # 1画素だけ、遠い色(合計は threshold より小さい)
enc, v, _, _ = encode_frames([flat, near, far], W, H, threshold=20, key_every=100, palette=pal)
fr = list(v.frames())
check(fr[1][2] == 1 + v.bitmap_bytes, "近い色の小さな揺れは送らない")
check(fr[2][2] > 1 + v.bitmap_bytes and fr[2][0][10, 20] == 3, "遠い色は1画素でも送る")

# ---------------------------------------------------------------- 色選び・大きさ
pal = gp.choose_palette(rng.integers(256, size=(5000, 3)).astype(np.uint8))
check(pal.shape == (16, 3), "16色")
check(tuple(pal[0]) == (0, 0, 0) and tuple(pal[15]) == (255, 255, 255), "0番=黒・15番=白")
check(all(tuple(c) == gp.rgb565_to_rgb(gp.rgb565(c)) for c in pal), "RGB565で表せる色だけ")
red = np.tile(np.array([[200, 30, 30]], np.uint8), (2000, 1))
pal = gp.choose_palette(np.concatenate([red, rng.integers(256, size=(2000, 3)).astype(np.uint8)]))
check(min(np.abs(pal.astype(int) - [200, 30, 30]).sum(1)) < 16, "多い色はパレットに入る")
free = gp.choose_palette(rng.integers(64, 192, size=(3000, 3)).astype(np.uint8), fixed_bw=False)
check(not any(tuple(c) in ((0, 0, 0), (255, 255, 255)) for c in free), "--free-palette は黒白を固定しない")

check(gp.fit_size(1920, 1080, 240, 240) == (240, 128), "16:9 を240x240に収める")
check(gp.fit_size(640, 480, 240, 240) == (240, 176), "4:3 を240x240に収める")
check(gp.fit_size(480, 640, 240, 300) == (224, 296), "縦長")

# ---------------------------------------------------------------- 壊れたファイル
good = open(os.path.join(TMP, "t.pvid"), "rb").read()
for name, bad in (("magic", b"XXXX" + good[4:]),
                  ("版", good[:4] + b"\x09" + good[5:]),
                  ("大きさ", good[:8] + struct.pack("<H", 250) + good[10:])):
    p = os.path.join(TMP, "bad.pvid")
    open(p, "wb").write(bad)
    try:
        gp.PvidFile(p)
        check(False, "壊れたファイルを断る (%s)" % name)
    except ValueError:
        pass
p = os.path.join(TMP, "bad.pvid")
b = bytearray(good)
struct.pack_into("<I", b, gp.HEADER_SIZE, 999999)   # 1フレーム目の長さを壊す
open(p, "wb").write(b)
try:
    list(gp.PvidFile(p).frames())
    check(False, "長さの壊れたフレームを断る")
except (ValueError, struct.error):
    pass

# ---------------------------------------------------------------- 動画から(ffmpeg があれば)
if shutil.which("ffmpeg") and shutil.which("ffprobe"):
    src = os.path.join(TMP, "in.mp4")
    r = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25",
                        "-f", "lavfi", "-i", "sine=frequency=440", "-t", "2", "-pix_fmt", "yuv420p", src])
    if r.returncode == 0:
        out = os.path.join(TMP, "out.pvid")
        prev = os.path.join(TMP, "prev.mp4")
        old = sys.stdout
        sys.stdout = io.StringIO()
        try:
            gp.main(["encode", src, out, "--fps", "10", "--preview", prev])
        finally:
            sys.stdout = old
        v = gp.PvidFile(out)
        check((v.w, v.h) == (240, 176), "変換した大きさ")
        check(v.frame_count == 20, "変換したフレーム数 (%d)" % v.frame_count)
        check(len(list(v.frames())) == 20, "変換したファイルを読み切れる")
        check(os.path.getsize(os.path.join(TMP, "out.wav")) > 22050 * 2 * 2 - 4096, "音声が書き出される")
        check(os.path.getsize(prev) > 0, "プレビューが書き出される")
    else:
        print("  ffmpeg で試しの動画を作れなかったので、変換は飛ばします")
else:
    print("  ffmpeg が無いので、動画からの変換は飛ばします")

shutil.rmtree(TMP, ignore_errors=True)
print("  %d 件中 %d 件失敗" % (checks, failures))
sys.exit(1 if failures else 0)
