#!/usr/bin/env python3
"""src/ の層の決まり(下の層から上の層を #include しない)を確かめる。

    python3 script/check_layers.py            … 違反があれば一覧を出して終了コード1
    python3 script/check_layers.py --graph    … 層ごとの依存(何本の include か)を出す
    python3 script/check_layers.py --sources  … 「段 パス」を1行ずつ(script/host_test/CMakeLists.txt が使う)

層は下から順に 0〜6。ファイルは「自分と同じ段か、それより下の段」のヘッダだけを
include してよい。同じ段の中は互いに include してよい(hal と機能のライブラリが
互いに使い合うように、段の中の向きまでは決めていない)。

段の割り当ては下の LAYERS(ディレクトリ)と FILE_LAYERS(1ファイルだけ別の段に置くもの)。
どうしても上の段へ手を伸ばす必要がある所は EXCEPTIONS に理由付きで書く。
EXCEPTIONS に書いたのに違反でなくなった行は「古い例外」として報告する(消し忘れ防止)。

ついでに、ビルド時に選ぶハードウェア機能(PICO_HW_* / HardwareConfig::)と内蔵アプリ(PICO_APP_*)を
読むファイルが config/Hardware_Config.hpp / config/App_Config.hpp を自分で include しているかも見る
(未定義のマクロは #if で黙って0になり、載せたはずの機能やアプリが消えても気づけないため)。

上の段の機能を下の段から呼びたいときは、下の段に関数ポインタ(差し込み口)を置いて
boot/ で差す(KeyInputFunctions::intercept・LuaAppScanner::apps_changed と同じ形)か、
下の段のヘッダで宣言した関数を boot/ で定義する(AppFunctions::Setup() と App_List.cpp)。

標準ライブラリだけで動く(CI と手元で同じに回せるように)。
"""
import fnmatch
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SRC = os.path.join(ROOT, "src")

LAYER_NAMES = ["base", "system", "lib", "ui", "service", "app", "boot"]

# ディレクトリ(src/ からの相対、前方一致で一番長いもの)→ 段
LAYERS = {
    # 0 base: どこからでも使える型・定数・グローバル状態(OSの機能を何も知らない)
    "consts.hpp": "base",
    "OS_Data.hpp": "base",
    "OS_Data.cpp": "base",
    "config": "base",
    "util": "base",
    # 1 system: UI もハードの具体も知らない OS の土台(ログ・設定・時刻・入力の列・通知の中身…)
    "system": "system",
    # 2 lib: ハードの窓口(hal)と、UI を持たない機能のライブラリ
    "hal": "lib",
    "storage": "lib",
    "net": "lib",
    "crypto": "lib",
    "task": "lib",
    "sound": "lib",
    "iso": "lib",
    "ime": "lib",
    # apps/<機能>/ は機能ごとの置き場。直下は機能の中身(lib)、ui/ はその機能専用の部品(ui)、
    # *Scene.* と *Scene_*.cpp は画面(app)。段は APP_PATTERNS が決める(下の段の機能の中身から画面を読まないように)
    "apps": "lib",
    "apps/home": "service",   # ホーム画面の並びとウィジェット(OSのシェルの部品)
    "apps/store": "app",      # アプリストア(登録簿と Lua アプリの走査を使う)
    # 3 ui: 描画・ウィジェット・シーン遷移の仕組み(OS の機能はまだ知らない)
    "gui": "ui",
    # 4 service: OS の機能(アラーム・電源・ロック…)と、それを見せるシェルの部品
    "services": "service",
    "devtools": "service",
    "gui/widgets/systems": "service",
    # 5 app: 画面(アプリ)と Lua
    "gui/scenes": "app",
    "lua": "app",
    # 6 boot: 起動とつなぎ(登録簿の中身・入力の配り先など、全部を知ってよい所)
    "boot": "boot",
    "main.cpp": "boot",
}

# ディレクトリの段と違う段に置く1ファイル(中身が型・定数だけのヘッダ)
FILE_LAYERS = {
    "storage/SD_Path.hpp": "base",          # SD 上のパスの定数だけ
    "gui/icons/icons_data.h": "base",       # IconID の列挙と絵のデータだけ(生成物)
    "lua/LuaPermissions.hpp": "base",       # 権限の構造体だけ(アプリ登録簿が持つ)
    "storage/SD_IO.hpp": "base",            # パスの組み立て(join/normalize/resolve)。SdFatに依存しない
    # ホーム画面にTODOウィジェットを置いている間だけ動くバックグラウンドの仕事(ホーム画面の並びを見る)
    "apps/todo/Todo_Watch.hpp": "service",
    "apps/todo/Todo_Watch.cpp": "service",
}

# (include する側, include される側) → 理由。理由の無い例外は書かないこと
EXCEPTIONS = {
    ("devtools/DevTools_Functions.cpp", "lua/LuaDebugger.hpp"):
        "debug.cfg の lua-debugger を LuaDebugger の全体のスイッチへ写す。"
        "DevTools は Firmware(service) からも読まれるので boot/ へは上げられない",
}

