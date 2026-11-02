#!/usr/bin/env python3
"""pico-os キャリアボード v0.1 の初版を生成する。

部品・結線・置き場所をこのファイルの表 (PARTS) に1か所で書き、そこから
  - 記号ライブラリ   lib/pico-os-carrier.kicad_sym
  - 部品形状         lib/pico-os-carrier.pretty/*.kicad_mod
  - 回路図           pico-os-carrier.kicad_sch
  - 基板(配線前)     pico-os-carrier.kicad_pcb
を作る。GNDのベタ(両面)は敷くが、信号の配線はしていない(KiCadで引く。README.md)。

**KiCad で手直しを始めたら、このスクリプトで作り直さないこと**(手直しが消える)。
初版を作るための道具で、以後の一次情報源は KiCad のファイル。

使い方(KiCad 8 の Python が要る。Ubuntu の kicad パッケージなら /usr/bin/python3 で import pcbnew できる):
  python3 hardware/carrier/tools/gen_carrier.py
"""
import os
import re
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, ".."))
LIB = "pico-os-carrier"
PROJECT = "pico-os-carrier"
KICAD_SYM = "/usr/share/kicad/symbols"
KICAD_FP = "/usr/share/kicad/footprints"

# 生成のたびにUUIDが変わると差分が読めないので、名前から決める
NS = uuid.UUID("7d5c1f0e-2b8a-4c39-9a51-0c0ffee0ca71")


def uid(*names):
    return str(uuid.uuid5(NS, "/".join(str(n) for n in names)))


# ---------------------------------------------------------------------------
# 基板の寸法(mm、表から見た座標。左上が原点、yは下向き)
# ---------------------------------------------------------------------------
BOARD_W = 56.0
BOARD_H = 116.0
# 液晶(MSP2807、50x86)の左上の位置。寸法は lcdwiki の MSP2807_Size.pdf から
LCD_X, LCD_Y = 3.0, 3.0
# Pico 2 W は下端に横向き(USBが左端、アンテナが右端)。アンテナの下は基板を切り欠く
PICO_CX, PICO_CY = 26.0, 102.5
NOTCH = (42.0, 96.0, 109.0)  # (x始まり, y上, y下) から右端まで切り欠く

# ---------------------------------------------------------------------------
# 部品の表
#   ref, value, symbol(自作は名前だけ / 既製は "Lib:Name"), footprint(自作は名前だけ / 既製は "Lib:Name"),
#   pins: [(番号, 名前, ネット or None(未接続))], side: "F"/"B", pos: (x, y), rot(度), dnp
#   turn: 置いた後にさらに回す角度(裏の部品は反転で向きが180度になるので、rot ではなく turn で回す)
# ---------------------------------------------------------------------------
PICO_PINS = [
    ("1", "GP0", "I2C_SDA"), ("2", "GP1", "I2C_SCL"), ("3", "GND", "GND"),
    ("4", "GP2", "I2S_BCLK"), ("5", "GP3", "I2S_LRCLK"), ("6", "GP4", "I2S_DIN"),
    ("7", "GP5", "AMP_DET"), ("8", "GND", "GND"), ("9", "GP6", "AMP_SD"),
    ("10", "GP7", "CARDKB_RX"), ("11", "GP8", "HP_DET"), ("12", "GP9", "TOUCH_IRQ"),
    ("13", "GND", "GND"), ("14", "GP10", "SD_SCK"), ("15", "GP11", "SD_MOSI"),
    ("16", "GP12", "SD_MISO"), ("17", "GP13", "TOUCH_CS"), ("18", "GND", "GND"),
    ("19", "GP14", "HP_MUTE"), ("20", "GP15", "SD_CS"),
    ("21", "GP16", "SPI0_MISO"), ("22", "GP17", "TFT_CS"), ("23", "GND", "GND"),
    ("24", "GP18", "SPI0_SCK"), ("25", "GP19", "SPI0_MOSI"), ("26", "GP20", "TFT_DC"),
    ("27", "GP21", "TFT_RST"), ("28", "GND", "GND"), ("29", "GP22", "TFT_LED"),
    ("30", "RUN", None), ("31", "GP26", "MIC_BCLK"), ("32", "GP27", "MIC_LRCLK"),
    ("33", "AGND", "GND"), ("34", "GP28", "MIC_DOUT"), ("35", "ADC_VREF", None),
    ("36", "3V3", "+3V3"), ("37", "3V3_EN", "3V3_EN"), ("38", "GND", "GND"),
    ("39", "VSYS", "VSYS"), ("40", "VBUS", "VBUS"),
]
# LiPo SHIM(PiCoLiPoSHIM)は Pico の 1〜8・33〜40 番と同じ位置に穴がある(片側8つ)。使うのは電源とGNDだけ
SHIM_USED = {"3": "GND", "8": "GND", "33": "GND", "36": "+3V3", "37": "3V3_EN", "38": "GND",
             "39": "VSYS", "40": "VBUS"}
SHIM_PINS = [(n, name, SHIM_USED.get(n)) for (n, name, _) in PICO_PINS
             if int(n) <= 8 or int(n) >= 33]

