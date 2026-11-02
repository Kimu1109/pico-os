#!/usr/bin/env python3
"""回路図の結線が、ファームのピン割り当て(src/consts.hpp)と合っているかを確かめる。

回路図から KiCad のネットリストを書き出し、各モジュールの端子が Pico のどの GP につながるかを
consts.hpp の #define と突き合わせる。回路図を直したら回すこと。

使い方(kicad-cli が要る):
  python3 hardware/carrier/tools/check_pins.py
"""
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
CARRIER = os.path.normpath(os.path.join(HERE, ".."))
REPO = os.path.normpath(os.path.join(CARRIER, "..", ".."))
SCH = os.path.join(CARRIER, "pico-os-carrier.kicad_sch")

# Pico 2 W の物理ピン番号 -> GP の番号(GND・電源は載せない)
PICO_GP = {}
for i, g in enumerate([0, 1, None, 2, 3, 4, 5, None, 6, 7, 8, 9, None, 10, 11, 12, 13, None, 14, 15,
                       16, 17, None, 18, 19, 20, 21, None, 22]):
    if g is not None:
        PICO_GP[str(i + 1)] = g
PICO_GP.update({"31": 26, "32": 27, "34": 28})

# (部品, 端子) -> consts.hpp の名前、または GP の番号そのもの(I2C は Wire の既定の GP0/GP1)
CHECKS = {
    ("U3", "3"): "TFT_CS", ("U3", "4"): "TFT_RST", ("U3", "5"): "TFT_DC", ("U3", "6"): "TFT_MOSI",
    ("U3", "7"): "TFT_SCK", ("U3", "8"): "TFT_LED", ("U3", "9"): "TFT_MISO",
    ("U3", "10"): "TOUCH_SCK", ("U3", "11"): "TOUCH_CS", ("U3", "12"): "TOUCH_MOSI",
    ("U3", "13"): "TOUCH_MISO", ("U3", "14"): "TOUCH_IRQ",
    ("U3", "15"): "SD_CS", ("U3", "16"): "SD_MOSI", ("U3", "17"): "SD_MISO", ("U3", "18"): "SD_SCK",
    ("U4", "1"): "AUDIO_I2S_LRCLK", ("U4", "2"): "AUDIO_I2S_BCLK", ("U4", "3"): "AUDIO_I2S_DATA",
    ("U4", "5"): "AUDIO_SHUTDOWN",
    ("U5", "2"): "AUDIO_I2S_BCLK", ("U5", "3"): "AUDIO_I2S_DATA", ("U5", "4"): "AUDIO_I2S_LRCLK",
    ("U5", "9"): "AUDIO_HP_MUTE",
    ("J3", "1"): "AUDIO_HP_DETECT", ("JP6", "1"): "AUDIO_DETECT",
    ("U6", "3"): "MIC_I2S_BCLK", ("U6", "4"): "MIC_I2S_DATA", ("U6", "5"): "MIC_I2S_LRCLK",
    ("JP4", "3"): "CARDKB_UART_RX",
    ("U7", "4"): 0, ("U7", "3"): 1, ("J2", "3"): 0, ("J2", "4"): 1, ("JP5", "1"): 0, ("JP4", "1"): 1,
}


def main():
    consts = open(os.path.join(REPO, "src", "consts.hpp"), encoding="utf-8").read()
    defines = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+(\w+)\s+(\d+)\b", consts)}
    with tempfile.TemporaryDirectory() as tmp:
        xml = os.path.join(tmp, "net.xml")
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadxml", "-o", xml, SCH],
                       check=True, stdout=subprocess.DEVNULL)
        tree = ET.parse(xml)
    node_net = {}
    for net in tree.iter("net"):
        for node in net.iter("node"):
            node_net[(node.get("ref"), node.get("pin"))] = net.get("name")
    gp_of_net = {node_net[("U1", pin)]: gp for pin, gp in PICO_GP.items()}
    bad = 0
    for (ref, pin), want in CHECKS.items():
        name = want if isinstance(want, str) else f"GP{want}"
        want_gp = defines.get(want) if isinstance(want, str) else want
        net = node_net.get((ref, pin))
        got = gp_of_net.get(net)
        if got != want_gp:
            bad += 1
            print(f"食い違い: {ref}-{pin} ({net}) は GP{got}、{name} は GP{want_gp}")
    print(f"{len(CHECKS)}項目を照合、食い違い {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
