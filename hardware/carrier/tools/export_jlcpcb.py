#!/usr/bin/env python3
"""JLCPCB へ出すガーバーとドリルを書き出して zip にまとめる(kicad-cli を使う)。

  python3 hardware/carrier/tools/export_jlcpcb.py

出力: hardware/carrier/fab/pico-os-carrier-v<版>-jlcpcb.zip
(作業用の hardware/carrier/gerber/ は .gitignore 済み)

設定は JLCPCB の KiCad 向けの案内に合わせた:
- ガーバー: 銅2層・マスク・シルク・外形。Protel の拡張子、X2 は使わない、ネットの情報は入れない、
  シルクからマスクの穴の分を引く
- ドリル: Excellon、mm、小数点あり、長穴は別の書き方、PTH と NPTH は別のファイル、穴の地図はガーバーで
"""
import glob
import os
import re
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CARRIER = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(CARRIER, "pico-os-carrier.kicad_pcb")
WORK = os.path.join(CARRIER, "gerber")
FAB = os.path.join(CARRIER, "fab")
LAYERS = "F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts"


def run(args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)


def main():
    sch = open(os.path.join(CARRIER, "pico-os-carrier.kicad_sch"), encoding="utf-8").read()
    m = re.search(r'\(rev "([^"]+)"\)', sch)
    rev = m.group(1) if m else "0"
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(FAB, exist_ok=True)
    for f in glob.glob(os.path.join(WORK, "*")):
        os.remove(f)
    run(["kicad-cli", "pcb", "export", "gerbers", "--layers", LAYERS, "--subtract-soldermask",
         "--no-x2", "--no-netlist", "-o", WORK + "/", PCB])
    run(["kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
         "--excellon-zeros-format", "decimal", "--excellon-oval-format", "alternate",
         "--excellon-separate-th", "--generate-map", "--map-format", "gerberx2", "-o", WORK + "/", PCB])
    files = sorted(os.listdir(WORK))
    out = os.path.join(FAB, f"pico-os-carrier-v{rev}-jlcpcb.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in files:
            z.write(os.path.join(WORK, f), f)
    print("書き出しました:", out)
    for f in files:
        print("  ", f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