PARTS = [
    dict(ref="U1", value="Raspberry Pi Pico 2 W", symbol="Pico2W", footprint="RaspberryPi_Pico2W_Socket",
         pins=PICO_PINS, side="B", pos=(PICO_CX, PICO_CY), rot=None,
         note="ピンソケット(1x20 2本)で抜き差し"),
    dict(ref="U2", value="LiPo SHIM for Pico", symbol="LiPo_SHIM", footprint="Pimoroni_LiPo_SHIM",
         pins=SHIM_PINS, side="B", pos=(12.0, 45.5), rot=0,
         note="Picoの裏に重ねず、同じピンを基板で結ぶ。使うのは電源とGNDだけ。JSTとボタンの面を外へ"),
    dict(ref="U3", value="MSP2807 (ILI9341 2.8inch)", symbol="MSP2807", footprint="MSP2807_LCD",
         pins=[("1", "VCC", "LCD_VCC"), ("2", "GND", "GND"), ("3", "CS", "TFT_CS"),
               ("4", "RESET", "TFT_RST"), ("5", "DC", "TFT_DC"), ("6", "SDI", "SPI0_MOSI"),
               ("7", "SCK", "SPI0_SCK"), ("8", "LED", "TFT_LED"), ("9", "SDO", "SPI0_MISO"),
               ("10", "T_CLK", "SPI0_SCK"), ("11", "T_CS", "TOUCH_CS"), ("12", "T_DIN", "SPI0_MOSI"),
               ("13", "T_DO", "SPI0_MISO"), ("14", "T_IRQ", "TOUCH_IRQ"),
               ("15", "SD_CS", "SD_CS"), ("16", "SD_MOSI", "SD_MOSI"), ("17", "SD_MISO", "SD_MISO"),
               ("18", "SD_SCK", "SD_SCK")],
         side="F", pos=(LCD_X, LCD_Y), rot=0,
         note="表に載せる。14ピンと4ピン(SD)をピンソケットで受ける"),
    dict(ref="U4", value="MAX98357A module", symbol="MAX98357A_Module", footprint="MAX98357A_Module",
         pins=[("1", "LRC", "I2S_LRCLK"), ("2", "BCLK", "I2S_BCLK"), ("3", "DIN", "I2S_DIN"),
               ("4", "GAIN", None), ("5", "SD", "AMP_SD"), ("6", "GND", "GND"), ("7", "VIN", "VSYS")],
         side="B", pos=(12.0, 69.0), rot=90,
         note="Adafruit 3006 と同じ並び(商品写真で確認)。端子台にスピーカー"),
    dict(ref="U5", value="PCM5102A module (GY-PCM5102)", symbol="PCM5102A_Module", footprint="PCM5102A_Module",
         pins=[("1", "SCK", "GND"), ("2", "BCK", "I2S_BCLK"), ("3", "DIN", "I2S_DIN"),
               ("4", "LCK", "I2S_LRCLK"), ("5", "GND", "GND"), ("6", "VIN", "PCM_VCC"),
               ("7", "FLT", "GND"), ("8", "DEMP", "GND"), ("9", "XSMT", "HP_MUTE"), ("10", "FMT", "GND"),
               ("11", "A3V3", None), ("12", "AGND", "GND"), ("13", "ROUT", None), ("14", "AGND", "GND"),
               ("15", "LROUT", None)],
         side="B", pos=(39.6, 44.0), rot=None, turn=180,
         note="GY-PCM5102(32x17)。ジャックを基板の右の縁に出す。裏のH3L(XSMT)ははんだを盛らない"),
    dict(ref="U6", value="SPH0645LM4H breakout (Adafruit 3421)", symbol="SPH0645_Module", footprint="SPH0645_Module",
         pins=[("1", "3V", "+3V3"), ("2", "GND", "GND"), ("3", "BCLK", "MIC_BCLK"),
               ("4", "DOUT", "MIC_DOUT"), ("5", "LRCL", "MIC_LRCLK"), ("6", "SEL", "GND")],
         side="B", pos=(47.7, 21.0), rot=None,
         note="Adafruit 3421。マイクの部品を基板側、音の穴を外へ(裏返して付ける)"),
    dict(ref="U7", value="DS3231 module (ZS-042)", symbol="DS3231_Module", footprint="DS3231_ZS042_Module",
         pins=[("1", "32K", None), ("2", "SQW", None), ("3", "SCL", "I2C_SCL"),
               ("4", "SDA", "I2C_SDA"), ("5", "VCC", "+3V3"), ("6", "GND", "GND")],
         side="B", pos=(21.1, 21.0), rot=0,
         note="VCCは3.3V(5Vにするとプルアップで信号線が5Vになる)"),
    dict(ref="J1", value="CardKB2 (Grove/HY2.0)", symbol="Connector_Generic:Conn_01x04",
         footprint="Connector_JST:JST_PH_S4B-PH-K_1x04_P2.00mm_Horizontal",
         pins=[("1", "Pin_1", "GND"), ("2", "Pin_2", "GROVE_VCC"), ("3", "Pin_3", "GROVE_Y"),
               ("4", "Pin_4", "GROVE_W")],
         side="B", pos=(50.0, 62.0), rot=90,
         note="1=黒GND 2=赤VCC 3=黄SDA 4=白SCL(UARTモードでは白=CardKB2のTX)"),
    dict(ref="J2", value="Qwiic (Wii adapter)", symbol="Connector_Generic_MountingPin:Conn_01x04_MountingPin",
         footprint="Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
         pins=[("1", "Pin_1", "GND"), ("2", "Pin_2", "+3V3"), ("3", "Pin_3", "I2C_SDA"),
               ("4", "Pin_4", "I2C_SCL"), ("MP", "MountPin", "GND")],
         side="B", pos=(51.5, 73.5), rot=90, note="Qwiicの並び: GND/3V3/SDA/SCL"),
    dict(ref="J3", value="HP_DET", symbol="Connector_Generic:Conn_01x02",
         footprint="Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
         pins=[("1", "Pin_1", "HP_DET"), ("2", "Pin_2", "GND")],
         side="B", pos=(28.0, 58.0), rot=90,
         note="イヤホンジャックの差し込み検出(差すとGNDへ落ちる接点)をつなぐ"),
    dict(ref="JP1", value="LCD_VCC 3V3|VSYS", symbol="Jumper:SolderJumper_3_Bridged12",
         footprint="Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm",
         pins=[("1", "A", "+3V3"), ("2", "C", "LCD_VCC"), ("3", "B", "VSYS")],
         side="B", pos=(28.0, 66.0), rot=0, note="液晶の電源。既定は3.3V"),
    dict(ref="JP2", value="PCM_VCC 3V3|VSYS", symbol="Jumper:SolderJumper_3_Bridged12",
         footprint="Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm",
         pins=[("1", "A", "+3V3"), ("2", "C", "PCM_VCC"), ("3", "B", "VSYS")],
         side="B", pos=(28.0, 71.0), rot=0, note="PCM5102Aの電源。既定は3.3V"),
    dict(ref="JP3", value="GROVE_VCC 3V3|VSYS", symbol="Jumper:SolderJumper_3_Bridged12",
         footprint="Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm",
         pins=[("1", "A", "+3V3"), ("2", "C", "GROVE_VCC"), ("3", "B", "VSYS")],
         side="B", pos=(36.0, 66.0), rot=0, note="CardKB2の電源。既定は3.3V"),
    dict(ref="JP4", value="GROVE_W SCL|GP7", symbol="Jumper:SolderJumper_3_Bridged12",
         footprint="Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm",
         pins=[("1", "A", "I2C_SCL"), ("2", "C", "GROVE_W"), ("3", "B", "CARDKB_RX")],
         side="B", pos=(36.0, 71.0), rot=0, note="白線: I2CモードはSCL(既定)、UARTモードはGP7"),
    dict(ref="JP5", value="GROVE_Y SDA", symbol="Jumper:SolderJumper_2_Bridged",
         footprint="Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm",
         pins=[("1", "A", "I2C_SDA"), ("2", "B", "GROVE_Y")],
         side="B", pos=(36.0, 76.0), rot=0, note="黄線: UARTモードでは切る(CardKB2のRXがI2Cを拾わないように)"),
    dict(ref="JP6", value="AMP_DET", symbol="Jumper:SolderJumper_2_Bridged",
         footprint="Jumper:SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm",
         pins=[("1", "A", "AMP_DET"), ("2", "B", "GND")],
         side="B", pos=(28.0, 76.0), rot=0, note="GP5をGNDへ(アンプあり)。アンプを載せないなら切る"),
    dict(ref="R1", value="4.7k", symbol="Device:R", footprint="Resistor_SMD:R_0805_2012Metric",
         pins=[("1", "~", "+3V3"), ("2", "~", "I2C_SDA")], side="B", pos=(43.0, 67.0), rot=90, dnp=True,
         note="I2Cのプルアップ(普段は載せない。モジュール側にある)"),
    dict(ref="R2", value="4.7k", symbol="Device:R", footprint="Resistor_SMD:R_0805_2012Metric",
         pins=[("1", "~", "+3V3"), ("2", "~", "I2C_SCL")], side="B", pos=(43.0, 72.0), rot=90, dnp=True,
         note="I2Cのプルアップ(普段は載せない)"),
    dict(ref="C1", value="100uF 10V", symbol="Device:C_Polarized", footprint="Capacitor_SMD:CP_Elec_6.3x5.4",
         pins=[("1", "~", "VSYS"), ("2", "~", "GND")], side="B", pos=(24.5, 82.5), rot=0,
         note="アンプの電源の安定用(電池で鳴らしたときの音割れ対策)"),
]

