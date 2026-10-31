#!/usr/bin/env python3
"""script/firmware_tool.py(ファームの更新ファイルを作る・送る)のテスト。

  python3 firmware_tool_test.py <firmware_testのパス> <作業ディレクトリ>

- 版の印とLittleFSの範囲を入れた偽の firmware.bin から作り、verify・gzipの往復・断る場合を見る
- 作ったものを C++ の firmware_test(本物の Fw_Image/Fw_Update)に読ませる(見出しの並び・署名が両方で同じか)
- push のTCPの取り決め(WAIT/OK/DONE/ERR)を偽の本体で確かめる
- sign(できている署名なしの更新ファイルに後から署名する。ナイトリーがCIで使う)と、その結果を firmware_test に読ませる
"""
import gzip
import os
import socket
import struct
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import firmware_tool as ft  # noqa: E402

failures = 0


def check(cond, label):
    global failures
    print(("[ OK ] " if cond else "[FAIL] ") + label)
    if not cond:
        failures += 1


def expect_exit(fn, part, label):
    try:
        fn()
    except SystemExit as e:
        check(part in str(e), f"{label}({e})")
        return
    check(False, label + "(断らなかった)")


def fake_bin(serial=4321, git="beef123+", fs=(0x1025F000, 0x103FF000), size=300000, marker=True, env=None):
    data = bytearray(os.urandom(size // 2) + bytes(size - size // 2))  # 半分は縮む
    struct.pack_into("<IIII", data, ft.FS_WORDS_OFFSET, fs[0], fs[1], 0x103FF000, 0x24F000)
    if marker:
        tail = f"env={env};" if env is not None else ""
        m = f"PICOOS-BUILD;serial={serial};git={git};date=2026-10-16 05:22;board=rpipico2w;{tail}\0".encode()
        data[0x5000:0x5000 + len(m)] = m
    return bytes(data)


def main():
    firmware_test, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    seed = bytes(range(1, 33))
    pub = ft.ed25519_public(seed)
    quiet = lambda *a, **k: None  # noqa: E731

    data = fake_bin()
    blob = ft.pack(data, seed, log=quiet)
    p = ft.parse(blob)
    check(p["serial"] == 4321 and p["git"] == "beef123+" and p["board"] == "rpipico2w", "版の印を firmware.bin から読む")
    check(p["fs_start"] == 0x1025F000 and p["fs_end"] == 0x103FF000, "LittleFSの範囲を 0x2FF0 から読む")
    check(p["image_bytes"] == len(data) and gzip.decompress(p["payload"]) == data, "中身は firmware.bin の gzip")
    check(p["flags"] & ft.FLAG_SIGNED and ft.ed25519_verify(pub, ft.SIGN_CONTEXT + p["header"], p["sig"]), "署名を確かめられる")
    check(p["build_time"] == 1792128120, "日時(UTC)")
    unsigned = ft.parse(ft.pack(data, None, log=quiet))
    check(unsigned["flags"] == 0 and unsigned["sig"] == bytes(64), "鍵が無ければ署名なし(署名の欄は0)")
    check(len(ft.SIGN_CONTEXT) == 16, "署名の前置きは16バイト")

    expect_exit(lambda: ft.pack(fake_bin(marker=False), seed, log=quiet), "版の印", "版の印が無いファームは断る")
    expect_exit(lambda: ft.pack(fake_bin(fs=(0x103FF000, 0x103FF000)), seed, log=quiet), "LittleFS", "LittleFSが無いファームは断る")
    expect_exit(lambda: ft.pack(b"\0" * 100, seed, log=quiet), "小さすぎ", "小さすぎるファイルは断る")
    big = bytearray(os.urandom(0x1025F000 - 0x10000000 + 10))
    struct.pack_into("<II", big, ft.FS_WORDS_OFFSET, 0x1025F000, 0x103FF000)
    m = b"PICOOS-BUILD;serial=1;git=a;date=;board=rpipico2w;"
    big[0x5000:0x5000 + len(m)] = m
    expect_exit(lambda: ft.pack(bytes(big), seed, log=quiet), "アプリの領域", "アプリの領域より大きいファームは断る")

    # ---- C++ の firmware_test に読ませる ----
    pfw = os.path.join(work, "cross.pfw")
    keyf = os.path.join(work, "cross.pub")
    with open(pfw, "wb") as f:
        f.write(blob)
    with open(keyf, "w") as f:
        f.write(pub.hex() + "\n")
    r = subprocess.run([firmware_test, pfw, keyf], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    out = r.stdout.decode("utf-8", "replace")
    print(out.rstrip())
    check(r.returncode == 0, "C++ の Fw_Image/Fw_Update が python の更新ファイルを読める")

    # ---- 版の印の env=(2026-10-29から) ----
    info = ft.read_build_info(fake_bin(env="rpipico2w_lite"))
    check(info[4] == "rpipico2w_lite" and info[0] == 4321, "版の印の env= を読む")
    check(ft.read_build_info(fake_bin())[4] == "", "env= の無い古い印も読める(構成は空)")
    check(ft.payload_build_info(ft.parse(ft.pack(fake_bin(env="rpipico2w"), None, log=quiet)))["env"] == "rpipico2w",
          "更新ファイルの中身から版の印を読める")

    # ---- 後から署名する ----
    unsigned_blob = ft.pack(data, None, log=quiet)
    signed_later = ft.sign_blob(unsigned_blob, seed)
    sp = ft.parse(signed_later)
    check(sp["flags"] & ft.FLAG_SIGNED and ft.ed25519_verify(pub, ft.SIGN_CONTEXT + sp["header"], sp["sig"]),
          "sign: 署名なしの更新ファイルに後から署名できる")
    check(sp["payload"] == unsigned["payload"] and sp["header"][16:] == unsigned["header"][16:],
          "sign: 中身と見出しの他の値は変えない")
    check(ft.sign_blob(signed_later, seed) == signed_later, "sign: 署名済みに署名し直しても同じ")
    broken = bytearray(unsigned_blob)
    broken[-10] ^= 0xFF
    expect_exit(lambda: ft.sign_blob(bytes(broken), seed), "合いません", "sign: 壊れた更新ファイルには署名しない")
    expect_exit(lambda: ft.sign_blob(b"xx", seed), "読めません", "sign: 更新ファイルでないものは断る")

    keys_hpp = os.path.join(work, "Fw_Keys.hpp")
    with open(keys_hpp, "w") as f:
        f.write("    inline constexpr Key kBuiltin[] = {\n")
        f.write("        { { " + ", ".join(["0x00"] * 32) + " } },\n")
        f.write("        { { " + ", ".join(f"0x{b:02x}" for b in pub) + " } },\n    };\n")
    check(ft.builtin_keys(keys_hpp) == [pub], "Fw_Keys.hpp の鍵を読む(全部0の未登録は除く)")
    real_keys = ft.builtin_keys(os.path.join(HERE, "..", "..", "src", "apps", "firmware", "Fw_Keys.hpp"))
    check(len(real_keys) >= 1 and all(len(k) == 32 for k in real_keys), "本物の Fw_Keys.hpp の鍵を読める")
    keyfile = os.path.join(work, "seed.key")
    with open(keyfile, "w") as f:
        f.write(seed.hex() + "\n")
    in_pfw = os.path.join(work, "unsigned.pfw")
    out_pfw = os.path.join(work, "signed_later.pfw")
    with open(in_pfw, "wb") as f:
        f.write(unsigned_blob)
    tool = os.path.join(HERE, "..", "firmware_tool.py")
    r = subprocess.run([sys.executable, tool, "sign", in_pfw, "--out", out_pfw, "--key", keyfile,
                        "--expect-keys", keys_hpp], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    check(r.returncode == 0 and open(out_pfw, "rb").read() == signed_later, "sign: コマンドで署名できる")
    other = os.path.join(work, "other.key")
    with open(other, "w") as f:
        f.write(bytes(range(2, 34)).hex() + "\n")
    r = subprocess.run([sys.executable, tool, "sign", in_pfw, "--out", out_pfw + ".x", "--key", other,
                        "--expect-keys", keys_hpp], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    check(r.returncode != 0 and "違います" in r.stdout.decode("utf-8", "replace") and not os.path.exists(out_pfw + ".x"),
          "sign: 焼き込みの鍵と違う鍵では断る")
    r = subprocess.run([firmware_test, out_pfw, keyf], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    check(r.returncode == 0, "C++ の Fw_Image/Fw_Update が後から署名した更新ファイルを読める")

    # ---- push(偽の本体) ----
    def fake_device(replies_after_header, replies_after_payload, got):
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 0))
        srv.listen(1)

        def run():
            c, _ = srv.accept()
            with c:
                buf = b""
                while len(buf) < 192:
                    buf += c.recv(192 - len(buf))
                got["prefix"] = buf
                for line in replies_after_header:
                    c.sendall(line.encode() + b"\n")
                if replies_after_header and replies_after_header[-1] != "OK":
                    return
                need = len(blob) - 192
                body = b""
                while len(body) < need:
                    chunk = c.recv(65536)
                    if not chunk:
                        break
                    body += chunk
                got["payload"] = body
                for line in replies_after_payload:
                    c.sendall(line.encode() + b"\n")
            srv.close()

        t = threading.Thread(target=run, daemon=True)
        t.start()
        return srv.getsockname()[1], t

    got = {}
    port, t = fake_device(["WAIT 今より古い版です", "OK"], ["DONE"], got)
    logs = []
    ft.push("127.0.0.1", port, blob, log=logs.append)
    t.join(5)
    check(got.get("prefix") == blob[:192] and got.get("payload") == blob[192:], "push: 見出しの後、OK を待って中身を送る")
    check(any("古い版" in s for s in logs), "push: WAIT の理由を出す")

    got = {}
    port, t = fake_device(["ERR 署名の無い更新ファイルは受け付けません"], [], got)
    expect_exit(lambda: ft.push("127.0.0.1", port, blob, log=quiet), "署名の無い", "push: ERR で止まる")
    t.join(5)
    check("payload" not in got, "push: 断られたら中身を送らない")

    got = {}
    port, t = fake_device(["OK"], ["ERR 中身が見出しと合いません(壊れています)"], got)
    expect_exit(lambda: ft.push("127.0.0.1", port, blob, log=quiet), "壊れて", "push: 中身の後の ERR")
    t.join(5)

    print(f"\n{'FAILED' if failures else 'ALL PASSED'} ({failures} failures)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
