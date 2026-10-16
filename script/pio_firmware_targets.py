# PlatformIO の追加スクリプト(platformio.ini の extra_scripts = post:script/pio_firmware_targets.py)。
# ファームの更新ファイルを作る・送るターゲット(FIRMWARE_UPDATE.md。説明は script/pio_firmware.py)。
# PROGNAME(firmware)はプラットフォームが決めるので、ビルドの後(post)で足す。
import os

Import("env")  # noqa: F821  (PlatformIOが用意する)

TOOL = os.path.join(env.subst("$PROJECT_DIR"), "script", "firmware_tool.py")  # noqa: F821
BIN = env.subst(os.path.join("$BUILD_DIR", "${PROGNAME}.bin"))  # noqa: F821
PFW = env.subst(os.path.join("$BUILD_DIR", "${PROGNAME}.pfw"))  # noqa: F821

env.AddCustomTarget(  # noqa: F821
    name="fwpack",
    dependencies=BIN,
    actions=[f'"$PYTHONEXE" "{TOOL}" pack --bin "{BIN}" --out "{PFW}"'],
    title="ファームの更新ファイルを作る",
    description="firmware.bin から firmware.pfw(SDに置いて更新する)を作る",
)

env.AddCustomTarget(  # noqa: F821
    name="fwpush",
    dependencies=BIN,
    actions=[f'"$PYTHONEXE" "{TOOL}" push --bin "{BIN}" --host "$UPLOAD_PORT"'],
    title="Wi-Fiでファームを送る",
    description="本体の「本体の更新」→[受け取る]へ送る(--upload-port に本体のIPアドレス)",
)
