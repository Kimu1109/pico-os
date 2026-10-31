#!/usr/bin/env python3
"""アプリのパッケージ(.papp)を作る側(script/build_app_repo.py の package / site)と読む側のテスト。

  python3 app_package_tool_test.py <app_package_testのパス> <作業ディレクトリ>

- build_app_repo.py でパッケージの変種(正しいもの・更新・鍵違い・改ざん・切れたもの等)を作り、
  C++ の app_package_test(本物の App_Package / App_Install)に読ませる
- package コマンドが作ったものを read_package で読み戻し、元のファイルと同じか
- build → site で、配布物にパッケージ・アイコン・index.html が足されるか(取り下げたものは載せない・HTMLを逃がす)
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import build_app_repo as b  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "pc", "sdcard", "lua", "apps")
SEED_A = bytes(range(1, 33))
SEED_B = bytes(range(101, 133))

failures = 0


def check(cond, label):
    global failures
    print(("[ OK ] " if cond else "[FAIL] ") + label)
    if not cond:
        failures += 1


def set_cfg(path, key, value):
    lines = [l for l in open(path, encoding="utf-8").read().splitlines() if not l.startswith(key + "=")]
    lines.append(f"{key}={value}")
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def copy_app(name, dst_parent):
    dst = os.path.join(dst_parent, name)
    shutil.copytree(os.path.join(SRC, name), dst,
                    ignore=shutil.ignore_patterns("store.json*", "settings.cfg", "hiscore.txt"))
    return dst


def make(parent, dirname, seed=SEED_A, row_seed=None, perms=None, status="-", files_sha=None):
    a = b.collect_apps(parent, None, dirname)[0]
    a.author_key = b.ed25519_public(row_seed or seed).hex()
    a.author = "tester"
    a.status = status
    if perms is not None:
        a.perms = perms
    line = b.make_catalog([a], 0).decode("utf-8").splitlines()[1]
    if files_sha:
        cols = line.split("\t")
        cols[5] = files_sha
        line = "\t".join(cols)
    datas = [open(os.path.join(a.path, rel), "rb").read() for rel, _, _ in a.files]
    return b.make_package(line, b.sign_files(seed, a.id, a.version, a.files_tsv), a.files_tsv, datas)


def write(out, name, data):
    with open(os.path.join(out, name + ".papp"), "wb") as f:
        f.write(data)


def main():
    exe, work = sys.argv[1], sys.argv[2]
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    with open(os.path.join(work, "author_a.txt"), "w") as f:
        f.write(b.ed25519_public(SEED_A).hex())

    # ---- C++ に読ませる変種 ----
    src = os.path.join(work, "src")
    os.makedirs(src)
    copy_app("テトリス", src)
    copy_app("リバーシ", src)
    copy_app("ブロック", src)
    good = make(src, "テトリス")
    write(work, "tetris-2.0.0", good)
    write(work, "reversi-2.0.0", make(src, "リバーシ"))
    write(work, "blocks-1.2.0", make(src, "ブロック"))

    v2 = os.path.join(work, "v2")
    os.makedirs(v2)
    tet = copy_app("テトリス", v2)
    set_cfg(os.path.join(tet, "app.cfg"), "version", "2.1.0")
    os.remove(os.path.join(tet, "bgm.mml"))
    os.makedirs(os.path.join(tet, "extra"))
    open(os.path.join(tet, "extra", "data.txt"), "w").write("v2 data\n")
    v2pkg = make(v2, "テトリス")
    write(work, "tetris-2.1.0", v2pkg)
    # 以下は入っている版(2.1.0)と同じ版にして、版の比較より後の検査で断られることを見る
    write(work, "tetris-trailing", v2pkg + b"x")
    write(work, "tetris-truncated", v2pkg[:-5])
    write(work, "tetris-format9", v2pkg.replace(b"pico-os-app\t1\t", b"pico-os-app\t9\t", 1))
    write(work, "notapackage", "PK\x03\x04 zip ではない\n".encode("utf-8"))
    write(work, "tetris-yanked", make(v2, "テトリス", status="yanked"))
    write(work, "tetris-filesmismatch", make(v2, "テトリス", files_sha="00" * 32))
    write(work, "tetris-badsig", make(v2, "テトリス", seed=SEED_A).replace(
        b.sign_files(SEED_A, "tetris", "2.1.0", b.collect_apps(v2, None, "テトリス")[0].files_tsv).strip(),
        b.sign_files(SEED_A, "tetris", "2.1.1", b.collect_apps(v2, None, "テトリス")[0].files_tsv).strip()))
    write(work, "tetris-wrongkey", make(v2, "テトリス", seed=SEED_B, row_seed=SEED_A))
    # 配ったあとで main.lua だけ書き換えたもの(files.tsv と署名は元のまま)
    a = b.collect_apps(v2, None, "テトリス")[0]
    line = None
    a.author_key, a.author, a.status = b.ed25519_public(SEED_A).hex(), "tester", "-"
    line = b.make_catalog([a], 0).decode("utf-8").splitlines()[1]
    datas = []
    for rel, _, _ in a.files:
        d = open(os.path.join(a.path, rel), "rb").read()
        datas.append(d.replace(b"--", b"++", 1) if rel == "main.lua" else d)
    write(work, "tetris-tampered", b.make_package(line, b.sign_files(SEED_A, a.id, a.version, a.files_tsv), a.files_tsv, datas))

    v3 = os.path.join(work, "v3")
    os.makedirs(v3)
    tet3 = copy_app("テトリス", v3)
    set_cfg(os.path.join(tet3, "app.cfg"), "version", "2.2.0")
    write(work, "tetris-2.2.0", make(v3, "テトリス"))
    write(work, "tetris-otherkey", make(v3, "テトリス", seed=SEED_B))
    set_cfg(os.path.join(tet3, "app.cfg"), "permission_network", "true")
    write(work, "tetris-perms", make(v3, "テトリス", perms="-"))  # 情報では権限を隠す

    rc = subprocess.run([exe, work]).returncode
    check(rc == 0, f"app_package_test(C++)が通る (rc={rc})")

    # ---- 読み戻し ----
    cols, sig, files_tsv, files = b.read_package(v2pkg)
    check(cols[0] == "tetris" and cols[1] == "2.1.0" and cols[2] == "テトリス", "read_package: id・版・ディレクトリ")
    check(sig.startswith("ed25519 ") and len(sig.split()) == 3, "read_package: files.sig の行")
    check(files["main.lua"] == open(os.path.join(tet, "main.lua"), "rb").read(), "read_package: 中身が元のファイルと同じ")
    check("extra/data.txt" in files and "bgm.mml" not in files, "read_package: ファイルの一覧")

    # ---- package コマンド ----
    key = os.path.join(work, "author.key")
    with open(key, "w") as f:
        f.write(SEED_A.hex() + "\n")
    out = os.path.join(work, "cli.papp")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "script", "build_app_repo.py"), "package",
                        "--key", key, "--app", os.path.join(src, "テトリス"), "--out", out, "--author", "tester"],
                       capture_output=True, text=True)
    check(r.returncode == 0 and os.path.isfile(out), f"package コマンドが書き出す ({r.stderr.strip()})")
    if os.path.isfile(out):
        check(open(out, "rb").read() == good, "package コマンドの出力はテストで作ったものと同じ(C++が読めた形)")

    # ---- site ----
    apps_dir = os.path.join(work, "site_src")
    os.makedirs(apps_dir)
    copy_app("テトリス", apps_dir)
    rev = copy_app("リバーシ", apps_dir)
    set_cfg(os.path.join(rev, "app.cfg"), "description", "<b>太字</b> & \"引用\"")
    set_cfg(os.path.join(rev, "app.cfg"), "name_en", "Reversi <EN>")
    set_cfg(os.path.join(rev, "app.cfg"), "description_en", "Two players & \"quotes\"")
    site = os.path.join(work, "site")
    apps = b.collect_apps(apps_dir, None)
    for a in apps:
        a.status = "yanked" if a.id == "tetris" else "-"
    b.write_index(site, apps, SEED_A, 100)
    for a in apps:
        for rel, _, _ in a.files:
            dst = os.path.join(site, a.id, a.version, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(a.path, rel), dst)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "script", "build_app_repo.py"), "site", "--dir", site],
                       capture_output=True, text=True)
    check(r.returncode == 0, f"site コマンドが通る ({r.stderr.strip()})")
    html = open(os.path.join(site, "index.html"), encoding="utf-8").read() if r.returncode == 0 else ""
    check(os.path.isfile(os.path.join(site, "reversi-2.0.0.papp")), "site: パッケージを置く")
    check(os.path.isfile(os.path.join(site, "reversi-2.0.0.png")), "site: アイコンをPNGにして置く")
    png = open(os.path.join(site, "reversi-2.0.0.png"), "rb").read() if os.path.isfile(os.path.join(site, "reversi-2.0.0.png")) else b""
    check(png.startswith(b"\x89PNG\r\n\x1a\n"), "site: アイコンはPNG")
    check('href="reversi-2.0.0.papp"' in html and "リバーシ" in html, "site: index.html にカードとダウンロードの印")
    check(not os.path.exists(os.path.join(site, "tetris-2.0.0.papp")) and "テトリス" not in html,
          "site: 取り下げたものは載せない")
    check("&lt;b&gt;太字&lt;/b&gt; &amp; &quot;引用&quot;" in html and "<b>太字" not in html, "site: 説明はHTMLとして逃がす")
    check("@@" not in html and "@APPS@" not in html, "site: 埋め込みの印が残らない")
    check('<h3 data-en="Reversi &lt;EN&gt;">' in html and 'data-en="Two players &amp; &quot;quotes&quot;"' in html,
          "site: 名前・説明の訳を data-en に逃がして置く")
    check("two players" in html, "site: 探す文字に訳も入る")
    cat = open(os.path.join(site, "catalog.tsv"), encoding="utf-8").read()
    check("\tname_en=Reversi <EN>\tdescription_en=Two players & \"quotes\"\n" in cat, "site: カタログの13列目から訳を並べる")
    pkg = open(os.path.join(site, "reversi-2.0.0.papp"), "rb").read() if os.path.isfile(os.path.join(site, "reversi-2.0.0.papp")) else b""
    if pkg:
        cols, _, _, files = b.read_package(pkg)
        check(cols[0] == "reversi" and "main.lua" in files, "site: パッケージを読み戻せる")
        catalog_line = [l for l in open(os.path.join(site, "catalog.tsv"), encoding="utf-8").read().splitlines()
                        if l.startswith("reversi\t")][0]
        check("\t".join(cols) == catalog_line, "site: パッケージの2行目はカタログの行そのもの")

    # ---- 訳の列と1行の長さ(古い本体は640バイトを超える行を読まない) ----
    a = b.App()
    a.id, a.version, a.dir, a.name, a.bytes, a.perms, a.status = "x", "1", "x", "名前", 1, "-", "-"
    a.description, a.author_key = "あ" * 60, "ab" * 32
    a.local = [("name_en", "Name"), ("description_en", "d" * 180)]
    line = b.catalog_line(a, "00" * 32, "me")
    check(len(line.encode("utf-8")) <= b.MAX_LINE_BYTES and "\tname_en=Name\tdescription_en=ddd" in line
          and line.endswith("d\n"), "catalog: 入りきらない説明の訳は切り詰めて640バイトに収める")
    cols = line.rstrip("\n").split("\t")
    check(len(cols) == 14 and cols[3] == "名前" and cols[7] == a.description and cols[10] == "-" and cols[11] == "-",
          "catalog: 訳の列は12列(原文と min_os)の後ろに足すだけ")
    a.description = "あ" * 60
    a.local = []
    cfg = {"name_en": "N", "description_en": "D\tE", "name_fr": "F", "description": "x"}
    check(b.local_texts(cfg, "t") == [("name_en", "N"), ("description_en", "D E")], "local_texts: 知る言語だけ・タブは空白に")
    check(b.parse_local(["name_en=N", "x=1", "name_fr=F", "description_en=", "description_en=a=b"]) == {"name_en": "N", "description_en": "a=b"},
          "parse_local: 本体と同じく知らない列・空の値は無視")

    print("")
    print("ALL PASSED" if failures == 0 else f"FAILED ({failures} failures)")
    sys.exit(0 if failures == 0 else 1)


if __name__ == "__main__":
    main()
