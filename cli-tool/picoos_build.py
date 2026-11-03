# -*- coding: utf-8 -*-
"""
picoos_build - `picoos fw build` / `picoos pc build` / `picoos profile` が共有する「ビルドの条件」。

ビルド時に選べるもの(画面の大きさ・液晶の種類・ハードウェア機能・内蔵アプリ・RAMへ置く所)を
1つの BuildConfig にまとめ、実機(PlatformIO の -D)とPC(cmake の -DPICOOS_*)の両方の形へ直す。

選べるものの一覧はここに書き写さず、毎回ソースから読む(二重管理すると片方だけ増えてずれる):
  内蔵アプリ      src/config/App_Config.hpp の `#ifndef PICO_APP_<名前>`
  ハードウェア    src/config/Hardware_Config.hpp の `#ifndef PICO_HW_<名前>`
  液晶の種類      src/config/LGFX_Config.hpp の `defined(PICO_PANEL_<名前>)`(既定のILI9341は #else)
  env             platformio.ini の [env:...] の build_flags

実機ビルドの流れ(build_fw_plan):
  1. 条件が platformio.ini のどれかの env と同じなら、その env をそのまま使う(版の「構成」も env の名前)。
  2. 違えば、元の env(既定 rpipico2w)に足りない -D を PLATFORMIO_BUILD_FLAGS で足し、
     作る場所を .pio/build-variant/<名前>/ に分ける(条件を変えるたびに全部作り直さないため)。
     版に刻む構成は「<env>+<名前>」(PICOOS_BUILD_VARIANT。script/pio_firmware.py が読む)。
     Webからの更新(Fw_Index)は構成が同じ版しか出さないので、条件の違うファームが既定の版で
     上書きされて機能が消える/増えることがない。

標準ライブラリだけで動く。
"""

import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_CONFIG = REPO_ROOT / "src" / "config"
PIO_INI = REPO_ROOT / "platformio.ini"
PROFILES_FILE = REPO_ROOT / ".picoos" / "profiles.json"

DEFAULT_ENV = "rpipico2w"
DEFAULT_PANEL = "ILI9341"
DEFAULT_SCREEN = (240, 320)
VARIANT_BUILD_ROOT = REPO_ROOT / ".pio" / "build-variant"

# 内蔵フラッシュ 4MB のうち、ファームが使える大きさ = 4MB - EEPROM(4KB) - LittleFS(platformio.ini)
FLASH_SIZE = 4 * 1024 * 1024
EEPROM_SIZE = 4 * 1024


class BuildError(ValueError):
    pass


# ---------------------------------------------------------------------------
# ソースから選べるものを読む
# ---------------------------------------------------------------------------

def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def available_apps():
    return re.findall(r"#ifndef\s+PICO_APP_(\w+)", _read(SRC_CONFIG / "App_Config.hpp"))


def available_hw():
    return re.findall(r"#ifndef\s+PICO_HW_(\w+)", _read(SRC_CONFIG / "Hardware_Config.hpp"))


def available_panels():
    found = re.findall(r"defined\(PICO_PANEL_(\w+)\)", _read(SRC_CONFIG / "LGFX_Config.hpp"))
    return [DEFAULT_PANEL] + [p for p in found if p != DEFAULT_PANEL]


def _first_sentence(text: str) -> str:
    """括弧の外の最初の「。」まで。"""
    depth = 0
    for i, ch in enumerate(text):
        if ch in "(（":
            depth += 1
        elif ch in ")）":
            depth = max(0, depth - 1)
        elif ch == "。" and depth == 0:
            return text[:i]
    return text


def _option_comments(header: str, prefix: str):
    """`#ifndef <prefix><名前>` の直前のコメントの1行目の最初の文(一覧の説明に使う)。"""
    out = {}
    block = []
    for line in _read(SRC_CONFIG / header).splitlines():
        s = line.strip()
        if s.startswith("//"):
            block.append(s.lstrip("/ "))
            continue
        m = re.match(r"#ifndef\s+" + prefix + r"(\w+)", s)
        if m and block:
            out[m.group(1)] = _first_sentence(block[0])
        block = []
    return out