NETS = sorted({p[2] for part in PARTS for p in part["pins"] if p[2]})


def unconnected_net(part, num, name):
    # KiCad が未接続のピンに付ける名前と同じ形(DRCの回路図との照合で一致させる)
    if name and name != "~":
        return f"unconnected-({part['ref']}-{name}-Pad{num})"
    return f"unconnected-({part['ref']}-Pad{num})"


# ---------------------------------------------------------------------------
# S式の小さな読み取り(既製の記号からピンの位置を拾うため)
# ---------------------------------------------------------------------------
def extract_block(text, start):
    depth = 0
    i = start
    in_str = False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    raise ValueError("括弧が閉じていない")


def stock_symbol(libname):
    lib, name = libname.split(":")
    text = open(os.path.join(KICAD_SYM, lib + ".kicad_sym"), encoding="utf-8").read()
    m = re.search(r'\(symbol "%s"\s' % re.escape(name), text)
    block = extract_block(text, m.start())
    block = block.replace(f'(symbol "{name}"', f'(symbol "{libname}"', 1)
    return block


PIN_RE = re.compile(r'\(pin\s+\w+\s+\w+\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\)\s*\(length\s+([-\d.]+)\).*?'
                    r'\(number\s+"([^"]+)"', re.S)


def symbol_pins(block):
    pins = {}
    for m in PIN_RE.finditer(block):
        x, y, a, _length, num = m.groups()
        pins[num] = (float(x), float(y), int(float(a)))
    return pins


# ---------------------------------------------------------------------------
# 自作の記号
# ---------------------------------------------------------------------------
G = 2.54


