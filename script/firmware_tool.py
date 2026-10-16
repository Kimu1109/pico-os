#!/usr/bin/env python3
"""pico-os のファームの更新ファイル(.pfw、FIRMWARE_UPDATE.md)を作る・確かめる・本体へ送る。
標準ライブラリだけで動く。

使い方:
  # 1. 署名の鍵を作る(1回だけ)。秘密鍵は手元のPCにだけ置く(リポジトリ・GitHubに置かない)。
  #    表示された1行を src/firmware/Fw_Keys.hpp へ足し、USBで1回書き込む
  python3 script/firmware_tool.py keygen

  # 2a. SDに置く更新ファイルを作る(pio run の後。pio run -t fwpack でも同じ)
  python3 script/firmware_tool.py pack --bin .pio/build/rpipico2w/firmware.bin --out firmware.pfw

  # 2b. Wi-Fiで本体へ送る(本体で「本体の更新」→[受け取る]を押しておく。pio run -t fwpush --upload-port IP でも同じ)
  python3 script/firmware_tool.py push --host 192.168.1.23 --bin .pio/build/rpipico2w/firmware.bin

  # 確かめる
  python3 script/firmware_tool.py verify firmware.pfw [--pubkey 16進64桁]

鍵は --key で指定する。無ければ環境変数 PICOOS_FW_KEY、それも無ければ ~/.pico-os/firmware.key
(あれば)を使う。鍵が無ければ署名なしで作る(本体は debug.cfg の fw-allow-unsigned = true のときだけ、
確認のうえで受け付ける)。
"""

import argparse
import calendar
import gzip
import hashlib
import os
import re
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_app_repo import ed25519_public, ed25519_sign, ed25519_verify  # noqa: E402

# ---- 本体(src/firmware/Fw_Image.hpp)と揃える値 ----
MAGIC = b"PICO-FW1"
HEADER_BYTES = 128
SIG_BYTES = 64
FORMAT = 1
FLAG_SIGNED = 1
SIGN_CONTEXT = b"pico-os-fw-v1\0\0\0"  # 16バイト
FLASH_BASE = 0x10000000
APP_OFFSET = 0x3000
FS_WORDS_OFFSET = 0x3000 - 0x10  # arduino-picoのOTAのローダーが __FS_START__/__FS_END__ を読む場所
BOARD = "rpipico2w"
DEFAULT_PORT = 8370
DEFAULT_KEY = os.path.join("~", ".pico-os", "firmware.key")

# src/firmware/Fw_Version.cpp の kMarker
MARKER_RE = re.compile(rb"PICOOS-BUILD;serial=(\d+);git=([^;\x00]*);date=([^;\x00]*);board=([^;\x00]*);")


# ---------------------------------------------------------------------------
# 鍵
# ---------------------------------------------------------------------------
def key_path(args_key):
    if args_key:
        return os.path.expanduser(args_key)
    env = os.environ.get("PICOOS_FW_KEY")
    if env:
        return os.path.expanduser(env)
    return os.path.expanduser(DEFAULT_KEY)


def load_seed(path):
    with open(path, "r", encoding="ascii") as f:
        text = f.read().strip()
    seed = bytes.fromhex(text)
    if len(seed) != 32:
        raise SystemExit(f"{path}: 秘密鍵は16進64桁です")
    return seed


def find_seed(args_key, required=False):
    path = key_path(args_key)
    if os.path.exists(path):
        return load_seed(path)
    if args_key or required:
        raise SystemExit(f"{path} がありません(python3 script/firmware_tool.py keygen で作れます)")
    return None


def print_pubkey(pub):
    print(f"公開鍵: {pub.hex()}")
    print("")
    print("src/firmware/Fw_Keys.hpp の kBuiltin をこの1行に置き換え(または足し)、USBで1回書き込んでください:")
    body = ", ".join(f"0x{b:02x}" for b in pub)
    print(f"        {{ {{ {body} }} }},")


def cmd_keygen(args):
    path = key_path(args.key)
    if os.path.exists(path) and not args.force:
        raise SystemExit(f"{path} は既にあります(上書きするなら --force)")
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    seed = os.urandom(32)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="ascii") as f:
        f.write(seed.hex() + "\n")
    print(f"秘密鍵: {path}(この端末の外へ出さないこと)")
    print_pubkey(ed25519_public(seed))


def cmd_pubkey(args):
    print_pubkey(ed25519_public(find_seed(args.key, required=True)))


# ---------------------------------------------------------------------------
# 作る
# ---------------------------------------------------------------------------
def read_build_info(data):
    m = MARKER_RE.search(data)
    if not m:
        raise SystemExit("firmware.bin に版の印(PICOOS-BUILD;...)が見つかりません。"
                         "pico-os のファームですか?(古いファームは更新ファイルにできません)")
    serial = int(m.group(1))
    git = m.group(2).decode("ascii", "replace")
    date = m.group(3).decode("ascii", "replace")
    board = m.group(4).decode("ascii", "replace")
    return serial, git, date, board


