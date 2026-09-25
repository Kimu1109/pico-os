#!/usr/bin/env python3
"""PCのキーボードをpico-osの外部コントローラーにする(USBシリアル経由)。

小さなウィンドウを開き、そこでのキーの押し下げ/離しをボタンの状態(ビットマスク)にして、
pico-osへ1行ずつ送る(形式は src/functions/Pad_Functions.hpp):

    pad XXXX\\n      XXXX = 押しているボタンの16進数

状態が変わったときと、変わらなくても100msごとに送る。pico-os側は500ms届かなければ
全部離した扱いにするので、このスクリプトを止めればボタンが押しっぱなしになることは無い。

使い方:
    python3 script/pad_serial.py COM3            # Windows
    python3 script/pad_serial.py /dev/ttyACM0    # Linux
    python3 script/pad_serial.py                 # ポートが1つだけならそれを使う(pyserialが要る)
    python3 script/pad_serial.py --list          # ポートの一覧
    python3 script/pad_serial.py --stdout | ./pc/build/picoos_pc    # PCビルドへ

- シリアルはpyserial(`pip install pyserial`)で開く。無ければLinux/macOSだけは標準ライブラリで開く
- ポートは同時に1つのプログラムしか開けないので、シリアルモニタとは同時に使えない。
  代わりに**pico-osのログをこのターミナルへそのまま出す**
- pico-osが再起動して(書き込み等で)ポートが一度消えても、1秒ごとに開き直す
- ウィンドウにフォーカスがあるときだけ効く。フォーカスが外れたら全部離す
"""

import argparse
import os
import sys
import threading
import time
import tkinter as tk

try:
    import serial  # noqa: F401  pyserial
    HAVE_PYSERIAL = True
except ImportError:
    HAVE_PYSERIAL = False

# Pad_Functions.hpp の Button と同じ並び(変えないこと)
BUTTONS = ["up", "down", "left", "right", "a", "b", "x", "y",
           "l", "r", "zl", "zr", "start", "select", "home"]
BIT = {name: 1 << i for i, name in enumerate(BUTTONS)}

# キー(tkinterのkeysym。英字は小文字にしてから引く) → ボタン
KEYMAP = {
    "Up": "up", "Down": "down", "Left": "left", "Right": "right",
    "x": "a", "z": "b", "s": "x", "a": "y",
    "q": "l", "w": "r", "e": "zl", "r": "zr",
    "Return": "start", "KP_Enter": "start",
    "BackSpace": "select", "Shift_R": "select",
    "h": "home", "Escape": "home",
}

SEND_INTERVAL_MS = 100
# X11はキーの自動リピートで「離した→押した」を連続して送ってくるので、離したのは少し待ってから採る
RELEASE_DELAY_MS = 30


# ---------------- 送り先 ----------------

class StdoutLink:
    """PCビルド(picoos_pc)の標準入力へパイプで流す"""
    name = "標準出力"

    def write(self, data):
        try:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        except BrokenPipeError:
            os._exit(0)

    def close(self):
        pass


class SerialLink:
    """USBシリアル。切れたら開き直し、届いたもの(pico-osのログ)はターミナルへ出す"""

    def __init__(self, port):
        self.port = port
        self.name = port
        self.lock = threading.Lock()
        self.dev = None
        self.running = True
        threading.Thread(target=self._reader, daemon=True).start()

    def _open(self):
        if HAVE_PYSERIAL:
            import serial
            return PySerialDev(serial.Serial(self.port, 115200, timeout=0.2))
        return PosixDev(self.port)

    def _ensure(self):
        with self.lock:
            if self.dev is None:
                try:
                    self.dev = self._open()
                    print(f"[pad_serial] {self.port} を開きました", file=sys.stderr)
                except Exception:  # 見つからない/まだ準備できていない。次の機会に開き直す
                    self.dev = None
            return self.dev

    def _drop(self, err):
        with self.lock:
            if self.dev is not None:
                print(f"[pad_serial] {self.port} が切れました({err})。開き直します", file=sys.stderr)
                try:
                    self.dev.close()
                except Exception:
                    pass
                self.dev = None

    def write(self, data):
        dev = self._ensure()
        if dev is None:
            return
        try:
            dev.write(data)
        except Exception as e:
            self._drop(e)

    def _reader(self):
        out = sys.stdout.buffer
        while self.running:
            dev = self._ensure()
            if dev is None:
                time.sleep(1.0)
                continue
            try:
                data = dev.read()
            except Exception as e:
                self._drop(e)
                continue
            if data:
                out.write(data)
                out.flush()

    def close(self):
        self.running = False
        with self.lock:
            if self.dev is not None:
                self.dev.close()


class PySerialDev:
    def __init__(self, s):
        self.s = s

    def write(self, data):
        self.s.write(data)

    def read(self):
        return self.s.read(max(1, self.s.in_waiting))

    def close(self):
        self.s.close()


