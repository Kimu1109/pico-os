#!/usr/bin/env python3
"""OSに同梱するLuaモジュール(src/lua/builtin/*.lua)から src/lua/LuaBuiltinModules.hpp を作る。

    python3 script/codegen/gen_lua_builtins.py          … 書き出す
    python3 script/codegen/gen_lua_builtins.py --check  … 書き出したものが .lua と食い違えば終了コード1(CIで回す)

<名前>.lua が require("pico.<名前>") になる。ファイルの頭の「--」で始まる行(最初の空行まで)は
モジュールの説明として C++ のコメントへ写し、フラッシュに置く文字列には入れない。
本文もコメント・行頭の字下げ・行末の空白を落として詰めてから置く(minify)。改行は1つも消さないので、
エラーやデバッガの行番号は .lua のままで合う(game.lua 37KB → 約26KB。2026-11-01)。
.hpp は生成物なので直接書き換えないこと(アイコンの icons_data.h と同じ扱い)。
標準ライブラリだけで動く。
"""
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC_DIR = os.path.join(ROOT, "src", "lua", "builtin")
OUT = os.path.join(ROOT, "src", "lua", "LuaBuiltinModules.hpp")
# 並びは .hpp の中の順(Source() が引く順)。足したらここにも足す
MODULES = ["ui", "async", "tween", "game"]
DELIM = "LUA"

HEAD = """#pragma once
// 生成物: script/codegen/gen_lua_builtins.py が src/lua/builtin/*.lua から作る。直接書き換えないこと
// (直したら python3 script/codegen/gen_lua_builtins.py。CI の「層の決まり」が --check で食い違いを見る)

#include <cstring>

// require("pico.ui") / require("pico.async") / require("pico.game") 等で読める、OSに同梱のLuaモジュール。
// アプリのフォルダのファイルより優先する(名前が"pico."で始まるものはここだけ)。
// ソースはフラッシュに置くだけで、require されたときにだけコンパイルする(実行の外の浅い所で。
// LuaEngine::preloadModules)。
namespace LuaBuiltin {
"""


def var_name(name):
    return "k" + name[:1].upper() + name[1:]


def split_doc(text):
    lines = text.split("\n")
    doc = []
    i = 0
    while i < len(lines) and lines[i].startswith("--"):
        doc.append(lines[i][2:].lstrip(" ") if lines[i].strip() != "--" else "")
        i += 1
    if doc and i < len(lines) and lines[i] == "":
        i += 1
    return doc, "\n".join(lines[i:])


def _long_bracket_level(s, i):
    """s[i] が '[' のとき、長い括弧 [[ / [=[ … の開きならその '=' の数を、違えば -1 を返す"""
    j = i + 1
    while j < len(s) and s[j] == "=":
        j += 1
    return j - i - 1 if j < len(s) and s[j] == "[" else -1


def minify(src):
    """Luaのソースからコメント・行頭の字下げ・行末の空白を除く。改行は全部残す(行番号を変えない)。
    文字列('…' "…" [[…]])の中身には触らない。長い文字列・長いコメントの中の改行もそのまま残す"""
    out = []
    i, n = 0, len(src)
    at_line_head = True
    while i < n:
        c = src[i]
        if c == "\n":
            # 行末の空白を落とす
            while out and out[-1] in (" ", "\t"):
                out.pop()
            out.append("\n")
            at_line_head = True
            i += 1
            continue
        if at_line_head and c in " \t":
            i += 1
            continue
        at_line_head = False
        if c == "-" and src.startswith("--", i):
            j = i + 2
            level = _long_bracket_level(src, j) if j < n and src[j] == "[" else -1
            if level >= 0:
                close = "]" + "=" * level + "]"
                end = src.find(close, j)
                if end < 0:
                    sys.exit("閉じていない長いコメントがあります")
                body = src[i:end + len(close)]
                out.append("\n" * body.count("\n"))  # 中の改行だけ残す
                if body.count("\n"):
                    at_line_head = True
                i = end + len(close)
            else:
                while i < n and src[i] != "\n":
                    i += 1
            continue
        if c == "[":
            level = _long_bracket_level(src, i)
            if level >= 0:
                close = "]" + "=" * level + "]"
                end = src.find(close, i)
                if end < 0:
                    sys.exit("閉じていない長い文字列があります")
                out.append(src[i:end + len(close)])
                i = end + len(close)
                continue
        if c in "'\"":
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == "\\":
                    j += 1
                elif src[j] == "\n":
                    sys.exit("文字列が行の途中で閉じていません")
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
            continue
        # 字句の間の空白の連なりは1つに詰める(行の途中の字下げ揃え等)
        if c in " \t":
            if out and out[-1] not in (" ", "\n"):
                out.append(" ")
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def generate():
    out = [HEAD]
    for name in MODULES:
        path = os.path.join(SRC_DIR, name + ".lua")
        with open(path, encoding="utf-8") as f:
            text = f.read()
        doc, body = split_doc(text)
        body = minify(body)
        if ")" + DELIM + '"' in body:
            sys.exit(f"{path}: 生の文字列の終わり ){DELIM}\" を含んでいます")
        out.append("\n")
        for d in doc:
            out.append("    //" + (" " + d if d else "") + "\n")
        out.append(f'    static const char* const {var_name(name)} = R"{DELIM}(\n{body}){DELIM}";\n')
    out.append("\n    inline const char* Source(const char* name) {\n        if (!name) return nullptr;\n")
    for name in MODULES:
        out.append(f'        if (strcmp(name, "pico.{name}") == 0) return {var_name(name)};\n')
    out.append("        return nullptr;\n    }\n}\n")
    return "".join(out)


def main():
    text = generate()
    if "--check" in sys.argv:
        with open(OUT, encoding="utf-8") as f:
            if f.read() != text:
                print("src/lua/LuaBuiltinModules.hpp が src/lua/builtin/*.lua と食い違っています。"
                      "python3 script/codegen/gen_lua_builtins.py で作り直してください")
                return 1
        print("LuaBuiltinModules.hpp は src/lua/builtin/*.lua と一致しています")
        return 0
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote", os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
