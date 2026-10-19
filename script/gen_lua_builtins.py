#!/usr/bin/env python3
"""OSに同梱するLuaモジュール(src/lua/builtin/*.lua)から src/lua/LuaBuiltinModules.hpp を作る。

    python3 script/gen_lua_builtins.py          … 書き出す
    python3 script/gen_lua_builtins.py --check  … 書き出したものが .lua と食い違えば終了コード1(CIで回す)

<名前>.lua が require("pico.<名前>") になる。ファイルの頭の「--」で始まる行(最初の空行まで)は
モジュールの説明として C++ のコメントへ写し、フラッシュに置く文字列には入れない。
.hpp は生成物なので直接書き換えないこと(アイコンの icons_data.h と同じ扱い)。
標準ライブラリだけで動く。
"""
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC_DIR = os.path.join(ROOT, "src", "lua", "builtin")
OUT = os.path.join(ROOT, "src", "lua", "LuaBuiltinModules.hpp")
# 並びは .hpp の中の順(Source() が引く順)。足したらここにも足す
MODULES = ["ui", "async", "tween", "game"]
DELIM = "LUA"

HEAD = """#pragma once
// 生成物: script/gen_lua_builtins.py が src/lua/builtin/*.lua から作る。直接書き換えないこと
// (直したら python3 script/gen_lua_builtins.py。CI の「層の決まり」が --check で食い違いを見る)

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


def generate():
    out = [HEAD]
    for name in MODULES:
        path = os.path.join(SRC_DIR, name + ".lua")
        with open(path, encoding="utf-8") as f:
            text = f.read()
        doc, body = split_doc(text)
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
                      "python3 script/gen_lua_builtins.py で作り直してください")
                return 1
        print("LuaBuiltinModules.hpp は src/lua/builtin/*.lua と一致しています")
        return 0
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote", os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
