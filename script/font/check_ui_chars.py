#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
check_ui_chars.py — OSの画面の文字が、フラッシュに残した漢字(script/font/base_kanji.txt)だけで書かれているかを見る。
docs/design/performance.md「漢字の一部をSDへ」。

常用漢字の外の字はSDの追加フォント(/sys/fonts/jpgothic*.pkf)にしか無いので、SDが無い・USBドライブ中・
エラーの画面で □ になる。通知・エラー・設定などは読めないと困るので、src/ の文字列(C++の "…" と
同梱Lua src/lua/builtin/*.lua の文字列)に base_kanji.txt の外の漢字があれば失敗にする(コメントは見ない)。

    python3 script/font/check_ui_chars.py

使ってしまったら、ひらがな・常用漢字へ言い換えるか、どうしても要る字なら base_kanji.txt の「残す字」へ理由つきで足して
script/font/split_font.py で作り直す(フラッシュが1字あたり約110B増える)。
"""

import glob
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from split_font import BASE_LIST, is_kanji, read_base_chars  # noqa: E402


def c_strings(text):
    """C/C++ のコメントを飛ばして "…" の中身を (行番号, 中身) で返す"""
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"' and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append((line, text[i + 1:j]))
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'" and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return out


def lua_strings(text):
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        m = re.match(r"--\[(=*)\[", text[i:i + 64])
        if m:
            end = text.find("]" + m.group(1) + "]", i)
            end = n if end < 0 else end + len(m.group(1)) + 2
            line += text.count("\n", i, end)
            i = end
        elif text.startswith("--", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append((line, text[i + 1:j]))
            i = j + 1
        else:
            m = re.match(r"\[(=*)\[", text[i:i + 64])
            if m:
                end = text.find("]" + m.group(1) + "]", i)
                end = n if end < 0 else end
                out.append((line, text[i + len(m.group(0)):end]))
                line += text.count("\n", i, end)
                i = end + len(m.group(1)) + 2
            else:
                i += 1
    return out


def main():
    base = read_base_chars(BASE_LIST)
    bad = []
    files = sorted(glob.glob(os.path.join(ROOT, "src", "**", "*.[ch]pp"), recursive=True)
                   + glob.glob(os.path.join(ROOT, "src", "**", "*.h"), recursive=True))
    lua = sorted(glob.glob(os.path.join(ROOT, "src", "lua", "builtin", "*.lua")))
    for path in files + lua:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        strings = lua_strings(text) if path.endswith(".lua") else c_strings(text)
        for line, s in strings:
            for ch in s:
                if is_kanji(ord(ch)) and ord(ch) not in base:
                    bad.append((os.path.relpath(path, ROOT), line, ch, s))
    if bad:
        print("フラッシュに無い漢字(常用漢字の外)を画面の文字に使っています。SDが無いと □ になります:")
        for path, line, ch, s in bad:
            print(f"  {path}:{line}: 「{ch}」 in \"{s[:60]}\"")
        print("言い換えるか、script/font/base_kanji.txt へ理由つきで足して split_font.py で作り直すこと")
        return 1
    print(f"画面の文字はフラッシュの漢字だけで書かれています({len(files) + len(lua)} ファイル)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