# ---------------------------------------------------------------------------
# platformio.ini
# ---------------------------------------------------------------------------

def read_pio_envs():
    """{env名: {"extends": 名前|None, "flags": [自分の build_flags の -D…(${...} の行は除く)]}}。
    configparser は ${env:...} の書き方や「;」の注釈で迷うので、必要な所だけ素朴に読む。"""
    envs = {}
    cur = None
    key = None
    for raw in _read(PIO_INI).splitlines():
        line = raw.split(";", 1)[0].rstrip()
        m = re.match(r"^\[env:([^\]]+)\]", line)
        if m:
            cur = envs.setdefault(m.group(1), {"extends": None, "flags": []})
            key = None
            continue
        if line.startswith("["):
            cur = None
            key = None
            continue
        if cur is None or not line.strip():
            continue
        if not line[0].isspace():
            k, sep, v = line.partition("=")
            key = k.strip() if sep else None
            v = v.strip()
            if key == "extends":
                cur["extends"] = v.replace("env:", "").strip()
            elif key == "build_flags" and v:
                cur["flags"] += v.split()
            continue
        if key == "build_flags":
            v = line.strip()
            if not v.startswith("${"):
                cur["flags"] += v.split()
    return envs


def filesystem_size():
    m = re.search(r"^board_build\.filesystem_size\s*=\s*(\d+)([kKmM]?)", _read(PIO_INI), re.M)
    if not m:
        return None
    n = int(m.group(1))
    unit = m.group(2).lower()
    return n * (1024 if unit == "k" else 1024 * 1024 if unit == "m" else 1)


def firmware_limit():
    fs = filesystem_size()
    return None if fs is None else FLASH_SIZE - EEPROM_SIZE - fs


# ---------------------------------------------------------------------------
# 条件
# ---------------------------------------------------------------------------

