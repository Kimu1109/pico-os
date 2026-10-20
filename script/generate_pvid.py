#!/usr/bin/env python3
"""
generate_pvid.py
----------------
動画(mp4 等、ffmpeg が読めるもの)を pico-os 用の動画形式 .pvid へ変換する。
形式の仕様は docs/VIDEO_FORMAT.md。

    本体の画面(OSData::frame)は16色(4bpp)なので、色を落とす重い処理(色選び・ディザ)と
    フレーム間の差分取りは全部ここ(PC)で済ませる。本体は「変わった8x8のタイルを
    バッファへ写す」だけで再生できる。

使い方:
    python3 script/generate_pvid.py encode in.mp4 out.pvid
    python3 script/generate_pvid.py encode in.mp4 out.pvid --fps 12 --start 10 --duration 30 --preview check.mp4
    python3 script/generate_pvid.py info out.pvid
    python3 script/generate_pvid.py preview out.pvid check.mp4

必要なもの: ffmpeg / ffprobe(PATH の上)、numpy。
音声は既定で out.pvid と同じ名前の .wav(モノラル・16bit・22050Hz)へ書き出す
(本体の WAV 再生 SoundFunctions::WavPlay で鳴らし、WavPositionMs() を時計にして映像を合わせる想定)。
"""

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("numpy が要ります: pip install numpy")

# ------------------------------------------------------------------ 形式の定数(docs/VIDEO_FORMAT.md と一致させる)
MAGIC = b"PVID"
VERSION = 1
TILE = 8
HEADER_SIZE = 64
TILE_BYTES = TILE * TILE // 2          # 4bpp で32バイト
MODE_RAW = 0                           # 32バイトそのまま
MODE_SOLID = 1                         # 1色(色は見出しの下位4bit)
MODE_TWO = 2                           # 2色: 見出しの下位4bit=c0、次の1バイト=c1、8バイトの印(1=c1)
FLAG_KEY = 0x01

MAX_W = 240
MAX_H = 320

# Bayer 8x8(0..63)。画面に固定した模様なので、動かない所は毎フレーム同じ色になる
# (誤差拡散だと1画素の変化が広がって差分が効かなくなる)
_B2 = np.array([[0, 2], [3, 1]])


def _bayer(n):
    m = _B2
    while m.shape[0] < n:
        m = np.block([[4 * m + 0, 4 * m + 2], [4 * m + 3, 4 * m + 1]])
    return m


BAYER8 = (_bayer(8) + 0.5) / 64.0 - 0.5          # -0.5 .. 0.5


# ------------------------------------------------------------------ 色
def rgb565(rgb):
    r, g, b = (int(v) for v in rgb)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb565_to_rgb(v):
    r5 = (v >> 11) & 0x1F
    g6 = (v >> 5) & 0x3F
    b5 = v & 0x1F
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def srgb_to_lab(rgb):
    """(...,3) の 0..255 を CIE L*a*b*(D65)へ。"""
    c = np.asarray(rgb, dtype=np.float32) / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    x = c[..., 0] * 0.4124 + c[..., 1] * 0.3576 + c[..., 2] * 0.1805
    y = c[..., 0] * 0.2126 + c[..., 1] * 0.7152 + c[..., 2] * 0.0722
    z = c[..., 0] * 0.0193 + c[..., 1] * 0.1192 + c[..., 2] * 0.9505
    x /= 0.95047
    z /= 1.08883

    def f(t):
        return np.where(t > 0.008856, np.cbrt(t), 7.787 * t + 16.0 / 116.0)

    fx, fy, fz = f(x), f(y), f(z)
    return np.stack([116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)], axis=-1)


