#!/usr/bin/env python3
"""投稿用リポジトリ(kimu1109/pico-os-apps)の道具(script/build_app_repo.py の
author-sign / registry-check / registry-build)のテスト。

  python3 registry_tool_test.py <作業ディレクトリ>

- pc/sdcard のアプリ2本から author-sign で投稿用リポジトリの形を作り、検査に通るか・配布物が verify / site に通るか
- main(base)と PR の中身の組み合わせで、自動でマージしてよいかの判定(owners・版・権限・meta.cfg・status・
  2本以上・アプリの外・新規・削除)
- 検査で落ちるもの(書き換えた中身・別の鍵の署名・リンク・隠しファイル・id とディレクトリの食い違い・dir の重なり・
  status.tsv の知らない id・Luaの構文)
- serial が前に公開したものより大きくなるか、author-sign が meta.cfg と違う鍵を断るか
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(ROOT, "script", "build_app_repo.py")
SRC = os.path.join(ROOT, "pc", "sdcard", "lua", "apps")
WORK = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "registry_work")

failures = 0


def check(cond, label):
    global failures
    print(("[ OK ] " if cond else "[FAIL] ") + label)
    if not cond:
        failures += 1


def run(*args):
    r = subprocess.run([sys.executable, "-I", TOOL, *args], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def key_file(name, byte):
    p = os.path.join(WORK, name)
    with open(p, "w", encoding="ascii") as f:
        f.write(f"{byte:02x}" * 32 + "\n")
    return p


def set_cfg(path, key, value):
    lines = [l for l in open(path, encoding="utf-8").read().splitlines() if not l.replace(" ", "").startswith(key + "=")]
    lines.append(f"{key}={value}")
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def judge(reg, base=None, user="alice", luac=None):
    out = os.path.join(WORK, "result.json")
    if os.path.exists(out):
        os.remove(out)
    args = ["registry-check", "--registry", reg, "--out", out]
    if base:
        args += ["--base", base, "--user", user]
    if luac:
        args += ["--luac", luac]
    rc, text = run(*args)
    res = json.load(open(out, encoding="utf-8")) if os.path.exists(out) else {"ok": False, "errors": [text]}
    return rc, res, text


def fork(base, name):
    dst = os.path.join(WORK, name)
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(base, dst, symlinks=True)
    return dst


def bump(reg, app_id, version, key):
    set_cfg(os.path.join(reg, "apps", app_id, "app", "app.cfg"), "version", version)
    return run("author-sign", "--key", key, "--app", os.path.join(reg, "apps", app_id))


def main():
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    ka, kb = key_file("a.key", 0x11), key_file("b.key", 0x22)

    # ---- 投稿用リポジトリの形を作る ----
    base = os.path.join(WORK, "base")
    rc, text = run("author-sign", "--key", ka, "--app", os.path.join(base, "apps", "tetris"),
                   "--src", os.path.join(SRC, "テトリス"))
    check(rc != 0 and "--owner" in text, "初めての投稿で --owner/--dir が無ければ meta.cfg の作り方を案内する")
    for app_id, d, key in (("tetris", "テトリス", ka), ("reversi", "リバーシ", kb)):
        rc, text = run("author-sign", "--key", key, "--app", os.path.join(base, "apps", app_id),
                       "--src", os.path.join(SRC, d), "--owner", "Alice" if key == ka else "bob", "--dir", d)
        check(rc == 0, f"author-sign で {app_id} を作れる")
    rc, res, text = judge(base)
    check(rc == 0 and res["ok"], "作った投稿用リポジトリは検査に通る")

    rc, text = run("author-sign", "--key", kb, "--app", os.path.join(base, "apps", "tetris"))
    check(rc != 0 and "meta.cfg" in text, "meta.cfg と違う鍵の author-sign は断る")

    # ---- 配布物 ----
    cat = key_file("catalog.key", 0x33)
    dist = os.path.join(WORK, "dist")
    rc, text = run("registry-build", "--registry", base, "--key", cat, "--out", dist)
    check(rc == 0, "registry-build で配布物を作れる")
    rc, text = run("verify", "--dir", dist)
    check(rc == 0 and "2件" in text, "配布物は verify に通る(2件)")
    lines = open(os.path.join(dist, "catalog.tsv"), encoding="utf-8").read().splitlines()
    check([l.split("\t")[0] for l in lines[1:]] == ["tetris", "reversi"], "カタログの並びは dir の順(テトリス→リバーシ)")
    check(lines[1].split("\t")[9] == "alice", "作者名は meta.cfg の owners の最初(小文字)")
    shutil.copyfile(os.path.join(base, "apps", "reversi", "files.sig"), os.path.join(WORK, "rsig"))
    check(open(os.path.join(dist, "reversi", lines[2].split("\t")[1], "files.sig"), encoding="ascii").read()
          == open(os.path.join(WORK, "rsig"), encoding="ascii").read(), "files.sig は作者のものをそのまま配る")
    prev = os.path.join(WORK, "prev.tsv")
    open(prev, "w", encoding="utf-8").write("pico-os-apps\t2\t4000000000\n")
    rc, text = run("registry-build", "--registry", base, "--key", cat, "--out", dist, "--prev-catalog", prev)
    head = open(os.path.join(dist, "catalog.tsv"), encoding="utf-8").readline().split("\t")
    check(rc == 0 and head[2].strip() == "4000000001", "serial は前に公開したものより1大きくなる")
    rc, text = run("registry-build", "--registry", base, "--key", cat, "--out", dist)
    rc, text = run("site", "--dir", dist, "--template", os.path.join(ROOT, "pc", "web", "store.html"))
    check(rc == 0 and os.path.isfile(os.path.join(dist, "index.html")), "site --template でページを作れる")

    # ---- 自動マージの判定 ----
    pr = fork(base, "pr")
    bump(pr, "tetris", "9.0.0", ka)
    rc, res, _ = judge(pr, base, "alice")
    check(res["ok"] and res["automerge"], "owners の人が版を上げた更新は自動でマージする")
    rc, res, _ = judge(pr, base, "ALICE")
    check(res["automerge"], "アカウント名は大文字小文字を区別しない")
    rc, res, _ = judge(pr, base, "mallory")
    check(res["ok"] and not res["automerge"] and any("owners" in r for r in res["reasons"]), "owners にいない人の更新は人が見る")
    rc, res, _ = judge(base, base, "alice")
    check(not res["automerge"], "変更の無いPRはマージしない")

    pr = fork(base, "pr")
    open(os.path.join(pr, "apps", "tetris", "app", "extra.lua"), "w").write("return 1\n")
    run("author-sign", "--key", ka, "--app", os.path.join(pr, "apps", "tetris"))
    rc, res, _ = judge(pr, base)
    check(res["ok"] and any("版" in r for r in res["reasons"]), "版を上げずに中身を変えた更新は人が見る")

    pr = fork(base, "pr")
    set_cfg(os.path.join(pr, "apps", "tetris", "app", "app.cfg"), "permission_network", "true")
    bump(pr, "tetris", "9.0.0", ka)
    rc, res, _ = judge(pr, base)
    check(res["ok"] and any("権限" in r for r in res["reasons"]), "権限が増える更新は人が見る")

    pr = fork(base, "pr")
    bump(pr, "tetris", "9.0.0", ka)
    set_cfg(os.path.join(pr, "apps", "tetris", "meta.cfg"), "owners", "alice, mallory")
    rc, res, _ = judge(pr, base)
    check(res["ok"] and any("meta.cfg" in r for r in res["reasons"]), "meta.cfg を変えるPRは人が見る")

    pr = fork(base, "pr")
    bump(pr, "tetris", "9.0.0", ka)
    bump(pr, "reversi", "9.0.0", kb)
    rc, res, _ = judge(pr, base)
    check(res["ok"] and any("2つ以上" in r for r in res["reasons"]), "2本以上のアプリを変えるPRは人が見る")

    pr = fork(base, "pr")
    bump(pr, "tetris", "9.0.0", ka)
    os.makedirs(os.path.join(pr, ".github", "workflows"))
    open(os.path.join(pr, ".github", "workflows", "x.yml"), "w").write("on: push\n")
    rc, res, _ = judge(pr, base)
    check(res["ok"] and any(".github" in r for r in res["reasons"]), "アプリの外(.github 等)に触れるPRは人が見る")

    pr = fork(base, "pr")
    run("author-sign", "--key", ka, "--app", os.path.join(pr, "apps", "minesweeper"),
        "--src", os.path.join(SRC, "マインスイーパー"), "--owner", "alice", "--dir", "マインスイーパー")
    rc, res, _ = judge(pr, base)
    check(res["ok"] and any("新しいアプリ" in r for r in res["reasons"]), "新しいアプリは人が見る")

    pr = fork(base, "pr")
    shutil.rmtree(os.path.join(pr, "apps", "reversi"))
    rc, res, _ = judge(pr, base, "bob")
    check(res["ok"] and any("消して" in r for r in res["reasons"]), "アプリを消すPRは人が見る")

    blocked = fork(base, "blocked")
    open(os.path.join(blocked, "status.tsv"), "w", encoding="utf-8").write("tetris\tblocked\t乗っ取り\n")
    rc, res, _ = judge(blocked)
    check(res["ok"], "status.tsv の blocked は検査に通る")
    pr = fork(blocked, "pr")
    bump(pr, "tetris", "9.0.0", ka)
    rc, res, _ = judge(pr, blocked)
    check(res["ok"] and any("blocked" in r for r in res["reasons"]), "status が - でないアプリの更新は人が見る")
    rc, text = run("registry-build", "--registry", blocked, "--key", cat, "--out", dist)
    row = [l for l in open(os.path.join(dist, "catalog.tsv"), encoding="utf-8").read().splitlines() if l.startswith("tetris\t")]
    check(row and row[0].split("\t")[10] == "blocked", "status.tsv の status がカタログに載る")

    # ---- 検査で落ちるもの ----
    def must_fail(pr, label, word, luac=None):
        rc, res, text = judge(pr, base, luac=luac)
        check(rc != 0 and not res["ok"] and not res.get("automerge") and word in "\n".join(res["errors"]), label)

    pr = fork(base, "pr")
    with open(os.path.join(pr, "apps", "tetris", "app", "main.lua"), "a") as f:
        f.write("-- x\n")
    must_fail(pr, "署名の後で書き換えた中身は落ちる", "files.tsv")

    pr = fork(base, "pr")
    set_cfg(os.path.join(pr, "apps", "tetris", "app", "app.cfg"), "version", "9.0.0")
    shutil.copyfile(os.path.join(base, "apps", "tetris", "files.sig"), os.path.join(WORK, "keep.sig"))
    run("author-sign", "--key", ka, "--app", os.path.join(pr, "apps", "tetris"))
    shutil.copyfile(os.path.join(WORK, "keep.sig"), os.path.join(pr, "apps", "tetris", "files.sig"))
    must_fail(pr, "前の版の files.sig のままの更新は落ちる", "署名が合いません")

    pr = fork(base, "pr")
    set_cfg(os.path.join(pr, "apps", "tetris", "meta.cfg"), "key", "22" * 32)
    must_fail(pr, "meta.cfg と違う鍵の署名は落ちる", "files.sig")

    pr = fork(base, "pr")
    os.symlink("/etc/passwd", os.path.join(pr, "apps", "tetris", "app", "leak.lua"))
    must_fail(pr, "app/ の中のリンクは落ちる", "リンク")

    pr = fork(base, "pr")
    shutil.rmtree(os.path.join(pr, "apps", "tetris", "app"))
    os.symlink("/etc", os.path.join(pr, "apps", "tetris", "app"))
    must_fail(pr, "app/ そのものがリンクなら落ちる", "app/")

    pr = fork(base, "pr")
    open(os.path.join(pr, "apps", "tetris", "app", ".hidden"), "w").write("x")
    must_fail(pr, "隠しファイルは落ちる", ". で始まる")

    pr = fork(base, "pr")
    os.rename(os.path.join(pr, "apps", "tetris"), os.path.join(pr, "apps", "tetris2"))
    must_fail(pr, "app.cfg の id とディレクトリ名が違えば落ちる", "id")

    pr = fork(base, "pr")
    set_cfg(os.path.join(pr, "apps", "reversi", "meta.cfg"), "dir", "テトリス")
    must_fail(pr, "dir が重なれば落ちる", "dir")

    pr = fork(base, "pr")
    open(os.path.join(pr, "status.tsv"), "w", encoding="utf-8").write("nothing\tyanked\t\n")
    must_fail(pr, "status.tsv に apps/ に無い id があれば落ちる", "status.tsv")

    pr = fork(base, "pr")
    open(os.path.join(pr, "status.tsv"), "w", encoding="utf-8").write("tetris\tdeleted\t\n")
    must_fail(pr, "status.tsv の知らない status は落ちる", "status")

    pr = fork(base, "pr")
    set_cfg(os.path.join(pr, "apps", "tetris", "meta.cfg"), "owners", "")
    must_fail(pr, "owners が空なら落ちる", "owners")

    # luac の代わり(中身に BAD があれば構文の誤りとする)
    fake = os.path.join(WORK, "fake_luac.py")
    open(fake, "w").write("import sys\nrc = 0\nfor p in sys.argv[sys.argv.index('--') + 1:]:\n"
                          "    if 'BAD' in open(p, encoding='utf-8', errors='replace').read():\n"
                          "        print(p + ': syntax error', file=sys.stderr); rc = 1\nsys.exit(rc)\n")
    luac = os.path.join(WORK, "luac")
    open(luac, "w").write(f"#!/bin/sh\nexec {sys.executable} {fake} \"$@\"\n")
    os.chmod(luac, 0o755)
    rc, res, _ = judge(base, luac=luac)
    check(rc == 0 and res["ok"], "Luaの構文に誤りが無ければ通る")
    pr = fork(base, "pr")
    open(os.path.join(pr, "apps", "tetris", "app", "lib2.lua"), "w").write("BAD\n")
    run("author-sign", "--key", ka, "--app", os.path.join(pr, "apps", "tetris"))
    must_fail(pr, "Luaの構文の誤りは落ちる", "lib2.lua", luac=luac)

    print("失敗 %d 件" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