@dataclass
class BuildConfig:
    screen: tuple = None          # (w, h)。None=既定(240x320)
    panel: str = None             # "ST7796" 等。None=既定(ILI9341)
    hw_off: set = field(default_factory=set)    # {"SOUND", ...}
    apps_off: set = field(default_factory=set)  # {"GB", ...}
    lua_ram: bool = False         # PICO_LUA_VM_IN_RAM=1
    hot_ram: bool = True          # PICO_HOT_IN_RAM(0 で文字を描く道もフラッシュへ)
    defines: list = field(default_factory=list)  # ["NAME", "NAME=VAL"]

    # ---- 比べる・保存する ----
    def key(self):
        return (self.screen or DEFAULT_SCREEN, (self.panel or DEFAULT_PANEL), frozenset(self.hw_off),
                frozenset(self.apps_off), self.lua_ram, self.hot_ram, tuple(sorted(self.defines)))

    def is_default(self):
        return self.key() == BuildConfig().key()

    def to_dict(self):
        d = {}
        if self.screen and tuple(self.screen) != DEFAULT_SCREEN:
            d["screen"] = f"{self.screen[0]}x{self.screen[1]}"
        if self.panel and self.panel != DEFAULT_PANEL:
            d["panel"] = self.panel.lower()
        if self.hw_off:
            d["no_hw"] = sorted(x.lower() for x in self.hw_off)
        if self.apps_off:
            d["no_app"] = sorted(x.lower() for x in self.apps_off)
        if self.lua_ram:
            d["lua_ram"] = True
        if not self.hot_ram:
            d["hot_ram"] = False
        if self.defines:
            d["define"] = list(self.defines)
        return d

    @classmethod
    def from_dict(cls, d):
        c = cls()
        if d.get("screen"):
            c.screen = parse_screen(d["screen"])
        if d.get("panel"):
            c.panel = normalize_panel(d["panel"])
        c.hw_off = {normalize_hw(x) for x in d.get("no_hw", [])}
        c.apps_off = {normalize_app(x) for x in d.get("no_app", [])}
        c.lua_ram = bool(d.get("lua_ram", False))
        c.hot_ram = bool(d.get("hot_ram", True))
        c.defines = [validate_define(x) for x in d.get("define", [])]
        return c

    def merged(self, other):
        """other で上書き・追加した新しい条件(画面・液晶は other が決めていれば other、外すものは和)。"""
        c = BuildConfig(
            screen=other.screen or self.screen,
            panel=other.panel or self.panel,
            hw_off=set(self.hw_off) | set(other.hw_off),
            apps_off=set(self.apps_off) | set(other.apps_off),
            lua_ram=self.lua_ram or other.lua_ram,
            hot_ram=self.hot_ram and other.hot_ram,
            defines=list(dict.fromkeys(list(self.defines) + list(other.defines))),
        )
        return c

    # ---- 実機(PlatformIO の build_flags) ----
    def pio_flags(self):
        flags = []
        if self.screen and tuple(self.screen) != DEFAULT_SCREEN:
            flags += [f"-DSCREEN_WIDTH={self.screen[0]}", f"-DSCREEN_HEIGHT={self.screen[1]}"]
        if self.panel and self.panel != DEFAULT_PANEL:
            flags.append(f"-DPICO_PANEL_{self.panel}")
        flags += [f"-DPICO_HW_{x}=0" for x in sorted(self.hw_off)]
        flags += [f"-DPICO_APP_{x}=0" for x in sorted(self.apps_off)]
        if self.lua_ram:
            flags.append("-DPICO_LUA_VM_IN_RAM=1")
        if not self.hot_ram:
            flags.append("-DPICO_HOT_IN_RAM=0")
        flags += [f"-D{d}" for d in self.defines]
        return flags

    # ---- PC(pc/CMakeLists.txt の -DPICOOS_*) ----
    def cmake_args(self):
        """毎回すべての値を渡す(cmake のキャッシュに前の条件が残って、条件を外したつもりが外れない事故を防ぐ)。"""
        w, h = self.screen or DEFAULT_SCREEN
        args = [f"-DPICOOS_SCREEN_W={w}", f"-DPICOOS_SCREEN_H={h}"]
        for x in available_hw():
            args.append(f"-DPICOOS_HW_{x}={'OFF' if x in self.hw_off else 'ON'}")
        for x in available_apps():
            args.append(f"-DPICOOS_APP_{x}={'OFF' if x in self.apps_off else 'ON'}")
        args.append("-DPICOOS_EXTRA_DEFINES=" + ";".join(self.defines))
        return args

    def pc_ignored(self):
        """PCビルドでは意味の無い条件(注意として出す)。"""
        out = []
        if self.panel and self.panel != DEFAULT_PANEL:
            out.append(f"--panel {self.panel.lower()}(PCは液晶の種類を持たない)")
        if self.lua_ram:
            out.append("--lua-ram(RAMへ置くのは実機だけ)")
        if not self.hot_ram:
            out.append("--no-hot-ram(RAMへ置くのは実機だけ)")
        return out

    # ---- 名前 ----
    def tag(self, base=None, hint=None):
        """作る場所と版の構成に使う短い名前([A-Za-z0-9._-] と + だけ)。
        base を渡すと、base(土台の env の条件)との違いだけを名前にする。"""
        base = base or BuildConfig()
        parts = []
        if self.screen and tuple(self.screen) != tuple(base.screen or DEFAULT_SCREEN):
            parts.append(f"{self.screen[0]}x{self.screen[1]}")
        if self.panel and self.panel != (base.panel or DEFAULT_PANEL):
            parts.append(self.panel.lower())
        hw = self.hw_off - base.hw_off
        if hw:
            parts.append("no-" + ".".join(sorted(x.lower() for x in hw)))
        apps = self.apps_off - base.apps_off
        if apps:
            keep = [a for a in available_apps() if a not in self.apps_off]
            if not keep:
                parts.append("no-apps")
            else:
                off = "no-" + ".".join(sorted(x.lower() for x in apps))
                only = "only-" + ".".join(x.lower() for x in keep)
                parts.append(off if len(off) <= len(only) else only)
        if self.lua_ram and not base.lua_ram:
            parts.append("lua-ram")
        if not self.hot_ram and base.hot_ram:
            parts.append("no-hot-ram")
        for d in self.defines:
            if d not in base.defines:
                parts.append("D" + re.sub(r"[^A-Za-z0-9._-]", "-", d))
        tag = "+".join(parts) or "default"
        if len(tag) > 48:
            # 長すぎるときは名前(保存した条件の名前があればそれ)+中身のハッシュ。中身が変われば名前も変わる
            digest = hashlib.sha1(json.dumps(self.to_dict(), sort_keys=True).encode()).hexdigest()[:8]
            tag = f"{hint or 'custom'}-{digest}"
        return tag

    def describe(self):
        w, h = self.screen or DEFAULT_SCREEN
        lines = [f"画面      {w}x{h}  液晶 {(self.panel or DEFAULT_PANEL).lower()}"]
        hw = available_hw()
        lines.append("ハード    " + "  ".join(f"{x.lower()}={'off' if x in self.hw_off else 'on'}" for x in hw))
        apps = available_apps()
        off = [a.lower() for a in apps if a in self.apps_off]
        lines.append(f"アプリ    {len(apps) - len(off)}/{len(apps)} を載せる" + (f"(外す: {', '.join(off)})" if off else ""))
        ram = []
        if self.lua_ram:
            ram.append("LuaのVMもRAMへ")
        if not self.hot_ram:
            ram.append("文字を描く道をフラッシュへ戻す")
        lines.append("RAM       " + ("、".join(ram) if ram else "既定"))
        if self.defines:
            lines.append("追加の-D  " + " ".join(self.defines))
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# 名前の読み取り
# ---------------------------------------------------------------------------