def choose_palette(samples, fixed_bw=True, iters=24, seed=1):
    """samples: (N,3) uint8 のRGB。16色(RGB565で表せる色)を選んで (16,3) の uint8 で返す。
    fixed_bw なら0番=黒・15番=白に固定し、残り14色を k-means(L*a*b* の距離)で選ぶ。"""
    rng = np.random.default_rng(seed)
    pts = samples.astype(np.float32)
    lab = srgb_to_lab(pts)
    n_free = 14 if fixed_bw else 16
    fixed = [np.array([0, 0, 0], np.float32), np.array([255, 255, 255], np.float32)] if fixed_bw else []

    # k-means++ で初期値を選ぶ(固定色も「既にある中心」として距離に入れる)
    centers = list(fixed)
    if not centers:
        centers.append(pts[rng.integers(len(pts))])
    while len(centers) < n_free + len(fixed):
        cl = srgb_to_lab(np.array(centers))
        d = ((lab[:, None, :] - cl[None, :, :]) ** 2).sum(-1).min(1)
        if d.sum() <= 0:
            centers.append(pts[rng.integers(len(pts))])
            continue
        centers.append(pts[rng.choice(len(pts), p=d / d.sum())])
    centers = np.array(centers, np.float32)
    nf = len(fixed)

    for _ in range(iters):
        cl = srgb_to_lab(centers)
        assign = ((lab[:, None, :] - cl[None, :, :]) ** 2).sum(-1).argmin(1)
        moved = False
        for k in range(nf, len(centers)):
            m = assign == k
            if m.any():
                c = pts[m].mean(0)
            else:
                # 空いた中心は一番遠い点へ置き直す
                d = ((lab[:, None, :] - cl[None, :, :]) ** 2).sum(-1).min(1)
                c = pts[int(d.argmax())]
            if np.abs(c - centers[k]).max() > 0.5:
                moved = True
            centers[k] = c
        if not moved:
            break

    free = centers[nf:]
    # 明るい順に並べる(見やすさのためだけ。並びに意味は無い)
    free = free[np.argsort(srgb_to_lab(free)[:, 0])]
    if fixed_bw:
        pal = [fixed[0]] + list(free) + [fixed[1]]
    else:
        pal = list(free)
    # 液晶に出る色(RGB565へ丸めた色)で持つ
    pal = [rgb565_to_rgb(rgb565(np.clip(np.rint(c), 0, 255))) for c in pal]
    return np.array(pal, np.uint8)