def parse_date(date):
    for fmt in ("%Y-%m-%d %H:%M", "%Y-%m-%d %H:%M:%S"):
        try:
            return calendar.timegm(time.strptime(date, fmt))
        except ValueError:
            pass
    return int(time.time())


def make_header(fields, signed):
    git = fields["git"].encode("ascii")[:16]
    board = fields["board"].encode("ascii")
    if len(board) > 16:
        raise SystemExit("基板の名前が長すぎます")
    h = bytearray(HEADER_BYTES)
    h[0:8] = MAGIC
    struct.pack_into("<HHI", h, 8, HEADER_BYTES, FORMAT, FLAG_SIGNED if signed else 0)
    h[16:16 + len(board)] = board
    struct.pack_into("<II", h, 32, fields["serial"], fields["build_time"])
    h[40:40 + len(git)] = git
    struct.pack_into("<IIIII", h, 56, fields["image_bytes"], fields["payload_bytes"],
                     FLASH_BASE, fields["fs_start"], fields["fs_end"])
    h[76:108] = fields["sha256"]
    return bytes(h)


def pack(data, seed, log=print):
    if len(data) <= APP_OFFSET + 16:
        raise SystemExit("firmware.bin が小さすぎます")
    serial, git, date, board = read_build_info(data)
    fs_start, fs_end = struct.unpack_from("<II", data, FS_WORDS_OFFSET)
    if not (FLASH_BASE < fs_start <= fs_end):
        raise SystemExit(f"LittleFSの範囲が読めません(0x{fs_start:08x}〜0x{fs_end:08x})")
    if fs_start == fs_end:
        raise SystemExit("このファームにはLittleFSの領域がありません(platformio.ini の board_build.filesystem_size)。"
                         "更新ファイルを置く場所が無いので、更新できません")
    if len(data) > fs_start - FLASH_BASE:
        raise SystemExit("firmware.bin がアプリの領域より大きいです")
    payload = gzip.compress(data, compresslevel=9, mtime=0)
    if len(payload) > fs_end - fs_start:
        raise SystemExit(f"gzipしても {len(payload)} バイトあり、置き場({fs_end - fs_start}バイト)に入りません")
    fields = dict(serial=serial, git=git, board=board, build_time=parse_date(date),
                  image_bytes=len(data), payload_bytes=len(payload),
                  fs_start=fs_start, fs_end=fs_end, sha256=hashlib.sha256(payload).digest())
    header = make_header(fields, seed is not None)
    sig = ed25519_sign(seed, SIGN_CONTEXT + header) if seed is not None else bytes(SIG_BYTES)
    log(f"版 #{serial} ({git}) {date}  {len(data)}バイト → gzip {len(payload)}バイト"
        f"(置き場 {fs_end - fs_start}バイト)  {'署名あり' if seed is not None else '署名なし'}")
    return header + sig + payload


def cmd_pack(args):
    with open(args.bin, "rb") as f:
        data = f.read()
    seed = None if args.unsigned else find_seed(args.key)
    blob = pack(data, seed)
    out = args.out or os.path.splitext(args.bin)[0] + ".pfw"
    with open(out, "wb") as f:
        f.write(blob)
    print(f"書きました: {out}")
    print("SDの /firmware/ に置き、ファイルアプリか「本体の更新」から開いてください")


# ---------------------------------------------------------------------------
# 確かめる
# ---------------------------------------------------------------------------
def parse(blob):
    if len(blob) < HEADER_BYTES + SIG_BYTES:
        raise ValueError("短すぎます")
    h = blob[:HEADER_BYTES]
    if h[0:8] != MAGIC:
        raise ValueError("更新ファイルではありません")
    hb, fmt, flags = struct.unpack_from("<HHI", h, 8)
    if hb != HEADER_BYTES or fmt != FORMAT:
        raise ValueError("形式が違います")
    serial, build_time = struct.unpack_from("<II", h, 32)
    image, payload_len, base, fs_start, fs_end = struct.unpack_from("<IIIII", h, 56)
    return dict(header=h, sig=blob[HEADER_BYTES:HEADER_BYTES + SIG_BYTES], flags=flags,
                board=h[16:32].rstrip(b"\0").decode(), git=h[40:56].rstrip(b"\0").decode(),
                serial=serial, build_time=build_time, image_bytes=image, payload_bytes=payload_len,
                flash_base=base, fs_start=fs_start, fs_end=fs_end, sha256=h[76:108],
                payload=blob[HEADER_BYTES + SIG_BYTES:])