def _norm(name: str) -> str:
    return name.strip().upper().replace("-", "_")


def normalize_app(name: str) -> str:
    n = _norm(name)
    if n.startswith("PICO_APP_"):
        n = n[len("PICO_APP_"):]
    apps = available_apps()
    if n not in apps:
        raise BuildError(f"内蔵アプリ {name!r} はありません。選べるのは: {', '.join(a.lower() for a in apps)}")
    return n


def normalize_hw(name: str) -> str:
    n = _norm(name)
    if n.startswith("PICO_HW_"):
        n = n[len("PICO_HW_"):]
    hw = available_hw()
    if n not in hw:
        raise BuildError(f"ハードウェア機能 {name!r} はありません。選べるのは: {', '.join(x.lower() for x in hw)}")
    return n


def normalize_panel(name: str) -> str:
    n = _norm(name)
    if n.startswith("PICO_PANEL_"):
        n = n[len("PICO_PANEL_"):]
    panels = available_panels()
    if n not in panels:
        raise BuildError(f"液晶 {name!r} はありません。選べるのは: {', '.join(p.lower() for p in panels)}")
    return n


def parse_screen(text: str):
    m = re.fullmatch(r"\s*(\d+)\s*[xX*]\s*(\d+)\s*", str(text))
    if not m:
        raise BuildError(f"--screen は 幅x高さ の形です(例 320x480): {text!r}")
    w, h = int(m.group(1)), int(m.group(2))
    if w < 240 or h < 240:
        raise BuildError(f"画面は幅・高さとも240以上を想定しています(consts.hpp): {w}x{h}")
    return (w, h)


def validate_define(text: str) -> str:
    s = text[2:] if text.startswith("-D") else text
    if not re.fullmatch(r"[A-Za-z_]\w*(=[A-Za-z0-9_.+\-x]*)?", s):
        # 括弧・空白・引用符は PlatformIO のシェル経由のコマンドで壊れる(platformio.ini の注釈を参照)
        raise BuildError(f"-D は NAME か NAME=値(英数字と _ . + - だけ)にしてください: {text!r}")
    return s