class Quantizer:
    """1フレームのRGBを、画面に固定した組織的ディザでパレットの番号へ落とす。"""

    def __init__(self, palette, w, h, dither=1.0):
        self.palette = palette.astype(np.float32)
        self.pal_lab = srgb_to_lab(self.palette)
        # ディザの振れ幅 = パレットの色どうしの距離(近い色との距離の中央値)
        d = np.sqrt(((self.palette[:, None, :] - self.palette[None, :, :]) ** 2).sum(-1))
        np.fill_diagonal(d, np.inf)
        spread = float(np.median(d.min(1)))
        tiles = np.tile(BAYER8, (h // 8 + 1, w // 8 + 1))[:h, :w]
        self.offset = (tiles * spread * dither)[..., None].astype(np.float32)

    def __call__(self, rgb):
        f = np.clip(rgb.astype(np.float32) + self.offset, 0, 255)
        lab = srgb_to_lab(f)
        d = ((lab[..., None, :] - self.pal_lab) ** 2).sum(-1)
        return d.argmin(-1).astype(np.uint8)


# ------------------------------------------------------------------ タイルの符号化
def to_tiles(img):
    """(H,W) -> (TH*TW, 8, 8)。タイルの並びは左上から行ごと。"""
    h, w = img.shape
    return img.reshape(h // TILE, TILE, w // TILE, TILE).swapaxes(1, 2).reshape(-1, TILE, TILE)


def from_tiles(tiles, w, h):
    return tiles.reshape(h // TILE, w // TILE, TILE, TILE).swapaxes(1, 2).reshape(h, w)


def encode_tile(t):
    """1タイル (8,8) を一番小さい形で符号化する(可逆)。(bytes, mode) を返す。"""
    colors = np.unique(t)
    if len(colors) == 1:
        return bytes([(MODE_SOLID << 4) | int(colors[0])]), MODE_SOLID
    if len(colors) == 2:
        c0, c1 = int(colors[0]), int(colors[1])
        mask = np.packbits(t == c1, axis=1).reshape(-1)       # 1行1バイト、bit7=左端
        return bytes([(MODE_TWO << 4) | c0, c1]) + mask.tobytes(), MODE_TWO
    packed = ((t[:, 0::2] << 4) | t[:, 1::2]).astype(np.uint8)  # 左の画素が上位4bit
    return bytes([MODE_RAW << 4]) + packed.tobytes(), MODE_RAW


def decode_tile(buf, pos):
    """encode_tile の逆。((8,8), 次の位置) を返す。"""
    head = buf[pos]
    mode, c0 = head >> 4, head & 0x0F
    pos += 1
    if mode == MODE_SOLID:
        return np.full((TILE, TILE), c0, np.uint8), pos
    if mode == MODE_TWO:
        c1 = buf[pos] & 0x0F
        bits = np.unpackbits(np.frombuffer(buf, np.uint8, TILE, pos + 1)).reshape(TILE, TILE)
        return np.where(bits == 1, c1, c0).astype(np.uint8), pos + 1 + TILE
    if mode == MODE_RAW:
        p = np.frombuffer(buf, np.uint8, TILE_BYTES, pos).reshape(TILE, TILE // 2)
        t = np.empty((TILE, TILE), np.uint8)
        t[:, 0::2] = p >> 4
        t[:, 1::2] = p & 0x0F
        return t, pos + TILE_BYTES
    raise ValueError("知らないタイルの形: %d" % mode)


# ------------------------------------------------------------------ ffmpeg
def need_tool(name):
    if shutil.which(name) is None:
        sys.exit("%s が見つかりません(PATH に入れてください)" % name)


def probe(path):
    need_tool("ffprobe")
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", path],
        capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("ffprobe が読めませんでした:\n" + out.stderr)
    info = json.loads(out.stdout)
    video = next((s for s in info.get("streams", []) if s.get("codec_type") == "video"), None)
    if video is None:
        sys.exit("映像が入っていません: " + path)
    has_audio = any(s.get("codec_type") == "audio" for s in info.get("streams", []))
    w, h = int(video["width"]), int(video["height"])
    sar = video.get("sample_aspect_ratio", "1:1")
    try:
        sn, sd = (int(x) for x in sar.split(":"))
        if sn > 0 and sd > 0:
            w = w * sn / sd
    except ValueError:
        pass
    return w, h, has_audio


def fit_size(src_w, src_h, box_w, box_h):
    """縦横比を保って box に収め、どちらも8の倍数にする。"""
    s = min(box_w / src_w, box_h / src_h)
    w = max(TILE, int(src_w * s) // TILE * TILE)
    h = max(TILE, int(src_h * s) // TILE * TILE)
    return w, h


def ffmpeg_frames(path, w, h, fps, start=None, duration=None):
    """RGB のフレームを (H,W,3) uint8 で1枚ずつ返す。"""
    need_tool("ffmpeg")
    cmd = ["ffmpeg", "-v", "error", "-nostdin"]
    if start is not None:
        cmd += ["-ss", str(start)]
    cmd += ["-i", path]
    if duration is not None:
        cmd += ["-t", str(duration)]
    cmd += ["-an", "-vf", "fps=%s,scale=%d:%d:flags=area,format=rgb24" % (fps, w, h),
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    size = w * h * 3
    try:
        while True:
            buf = proc.stdout.read(size)
            if len(buf) < size:
                break
            yield np.frombuffer(buf, np.uint8).reshape(h, w, 3)
    finally:
        proc.stdout.close()
        proc.wait()
    if proc.returncode not in (0, None) and proc.returncode != -13:
        sys.exit("ffmpeg が失敗しました(終了コード %d)" % proc.returncode)


def ffmpeg_audio(path, out, rate, start=None, duration=None):
    cmd = ["ffmpeg", "-v", "error", "-nostdin", "-y"]
    if start is not None:
        cmd += ["-ss", str(start)]
    cmd += ["-i", path]
    if duration is not None:
        cmd += ["-t", str(duration)]
    cmd += ["-vn", "-ac", "1", "-ar", str(rate), "-c:a", "pcm_s16le", out]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        sys.exit("音声の書き出しに失敗しました")


# ------------------------------------------------------------------ 書き出し
def pack_header(w, h, fps_num, fps_den, frame_count, index_offset, index_count, max_frame, palette):
    hdr = struct.pack("<4sBBHHHHHIIII", MAGIC, VERSION, TILE, HEADER_SIZE, w, h, fps_num, fps_den,
                      frame_count, index_offset, index_count, max_frame)
    hdr += struct.pack("<16H", *[rgb565(c) for c in palette])
    assert len(hdr) == HEADER_SIZE
    return hdr


class Encoder:
    """フレームの番号の配列 (H,W) を順に受け取り、.pvid を書く。"""

    def __init__(self, f, w, h, fps_num, fps_den, palette, keyframe_every, threshold):
        self.f = f
        self.w, self.h = w, h
        self.fps = (fps_num, fps_den)
        self.palette = palette
        self.key_every = max(1, keyframe_every)
        self.threshold = threshold
        self.n_tiles = (w // TILE) * (h // TILE)
        self.bitmap_bytes = (self.n_tiles + 7) // 8
        self.recon = None                    # 本体の画面に今出ているはずの絵(タイル)
        # 色どうしの違い(L*a*b*)を「隣の色1つぶん」(近い色との距離の中央値)で割ったもの。
        # ディザの揺れ(隣の色との入れ替わり)は小さく、違う物が動いてきた所は大きくなる
        lab = srgb_to_lab(palette.astype(np.float32))
        d = np.sqrt(((lab[:, None, :] - lab[None, :, :]) ** 2).sum(-1))
        nd = d.copy()
        np.fill_diagonal(nd, np.inf)
        self.cost = (d / max(1e-6, float(np.median(nd.min(1))))).astype(np.float32)
        self.count = 0
        self.index = []
        self.max_frame = 0
        self.stats = {"bytes": 0, "keys": 0, "tiles": 0, "modes": [0, 0, 0], "empty": 0}
        f.write(b"\0" * HEADER_SIZE)         # 見出しは最後に書き直す

    def add(self, idx):
        tiles = to_tiles(idx)
        key = self.recon is None or self.count % self.key_every == 0
        if key:
            changed = np.ones(self.n_tiles, bool)
        else:
            # 違いの合計が threshold を超えるか、1画素でも大きく違う(隣の色3つぶんより遠い)なら送る。
            # 後者が無いと、小さな点が動いても次のキーフレームまで残る
            c = self.cost[self.recon, tiles].reshape(self.n_tiles, -1)
            changed = (c.sum(1) > self.threshold) | (c.max(1) > 3.0)
        if key:
            self.recon = tiles.copy()
        else:
            self.recon[changed] = tiles[changed]

        body = bytearray()
        body.append(FLAG_KEY if key else 0)
        bits = np.packbits(changed, bitorder="little")
        body += bits.tobytes().ljust(self.bitmap_bytes, b"\0")
        for i in np.flatnonzero(changed):
            data, mode = encode_tile(tiles[i])
            body += data
            self.stats["modes"][mode] += 1
        rec = struct.pack("<I", len(body)) + bytes(body)

        if key:
            self.index.append((self.count, self.f.tell()))
            self.stats["keys"] += 1
        if not changed.any():
            self.stats["empty"] += 1
        self.f.write(rec)
        self.max_frame = max(self.max_frame, len(body))
        self.stats["bytes"] += len(rec)
        self.stats["tiles"] += int(changed.sum())
        self.count += 1

    def finish(self):
        index_offset = self.f.tell()
        for n, off in self.index:
            self.f.write(struct.pack("<II", n, off))
        self.f.seek(0)
        self.f.write(pack_header(self.w, self.h, self.fps[0], self.fps[1], self.count, index_offset,
                                 len(self.index), self.max_frame, self.palette))


# ------------------------------------------------------------------ 読み出し(検証・プレビュー用。本体の再生と同じ手順)
class PvidFile:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if len(d) < HEADER_SIZE or d[:4] != MAGIC:
            raise ValueError(".pvid ではありません")
        (_, self.version, tile, hsize, self.w, self.h, self.fps_num, self.fps_den, self.frame_count,
         self.index_offset, self.index_count, self.max_frame) = struct.unpack_from("<4sBBHHHHHIIII", d, 0)
        if self.version != VERSION or tile != TILE or hsize != HEADER_SIZE:
            raise ValueError("対応していない版です(version=%d tile=%d header=%d)" % (self.version, tile, hsize))
        if self.w % TILE or self.h % TILE or not (0 < self.w <= MAX_W and 0 < self.h <= MAX_H):
            raise ValueError("大きさが不正です: %dx%d" % (self.w, self.h))
        self.palette565 = struct.unpack_from("<16H", d, 32)
        self.palette = np.array([rgb565_to_rgb(v) for v in self.palette565], np.uint8)
        self.n_tiles = (self.w // TILE) * (self.h // TILE)
        self.bitmap_bytes = (self.n_tiles + 7) // 8
        self.index = [struct.unpack_from("<II", d, self.index_offset + 8 * i) for i in range(self.index_count)]

    def frames(self, start_offset=HEADER_SIZE):
        """(番号の配列 (H,W), キーフレームか, フレームの大きさ) を順に返す。"""
        d = self.data
        pos = start_offset
        screen = np.zeros((self.n_tiles, TILE, TILE), np.uint8)
        for _ in range(self.frame_count):
            (length,) = struct.unpack_from("<I", d, pos)
            end = pos + 4 + length
            if end > self.index_offset:
                raise ValueError("フレームが目録へはみ出しています")
            flags = d[pos + 4]
            bits = np.unpackbits(np.frombuffer(d, np.uint8, self.bitmap_bytes, pos + 5),
                                 bitorder="little")[:self.n_tiles]
            p = pos + 5 + self.bitmap_bytes
            for i in np.flatnonzero(bits):
                screen[i], p = decode_tile(d, p)
            if p != end:
                raise ValueError("フレームの長さが合いません")
            yield from_tiles(screen, self.w, self.h), bool(flags & FLAG_KEY), length
            pos = end


# ------------------------------------------------------------------ コマンド
def parse_fps(s):
    if "/" in s:
        n, d = s.split("/")
        return int(n), int(d)
    v = float(s)
    if v == int(v):
        return int(v), 1
    return int(round(v * 1000)), 1000


def cmd_encode(a):
    fps_num, fps_den = parse_fps(a.fps)
    if not (0 < fps_num / fps_den <= 60):
        sys.exit("--fps は 0〜60")
    src_w, src_h, has_audio = probe(a.input)
    if a.size:
        w, h = (int(v) for v in a.size.lower().split("x"))
        if w % TILE or h % TILE:
            sys.exit("--size はどちらも8の倍数にしてください")
    else:
        w, h = fit_size(src_w, src_h, a.width, a.height)
    if not (0 < w <= MAX_W and 0 < h <= MAX_H):
        sys.exit("大きさは %dx%d までです(今: %dx%d)" % (MAX_W, MAX_H, w, h))
    fps_s = "%d/%d" % (fps_num, fps_den)

    # 1周目: 色を選ぶ(1秒に2枚ほど拾う)
    print("色を選んでいます… (%dx%d, %s fps)" % (w, h, fps_s))
    rng = np.random.default_rng(1)
    samples = []
    per = max(1, 60000 // 200)
    n_seen = 0
    for fr in ffmpeg_frames(a.input, w, h, min(2.0, fps_num / fps_den), a.start, a.duration):
        px = fr.reshape(-1, 3)
        samples.append(px[rng.integers(len(px), size=min(per, len(px)))])
        n_seen += 1
        if n_seen >= 200:
            break
    if not samples:
        sys.exit("フレームが1枚も読めませんでした")
    palette = choose_palette(np.concatenate(samples), fixed_bw=not a.free_palette)

    # 2周目: 色を落として差分を書く
    quant = Quantizer(palette, w, h, a.dither)
    key_every = max(1, int(round(a.keyframe * fps_num / fps_den)))
    with open(a.output, "wb") as f:
        enc = Encoder(f, w, h, fps_num, fps_den, palette, key_every, a.threshold)
        for fr in ffmpeg_frames(a.input, w, h, fps_s, a.start, a.duration):
            enc.add(quant(fr))
            if enc.count % 50 == 0:
                print("  %d フレーム" % enc.count, end="\r", flush=True)
        print()
        if enc.count == 0:
            sys.exit("フレームが1枚も読めませんでした")
        enc.finish()

    print_stats(enc, w, h, fps_num / fps_den)

    if not a.no_audio and has_audio:
        wav = a.audio or os.path.splitext(a.output)[0] + ".wav"
        ffmpeg_audio(a.input, wav, a.audio_rate, a.start, a.duration)
        print("音声: %s (%d Hz・モノラル)" % (wav, a.audio_rate))
    if a.preview:
        write_preview(a.output, a.preview, a.preview_scale)


def print_stats(enc, w, h, fps):
    s = enc.stats
    n = enc.count
    total_tiles = n * enc.n_tiles
    print("書き出しました: %d フレーム (%.1f 秒)、%d KB" % (n, n / fps, (s["bytes"] + HEADER_SIZE) // 1024))
    print("  1フレーム平均 %.1f KB / 最大 %.1f KB、平均 %.0f KB/秒" %
          (s["bytes"] / n / 1024, enc.max_frame / 1024, s["bytes"] / n * fps / 1024))
    print("  送ったタイル %.1f%%(キーフレーム %d 枚、変化なし %d 枚)" %
          (100.0 * s["tiles"] / max(1, total_tiles), s["keys"], s["empty"]))
    m = s["modes"]
    tot = max(1, sum(m))
    print("  タイルの形: 1色 %.0f%% / 2色 %.0f%% / そのまま %.0f%%" %
          (100.0 * m[MODE_SOLID] / tot, 100.0 * m[MODE_TWO] / tot, 100.0 * m[MODE_RAW] / tot))


def write_preview(pvid_path, out, scale):
    need_tool("ffmpeg")
    v = PvidFile(pvid_path)
    fps = "%d/%d" % (v.fps_num, v.fps_den)
    cmd = ["ffmpeg", "-v", "error", "-nostdin", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
           "-s", "%dx%d" % (v.w * scale, v.h * scale), "-r", fps, "-i", "-",
           "-pix_fmt", "yuv420p", out]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for idx, _, _ in v.frames():
        rgb = v.palette[idx]
        if scale > 1:
            rgb = rgb.repeat(scale, 0).repeat(scale, 1)
        proc.stdin.write(np.ascontiguousarray(rgb).tobytes())
    proc.stdin.close()
    if proc.wait() != 0:
        sys.exit("プレビューの書き出しに失敗しました")
    print("プレビュー: %s" % out)


def cmd_info(a):
    v = PvidFile(a.file)
    print("%s: %dx%d、%d/%d fps、%d フレーム(%.1f 秒)" %
          (a.file, v.w, v.h, v.fps_num, v.fps_den, v.frame_count, v.frame_count * v.fps_den / v.fps_num))
    print("  キーフレーム %d 枚、1フレーム最大 %d バイト" % (v.index_count, v.max_frame))
    print("  パレット: " + " ".join("%04X" % c for c in v.palette565))
    if a.verbose:
        for n, (_, key, length) in enumerate(v.frames()):
            print("  %5d %s %6d" % (n, "K" if key else " ", length))


def cmd_preview(a):
    write_preview(a.file, a.output, a.scale)


def main(argv=None):
    p = argparse.ArgumentParser(description="動画を pico-os の .pvid へ変換する(docs/VIDEO_FORMAT.md)")
    sub = p.add_subparsers(dest="cmd", required=True)

    e = sub.add_parser("encode", help="動画を .pvid へ変換する")
    e.add_argument("input")
    e.add_argument("output")
    e.add_argument("--width", type=int, default=240, help="収める枠の幅(既定240)")
    e.add_argument("--height", type=int, default=240, help="収める枠の高さ(既定240)")
    e.add_argument("--size", help="大きさを直接決める(例 240x136。どちらも8の倍数)")
    e.add_argument("--fps", default="15", help="フレームレート(既定15。'30000/1001' の形も可)")
    e.add_argument("--start", type=float, help="切り出しの始め(秒)")
    e.add_argument("--duration", type=float, help="切り出す長さ(秒)")
    e.add_argument("--keyframe", type=float, default=2.0, help="キーフレームの間隔(秒、既定2)")
    e.add_argument("--threshold", type=float, default=4,
                   help="1タイル(64画素)の違いの合計がこれ以下なら送らない。単位は「1画素が隣の色1つぶん違う」(既定4。0で変わったタイルは全部送る)")
    e.add_argument("--dither", type=float, default=1.0, help="ディザの強さ(0で無し、既定1)")
    e.add_argument("--free-palette", action="store_true",
                   help="0番=黒・15番=白の固定をやめ、16色全部を動画から選ぶ")
    e.add_argument("--audio", help="音声の書き出し先(既定は出力と同じ名前の .wav)")
    e.add_argument("--audio-rate", type=int, default=22050, choices=[22050, 44100])
    e.add_argument("--no-audio", action="store_true", help="音声を書き出さない")
    e.add_argument("--preview", help="変換結果を見るための mp4 も書く")
    e.add_argument("--preview-scale", type=int, default=2)
    e.set_defaults(func=cmd_encode)

    i = sub.add_parser("info", help=".pvid の中身を表示する")
    i.add_argument("file")
    i.add_argument("-v", "--verbose", action="store_true", help="フレームごとの大きさも出す")
    i.set_defaults(func=cmd_info)

    v = sub.add_parser("preview", help=".pvid を mp4 へ戻して見る")
    v.add_argument("file")
    v.add_argument("output")
    v.add_argument("--scale", type=int, default=2)
    v.set_defaults(func=cmd_preview)

    a = p.parse_args(argv)
    a.func(a)


if __name__ == "__main__":
    main()
