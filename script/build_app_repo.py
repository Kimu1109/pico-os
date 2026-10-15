#!/usr/bin/env python3
"""pico-os のアプリストアの配布物(APP_REPO.md)を作る。標準ライブラリだけで動く。

使い方:
  # 1. 鍵を作る(1回だけ)。秘密鍵は手元のPCにだけ置く(リポジトリ・サーバに置かない)
  python3 script/build_app_repo.py keygen --key ~/.pico-os/app_repo.key

  # 2a. 配布物をまるごと作る(Raspberry Pi 等へそのまま置く)
  python3 script/build_app_repo.py build --key ~/.pico-os/app_repo.key --out /tmp/apps

  # 2b. GitHub Pages 向け: 署名した目録(catalog.tsv / catalog.sig / <id>/<版>/files.tsv)だけを
  #     リポジトリの store/ へ書き、コミットする。アプリの中身は Pages のワークフローが
  #     pc/sdcard/lua/apps/ から写す(assemble)。秘密鍵は GitHub に置かなくてよい
  python3 script/build_app_repo.py sign --key ~/.pico-os/app_repo.key --out store
  python3 script/build_app_repo.py assemble --index store --out _site/apps

  # 署名を確かめる
  python3 script/build_app_repo.py verify --dir /tmp/apps [--pubkey 16進64桁]

配るアプリは pc/sdcard/lua/apps/<ディレクトリ>/app.cfg に id = ... があるものだけ
(--apps-dir で場所を変えられる)。gitの管理下なら、gitが管理しているファイルだけを配る
(遊んでいる間にできた store.json・セーブデータ等を配らないため)。
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time

# ---- 本体(store/App_Repo.hpp)と揃える値 ----
MAGIC = "pico-os-apps"
FORMAT = 1
SIGN_CONTEXT = b"pico-os-apps-v1\0"  # 16バイト
MAX_ENTRIES = 40
MAX_CATALOG_BYTES = 32 * 1024
MAX_LINE_BYTES = 510  # 本体は512バイト(改行込み)まで読む
MAX_FILES = 64
MAX_FILES_BYTES = 8 * 1024
MAX_FILE_BYTES = 1024 * 1024
MAX_APP_BYTES = 4 * 1024 * 1024
MAX_PATH_LEN = 95
MAX_PATH_DEPTH = 4
MAX_NAME_BYTES = 47
MAX_DESC_BYTES = 180

ID_RE = re.compile(r"^[a-z0-9_-]{1,23}$")
VERSION_RE = re.compile(r"^[0-9]+(\.[0-9]+)*$")
SEG_RE = re.compile(r"^[A-Za-z0-9_-][A-Za-z0-9._-]*$")

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_APPS_DIR = os.path.join(ROOT, "pc", "sdcard", "lua", "apps")


# ---------------------------------------------------------------------------
# Ed25519(RFC 8032 の参照実装を短くしたもの。署名は1回だけなので速さは気にしない)
# ---------------------------------------------------------------------------
_P = 2 ** 255 - 19
_L = 2 ** 252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_I = pow(2, (_P - 1) // 4, _P)


def _inv(x):
    return pow(x, _P - 2, _P)


def _xrecover(y):
    xx = (y * y - 1) * _inv(_D * y * y + 1)
    x = pow(xx, (_P + 3) // 8, _P)
    if (x * x - xx) % _P != 0:
        x = (x * _I) % _P
    if x % 2 != 0:
        x = _P - x
    return x


_BY = 4 * _inv(5) % _P
_B = (_xrecover(_BY), _BY, 1, _xrecover(_BY) * _BY % _P)


def _add(p, q):
    x1, y1, z1, t1 = p
    x2, y2, z2, t2 = q
    a = (y1 - x1) * (y2 - x2) % _P
    b = (y1 + x1) * (y2 + x2) % _P
    c = t1 * 2 * _D * t2 % _P
    d = z1 * 2 * z2 % _P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % _P, g * h % _P, f * g % _P, e * h % _P)


def _mul(s, p):
    q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _encode_point(p):
    x, y, z, _ = p
    zi = _inv(z)
    x, y = x * zi % _P, y * zi % _P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


def _decode_point(b):
    y = int.from_bytes(b, "little") & ((1 << 255) - 1)
    x = _xrecover(y)
    if (x & 1) != (b[31] >> 7):
        x = _P - x
    return (x, y, 1, x * y % _P)


def _secret_scalar(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def ed25519_public(seed):
    a, _ = _secret_scalar(seed)
    return _encode_point(_mul(a, _B))


def ed25519_sign(seed, msg):
    a, prefix = _secret_scalar(seed)
    pub = _encode_point(_mul(a, _B))
    r = int.from_bytes(hashlib.sha512(prefix + msg).digest(), "little") % _L
    R = _encode_point(_mul(r, _B))
    k = int.from_bytes(hashlib.sha512(R + pub + msg).digest(), "little") % _L
    s = (r + k * a) % _L
    return R + s.to_bytes(32, "little")


def ed25519_verify(pub, msg, sig):
    if len(sig) != 64 or len(pub) != 32:
        return False
    s = int.from_bytes(sig[32:], "little")
    if s >= _L:
        return False
    try:
        A = _decode_point(pub)
        R = _decode_point(sig[:32])
    except Exception:
        return False
    k = int.from_bytes(hashlib.sha512(sig[:32] + pub + msg).digest(), "little") % _L
    left = _mul(s, _B)
    right = _add(R, _mul(k, A))
    return _encode_point(left) == _encode_point(right)


# ---------------------------------------------------------------------------
# 鍵
# ---------------------------------------------------------------------------
def load_seed(path):
    path = os.path.expanduser(path)
    with open(path, "r", encoding="ascii") as f:
        text = f.read().strip()
    seed = bytes.fromhex(text)
    if len(seed) != 32:
        raise SystemExit(f"{path}: 秘密鍵は16進64桁です")
    return seed


def cmd_keygen(args):
    path = os.path.expanduser(args.key)
    if os.path.exists(path) and not args.force:
        raise SystemExit(f"{path} は既にあります(上書きするなら --force)")
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    seed = os.urandom(32)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="ascii") as f:
        f.write(seed.hex() + "\n")
    pub = ed25519_public(seed)
    print(f"秘密鍵: {path}(この端末の外へ出さないこと)")
    print_pubkey(pub)


def print_pubkey(pub):
    print(f"公開鍵: {pub.hex()}")
    print("")
    print("本体に信頼させるには、どちらか:")
    print(f"  SDの /sys/store.cfg に  key = {pub.hex()}")
    body = ", ".join(f"0x{b:02x}" for b in pub)
    print("  src/store/App_Repo_Keys.hpp の kBuiltin に")
    print(f"        {{ {{ {body} }} }},")


def cmd_pubkey(args):
    print_pubkey(ed25519_public(load_seed(args.key)))


# ---------------------------------------------------------------------------
# アプリを集める
# ---------------------------------------------------------------------------
def read_app_cfg(path):
    cfg = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip()
    return cfg


def truthy(v):
    return v.strip().lower() in ("true", "1", "yes", "on")


def cut_utf8(s, limit):
    b = s.encode("utf-8")
    if len(b) <= limit:
        return s
    return b[:limit].decode("utf-8", errors="ignore")


def valid_dir_name(name):
    if not name or name.startswith(".") or len(name.encode("utf-8")) > 47:
        return False
    if any(ord(c) < 0x20 or ord(c) == 0x7F or c in '/\\:*?"<>|' for c in name):
        return False
    return not name.endswith((" ", "."))


def valid_rel_path(p):
    if len(p) > MAX_PATH_LEN:
        return False
    segs = p.split("/")
    if len(segs) > MAX_PATH_DEPTH:
        return False
    return all(SEG_RE.match(s) for s in segs)


def tracked_files(app_dir):
    """gitが管理しているファイル(gitの外ならディレクトリの中の全部)。app_dir からの相対パス"""
    try:
        out = subprocess.run(["git", "-C", app_dir, "ls-files", "-z", "--", "."],
                             check=True, capture_output=True).stdout
        names = [n for n in out.decode("utf-8").split("\0") if n]
        if names:
            return sorted(names)
    except (OSError, subprocess.CalledProcessError):
        pass
    result = []
    for base, dirs, files in os.walk(app_dir):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for f in files:
            if f.startswith("."):
                continue
            result.append(os.path.relpath(os.path.join(base, f), app_dir).replace(os.sep, "/"))
    return sorted(result)


class App:
    pass


def collect_apps(apps_dir, only):
    apps = []
    for name in sorted(os.listdir(apps_dir)):
        app_dir = os.path.join(apps_dir, name)
        cfg_path = os.path.join(app_dir, "app.cfg")
        if not os.path.isdir(app_dir) or not os.path.isfile(cfg_path):
            continue
        cfg = read_app_cfg(cfg_path)
        app_id = cfg.get("id", "")
        if not app_id:
            continue
        if only and app_id not in only:
            continue
        where = f"{name}/app.cfg"
        if not ID_RE.match(app_id):
            raise SystemExit(f"{where}: id は英小文字・数字・-・_ の23文字までです ({app_id})")
        version = cfg.get("version", "")
        if not VERSION_RE.match(version) or len(version) > 23:
            raise SystemExit(f"{where}: version は 1.2.3 のような数字と.だけにしてください ({version})")
        if not valid_dir_name(name):
            raise SystemExit(f"{name}: ディレクトリ名に使えない文字があります")
        a = App()
        a.id, a.version, a.dir, a.path = app_id, version, name, app_dir
        a.name = cut_utf8(cfg.get("name", name).replace("\t", " "), MAX_NAME_BYTES) or name
        a.description = cut_utf8(cfg.get("description", "").replace("\t", " "), MAX_DESC_BYTES)
        perms = []
        if truthy(cfg.get("permission_network", "false")):
            perms.append("network")
        if truthy(cfg.get("permission_notify", "false")):
            perms.append("notify")
        if truthy(cfg.get("permission_sd_outside_app_dir", "false")):
            perms.append("sd")
        if truthy(cfg.get("permission_background", "false")):
            perms.append("background")
        a.perms = ",".join(perms) or "-"
        files = tracked_files(app_dir)
        if "main.lua" not in files or "app.cfg" not in files:
            raise SystemExit(f"{name}: main.lua と app.cfg が要ります(gitの管理下ならgit addも)")
        files.sort(key=lambda p: (p.lower(), p))
        lowered = [p.lower() for p in files]
        if len(set(lowered)) != len(lowered):
            raise SystemExit(f"{name}: 大文字小文字だけが違うファイルがあります(SDでは同じ名前になる)")
        if len(files) > MAX_FILES:
            raise SystemExit(f"{name}: ファイルは{MAX_FILES}個までです")
        a.files = []
        total = 0
        for rel in files:
            if not valid_rel_path(rel):
                raise SystemExit(f"{name}/{rel}: パスは英数字と . - _ で、{MAX_PATH_DEPTH}段・{MAX_PATH_LEN}文字までにしてください")
            with open(os.path.join(app_dir, rel), "rb") as f:
                data = f.read()
            if len(data) > MAX_FILE_BYTES:
                raise SystemExit(f"{name}/{rel}: 1ファイル{MAX_FILE_BYTES}バイトまでです")
            total += len(data)
            a.files.append((rel, len(data), hashlib.sha256(data).hexdigest()))
        if total > MAX_APP_BYTES:
            raise SystemExit(f"{name}: 合計{MAX_APP_BYTES}バイトまでです")
        a.bytes = total
        a.files_tsv = "".join(f"{p}\t{n}\t{h}\n" for p, n, h in a.files).encode("ascii")
        if len(a.files_tsv) > MAX_FILES_BYTES:
            raise SystemExit(f"{name}: files.tsv が大きすぎます")
        apps.append(a)
    ids = [a.id for a in apps]
    if len(set(ids)) != len(ids):
        raise SystemExit("同じ id のアプリが2つあります")
    if len(apps) > MAX_ENTRIES:
        raise SystemExit(f"アプリは{MAX_ENTRIES}個までです")
    return apps


def make_catalog(apps, serial):
    lines = [f"{MAGIC}\t{FORMAT}\t{serial}\n"]
    for a in apps:
        h = hashlib.sha256(a.files_tsv).hexdigest()
        line = f"{a.id}\t{a.version}\t{a.dir}\t{a.name}\t{a.bytes}\t{h}\t{a.perms}\t{a.description}\n"
        if len(line.encode("utf-8")) > MAX_LINE_BYTES:
            raise SystemExit(f"{a.dir}: カタログの1行が長すぎます(名前・説明を短く)")
        lines.append(line)
    data = "".join(lines).encode("utf-8")
    if len(data) > MAX_CATALOG_BYTES:
        raise SystemExit("catalog.tsv が大きすぎます")
    return data


def sign_catalog(seed, catalog):
    msg = SIGN_CONTEXT + hashlib.sha256(catalog).digest()
    pub = ed25519_public(seed)
    sig = ed25519_sign(seed, msg)
    return f"ed25519 {pub.hex()} {sig.hex()}\n".encode("ascii")


def write_bytes(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def write_index(out, apps, seed, serial):
    catalog = make_catalog(apps, serial)
    write_bytes(os.path.join(out, "catalog.tsv"), catalog)
    write_bytes(os.path.join(out, "catalog.sig"), sign_catalog(seed, catalog))
    for a in apps:
        write_bytes(os.path.join(out, a.id, a.version, "files.tsv"), a.files_tsv)


def default_serial(args):
    return args.serial if args.serial is not None else int(time.time())


def cmd_build(args):
    seed = load_seed(args.key)
    apps = collect_apps(args.apps_dir, set(args.only.split(",")) if args.only else None)
    if args.clean and os.path.isdir(args.out):
        shutil.rmtree(args.out)
    write_index(args.out, apps, seed, default_serial(args))
    for a in apps:
        for rel, _, _ in a.files:
            dst = os.path.join(args.out, a.id, a.version, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(a.path, rel), dst)
    print(f"{args.out}: {len(apps)}件のアプリ")
    for a in apps:
        print(f"  {a.id} {a.version} ({a.dir}) {a.bytes}バイト perms={a.perms}")


def cmd_sign(args):
    seed = load_seed(args.key)
    apps = collect_apps(args.apps_dir, set(args.only.split(",")) if args.only else None)
    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    write_index(args.out, apps, seed, default_serial(args))
    print(f"{args.out}: {len(apps)}件のアプリの目録に署名しました。コミットしてください")


def read_catalog(index_dir):
    with open(os.path.join(index_dir, "catalog.tsv"), "rb") as f:
        catalog = f.read()
    lines = catalog.decode("utf-8").splitlines()
    head = lines[0].split("\t")
    if head[0] != MAGIC or head[1] != str(FORMAT):
        raise SystemExit("catalog.tsv の1行目が違います")
    entries = []
    for line in lines[1:]:
        if not line or line.startswith("#"):
            continue
        c = line.split("\t")
        entries.append({"id": c[0], "version": c[1], "dir": c[2], "bytes": int(c[4]), "files_sha256": c[5]})
    return catalog, entries


def check_signature(index_dir, catalog, pubkey_hex):
    with open(os.path.join(index_dir, "catalog.sig"), "r", encoding="ascii") as f:
        parts = f.read().split()
    if len(parts) != 3 or parts[0] != "ed25519":
        raise SystemExit("catalog.sig の形式が違います")
    pub, sig = bytes.fromhex(parts[1]), bytes.fromhex(parts[2])
    if pubkey_hex and pub.hex() != pubkey_hex.lower():
        raise SystemExit("catalog.sig の鍵が指定した公開鍵と違います")
    if not ed25519_verify(pub, SIGN_CONTEXT + hashlib.sha256(catalog).digest(), sig):
        raise SystemExit("catalog.sig の署名が合いません")
    return pub


def cmd_assemble(args):
    """署名済みの目録(sign の出力)に、アプリの中身を写して配布物にする。中身が目録と違えば止まる"""
    catalog, entries = read_catalog(args.index)
    check_signature(args.index, catalog, args.pubkey)
    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    shutil.copytree(args.index, args.out)
    stale = []
    for e in entries:
        files_tsv_path = os.path.join(args.index, e["id"], e["version"], "files.tsv")
        with open(files_tsv_path, "rb") as f:
            files_tsv = f.read()
        if hashlib.sha256(files_tsv).hexdigest() != e["files_sha256"]:
            raise SystemExit(f"{e['id']}: files.tsv が catalog.tsv と合いません")
        src_dir = os.path.join(args.apps_dir, e["dir"])
        for line in files_tsv.decode("ascii").splitlines():
            rel, size, digest = line.split("\t")
            with open(os.path.join(src_dir, rel), "rb") as f:
                data = f.read()
            if len(data) != int(size) or hashlib.sha256(data).hexdigest() != digest:
                msg = (f"{e['dir']}/{rel}: 署名した後で中身が変わっています。"
                       "build_app_repo.py sign をやり直してください")
                if not args.allow_stale:
                    raise SystemExit(msg)
                # 署名した中身はもう手元に無いので、このアプリは配らない(本体ではインストールが404で止まる)
                print(f"::warning::{msg}(このアプリは配りません)")
                shutil.rmtree(os.path.join(args.out, e["id"], e["version"]), ignore_errors=True)
                stale.append(e["id"])
                break
            write_bytes(os.path.join(args.out, e["id"], e["version"], rel), data)
    print(f"{args.out}: {len(entries) - len(stale)}件のアプリ" + (f"(古いので配らない: {', '.join(stale)})" if stale else ""))


def cmd_verify(args):
    catalog, entries = read_catalog(args.dir)
    pub = check_signature(args.dir, catalog, args.pubkey)
    skipped = 0
    for e in entries:
        base = os.path.join(args.dir, e["id"], e["version"])
        if not os.path.isdir(base):
            print(f"  {e['id']} {e['version']}: 配っていません")
            skipped += 1
            continue
        with open(os.path.join(base, "files.tsv"), "rb") as f:
            files_tsv = f.read()
        if hashlib.sha256(files_tsv).hexdigest() != e["files_sha256"]:
            raise SystemExit(f"{e['id']}: files.tsv が合いません")
        total = 0
        for line in files_tsv.decode("ascii").splitlines():
            rel, size, digest = line.split("\t")
            p = os.path.join(base, rel)
            if os.path.exists(p):
                with open(p, "rb") as f:
                    data = f.read()
                if len(data) != int(size) or hashlib.sha256(data).hexdigest() != digest:
                    raise SystemExit(f"{e['id']}/{rel}: 中身が合いません")
            total += int(size)
        if total != e["bytes"]:
            raise SystemExit(f"{e['id']}: 合計が合いません")
    print(f"署名OK(鍵 {pub.hex()})、{len(entries) - skipped}件")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("keygen", help="署名の鍵を作る")
    p.add_argument("--key", required=True)
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_keygen)

    p = sub.add_parser("pubkey", help="秘密鍵から公開鍵を表示する")
    p.add_argument("--key", required=True)
    p.set_defaults(func=cmd_pubkey)

    for name, func, help_text in (("build", cmd_build, "配布物をまるごと作る"),
                                  ("sign", cmd_sign, "署名した目録だけを作る(GitHub Pages向け)")):
        p = sub.add_parser(name, help=help_text)
        p.add_argument("--key", required=True)
        p.add_argument("--out", required=True)
        p.add_argument("--apps-dir", default=DEFAULT_APPS_DIR)
        p.add_argument("--only", help="配るidをカンマ区切りで(既定は id のある全部)")
        p.add_argument("--serial", type=int, help="版の番号(既定は今のUNIX時刻。前より小さいと本体が断る)")
        if name == "build":
            p.add_argument("--clean", action="store_true", help="--out を消してから作る")
        p.set_defaults(func=func)

    p = sub.add_parser("assemble", help="署名した目録にアプリの中身を写す")
    p.add_argument("--index", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--apps-dir", default=DEFAULT_APPS_DIR)
    p.add_argument("--pubkey", help="この公開鍵の署名だけを受け付ける(16進64桁)")
    p.add_argument("--allow-stale", action="store_true",
                   help="署名の後で中身が変わったアプリは警告して配らない(既定は止まる)")
    p.set_defaults(func=cmd_assemble)

    p = sub.add_parser("verify", help="配布物の署名と中身を確かめる")
    p.add_argument("--dir", required=True)
    p.add_argument("--pubkey")
    p.set_defaults(func=cmd_verify)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