def split_list(values):
    """['gb,mail', 'ssh'] → ['gb', 'mail', 'ssh']"""
    out = []
    for v in values or []:
        out += [x for x in re.split(r"[,\s]+", v) if x]
    return out


# platformio.ini の build_flags を BuildConfig へ。分からない -D は defines に入れる
_KNOWN_BASE_PREFIXES = ("-DLLONG_MAX", "-DLLONG_MIN", "-DULLONG_MAX")


def config_from_flags(flags):
    c = BuildConfig()
    w = h = None
    for f in flags:
        if not f.startswith("-D") or f.startswith(_KNOWN_BASE_PREFIXES):
            continue
        name, _, val = f[2:].partition("=")
        if name == "SCREEN_WIDTH":
            w = int(val)
        elif name == "SCREEN_HEIGHT":
            h = int(val)
        elif name.startswith("PICO_PANEL_"):
            c.panel = name[len("PICO_PANEL_"):]
        elif name.startswith("PICO_HW_") and val == "0":
            c.hw_off.add(name[len("PICO_HW_"):])
        elif name.startswith("PICO_APP_") and val == "0":
            c.apps_off.add(name[len("PICO_APP_"):])
        elif name == "PICO_LUA_VM_IN_RAM" and val == "1":
            c.lua_ram = True
        elif name == "PICO_HOT_IN_RAM" and val == "0":
            c.hot_ram = False
        else:
            c.defines.append(f[2:])
    if w or h:
        c.screen = (w or DEFAULT_SCREEN[0], h or DEFAULT_SCREEN[1])
    return c


def env_flags(envs, name):
    """env の build_flags を extends をたどって集める(${...} で引いている分の代わり)。"""
    seen = set()
    flags = []
    chain = []
    cur = name
    while cur and cur not in seen:
        if cur not in envs:
            raise BuildError(f"platformio.ini に env:{cur} がありません。あるのは: {', '.join(envs)}")
        seen.add(cur)
        chain.append(cur)
        cur = envs[cur]["extends"]
    for n in reversed(chain):
        flags += envs[n]["flags"]
    return flags


def env_configs():
    envs = read_pio_envs()
    return {name: config_from_flags(env_flags(envs, name)) for name in envs}


# ---------------------------------------------------------------------------
# argparse のオプション(fw build / pc build / profile save で同じ)
# ---------------------------------------------------------------------------

def add_build_options(parser, fw=True):
    g = parser.add_argument_group("ビルドの条件(`picoos fw options` で選べる名前の一覧)")
    g.add_argument("--profile", metavar="NAME", help="`picoos profile save` で保存した条件を読む(後ろのオプションで足せる)")
    if fw:
        g.add_argument("--env", metavar="ENV",
                       help="platformio.ini の env を土台にする(例 rpipico2w_minimal。既定 rpipico2w)")
    else:
        g.add_argument("--env", metavar="ENV",
                       help="platformio.ini の env と同じ条件でPC版を作る(例 rpipico2w_lite)")
    g.add_argument("--screen", metavar="WxH", help="画面の大きさ(例 320x480)")
    g.add_argument("--panel", metavar="NAME", help="液晶の種類(ili9341 / st7789 / st7796 / ili9488。実機だけ)")
    g.add_argument("--no-sound", action="append_const", dest="no_hw", const="SOUND", help="音声出力を載せない")
    g.add_argument("--no-mic", action="append_const", dest="no_hw", const="MIC", help="マイクを載せない")
    g.add_argument("--no-battery", action="append_const", dest="no_hw", const="BATTERY", help="電池の残量を載せない")
    g.add_argument("--no-hw", metavar="LIST", action="append", dest="no_hw_list", default=[],
                   help="載せないハードウェア機能(カンマ区切り。例 sound,mic)")
    g.add_argument("--no-app", metavar="LIST", action="append", default=[],
                   help="外す内蔵アプリ(カンマ区切り。例 gb,mail,ssh。何度でも)")
    g.add_argument("--only-app", metavar="LIST", action="append", default=[],
                   help="この内蔵アプリだけ載せる(ほかは全部外す。例 notes,calculator)")
    g.add_argument("--no-apps", action="store_true", help="選べる内蔵アプリを全部外す(env rpipico2w_lite と同じ)")
    g.add_argument("--lua-ram", action="store_true", help="LuaのVM(luaV_execute)もRAMへ置く(実機だけ)")
    g.add_argument("--no-hot-ram", action="store_true", help="文字を描く道もフラッシュへ戻す(実機だけ)")
    g.add_argument("-D", "--define", metavar="NAME[=VAL]", action="append", default=[], dest="defines",
                   help="任意のマクロを足す(何度でも)")
    return g


