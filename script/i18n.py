#!/usr/bin/env python3
"""表示言語の訳(i18n)の一覧と生成物を作る。設計は docs/design/i18n.md。

    python3 script/i18n.py update    … src/ の TR("…") / TR_NOOP("…") を拾って i18n/<言語>.po を更新し、
                                       src/system/I18n_Table_<言語>.cpp を作り直す
    python3 script/i18n.py --check   … 上の2つが src/ と食い違っていないか(CI)。書式の食い違い・
                                       ハッシュの衝突もここで落とす。訳の無い数は表示するだけ(落とさない)
    python3 script/i18n.py missing [言語]  … 訳の無い原文と、それを使うファイルを出す
    python3 script/i18n.py lua <アプリのフォルダ>...  … Luaアプリの pico.tr("…") を拾って lang/en.lua へ原文を足す
                                       (訳の無い原文は「-- TODO」の行で足す。--check は同梱アプリの lang/ も確かめる)

.po は gettext の形の一部だけ(msgid / msgstr / "#:" の出どころ)。Poedit 等でも開ける。
1つの原文は1行の文字列にする(複数行に分けた msgid "" "…" も読めるが、書き戻すときは1行にする)。
訳を空にした原文は表に入らない(日本語のまま出る)。

標準ライブラリだけで動く。
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(ROOT, "src")
PO_DIR = os.path.join(ROOT, "i18n")

# 言語コード → (生成物のパス、表の名前)。言語を足すときは I18n_Functions の Lang と tableOf() も足す
LANGS = {
    "en": ("src/system/I18n_Table_En.cpp", "kTableEn"),
}

MACROS = ("TR", "TR_NOOP")


# ---------------------------------------------------------------------------
# C の文字列リテラル
# ---------------------------------------------------------------------------

_SIMPLE_ESC = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "a": "\a"}


def c_unescape(body):
    """C の文字列リテラルの中身(引用符の内側)をバイト列へ"""
    out = bytearray()
    i = 0
    raw = body.encode("utf-8")
    while i < len(raw):
        c = raw[i]
        if c != 0x5C:  # '\\'
            out.append(c)
            i += 1
            continue
        i += 1
        e = chr(raw[i])
        if e == "x":
            j = i + 1
            while j < len(raw) and chr(raw[j]) in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(raw[i + 1:j], 16) & 0xFF)
            i = j
        elif e in "01234567":
            j = i
            while j < len(raw) and j < i + 3 and chr(raw[j]) in "01234567":
                j += 1
            out.append(int(raw[i:j], 8) & 0xFF)
            i = j
        elif e in _SIMPLE_ESC:
            out.extend(_SIMPLE_ESC[e].encode())
            i += 1
        else:
            raise ValueError("知らないエスケープ: \\" + e)
    return bytes(out)


def c_escape(data):
    """バイト列(UTF-8)を C/.po の文字列リテラルの中身へ"""
    s = data.decode("utf-8")
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif ord(ch) < 0x20:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


def fnv1a(data):
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


# ---------------------------------------------------------------------------
# src/ から拾う
# ---------------------------------------------------------------------------

def scan_file(path):
    """ファイルの中の TR("…") / TR_NOOP("…") の原文(バイト列)を出てきた順に。コメントの中は見ない"""
    text = open(path, encoding="utf-8").read()
    found = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '"' or c == "'":
            i = skip_literal(text, i)
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            word = text[i:j]
            if word in MACROS:
                k = j
                while k < n and text[k] in " \t\r\n":
                    k += 1
                if k < n and text[k] == "(":
                    parts, end = read_string_args(text, k + 1)
                    if parts is not None:
                        found.append(b"".join(c_unescape(p) for p in parts))
                        i = end
                        continue
            i = j
            continue
        i += 1
    return found


def skip_literal(text, i):
    q = text[i]
    j = i + 1
    while j < len(text) and text[j] != q:
        j += 2 if text[j] == "\\" else 1
    return j + 1


def read_string_args(text, i):
    """'(' の直後から、連結された文字列リテラルだけが続いて ')' で閉じるなら(中身の並び, ')'の次)"""
    parts = []
    n = len(text)
    while True:
        while i < n and text[i] in " \t\r\n":
            i += 1
        if i < n and text[i] == '"':
            end = skip_literal(text, i)
            parts.append(text[i + 1:end - 1])
            i = end
            continue
        break
    if parts and i < n and text[i] == ")":
        return parts, i + 1
    return None, i


def scan_sources():
    """原文 → 使っているファイル(src/からの相対、出てきた順)。原文の並びはファイル名順・出てきた順"""
    uses = {}
    for root, dirs, files in os.walk(SRC):
        dirs.sort()
        for f in sorted(files):
            if not f.endswith((".cpp", ".hpp", ".h")):
                continue
            path = os.path.join(root, f)
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            for msg in scan_file(path):
                files_of = uses.setdefault(msg, [])
                if rel not in files_of:
                    files_of.append(rel)
    return uses


# ---------------------------------------------------------------------------
# .po
# ---------------------------------------------------------------------------

PO_HEADER = (
    '# pico-os の表示の訳。原文(msgid)は src/ の TR("…") の日本語そのもの。\n'
    "# 訳(msgstr)を書いたら python3 script/i18n.py update で src/system/I18n_Table_*.cpp を作り直す。\n"
    "# 訳を空にした原文は日本語のまま出る。%d 等の書式は原文と同じ並びに保つ(違うと --check が落ちる)。\n"
    "# 画面は240px幅が基本なので、訳は原文の幅に近い短い言い方を選ぶ。\n"
    'msgid ""\n'
    'msgstr ""\n'
    '"Content-Type: text/plain; charset=UTF-8\\n"\n'
)


def read_po(path):
    """msgid(バイト列) → msgstr(バイト列)"""
    if not os.path.exists(path):
        return {}
    entries = {}
    cur_id = cur_str = None
    field = None

    def flush():
        if cur_id is not None and cur_id != b"":
            entries[cur_id] = cur_str or b""

    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r'(msgid|msgstr)\s+"(.*)"$', line)
        if m:
            if m.group(1) == "msgid":
                flush()
                cur_id, cur_str = c_unescape(m.group(2)), None
                field = "id"
            else:
                cur_str = c_unescape(m.group(2))
                field = "str"
            continue
        m = re.match(r'"(.*)"$', line)
        if m and field == "id":
            cur_id += c_unescape(m.group(1))
        elif m and field == "str":
            cur_str += c_unescape(m.group(1))
        else:
            raise ValueError("%s: 読めない行: %s" % (path, line))
    flush()
    return entries


def render_po(uses, translations):
    out = [PO_HEADER]
    for msg, files in uses.items():
        out.append("\n")
        out.append("#: %s\n" % " ".join(files))
        out.append('msgid "%s"\n' % c_escape(msg))
        out.append('msgstr "%s"\n' % c_escape(translations.get(msg, b"")))
    return "".join(out)


# ---------------------------------------------------------------------------
# 生成物
# ---------------------------------------------------------------------------

def render_table(table_name, uses, translations):
    rows = []
    for msg in uses:
        tr = translations.get(msg, b"")
        if tr:
            rows.append((fnv1a(msg), msg, tr))
    rows.sort()
    out = [
        "// 生成物: python3 script/i18n.py update が i18n/ の .po から作る。手で直さない(docs/design/i18n.md)\n",
        '#include "system/I18n_Functions.hpp"\n',
        "\n",
        "// hash の昇順(I18nFunctions::Lookup() が二分探索する)\n",
        "const I18nFunctions::Entry I18nFunctions::%s[] = {\n" % table_name,
    ]
    for h, msg, tr in rows:
        # 原文はコメントに残すだけ(フラッシュには入らない)
        note = c_escape(msg)
        if note.endswith("\\"):
            note += " ."  # 行末の \ は次の行をコメントへ繋いでしまう
        out.append('    { 0x%08xu, "%s" }, // %s\n' % (h, c_escape(tr), note))
    if not rows:
        out.append("    { 0u, nullptr },\n")
    out.append("};\n")
    out.append("const size_t I18nFunctions::%sCount = %d;\n" % (table_name, len(rows)))
    return "".join(out)


# ---------------------------------------------------------------------------
# 確かめる
# ---------------------------------------------------------------------------

_FMT = re.compile(rb"%(?:\d+\$)?[-+ #0']*(\*|\d+)?(?:\.(\*|\d+))?(hh|h|ll|l|L|z|j|t)?([diouxXeEfFgGaAcspn%])")


def format_specs(data):
    """printf の書式の並び(%% は除く)。幅の数字は訳で変えてよいが、* と型は同じでなければならない"""
    specs = []
    for m in _FMT.finditer(data):
        if m.group(4) == b"%":
            continue
        specs.append((m.group(1) == b"*", m.group(2) == b"*", m.group(3) or b"", m.group(4)))
    return specs


def check_entries(lang, uses, translations):
    errors = []
    seen = {}
    for msg in uses:
        h = fnv1a(msg)
        if h in seen and seen[h] != msg:
            errors.append("ハッシュが衝突しています: %r と %r" % (seen[h].decode(), msg.decode()))
        seen[h] = msg
        tr = translations.get(msg, b"")
        if tr and format_specs(tr) != format_specs(msg):
            errors.append("%s: 書式が原文と違います: %r → %r" % (lang, msg.decode(), tr.decode()))
        if tr:
            try:
                tr.decode("utf-8")
            except UnicodeDecodeError:
                errors.append("%s: 訳がUTF-8ではありません: %r" % (lang, msg.decode()))
    return errors


# ---------------------------------------------------------------------------
# Luaアプリ(pico.tr と lang/<言語>.lua。docs/design/i18n.md「Luaアプリ」)
# ---------------------------------------------------------------------------

LUA_APPS = os.path.join(ROOT, "pc", "sdcard", "lua", "apps")
_LUA_TR = re.compile(r"""pico\.tr\(\s*("(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')""")
_LUA_ENTRY = re.compile(r'^\s*\["((?:[^"\\]|\\.)*)"\]\s*=\s*"((?:[^"\\]|\\.)*)"\s*,?\s*$')
_LUA_TODO = re.compile(r'^\s*--\s*TODO\s')


def lua_unescape(body):
    """Luaの文字列リテラルの中身(引用符の内側)をバイト列へ。使うのは \\n \\t \\" \\' \\\\ くらい"""
    return c_unescape(body)


def lua_escape(data):
    return c_escape(data)


def lua_app_files(app_dir):
    """アプリのフォルダの .lua(lang/ と tests/ は除く)をファイル名順に"""
    out = []
    for root, dirs, files in os.walk(app_dir):
        rel = os.path.relpath(root, app_dir).replace(os.sep, "/")
        if rel.split("/")[0] in ("lang", "tests"):
            continue
        dirs.sort()
        for f in sorted(files):
            if f.endswith(".lua"):
                out.append(os.path.join(root, f))
    return out


def scan_lua_app(app_dir):
    """pico.tr の原文(バイト列)を出てきた順に(重複なし)"""
    found = []
    for path in lua_app_files(app_dir):
        for line in open(path, encoding="utf-8"):
            code = line
            # 行のコメントは捨てる(文字列の中の -- は気にしない: pico.tr の引数に -- を書かないこと)
            if "--" in code:
                head = code.split("--", 1)[0]
                if "pico.tr" not in head:
                    continue
                code = head if head.count('"') % 2 == 0 else code
            for m in _LUA_TR.finditer(code):
                msg = lua_unescape(m.group(1)[1:-1])
                if msg not in found:
                    found.append(msg)
    return found


def read_lua_lang(path):
    """lang/<言語>.lua の ["原文"] = "訳" の行を読む(TODO の行は訳なし)"""
    entries = {}
    if not os.path.exists(path):
        return entries
    for line in open(path, encoding="utf-8"):
        if _LUA_TODO.match(line):
            continue
        m = _LUA_ENTRY.match(line)
        if m:
            entries[lua_unescape(m.group(1))] = lua_unescape(m.group(2))
    return entries


def render_lua_lang(app_name, lang, msgs, entries):
    out = ["-- %s の%sの表示(pico.tr の原文 → 訳)。原文は python3 script/i18n.py lua で足す\n" % (
        app_name, "英語" if lang == "en" else lang), "return {\n"]
    for msg in msgs:
        tr = entries.get(msg)
        if tr:
            out.append('    ["%s"] = "%s",\n' % (lua_escape(msg), lua_escape(tr)))
        else:
            out.append('    -- TODO ["%s"] = "",\n' % lua_escape(msg))
    out.append("}\n")
    return "".join(out)


def lua_app(app_dir, lang, write):
    """1つのアプリの lang/<言語>.lua を作り直す(write=False なら食い違いと誤りを返すだけ)"""
    msgs = scan_lua_app(app_dir)
    path = os.path.join(app_dir, "lang", lang + ".lua")
    entries = read_lua_lang(path)
    errors = []
    for msg in msgs:
        tr = entries.get(msg)
        if tr and format_specs(tr) != format_specs(msg):
            errors.append("%s: 書式が原文と違います: %r → %r" % (os.path.relpath(path, ROOT), msg.decode(), tr.decode()))
    text = render_lua_lang(os.path.basename(os.path.normpath(app_dir)), lang, msgs, entries)
    missing = [m for m in msgs if not entries.get(m)]
    if write and msgs and not errors:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    cur = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
    stale = bool(msgs) and cur != text
    return msgs, missing, errors, stale


def main_lua(argv):
    dirs = argv[2:]
    if not dirs:
        print("使い方: python3 script/i18n.py lua <アプリのフォルダ>...")
        return 2
    rc = 0
    for d in dirs:
        msgs, missing, errors, _ = lua_app(d, "en", True)
        for e in errors:
            print("エラー: " + e)
            rc = 1
        print("%s: 原文 %d / 訳なし %d" % (os.path.relpath(d, ROOT), len(msgs), len(missing)))
    return rc


def check_lua_apps():
    """同梱アプリのうち lang/ を持つものが pico.tr と合っているか(訳し漏れ・書式の食い違い)"""
    rc = 0
    if not os.path.isdir(LUA_APPS):
        return 0
    for name in sorted(os.listdir(LUA_APPS)):
        d = os.path.join(LUA_APPS, name)
        if not os.path.isdir(os.path.join(d, "lang")):
            continue
        msgs, missing, errors, stale = lua_app(d, "en", False)
        for e in errors:
            print("エラー: " + e)
            rc = 1
        if missing or stale:
            print("エラー: %s の lang/en.lua が pico.tr と合っていません(訳なし %d)。"
                  "python3 script/i18n.py lua %s を回して訳を書いてください"
                  % (name, len(missing), os.path.relpath(d, ROOT)))
            rc = 1
    return rc


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "--help"
    if cmd == "lua":
        return main_lua(argv)
    if cmd not in ("update", "--check", "missing"):
        print(__doc__)
        return 0 if cmd in ("-h", "--help") else 2

    uses = scan_sources()
    rc = 0
    for lang, (table_path, table_name) in LANGS.items():
        if cmd == "missing" and len(argv) > 2 and argv[2] != lang:
            continue
        po_path = os.path.join(PO_DIR, lang + ".po")
        translations = read_po(po_path)
        po_text = render_po(uses, translations)
        table_text = render_table(table_name, uses, translations)
        table_abs = os.path.join(ROOT, table_path)

        errors = check_entries(lang, uses, translations)
        for e in errors:
            print("エラー: " + e)
        if errors:
            rc = 1

        missing = [m for m in uses if not translations.get(m)]
        if cmd == "missing":
            for m in missing:
                print("%s\t%s" % (c_escape(m), " ".join(uses[m])))
            continue

        if cmd == "update":
            if errors:
                print("%s: 直してから update し直してください(書き出していません)" % lang)
                continue
            os.makedirs(PO_DIR, exist_ok=True)
            with open(po_path, "w", encoding="utf-8", newline="\n") as f:
                f.write(po_text)
            with open(table_abs, "w", encoding="utf-8", newline="\n") as f:
                f.write(table_text)
            dropped = [m for m in translations if m not in uses]
            print("%s: 原文 %d / 訳あり %d / 訳なし %d(%s を書きました)" % (
                lang, len(uses), len(uses) - len(missing), len(missing), os.path.relpath(po_path, ROOT)))
            if dropped:
                print("%s: src/ で使われなくなった訳を %d 件捨てました" % (lang, len(dropped)))
        else:
            cur_po = open(po_path, encoding="utf-8").read() if os.path.exists(po_path) else ""
            cur_table = open(table_abs, encoding="utf-8").read() if os.path.exists(table_abs) else ""
            if cur_po != po_text:
                print("エラー: %s が src/ の TR() と合っていません。python3 script/i18n.py update を回してください"
                      % os.path.relpath(po_path, ROOT))
                rc = 1
            if cur_table != table_text:
                print("エラー: %s が %s と合っていません。python3 script/i18n.py update を回してください"
                      % (table_path, os.path.relpath(po_path, ROOT)))
                rc = 1
            print("%s: 原文 %d / 訳なし %d" % (lang, len(uses), len(missing)))
    if cmd == "--check" and check_lua_apps() != 0:
        rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
