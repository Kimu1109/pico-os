# PlatformIO の追加スクリプト(platformio.ini の extra_scripts = pre:script/pio_hot_o2.py)。
#
# 速さが効く少しのファイルだけを -O2 でコンパイルする(ほかは今まで通り -Os)。
# docs/design/performance.md「よく通る所だけ -O2 でコンパイルする」。
#
# - ビルドの middleware(env.AddBuildMiddleware)で、下の HOT_FILES に当たるファイルのコンパイルにだけ -O2 を足す
#   (-Os より後ろに足すので -O2 が勝つ)。pre: で登録するので、ライブラリ(lib/lua 等)のビルドにも引き継がれる
#   (ライブラリのビルドは pre: の後に env を写して作られる)。
# - フラッシュの残りが少ないので、足すのは「計測で効くと分かった」「大きくならない」ファイルだけ。
#   -O2 で大きく膨らむもの(テンプレートの多い Widget_Functions.cpp は +39KB 等)は入れない。
# - -DPICO_HOT_O2=0 で全部 -Os に戻す(実機で比べる用。build_flags に足す)。
# - PC/Web のビルド(pc/CMakeLists.txt)には関係しない。

Import("env")  # noqa: F821  (PlatformIOが用意する)

# パスの末尾で当てる(fnmatch。パスは相対のことがあるので先頭は * だけ)。
# Lua の命令の実行・表・ごみ集め(実機の Flash +3.8KB)。ARM(Thumb-2)で数えた実行した命令数は、Luaのアプリのテスト8本の合計で -1.7%
# (Lua の中心の9本では -1.4% で実機の Flash +6.6KB、Lua 全部では -2.6% で試しのARMのビルドで約 +19KB)
HOT_FILES = [
    "*lib/lua/src/lvm.c",
    "*lib/lua/src/ltable.c",
    "*lib/lua/src/lgc.c",
]


def _disabled(e):
    for d in e.get("CPPDEFINES", []):
        name, value = (d if isinstance(d, (list, tuple)) else (d, None))
        if name == "PICO_HOT_O2" and str(value) == "0":
            return True
    return False


def _o2(e, node):
    if _disabled(e):
        return node
    return e.Object(node, CCFLAGS=e["CCFLAGS"] + ["-O2"])


for pattern in HOT_FILES:
    env.AddBuildMiddleware(_o2, pattern)  # noqa: F821