def config_from_args(args):
    """argparse の結果から BuildConfig を作る(profile → env → オプションの順に重ねる)。"""
    base = BuildConfig()
    if getattr(args, "profile", None):
        base = load_profile(args.profile)
    if getattr(args, "env", None):
        base = base.merged(env_configs().get(args.env) or _missing_env(args.env))
    c = BuildConfig()
    if args.screen:
        c.screen = parse_screen(args.screen)
    if args.panel:
        c.panel = normalize_panel(args.panel)
    c.hw_off = {normalize_hw(x) for x in (args.no_hw or []) + split_list(args.no_hw_list)}
    apps = available_apps()
    off = {normalize_app(x) for x in split_list(args.no_app)}
    if args.only_app:
        keep = {normalize_app(x) for x in split_list(args.only_app)}
        off |= {a for a in apps if a not in keep}
    if args.no_apps:
        off |= set(apps)
    c.apps_off = off
    c.lua_ram = args.lua_ram
    c.hot_ram = not args.no_hot_ram
    c.defines = [validate_define(d) for d in args.defines]
    return base.merged(c)


def cached_cmake_args(cache: Path):
    """pc/build*/CMakeCache.txt から条件(PICOOS_SCREEN_* / HW_* / APP_* / EXTRA_DEFINES)を -D の形で読み戻す。"""
    out = []
    for line in _read(cache).splitlines():
        m = re.match(r"^(PICOOS_(?:SCREEN_[WH]|HW_\w+|APP_\w+|EXTRA_DEFINES)):\w+=(.*)$", line)
        if m:
            out.append(f"-D{m.group(1)}={m.group(2)}")
    return out


def has_build_options(args):
    """条件のオプションを1つでも付けたか(pc build で「前の条件のまま作る」かを決める)。"""
    return bool(getattr(args, "profile", None) or getattr(args, "env", None) or args.screen or args.panel or
                args.no_hw or args.no_hw_list or args.no_app or args.only_app or args.no_apps or
                args.lua_ram or args.no_hot_ram or args.defines)


def _missing_env(name):
    raise BuildError(f"platformio.ini に env:{name} がありません。あるのは: {', '.join(read_pio_envs())}")


# ---------------------------------------------------------------------------
# プラットフォーム(CIと同じものへ差し替える)
# ---------------------------------------------------------------------------
#
# platformio.ini の `platform = raspberrypi`(公式)には rpipico2w のボード定義が無いので、CI(firmware-build.yml)は
# maxgerhardt/platform-raspberrypi の固定の版へ差し替えてビルドしている。手元でも同じ版で作れるよう、
# platformio.ini を写した .pio/picoos/platformio.ini の platform の行だけを差し替え、`pio run -c` で渡す
# (platformio.ini 自体は書き換えない。版はワークフローの PLATFORM_REPO / PLATFORM_REF が唯一の情報源)。

FW_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "firmware-build.yml"
PIO_INI_COPY = REPO_ROOT / ".pio" / "picoos" / "platformio.ini"


def ci_platform():
    """firmware-build.yml の PLATFORM_REPO#PLATFORM_REF。読めなければ None。"""
    if not FW_WORKFLOW.exists():
        return None
    text = _read(FW_WORKFLOW)
    repo = re.search(r"^\s*PLATFORM_REPO:\s*(\S+)", text, re.M)
    ref = re.search(r"^\s*PLATFORM_REF:\s*(\S+)", text, re.M)
    if not repo or not ref:
        return None
    return f"{repo.group(1)}#{ref.group(1)}"