class PosixDev:
    """pyserialが無いとき用(Linux/macOS)。生のモードにして開く"""

    def __init__(self, path):
        import termios
        import tty
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)
        attrs = termios.tcgetattr(self.fd)
        attrs[4] = attrs[5] = termios.B115200  # USB CDCでは意味が無いが揃えておく
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)

    def write(self, data):
        os.write(self.fd, data)

    def read(self):
        import select
        r, _, _ = select.select([self.fd], [], [], 0.2)
        if not r:
            return b""
        data = os.read(self.fd, 256)
        if not data:
            raise OSError("EOF")
        return data

    def close(self):
        os.close(self.fd)


def list_ports():
    try:
        from serial.tools import list_ports as lp
    except ImportError:
        return None
    return list(lp.comports())


# ---------------- ウィンドウ ----------------

class PadWindow:
    def __init__(self, link):
        self.link = link
        self.held = set()       # 押しているキー(keysym)
        self.pending_release = {}
        self.last_sent = None

        self.root = tk.Tk()
        self.root.title(f"pico-os コントローラー → {link.name}")
        self.root.resizable(False, False)

        tk.Label(self.root, justify="left", font=("TkFixedFont", 10), text=(
            "このウィンドウを選んだ状態でキーを押す\n"
            "  十字キー      : 矢印キー\n"
            "  A / B         : X / Z\n"
            "  X / Y         : S / A\n"
            "  L / R / ZL / ZR : Q / W / E / R\n"
            "  START / SELECT: Enter / BackSpace(右Shift)\n"
            "  HOME          : H / Esc\n"
        )).pack(padx=12, pady=(10, 4), anchor="w")

        self.state_label = tk.Label(self.root, font=("TkFixedFont", 12, "bold"),
                                    width=40, anchor="w")
        self.state_label.pack(padx=12, pady=(0, 10), anchor="w")

        self.root.bind("<KeyPress>", self.on_press)
        self.root.bind("<KeyRelease>", self.on_release)
        self.root.bind("<FocusOut>", self.on_focus_out)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        self.update_label()
        self.tick()

    @staticmethod
    def key_of(event):
        k = event.keysym
        return k.lower() if len(k) == 1 else k

    def mask(self):
        m = 0
        for k in self.held:
            name = KEYMAP.get(k)
            if name:
                m |= BIT[name]
        return m

    def send(self, force=False):
        m = self.mask()
        if force or m != self.last_sent:
            self.link.write(f"pad {m:04x}\n".encode("ascii"))
            if m != self.last_sent:
                self.last_sent = m
                self.update_label()

    def update_label(self):
        m = self.last_sent or 0
        names = [n.upper() for n in BUTTONS if m & BIT[n]]
        self.state_label.config(text="押している: " + (" ".join(names) if names else "(なし)"))

    def on_press(self, event):
        k = self.key_of(event)
        if k not in KEYMAP:
            return
        job = self.pending_release.pop(k, None)
        if job:
            self.root.after_cancel(job)  # 自動リピートの「離した」だった
        if k not in self.held:
            self.held.add(k)
            self.send()

    def on_release(self, event):
        k = self.key_of(event)
        if k not in KEYMAP:
            return
        old = self.pending_release.pop(k, None)
        if old:
            self.root.after_cancel(old)
        self.pending_release[k] = self.root.after(RELEASE_DELAY_MS, self.do_release, k)

    def do_release(self, k):
        self.pending_release.pop(k, None)
        if k in self.held:
            self.held.discard(k)
            self.send()

    def on_focus_out(self, _event):
        for job in self.pending_release.values():
            self.root.after_cancel(job)
        self.pending_release.clear()
        if self.held:
            self.held.clear()
            self.send()

    def tick(self):
        self.send(force=True)
        self.root.after(SEND_INTERVAL_MS, self.tick)

    def on_close(self):
        self.held.clear()
        self.send(force=True)
        self.link.close()
        self.root.destroy()

    def run(self):
        self.root.mainloop()


def main():
    ap = argparse.ArgumentParser(description="PCのキーボードをpico-osの外部コントローラーにする")
    ap.add_argument("port", nargs="?", help="シリアルポート(COM3 / /dev/ttyACM0 等)")
    ap.add_argument("--stdout", action="store_true", help="標準出力へ流す(PCビルドへパイプする)")
    ap.add_argument("--list", action="store_true", help="シリアルポートの一覧を出す")
    args = ap.parse_args()

    if args.list:
        ports = list_ports()
        if ports is None:
            sys.exit("一覧を出すにはpyserialが要ります: pip install pyserial")
        for p in ports:
            print(f"{p.device}\t{p.description}")
        return

    if args.stdout:
        link = StdoutLink()
    else:
        port = args.port
        if port is None:
            ports = list_ports()
            if not ports:
                sys.exit("ポートを指定してください(--list で一覧)")
            if len(ports) > 1:
                sys.exit("ポートが複数あります。指定してください:\n" +
                         "\n".join(f"  {p.device}\t{p.description}" for p in ports))
            port = ports[0].device
        if not HAVE_PYSERIAL and os.name == "nt":
            sys.exit("Windowsではpyserialが要ります: pip install pyserial")
        link = SerialLink(port)

    PadWindow(link).run()


if __name__ == "__main__":
    main()