# apps/<機能>/ の中の置き場所 → 段(fnmatch。FILE_LAYERS の次、LAYERS より先に見る)
APP_PATTERNS = [
    ("apps/*/ui/*", "ui"),
    ("apps/*/*Scene.cpp", "app"),
    ("apps/*/*Scene.hpp", "app"),
    ("apps/*/*Scene_*.cpp", "app"),   # 大きな画面を分けたもの(TextEditorScene_Edit.cpp 等)
]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
# ビルド時に選ぶものの定義ヘッダ → それを読んでいると分かる書き方
BUILD_CONFIGS = [
    ("config/Hardware_Config.hpp", re.compile(r'\bPICO_HW_[A-Z0-9_]+\b|\bHardwareConfig::')),
    ("config/App_Config.hpp", re.compile(r'\bPICO_APP_[A-Z0-9_]+\b')),
]


def layer_of(rel):
    if rel in FILE_LAYERS:
        return FILE_LAYERS[rel]
    for pat, layer in APP_PATTERNS:
        if fnmatch.fnmatchcase(rel, pat):
            return layer
    best = None
    for key, layer in LAYERS.items():
        if rel == key or rel.startswith(key + "/"):
            if best is None or len(key) > len(best[0]):
                best = (key, layer)
    return best[1] if best else None


def source_files():
    for dirpath, _, names in os.walk(SRC):
        for n in names:
            if n.endswith((".cpp", ".hpp", ".h", ".c")):
                yield os.path.relpath(os.path.join(dirpath, n), SRC).replace(os.sep, "/")


def resolve(includer, target):
    # src/ からの絶対(このリポジトリの決まり)を先に、無ければ同じディレクトリからの相対
    if os.path.isfile(os.path.join(SRC, target)):
        return os.path.normpath(target).replace(os.sep, "/")
    rel = os.path.normpath(os.path.join(os.path.dirname(includer), target)).replace(os.sep, "/")
    if os.path.isfile(os.path.join(SRC, rel)):
        return rel
    return None  # src/ の外(ライブラリ・pc/compat・スタブ)


def list_sources():
    # script/host_test/CMakeLists.txt が段ごとのライブラリを作るのに使う(「段 パス」を1行ずつ)
    rc = 0
    for f in sorted(source_files()):
        if not f.endswith((".cpp", ".c")):
            continue
        layer = layer_of(f)
        if layer is None:
            print("どの段にも入っていないファイル: src/" + f, file=sys.stderr)
            rc = 1
            continue
        print(layer, f)
    return rc


def main():
    if "--sources" in sys.argv:
        return list_sources()
    graph_mode = "--graph" in sys.argv
    rank = {n: i for i, n in enumerate(LAYER_NAMES)}
    unknown = []
    violations = []
    used_exceptions = set()
    missing_cfg = []
    edges = {}
    for f in sorted(source_files()):
        lf = layer_of(f)
        if lf is None:
            unknown.append(f)
            continue
        text = open(os.path.join(SRC, f), encoding="utf-8", errors="replace").read()
        includes = INCLUDE_RE.findall(text)
        for cfg, use_re in BUILD_CONFIGS:
            if f != cfg and use_re.search(text) and cfg not in includes:
                missing_cfg.append((f, cfg))
        for m in INCLUDE_RE.finditer(text):
            t = resolve(f, m.group(1))
            if t is None:
                continue
            lt = layer_of(t)
            if lt is None:
                unknown.append(t)
                continue
            edges[(lf, lt)] = edges.get((lf, lt), 0) + 1
            if rank[lt] > rank[lf]:
                if (f, t) in EXCEPTIONS:
                    used_exceptions.add((f, t))
                else:
                    line = text.count("\n", 0, m.start()) + 1
                    violations.append((f, line, lf, t, lt))

    if graph_mode:
        for (a, b), n in sorted(edges.items(), key=lambda e: (rank[e[0][0]], rank[e[0][1]])):
            mark = "  ↑上向き" if rank[b] > rank[a] else ""
            print(f"{a:>8} → {b:<8} {n:4d}{mark}")
        print()

    ok = True
    if unknown:
        ok = False
        print("どの段にも入っていないファイル(script/check_layers.py の LAYERS に足すこと):")
        for u in sorted(set(unknown)):
            print("  src/" + u)
    if violations:
        ok = False
        print("下の段から上の段への #include:")
        for f, line, lf, t, lt in violations:
            print(f"  src/{f}:{line}  [{lf}] → src/{t} [{lt}]")
        print("直し方は script/check_layers.py の先頭のコメントを参照")
    if missing_cfg:
        ok = False
        print("ビルド時に選ぶもの(PICO_HW_* / HardwareConfig:: / PICO_APP_*)を読むのに、定義のヘッダを include していないファイル:")
        for f, cfg in missing_cfg:
            print(f"  src/{f}  → {cfg} を include すること")
    stale = set(EXCEPTIONS) - used_exceptions
    if stale:
        ok = False
        print("もう違反ではない例外(EXCEPTIONS から消すこと):")
        for f, t in sorted(stale):
            print(f"  {f} → {t}")
    if ok:
        print(f"層の決まりは守られています({len(EXCEPTIONS)} 件の例外を除く)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