def project_conf(platform_mode: str):
    """`pio run -c` に渡す platformio.ini(None なら platformio.ini をそのまま使う)と、使うプラットフォームの説明。
    platform_mode: "ci"(既定。CIと同じ版)/ "ini"(platformio.ini のまま)/ それ以外はプラットフォームの指定そのもの。"""
    text = _read(PIO_INI)
    line_re = re.compile(r"^platform\s*=\s*raspberrypi\s*$", re.M)
    if platform_mode == "ini":
        return None, "platformio.ini のまま"
    if platform_mode == "ci":
        target = ci_platform()
        if target is None:
            return None, "platformio.ini のまま(firmware-build.yml から版を読めない)"
    else:
        target = platform_mode
    if not line_re.search(text):
        # 手元で platform の行を書き換えてある(差し替え済み等)ならそれを尊重する
        return None, "platformio.ini のまま(platform の行が既定と違う)"
    PIO_INI_COPY.parent.mkdir(parents=True, exist_ok=True)
    new_text = line_re.sub(f"platform = {target}", text, count=1)
    header = ("; cli-tool/picoos_build.py が platformio.ini から作る写し(platform の行だけ差し替え)。手で直さない\n")
    new_text = header + new_text
    if not PIO_INI_COPY.exists() or _read(PIO_INI_COPY) != new_text:
        PIO_INI_COPY.write_text(new_text, encoding="utf-8")
    return PIO_INI_COPY, target


# ---------------------------------------------------------------------------
# 実機ビルドの段取り
# ---------------------------------------------------------------------------

@dataclass
class FwPlan:
    env: str                 # pio run -e に渡す env
    extra_flags: list        # PLATFORMIO_BUILD_FLAGS に足す -D
    variant: str             # 版に刻む構成の後ろ(空なら env のまま)
    build_dir: Path          # firmware.bin のある所
    build_root: Path = None  # PLATFORMIO_BUILD_DIR(None なら既定の .pio/build)

    def environ(self):
        env = {}
        if self.extra_flags:
            env["PLATFORMIO_BUILD_FLAGS"] = " ".join(self.extra_flags)
        if self.build_root is not None:
            env["PLATFORMIO_BUILD_DIR"] = str(self.build_root)
        if self.variant:
            env["PICOOS_BUILD_VARIANT"] = self.variant
        return env

    @property
    def bin_path(self):
        return self.build_dir / "firmware.bin"

    @property
    def build_env_name(self):
        return f"{self.env}+{self.variant}" if self.variant else self.env


def build_fw_plan(config: BuildConfig, base_env=None, hint=None):
    configs = env_configs()
    if base_env is None:
        # 条件がどれかの env と同じなら、その env をそのまま使う(既定の env を先に見る)
        for name in [DEFAULT_ENV] + [n for n in configs if n != DEFAULT_ENV]:
            if configs[name].key() == config.key():
                return FwPlan(env=name, extra_flags=[], variant="",
                              build_dir=REPO_ROOT / ".pio" / "build" / name)
        base_env = DEFAULT_ENV
    if base_env not in configs:
        _missing_env(base_env)
    base = configs[base_env]
    if base.key() == config.key():
        return FwPlan(env=base_env, extra_flags=[], variant="", build_dir=REPO_ROOT / ".pio" / "build" / base_env)
    # 土台の env が既に持っている -D は足さない。土台が外している物を戻すことはできない
    if (base.screen and tuple(base.screen) != DEFAULT_SCREEN and config.screen != base.screen) or \
       (base.panel and base.panel != DEFAULT_PANEL and config.panel != base.panel) or \
       not base.hw_off <= config.hw_off or not base.apps_off <= config.apps_off or \
       (base.lua_ram and not config.lua_ram) or (not base.hot_ram and config.hot_ram):
        raise BuildError(f"env:{base_env} が外している物を戻すことはできません(--env を外すか rpipico2w を土台に)")
    base_flags = set(base.pio_flags())
    extra = [f for f in config.pio_flags() if f not in base_flags]
    # 画面の大きさは2つで1組なので、片方だけ同じでも両方渡す
    if any(f.startswith("-DSCREEN_") for f in extra):
        extra = [f for f in extra if not f.startswith("-DSCREEN_")]
        w, h = config.screen
        extra = [f"-DSCREEN_WIDTH={w}", f"-DSCREEN_HEIGHT={h}"] + extra
    variant = config.tag(base, hint=hint)
    root = VARIANT_BUILD_ROOT / variant
    return FwPlan(env=base_env, extra_flags=extra, variant=variant, build_dir=root / base_env, build_root=root)