def cmd_verify(args):
    with open(args.file, "rb") as f:
        blob = f.read()
    try:
        p = parse(blob)
    except ValueError as e:
        raise SystemExit(f"{args.file}: {e}")
    ok = True
    print(f"版 #{p['serial']} ({p['git']})  基板 {p['board']}  "
          f"{time.strftime('%Y-%m-%d %H:%M', time.gmtime(p['build_time']))} UTC")
    print(f"イメージ {p['image_bytes']}バイト / gzip {p['payload_bytes']}バイト  "
          f"LittleFS 0x{p['fs_start']:08x}〜0x{p['fs_end']:08x}")
    if len(p["payload"]) != p["payload_bytes"] or hashlib.sha256(p["payload"]).digest() != p["sha256"]:
        print("中身: 見出しと合いません")
        ok = False
    else:
        try:
            if len(gzip.decompress(p["payload"])) != p["image_bytes"]:
                print("中身: 解いた大きさが見出しと合いません")
                ok = False
            else:
                print("中身: OK")
        except OSError as e:
            print(f"中身: gzipとして読めません({e})")
            ok = False
    if p["flags"] & FLAG_SIGNED:
        if args.pubkey:
            good = ed25519_verify(bytes.fromhex(args.pubkey), SIGN_CONTEXT + p["header"], p["sig"])
            print("署名: OK" if good else "署名: 合いません")
            ok = ok and good
        else:
            print("署名: あり(--pubkey で確かめられます)")
    else:
        print("署名: なし")
    if not ok:
        raise SystemExit(1)


# ---------------------------------------------------------------------------
# 送る
# ---------------------------------------------------------------------------
def read_line(sock, timeout):
    sock.settimeout(timeout)
    buf = bytearray()
    while True:
        b = sock.recv(1)
        if not b:
            raise SystemExit("本体が接続を切りました" + (f": {buf.decode('utf-8', 'replace')}" if buf else ""))
        if b == b"\n":
            return buf.decode("utf-8", "replace")
        buf += b


def push(host, port, blob, log=print, confirm_timeout=180):
    prefix = blob[:HEADER_BYTES + SIG_BYTES]
    payload = blob[HEADER_BYTES + SIG_BYTES:]
    log(f"{host}:{port} へ繋いでいます…")
    try:
        sock = socket.create_connection((host, port), timeout=10)
    except OSError as e:
        raise SystemExit(f"繋がりません({e})。本体で「本体の更新」→[受け取る]を押していますか?")
    with sock:
        sock.sendall(prefix)
        while True:
            line = read_line(sock, confirm_timeout)
            if line.startswith("WAIT"):
                log(f"本体で確認してください: {line[5:]}")
                continue
            if line == "OK":
                break
            raise SystemExit(f"本体が断りました: {line[4:] if line.startswith('ERR ') else line}")
        sock.settimeout(60)
        total = len(payload)
        sent = 0
        last = 0.0
        while sent < total:
            n = sock.send(payload[sent:sent + 4096])
            sent += n
            now = time.time()
            if now - last > 0.5 or sent == total:
                last = now
                sys.stdout.write(f"\r送っています {sent * 100 // total:3d}% ({sent // 1024}/{total // 1024}KB)")
                sys.stdout.flush()
        sys.stdout.write("\n")
        line = read_line(sock, 120)
        if line != "DONE":
            raise SystemExit(f"本体が断りました: {line[4:] if line.startswith('ERR ') else line}")
    log("送りました。本体が再起動して書き換えます(書き換えの間、画面は消えたままです)")


def cmd_push(args):
    if args.pfw:
        with open(args.pfw, "rb") as f:
            blob = f.read()
    elif args.bin:
        with open(args.bin, "rb") as f:
            data = f.read()
        blob = pack(data, None if args.unsigned else find_seed(args.key))
    else:
        raise SystemExit("--bin か --pfw が要ります")
    host = args.host or os.environ.get("PICOOS_HOST")
    if not host:
        raise SystemExit("--host(本体のIPアドレス)が要ります。本体の「本体の更新」→[受け取る]に出ます")
    port = args.port
    if ":" in host:
        host, p = host.rsplit(":", 1)
        port = int(p)
    push(host, port, blob)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("keygen", help="署名の鍵を作る")
    p.add_argument("--key")
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_keygen)

    p = sub.add_parser("pubkey", help="公開鍵を表示する")
    p.add_argument("--key")
    p.set_defaults(func=cmd_pubkey)

    p = sub.add_parser("pack", help="更新ファイル(.pfw)を作る")
    p.add_argument("--bin", required=True, help="firmware.bin")
    p.add_argument("--out")
    p.add_argument("--key")
    p.add_argument("--unsigned", action="store_true", help="鍵があっても署名しない")
    p.set_defaults(func=cmd_pack)

    p = sub.add_parser("verify", help="更新ファイルを確かめる")
    p.add_argument("file")
    p.add_argument("--pubkey")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("push", help="Wi-Fiで本体へ送る")
    p.add_argument("--host", help="本体のIPアドレス(host:port でも可)")
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--bin")
    p.add_argument("--pfw")
    p.add_argument("--key")
    p.add_argument("--unsigned", action="store_true")
    p.set_defaults(func=cmd_push)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