def make_symbol(name, pins, ref_prefix, title, split=None):
    """箱の左右にピンを並べる。split: 左に置くピン数(既定は半分)"""
    n = len(pins)
    left = pins[: (split if split is not None else (n + 1) // 2)]
    right = pins[len(left):]
    rows = max(len(left), len(right))
    longest = max(len(p[1]) for p in pins)
    # 左右のピン名が箱の中で重ならない幅(1文字およそ1.1mm)
    half_w = max(7.62, -(-(longest * 1.1 + 2.0) // G) * G)
    top = ((rows - 1 + 1) // 2) * G  # 2.54の格子に乗せる
    h_top = top + G
    h_bot = top - (rows - 1) * G - G
    out = [f'\t(symbol "{name}"', '\t\t(pin_names (offset 1.016))', '\t\t(exclude_from_sim no)',
           '\t\t(in_bom yes)', '\t\t(on_board yes)']

    def prop(k, v, y, hide=False):
        h = " (hide yes)" if hide else ""
        return (f'\t\t(property "{k}" "{v}" (at 0 {y:.2f} 0) '
                f'(effects (font (size 1.27 1.27)){h}))')

    out.append(prop("Reference", ref_prefix, h_top + 1.27))
    out.append(prop("Value", title, h_bot - 1.27))
    out.append(prop("Footprint", "", 0, True))
    out.append(prop("Datasheet", "", 0, True))
    out.append(prop("Description", title, 0, True))
    out.append(f'\t\t(symbol "{name}_0_1" (rectangle (start {-half_w:.2f} {h_top:.2f}) (end {half_w:.2f} {h_bot:.2f}) '
               '(stroke (width 0.254) (type default)) (fill (type background))))')
    out.append(f'\t\t(symbol "{name}_1_1"')
    pinpos = {}
    for i, (num, pname, _net) in enumerate(left):
        y = top - i * G
        x = -half_w - G
        pinpos[num] = (x, y, 0)
        out.append(f'\t\t\t(pin passive line (at {x:.2f} {y:.2f} 0) (length 2.54) '
                   f'(name "{pname}" (effects (font (size 1.27 1.27)))) '
                   f'(number "{num}" (effects (font (size 1.27 1.27)))))')
    for i, (num, pname, _net) in enumerate(right):
        y = top - i * G
        x = half_w + G
        pinpos[num] = (x, y, 180)
        out.append(f'\t\t\t(pin passive line (at {x:.2f} {y:.2f} 180) (length 2.54) '
                   f'(name "{pname}" (effects (font (size 1.27 1.27)))) '
                   f'(number "{num}" (effects (font (size 1.27 1.27)))))')
    out.append('\t\t)')
    out.append('\t)')
    return "\n".join(out), pinpos, (half_w, h_top, h_bot)


# Picoは物と同じく 1〜20 を左、21〜40 を右(下から上)に並べる
def pico_symbol_order(pins):
    left = pins[:20]
    right = list(reversed(pins[20:]))
    return left + right, 20


# ---------------------------------------------------------------------------
# 自作の部品形状
# ---------------------------------------------------------------------------
def fp_header(name, descr, attr="through_hole"):
    return [f'(footprint "{name}"', '\t(version 20240108)', '\t(generator "pcbnew")',
            '\t(generator_version "8.0")', '\t(layer "F.Cu")', f'\t(descr "{descr}")',
            f'\t(attr {attr})']


def fp_prop(k, v, x, y, layer, hide=False):
    h = "\n\t\t(hide yes)" if hide else ""
    return (f'\t(property "{k}" "{v}"\n\t\t(at {x:.3f} {y:.3f} 0)\n\t\t(layer "{layer}"){h}\n'
            f'\t\t(uuid "{uid("fpprop", k, v, x, y)}")\n'
            '\t\t(effects (font (size 1 1) (thickness 0.15)))\n\t)')


def fp_rect(x1, y1, x2, y2, layer, width):
    return (f'\t(fp_rect (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(stroke (width {width}) (type solid)) (fill none) (layer "{layer}") '
            f'(uuid "{uid("rect", x1, y1, x2, y2, layer)}"))')


def fp_line(x1, y1, x2, y2, layer, width):
    return (f'\t(fp_line (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(stroke (width {width}) (type solid)) (layer "{layer}") '
            f'(uuid "{uid("line", x1, y1, x2, y2, layer)}"))')


def fp_text(text, x, y, layer, size=1.0):
    return (f'\t(fp_text user "{text}" (at {x:.3f} {y:.3f} 0) (layer "{layer}") '
            f'(uuid "{uid("text", text, x, y, layer)}") '
            f'(effects (font (size {size} {size}) (thickness 0.15))))')


def tht_pad(num, x, y, square=False, drill=1.0, size=1.7):
    shape = "rect" if square else "circle"
    return (f'\t(pad "{num}" thru_hole {shape} (at {x:.3f} {y:.3f}) (size {size} {size}) (drill {drill}) '
            f'(layers "*.Cu" "*.Mask") (remove_unused_layers no) (uuid "{uid("pad", num, x, y)}"))')


def npth(x, y, d):
    return (f'\t(pad "" np_thru_hole circle (at {x:.3f} {y:.3f}) (size {d} {d}) (drill {d}) '
            f'(layers "*.Cu" "*.Mask") (uuid "{uid("npth", x, y)}"))')


def module_fp(name, descr, w, h, pads, extra=(), label=None, silk=True):
    """原点を外形の中心に置いた、モジュール(ピンヘッダで挿す小基板)の形。
    pads: [(番号, x, y)] 原点からの位置。外形は Fab、外形+0.25 を Courtyard、外形をシルクにも描く"""
    L = fp_header(name, descr)
    L.append(fp_prop("Reference", "REF**", 0, -h / 2 - 1.2, "F.SilkS"))
    L.append(fp_prop("Value", name, 0, h / 2 + 1.2, "F.Fab"))
    L.append(fp_prop("Footprint", "", 0, 0, "F.Fab", True))
    L.append(fp_prop("Datasheet", "", 0, 0, "F.Fab", True))
    L.append(fp_prop("Description", descr, 0, 0, "F.Fab", True))
    L.append(fp_rect(-w / 2, -h / 2, w / 2, h / 2, "F.Fab", 0.1))
    if silk:
        L.append(fp_rect(-w / 2, -h / 2, w / 2, h / 2, "F.SilkS", 0.12))
    L.append(fp_rect(-w / 2 - 0.25, -h / 2 - 0.25, w / 2 + 0.25, h / 2 + 0.25, "F.CrtYd", 0.05))
    if label:
        L.append(fp_text(label, 0, 0, "F.Fab", 0.8))
    for i, (num, x, y) in enumerate(pads):
        L.append(tht_pad(num, x, y, square=(i == 0)))
    L.extend(extra)
    L.append(")")
    return "\n".join(L) + "\n"


def footprints():
    fps = {}
    P = 2.54
    # --- Pico 2 W(部品面から見た形。USBが上、1番が左上)。裏に置くと鏡映になる ---
    pads = []
    for i in range(20):
        pads.append((str(i + 1), -8.89, -24.13 + i * P))
    for i in range(20):
        pads.append((str(21 + i), 8.89, 24.13 - i * P))
    extra = [fp_text("USB", 0, -23.5, "F.Fab", 1.0), fp_text("ANT", 0, 22.5, "F.Fab", 1.0),
             fp_line(-4, -25.5, -4, -26.8, "F.Fab", 0.1), fp_line(4, -25.5, 4, -26.8, "F.Fab", 0.1),
             fp_line(-4, -26.8, 4, -26.8, "F.Fab", 0.1)]
    fps["RaspberryPi_Pico2W_Socket"] = module_fp(
        "RaspberryPi_Pico2W_Socket",
        "Raspberry Pi Pico 2 W, 1x20 pin sockets x2 (2.54mm, rows 17.78mm apart), 21x51mm",
        21.0, 51.0, pads, extra, "Pico 2 W", silk=False)
    # --- LiPo SHIM(部品面=JSTとボタンの面から見た形)。Picoの裏に重ねたときと同じ穴が鏡映で並ぶ ---
    #  文字の面が Pico の裏と向き合う(Pimoroni の説明)。部品(JST・ボタン)は文字の無い面にある(商品写真で確認)。
    #  Picoの上から見て1番が左上なので、SHIMの部品面から見ると1番は右上(ボタンの端が上)
    pads = []
    for i in range(8):
        pads.append((str(i + 1), 8.89, -8.89 + i * P))
    for i in range(8):
        pads.append((str(40 - i), -8.89, -8.89 + i * P))
    pads.sort(key=lambda p: int(p[0]))
    extra = [fp_text("BTN", 0, -7.0, "F.SilkS", 1.0), fp_text("JST", 0, 7.0, "F.SilkS", 1.0)]
    fps["Pimoroni_LiPo_SHIM"] = module_fp(
        "Pimoroni_LiPo_SHIM",
        "Pimoroni LiPo SHIM for Pico (PiCoLiPoSHIM), holes = Pico pins 1-8 and 33-40, seen from the parts side, 21x21mm",
        21.0, 21.0, pads, extra, "LiPo SHIM")
    # --- MSP2807 液晶(表から見た形。原点は液晶の左上の角) ---
    L = fp_header("MSP2807_LCD", "MSP2807 2.8inch ILI9341 SPI LCD 50x86mm, J2 14pin + J4 SD 4pin, M3 holes x4 "
                  "(lcdwiki MSP2807_Size.pdf)")
    L.append(fp_prop("Reference", "REF**", 25, -1.5, "F.SilkS"))
    L.append(fp_prop("Value", "MSP2807", 25, 43, "F.Fab"))
    L.append(fp_prop("Footprint", "", 0, 0, "F.Fab", True))
    L.append(fp_prop("Datasheet", "https://www.lcdwiki.com/2.8inch_SPI_Module_ILI9341_SKU:MSP2807", 0, 0,
                     "F.Fab", True))
    L.append(fp_prop("Description", "MSP2807 LCD", 0, 0, "F.Fab", True))
    L.append(fp_rect(0, 0, 50, 86, "F.Fab", 0.1))
    L.append(fp_rect(-0.25, -0.25, 50.25, 86.25, "F.CrtYd", 0.05))
    L.append(fp_rect(3.4, 10.0, 46.6, 67.6, "F.Fab", 0.1))  # 表示部(おおよそ)
    # SDスロット(液晶の裏、表から見て左の縁。カードはここから差す)
    L.append(fp_rect(0.0, 28.0, 20.0, 56.0, "F.Fab", 0.1))
    L.append(fp_text("SD slot (LCD back)", 10, 42, "F.Fab", 0.8))
    for i in range(14):
        L.append(tht_pad(str(i + 1), 8.49 + i * P, 84.0, square=(i == 0)))
    for i in range(4):
        L.append(tht_pad(str(15 + i), 21.19 + i * P, 3.0, square=(i == 0)))
    for (x, y) in ((3.0, 3.0), (47.0, 3.0), (3.0, 79.08), (47.0, 79.08)):
        L.append(npth(x, y, 3.2))
    L.append(fp_text("VCC", 8.49, 81.8, "F.SilkS", 0.8))
    L.append(fp_text("SD_CS", 21.19, 5.2, "F.SilkS", 0.8))
    L.append(")")
    fps["MSP2807_LCD"] = "\n".join(L) + "\n"
    # --- MAX98357A(TVETE、Adafruit 3006 と同じ形。18x19、7ピンが18mmの辺、反対の辺に端子台) ---
    #  部品面から見てピンの列を上にすると、左から VIN/GND/SD/GAIN/DIN/BCLK/LRC(商品写真で確認)
    pads = [(str(i + 1), 7.62 - i * P, -8.0) for i in range(7)]
    pads.sort(key=lambda p: int(p[0]))
    extra = [fp_text("SPK terminal", 0, 7.5, "F.Fab", 0.8)]
    fps["MAX98357A_Module"] = module_fp(
        "MAX98357A_Module", "MAX98357A I2S amp module (Adafruit 3006 compatible), 18x19mm, 1x7 2.54mm",
        18.0, 19.0, pads, extra, "MAX98357A")
    # --- PCM5102A(GY-PCM5102、32x17。部品面=ジャックの面から見て、ジャックが右) ---
    #  短い辺(左)に SCK/BCK/DIN/LCK/GND/VIN、長い辺(上)に FLT/DEMP/XSMT/FMT/A3V3/AGND/ROUT/AGND/LROUT。
    #  端子の位置は商品写真から読んだ(左の列は縁から約1.3mm、上の列は左の縁から約7mm)
    pads = [(str(i + 1), -14.73, -6.35 + i * P) for i in range(6)]
    pads += [(str(7 + i), -9.0 + i * P, -7.23) for i in range(9)]
    extra = [fp_rect(8.5, -2.0, 18.0, 8.5, "F.Fab", 0.1), fp_text("3.5mm jack", 13.0, 3.5, "F.Fab", 0.8)]
    fps["PCM5102A_Module"] = module_fp(
        "PCM5102A_Module", "GY-PCM5102 PCM5102A DAC module 32x17mm, 1x6 + 1x9 2.54mm, jack sticks out ~2mm",
        32.0, 17.0, pads, extra, "PCM5102A")
    # --- SPH0645 ブレークアウト(Adafruit 3421、12.7x16.7)。マイクの部品を基板側にして裏返して付けるので、
    #  外から見えるのは音の穴の面。その面でピンの列を左にすると、上から SEL/LRCL/DOUT/BCLK/GND/3V
    #  (Adafruit の写真と実物で確認)。ピンは左の縁から約2.4mm、音の穴は中央の少し右
    pads = [(str(6 - i), -3.95, -6.35 + i * P) for i in range(6)]
    pads.sort(key=lambda p: int(p[0]))
    extra = [fp_text("PORT", 1.35, -1.6, "F.Fab", 0.8),
             f'\t(fp_circle (center 1.35 0) (end 1.85 0) (stroke (width 0.1) (type solid)) (fill none) '
             f'(layer "F.Fab") (uuid "{uid("mic-port")}"))']
    fps["SPH0645_Module"] = module_fp(
        "SPH0645_Module", "SPH0645LM4H I2S MEMS mic breakout (Adafruit 3421) 12.7x16.7mm, seen from the port side",
        12.7, 16.7, pads, extra, "SPH0645")
    # --- DS3231 ZS-042(38.2x21.6、短い辺に6ピン)。ピンはチップの面から出るので、電池の面が外を向く。
    #  形は電池の面から見たもの(ピンを左にすると上から GND/VCC/SDA/SCL/SQW/32K)
    pads = [(str(6 - i), -17.6, -6.35 + i * P) for i in range(6)]
    pads.sort(key=lambda p: int(p[0]))
    extra = [fp_text("CR2032 side (outward)", 2, 0, "F.Fab", 0.8)]
    fps["DS3231_ZS042_Module"] = module_fp(
        "DS3231_ZS042_Module", "DS3231 + AT24C32 module (ZS-042) 38.2x21.6mm, 1x6 2.54mm, seen from the CR2032 side",
        38.2, 21.6, pads, extra, "DS3231")
    return fps


# ---------------------------------------------------------------------------
# 書き出し
# ---------------------------------------------------------------------------
def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def build_symbols():
    syms = {}   # 名前 -> (lib本文, pinpos, 箱の大きさ)
    for part in PARTS:
        s = part["symbol"]
        if ":" in s or s in syms:
            continue
        pins = part["pins"]
        split = None
        if s == "Pico2W":
            pins, split = pico_symbol_order(pins)
        prefix = "U"
        body, pinpos, box = make_symbol(s, pins, prefix, part["value"], split)
        syms[s] = (body, pinpos, box)
    lib = ["(kicad_symbol_lib", "\t(version 20231120)", '\t(generator "pico-os-carrier-gen")',
           '\t(generator_version "8.0")']
    for name in sorted(syms):
        lib.append(syms[name][0])
    lib.append(")")
    write(os.path.join(OUT, "lib", LIB + ".kicad_sym"), "\n".join(lib) + "\n")
    return syms


def fmt(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def build_schematic(syms):
    root = uid("root")
    items = []
    lib_symbols = []
    pin_tables = {}
    done = set()
    for part in PARTS:
        s = part["symbol"]
        if s in done:
            continue
        done.add(s)
        if ":" in s:
            block = stock_symbol(s)
            pin_tables[s] = symbol_pins(block)
            lib_symbols.append(block)
        else:
            body, pinpos, _box = syms[s]
            pin_tables[s] = pinpos
            lib_symbols.append(body.replace(f'(symbol "{s}"', f'(symbol "{LIB}:{s}"', 1))

    # 参照と値の位置: 自作は箱の上下、既製は記号の既定の位置
    prop_pos = {}
    for s_name in pin_tables:
        if ":" in s_name:
            block = stock_symbol(s_name)
            pos = {}
            for k in ("Reference", "Value"):
                m = re.search(r'\(property "%s" "[^"]*"\s*\(at ([-\d.]+) ([-\d.]+)' % k, block)
                pos[k] = (float(m.group(1)), float(m.group(2)))
            prop_pos[s_name] = (pos["Reference"][0], pos["Reference"][1], "",
                                pos["Value"][0], pos["Value"][1], "")
        else:
            _hw, h_top, h_bot = syms[s_name][2]
            prop_pos[s_name] = (0, h_top + 1.27, "", 0, h_bot - 1.27, "")

    # 置き場所(A3、mm)。大きい物から列に並べる
    layout = {
        "U1": (60.96, 88.9), "U2": (152.4, 63.5), "U3": (243.84, 76.2), "U4": (337.82, 50.8),
        "U5": (337.82, 101.6), "U6": (152.4, 132.08), "U7": (243.84, 147.32),
        "J1": (60.96, 190.5), "J2": (101.6, 190.5), "J3": (139.7, 190.5),
        "JP1": (190.5, 190.5), "JP2": (190.5, 210.82), "JP3": (190.5, 231.14),
        "JP4": (251.46, 190.5), "JP5": (251.46, 210.82), "JP6": (251.46, 231.14),
        "R1": (312.42, 190.5), "R2": (330.2, 190.5), "C1": (360.68, 190.5),
    }
    for part in PARTS:
        ref = part["ref"]
        s = part["symbol"]
        lib_id = s if ":" in s else f"{LIB}:{s}"
        fp = part["footprint"] if ":" in part["footprint"] else f"{LIB}:{part['footprint']}"
        X, Y = layout[ref]
        su = uid("sym", ref)
        part["sym_uuid"] = su
        dnp = "yes" if part.get("dnp") else "no"
        in_bom = "no" if ref.startswith("JP") else "yes"
        rx, ry, rj, vx, vy, vj = prop_pos[s]
        pins_xml = "\n".join(f'\t\t(pin "{n}" (uuid "{uid("pin", ref, n)}"))' for (n, _nm, _net) in part["pins"])
        items.append(f"""\t(symbol
\t\t(lib_id "{lib_id}")
\t\t(at {fmt(X)} {fmt(Y)} 0)
\t\t(unit 1)
\t\t(exclude_from_sim no)
\t\t(in_bom {in_bom})
\t\t(on_board yes)
\t\t(dnp {dnp})
\t\t(uuid "{su}")
\t\t(property "Reference" "{ref}" (at {fmt(X + rx)} {fmt(Y - ry)} 0) (effects (font (size 1.27 1.27)){rj}))
\t\t(property "Value" "{part['value']}" (at {fmt(X + vx)} {fmt(Y - vy)} 0) (effects (font (size 1.27 1.27)){vj}))
\t\t(property "Footprint" "{fp}" (at {fmt(X)} {fmt(Y)} 0) (effects (font (size 1.27 1.27)) (hide yes)))
\t\t(property "Datasheet" "~" (at {fmt(X)} {fmt(Y)} 0) (effects (font (size 1.27 1.27)) (hide yes)))
\t\t(property "Description" "{part.get('note', '')}" (at {fmt(X)} {fmt(Y)} 0) (effects (font (size 1.27 1.27)) (hide yes)))
{pins_xml}
\t\t(instances (project "{PROJECT}" (path "/{root}" (reference "{ref}") (unit 1))))
\t)""")
        # 参照と値は箱の上下へ寄せる(自作の記号だけ。既製の記号は記号の既定の位置に近い所)
        table = pin_tables[s]
        for (num, _nm, net) in part["pins"]:
            px, py, pa = table[num]
            x = X + px
            y = Y - py
            if net is None:
                items.append(f'\t(no_connect (at {fmt(x)} {fmt(y)}) (uuid "{uid("nc", ref, num)}"))')
                continue
            ang = {0: 180, 180: 0, 90: 270, 270: 90}[pa]
            just = "left bottom" if ang in (0, 90) else "right bottom"
            items.append(f'\t(label "{net}" (at {fmt(x)} {fmt(y)} {ang}) '
                         f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid("label", ref, num)}"))')

    notes = [
        ("pico-os キャリアボード v0.1(配線前の初版)。同じ名前のラベルどうしがつながる。", 20.32, 17.78),
        ("仮のもの: U4 MAX98357A・U5 PCM5102A・U6 SPH0645・U7 DS3231 のピンの並びと外形は写真で確かめて直す。", 20.32, 22.86),
        ("ジャンパーの既定: JP1/JP2/JP3=3.3V、JP4=SCL(I2Cモード)、JP5=つなぐ、JP6=つなぐ(GP5をGNDへ)。", 20.32, 27.94),
        ("CardKB2をUARTモードで使うとき: JP4を2-3(GP7)へ、JP5を切る。", 20.32, 33.02),
        ("SHIMの電源ボタンが切るのは3V3_ENだけ。VSYS(アンプ・C1)は電池につながったまま。", 20.32, 38.1),
    ]
    for (t, x, y) in notes:
        items.append(f'\t(text "{t}" (exclude_from_sim no) (at {fmt(x)} {fmt(y)} 0) '
                     f'(effects (font (size 2 2)) (justify left bottom)) (uuid "{uid("note", t)}"))')
    sch = [
        "(kicad_sch", "\t(version 20231120)", '\t(generator "eeschema")', '\t(generator_version "8.0")',
        f'\t(uuid "{root}")', '\t(paper "A3")',
        '\t(title_block (title "pico-os carrier board") (date "2026-11-02") (rev "0.1") '
        '(company "pico-os") (comment 1 "Raspberry Pi Pico 2 W + MSP2807 + modules") '
        '(comment 2 "Generated by hardware/carrier/tools/gen_carrier.py"))',
        "\t(lib_symbols",
    ]
    for b in lib_symbols:
        sch.append("\n".join("\t" + ln for ln in b.splitlines()))
    sch.append("\t)")
    sch.extend(items)
    sch.append('\t(sheet_instances (path "/" (page "1")))')
    sch.append(")")
    write(os.path.join(OUT, PROJECT + ".kicad_sch"), "\n".join(sch) + "\n")
    return root


def build_footprints():
    for name, text in footprints().items():
        write(os.path.join(OUT, "lib", LIB + ".pretty", name + ".kicad_mod"), text)


def build_tables_and_project():
    write(os.path.join(OUT, "sym-lib-table"),
          f'(sym_lib_table\n  (version 7)\n  (lib (name "{LIB}")(type "KiCad")(uri "${{KIPRJMOD}}/lib/{LIB}.kicad_sym")'
          '(options "")(descr "pico-os carrier"))\n)\n')
    write(os.path.join(OUT, "fp-lib-table"),
          f'(fp_lib_table\n  (version 7)\n  (lib (name "{LIB}")(type "KiCad")(uri "${{KIPRJMOD}}/lib/{LIB}.pretty")'
          '(options "")(descr "pico-os carrier"))\n)\n')
    import json
    power = ["GND", "VSYS", "VBUS", "+3V3", "LCD_VCC", "PCM_VCC", "GROVE_VCC"]
    pro = {
        "board": {
            "design_settings": {
                "defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2},
                "rules": {
                    "min_clearance": 0.2, "min_track_width": 0.2, "min_via_diameter": 0.6,
                    "min_through_hole_diameter": 0.3, "min_hole_clearance": 0.25,
                    "min_copper_edge_clearance": 0.3, "min_hole_to_hole": 0.25,
                    "min_via_annular_width": 0.13,
                },
                "track_widths": [0.0, 0.25, 0.5, 0.8],
                "via_dimensions": [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.7, "drill": 0.35}],
            },
        },
        "meta": {"filename": PROJECT + ".kicad_pro", "version": 1},
        "net_settings": {
            "classes": [
                {"name": "Default", "clearance": 0.2, "track_width": 0.25, "via_diameter": 0.7,
                 "via_drill": 0.35, "microvia_diameter": 0.3, "microvia_drill": 0.1,
                 "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "diff_pair_via_gap": 0.25,
                 "wire_width": 6, "bus_width": 12, "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
                 "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": 2147483647},
                {"name": "Power", "clearance": 0.25, "track_width": 0.8, "via_diameter": 0.9,
                 "via_drill": 0.45, "microvia_diameter": 0.3, "microvia_drill": 0.1,
                 "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "diff_pair_via_gap": 0.25,
                 "wire_width": 6, "bus_width": 12, "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
                 "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": 0},
            ],
            "meta": {"version": 3},
            "netclass_patterns": [{"netclass": "Power", "pattern": "/" + p} for p in power],
        },
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[uid("root"), "Root"]],
        "text_variables": {},
    }
    write(os.path.join(OUT, PROJECT + ".kicad_pro"), json.dumps(pro, indent=2, ensure_ascii=False) + "\n")


# ---------------------------------------------------------------------------
# 基板(pcbnew)
# ---------------------------------------------------------------------------
def build_pcb():
    import pcbnew
    mm = pcbnew.FromMM
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(mm(1.6))

    def vec(x, y):
        return pcbnew.VECTOR2I(mm(x), mm(y))

    # 外形(アンテナの下を切り欠く)
    nx, ny1, ny2 = NOTCH
    outline = [(0, 0), (BOARD_W, 0), (BOARD_W, ny1), (nx, ny1), (nx, ny2), (BOARD_W, ny2),
               (BOARD_W, BOARD_H), (0, BOARD_H)]
    for i in range(len(outline)):
        a = outline[i]
        b = outline[(i + 1) % len(outline)]
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(vec(*a))
        seg.SetEnd(vec(*b))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(mm(0.1))
        board.Add(seg)

    nets = {}

    def get_net(name):
        # 回路図のローカルラベルのネットは "/名前"(未接続の "unconnected-..." はそのまま)
        if not name.startswith("unconnected-"):
            name = "/" + name
        if name not in nets:
            ni = pcbnew.NETINFO_ITEM(board, name)
            board.Add(ni)
            nets[name] = ni
        return nets[name]

    for part in PARTS:
        lib, name = (part["footprint"].split(":") if ":" in part["footprint"]
                     else (LIB, part["footprint"]))
        libpath = (os.path.join(OUT, "lib", LIB + ".pretty") if lib == LIB
                   else os.path.join(KICAD_FP, lib + ".pretty"))
        fp = pcbnew.FootprintLoad(libpath, name)
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(part["ref"])
        fp.SetValue(part["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + part["sym_uuid"]))
        fp.SetSheetname("Root")
        fp.SetSheetfile(PROJECT + ".kicad_sch")
        if part.get("dnp"):
            fp.SetDNP(True)
        if part["ref"][0] in "JRC":
            fp.Value().SetVisible(False)
        board.Add(fp)
        x, y = part["pos"]
        fp.SetPosition(vec(x, y))
        if part["side"] == "B":
            fp.Flip(fp.GetPosition(), True)
        if part["ref"] == "U1":
            # USB(1番と40番の側)が左、アンテナ(20/21番)が右になる向きを探す
            for deg in (0, 90, 180, 270):
                fp.SetOrientationDegrees(deg)
                p1 = [p for p in fp.Pads() if p.GetNumber() == "1"][0].GetPosition()
                p20 = [p for p in fp.Pads() if p.GetNumber() == "20"][0].GetPosition()
                if p1.x < p20.x and abs(p1.y - p20.y) < mm(0.1):
                    break
        elif part.get("rot"):
            fp.SetOrientationDegrees(part["rot"])
        if part.get("turn"):
            # 裏の部品は反転した時点で向きが180度になっている。そこからさらに回す
            fp.SetOrientationDegrees(fp.GetOrientationDegrees() + part["turn"])
        if part["ref"] == "J2":
            fp.Reference().SetPosition(vec(x - 4.5, y))
        pinmap = {n: (nm, net) for (n, nm, net) in part["pins"]}
        for pad in fp.Pads():
            num = pad.GetNumber()
            if num not in pinmap:
                continue
            nm, net = pinmap[num]
            if net is None:
                net = unconnected_net(part, num, nm)
            pad.SetNet(get_net(net))
        # 液晶の外形は表(液晶の左上が原点)
    # シルク: 名前と版、Groveの色
    def silk(text, x, y, layer, size=1.0):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetPosition(vec(x, y))
        t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(0.15))
        if layer in (pcbnew.B_SilkS,):
            t.SetMirrored(True)
        board.Add(t)

    # GNDのベタ(両面)。アンテナの横(切り欠きの上下の細い所)には敷かない
    def rect_outline(x1, y1, x2, y2):
        poly = pcbnew.SHAPE_LINE_CHAIN()
        for (px, py) in ((x1, y1), (x2, y1), (x2, y2), (x1, y2)):
            poly.Append(mm(px), mm(py))
        poly.SetClosed(True)
        return poly

    keep = pcbnew.ZONE(board)
    keep.SetIsRuleArea(True)
    keep.SetDoNotAllowCopperPour(True)
    keep.SetDoNotAllowTracks(False)
    keep.SetDoNotAllowVias(False)
    keep.SetDoNotAllowPads(False)
    keep.SetDoNotAllowFootprints(False)
    keep.SetLayerSet(pcbnew.LSET.AllCuMask())
    keep.Outline().AddOutline(rect_outline(NOTCH[0], 89.5, BOARD_W, BOARD_H))
    keep.SetZoneName("antenna keep-out")
    board.Add(keep)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(get_net("GND"))
        z.Outline().AddOutline(rect_outline(0, 0, BOARD_W, BOARD_H))
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.3))
        z.SetThermalReliefSpokeWidth(mm(0.5))
        z.SetZoneName("GND " + ("F" if layer == pcbnew.F_Cu else "B"))
        board.Add(z)

    silk("pico-os carrier v0.1", 28, 88.6, pcbnew.B_SilkS, 1.0)
    silk("G V Y W", 45.0, 57.2, pcbnew.B_SilkS, 0.8)
    silk("JP4 1-2:SCL 2-3:GP7", 40.0, 84.6, pcbnew.B_SilkS, 0.8)
    silk("USB", 3.0, 114.5, pcbnew.B_SilkS, 0.8)
    silk("ANT", 49.0, 102.5, pcbnew.B_SilkS, 0.8)
    path = os.path.join(OUT, PROJECT + ".kicad_pcb")
    pcbnew.SaveBoard(path, board)
    # ベタを埋める(作ったばかりの BOARD では埋められず落ちるので、読み直してから)
    loaded = pcbnew.LoadBoard(path)
    pcbnew.ZONE_FILLER(loaded).Fill(loaded.Zones())
    pcbnew.SaveBoard(path, loaded)
    return loaded


def main():
    syms = build_symbols()
    build_footprints()
    build_schematic(syms)
    build_pcb()
    build_tables_and_project()  # SaveBoard が .kicad_pro を書き直すので最後に書く
    print("生成しました:", OUT)


if __name__ == "__main__":
    main()