# ---------------------------------------------------------------------------
# 保存した条件(.picoos/profiles.json。手元だけ、git には入れない)
# ---------------------------------------------------------------------------

def _load_all():
    if not PROFILES_FILE.exists():
        return {}
    try:
        data = json.loads(PROFILES_FILE.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise BuildError(f"{PROFILES_FILE} が読めません: {e}") from None
    return data if isinstance(data, dict) else {}


def list_profiles():
    return {k: BuildConfig.from_dict(v) for k, v in sorted(_load_all().items())}


def load_profile(name):
    data = _load_all()
    if name not in data:
        have = ", ".join(sorted(data)) or "(まだ無い)"
        raise BuildError(f"保存した条件 {name!r} がありません。あるのは: {have}")
    return BuildConfig.from_dict(data[name])


def save_profile(name, config: BuildConfig):
    if not re.fullmatch(r"[A-Za-z0-9._-]+", name):
        raise BuildError(f"名前は英数字と . _ - だけにしてください: {name!r}")
    data = _load_all()
    data[name] = config.to_dict()
    PROFILES_FILE.parent.mkdir(parents=True, exist_ok=True)
    PROFILES_FILE.write_text(json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def remove_profile(name):
    data = _load_all()
    if name not in data:
        raise BuildError(f"保存した条件 {name!r} がありません")
    del data[name]
    PROFILES_FILE.write_text(json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# 一覧の表示(picoos fw options)
# ---------------------------------------------------------------------------

def print_options():
    app_desc = _option_comments("App_Config.hpp", "PICO_APP_")
    hw_desc = _option_comments("Hardware_Config.hpp", "PICO_HW_")
    print("内蔵アプリ(--no-app / --only-app / --no-apps):")
    for a in available_apps():
        print(f"  {a.lower():<12} {app_desc.get(a, '')}")
    print("\nハードウェア機能(--no-hw、または --no-sound / --no-mic / --no-battery):")
    for x in available_hw():
        print(f"  {x.lower():<12} {hw_desc.get(x, '')}")
    print("\n液晶(--panel、実機だけ):")
    for p in available_panels():
        print(f"  {p.lower()}" + ("   (既定)" if p == DEFAULT_PANEL else ""))
    print("\n画面の大きさ(--screen WxH): 既定 240x320。幅・高さとも240以上(例 320x480 は --panel st7796 等と組む)")
    print("RAMへ置く所(実機だけ): --lua-ram / --no-hot-ram")
    print("\nplatformio.ini の env(--env。同じ条件を選ぶと自動でその env を使う):")
    for name, c in env_configs().items():
        print(f"  {name:<22} {'(既定)' if c.is_default() else c.tag()}")
    profiles = list_profiles()
    if profiles:
        print(f"\n保存した条件(--profile。{PROFILES_FILE.relative_to(REPO_ROOT)}):")
        for name, c in profiles.items():
            print(f"  {name:<22} {'(既定)' if c.is_default() else c.tag()}")
    limit = firmware_limit()
    if limit:
        print(f"\nファームの上限: {limit:,} バイト(4MB - EEPROM 4KB - LittleFS {filesystem_size() // 1024}KB)")
