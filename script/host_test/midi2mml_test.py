#!/usr/bin/env python3
"""script/midi2mml.py の検証。テストの中でMIDIを組み立てて変換し、出てきたMMLを
本物の読み取り(MmlCompiler。mml_dump.cpp)へ通して、音の位置・高さ・長さ・テンポを確かめる。

    python3 script/host_test/midi2mml_test.py <mml_dumpの実行ファイル>

run.sh が mml_dump をビルドしてから呼ぶ。
"""
import io
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "script"))
import midi2mml  # noqa: E402

DUMP = sys.argv[1] if len(sys.argv) > 1 else None
TMP = tempfile.mkdtemp(prefix="midi2mml_test_")
failures = 0
checks = 0


def check(cond, what):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print("  [NG] " + what)


# ---------------------------------------------------------------- MIDIを組み立てる

def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def track(events, running_status=False):
    """events: [(絶対時刻, bytes)]"""
    data = b""
    last_t = 0
    last_status = None
    for t, ev in sorted(events, key=lambda e: e[0]):
        data += vlq(t - last_t)
        last_t = t
        if running_status and ev[0] < 0xF0 and ev[0] == last_status:
            data += ev[1:]
        else:
            data += ev
            last_status = ev[0] if ev[0] < 0xF0 else None
    data += b"\x00\xFF\x2F\x00"
    return b"MTrk" + len(data).to_bytes(4, "big") + data


def smf(tracks, division=480, fmt=1):
    head = b"MThd" + (6).to_bytes(4, "big") + fmt.to_bytes(2, "big") + len(tracks).to_bytes(2, "big") + division.to_bytes(2, "big")
    return head + b"".join(tracks)


def tempo(t, bpm):
    return (t, b"\xFF\x51\x03" + int(round(60000000 / bpm)).to_bytes(3, "big"))


def name(text, enc="utf-8"):
    raw = text.encode(enc)
    return (0, b"\xFF\x03" + vlq(len(raw)) + raw)


def notes(chan, seq, vel=100, off_as_zero=False):
    """seq: [(開始, 長さ, ノート番号)] → イベント"""
    evs = []
    for s, d, p in seq:
        evs.append((s, bytes([0x90 | chan, p, vel])))
        if off_as_zero:
            evs.append((s + d, bytes([0x90 | chan, p, 0])))
        else:
            evs.append((s + d, bytes([0x80 | chan, p, 64])))
    # 同じ時刻では離すのを先に(並べ替えは安定なので、ここで順番を作る)
    evs.sort(key=lambda e: (e[0], 0 if (e[1][0] & 0xF0) == 0x80 or e[1][2] == 0 else 1))
    return evs


# ---------------------------------------------------------------- 変換して読む

def convert(data, *opts, expect_fail=False):
    path = os.path.join(TMP, "in.mid")
    with open(path, "wb") as f:
        f.write(data)
    out = os.path.join(TMP, "out.mml")
    err = io.StringIO()
    old = sys.stderr
    sys.stderr = err
    try:
        rc = midi2mml.main([path, "-o", out] + list(opts))
    finally:
        sys.stderr = old
    if expect_fail:
        return rc, err.getvalue(), None
    if rc != 0:
        print("  変換に失敗: " + err.getvalue())
        return rc, err.getvalue(), None
    with open(out, encoding="utf-8") as f:
        text = f.read()
    return rc, err.getvalue(), text


class Dump:
    def __init__(self, text):
        path = os.path.join(TMP, "check.mml")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        res = subprocess.run([DUMP, path], capture_output=True, text=True, timeout=30)
        self.lines = res.stdout.splitlines()
        head = self.lines[0].split()
        self.ok = head[1] == "ok"
        self.size = int(head[4])
        self.tempo = int(head[5])
        self.message = next((l[8:] for l in self.lines if l.startswith("message ")), "")
        self.warning = next((l[8:] for l in self.lines if l.startswith("warning ")), "")

    def rows(self, kind, ch=None):
        out = []
        for l in self.lines:
            f = l.split()
            if f[0] == kind and (ch is None or int(f[1]) == ch):
                out.append(tuple(int(x) for x in f[1:]))
        return out

    def notes(self, ch):
        return [(t, p, d) for _c, t, p, d in self.rows("note", ch)]


def compile_ok(text, what):
    d = Dump(text)
    check(d.ok, "%s: MMLとして読める(%s)" % (what, d.message))
    return d


def mml_ch(letter):
    return "ABCD".index(letter)


# ---------------------------------------------------------------- テスト

def test_simple_melody():
    print("--- 単音のメロディ(形式0、テンポ150)")
    q = 480
    seq = [(0, q, 60), (q, q, 62), (2 * q, q // 2, 64), (2 * q + q // 2, q // 2, 65), (3 * q, 2 * q, 67)]
    data = smf([track([name("テスト曲"), tempo(0, 150)] + notes(0, seq))], fmt=0)
    _rc, _err, text = convert(data)
    d = compile_ok(text, "単音")
    check(d.tempo == 150, "#tempo 150(%d)" % d.tempo)
    check("#title テスト曲" in text, "トラック名が曲名になる")
    check(d.notes(0) == [(0, 60, 48), (48, 62, 48), (96, 64, 24), (120, 65, 24), (144, 67, 96)],
          "音の位置・高さ・長さ(%s)" % d.notes(0))
    check(d.rows("gate", 0) == [(0, 0, 8)], "q8で切らない")
    check(("wave", 0, 0, 2) in [("wave",) + r for r in d.rows("wave", 0)], "Aは pulse50")
    check(d.size == int(text.split("演奏データ: 約")[1].split("バイト")[0]), "見積もったバイト数が本物と一致(%d)" % d.size)


def test_running_status_and_zero_velocity():
    print("--- ランニングステータスとベロシティ0の音止め")
    seq = [(0, 240, 72), (240, 240, 74), (480, 480, 76)]
    data = smf([track([tempo(0, 120)] + notes(3, seq, off_as_zero=True), running_status=True)])
    _rc, _err, text = convert(data)
    d = compile_ok(text, "ランニングステータス")
    check(d.notes(0) == [(0, 72, 24), (24, 74, 24), (48, 76, 48)], "音符(%s)" % d.notes(0))


def test_triplets_and_grid():
    print("--- 3連符と格子の自動選択")
    q = 480
    trip = q * 2 // 3   # 3連4分
    seq = [(0, trip, 60), (trip, trip, 62), (2 * trip, trip, 64), (2 * q, q // 4, 65), (2 * q + q // 4, q // 4, 67)]
    data = smf([track([tempo(0, 120)] + notes(0, seq))])
    _rc, _err, text = convert(data)
    d = compile_ok(text, "3連符")
    check(d.notes(0) == [(0, 60, 32), (32, 62, 32), (64, 64, 32), (96, 65, 12), (108, 67, 12)],
          "3連4分と16分が両方正しい(%s)" % d.notes(0))
    check("3連32分音符" in text, "格子は3連32分(4ティック)を選ぶ")
    # 格子を指定すると揃え直す
    _rc, _err, text = convert(data, "--grid", "16")
    d = compile_ok(text, "--grid 16")
    check([n[0] for n in d.notes(0)][:3] == [0, 36, 60], "16分に揃える(%s)" % d.notes(0))


def test_chords_and_assign():
    print("--- 和音の声部分けとチャンネルの割り当て")
    q = 480
    chords = []
    for i, root in enumerate((60, 65, 67, 60)):
        for p in (root, root + 4, root + 7):
            chords.append((i * 4 * q, 4 * q, p))
    melody = [(i * q, q, 72 + (i % 5)) for i in range(16)]
    bass = [(i * 2 * q, 2 * q, 36 + (i % 3) * 5) for i in range(8)]
    data = smf([
        track([tempo(0, 120)]),
        track([name("メロディ")] + notes(0, melody)),
        track([name("ピアノ"), (0, b"\xC0\x00")] + notes(1, chords)),
        track([name("ベース"), (0, b"\xC2\x21")] + notes(2, bass)),
    ])
    _rc, _err, text = convert(data)
    d = compile_ok(text, "和音")
    check([p for _t, p, _d in d.notes(mml_ch("A"))][:3] == [72, 73, 74], "A はいちばん高いメロディ")
    check([p for _t, p, _d in d.notes(mml_ch("C"))][:3] == [36, 41, 46], "C はベース(%s)" % d.notes(2)[:3])
    check(d.rows("wave", mml_ch("C"))[0][2] == 4, "C は三角波")
    check(len(d.notes(mml_ch("B"))) == 4 and len(d.notes(mml_ch("D"))) == 4, "和音の上2声が B と D に入る")
    check([p for _t, p, _d in d.notes(mml_ch("B"))] == [67, 72, 74, 67], "B は和音の最高音(%s)" % d.notes(1))

    # --list と --pick
    path = os.path.join(TMP, "in.mid")
    buf = io.StringIO()
    old = sys.stdout
    sys.stdout = buf
    try:
        midi2mml.main([path, "--list"])
    finally:
        sys.stdout = old
    listing = buf.getvalue()
    check("「ベース」" in listing and "[ベース]" in listing, "一覧にトラック名と音色の系統が出る")
    first = [l for l in listing.splitlines() if l.strip().startswith("1 ")]
    check(first and "「メロディ」" in first[0], "1番は鳴っている時間の長いメロディ(%s)" % first)

    _rc, _err, text = convert(data, "--pick", "A=1,B=2+3")
    d = compile_ok(text, "--pick")
    check(set(int(l.split()[1]) for l in d.lines if l.startswith("note")) == {0, 1}, "選んだ A と B だけ")
    rc, err, _ = convert(data, "--pick", "A=99", expect_fail=True)
    check(rc == 1 and "声部99" in err, "無い番号は断る")


def test_drums():
    print("--- 打楽器(10ch)")
    q = 480
    drums = []
    for i in range(4):
        drums.append((i * q, 60, 36 if i % 2 == 0 else 38))     # キック/スネア
        drums.append((i * q, 60, 42))                          # 同時のハイハットは負ける
        drums.append((i * q + q // 2, 60, 42))                 # 裏のハイハット
    melody = [(i * q, q, 72) for i in range(4)]
    data = smf([track([tempo(0, 120)] + notes(0, melody) + notes(9, drums))])
    _rc, _err, text = convert(data)
    d = compile_ok(text, "打楽器")
    dn = d.notes(mml_ch("D"))
    check([p for _t, p, _d in dn] == [72, 108, 96, 108, 72, 108, 96, 108], "キック/ハット/スネアの高さ(%s)" % dn)
    check([t for t, _p, _d in dn] == [0, 24, 48, 72, 96, 120, 144, 168], "位置")
    check([w for _c, _t, w in d.rows("wave", 3)][:3] == [6, 7, 6], "ノイズ/短いノイズを切り替える")
    _rc, _err, text = convert(data, "--no-drums")
    d = compile_ok(text, "--no-drums")
    check(not d.notes(3), "--no-drums なら D は使わない")


def test_tempo_changes():
    print("--- テンポの変化")
    q = 480
    melody = [(0, 8 * q, 60)]       # テンポの変わり目をまたぐ長い音
    data = smf([track([tempo(0, 100), tempo(4 * q, 140), tempo(6 * q, 400)] + notes(0, melody))])
    _rc, err, text = convert(data)
    d = compile_ok(text, "テンポ")
    check(d.tempo == 100, "最初のテンポ")
    check(d.rows("tempo") == [(1, 192, 140), (1, 288, 300)], "空いたチャンネルに t を書く/300へ寄せる(%s)" % d.rows("tempo"))
    check("端へ寄せました" in err, "範囲外のテンポは警告する")
    check(d.notes(0) == [(0, 60, 384)], "音は分けない")

    # 4チャンネルとも音符でふさがっていれば、音符を分ける
    seqs = [notes(c, [(0, 8 * q, 60 + c * 5)]) for c in range(4)]
    data = smf([track([tempo(0, 100), tempo(4 * q, 140)] + sum(seqs, []))])
    _rc, err, text = convert(data)
    d = compile_ok(text, "テンポ(空き無し)")
    check(len(d.rows("tempo")) == 1 and d.rows("tempo")[0][1:] == (192, 140), "t の位置(%s)" % d.rows("tempo"))
    ch = d.rows("tempo")[0][0]
    check(d.notes(ch) == [(0, d.notes(ch)[0][1], 192), (192, d.notes(ch)[0][1], 192)], "音符を2つに分ける(%s)" % d.notes(ch))
    check("分けました" in err, "分けたことを警告する")


def test_loop():
    print("--- --loop")
    q = 480
    data = smf([track([tempo(0, 120), tempo(2 * q, 90)] + notes(0, [(0, 3 * q, 60)]) + notes(1, [(0, 5 * q, 48)]))])
    _rc, _err, text = convert(data, "--loop")
    d = compile_ok(text, "ループ")
    segnos = d.rows("segno")
    check(len(segnos) == 3 and all(t == 0 for _c, t in segnos), "使う全チャンネルの頭に L(%s)" % segnos)
    check(d.warning == "", "長さが揃うので警告が出ない(%s)" % d.warning)
    check(d.rows("tempo") == [(1, 0, 120), (1, 96, 90)], "ループの頭で最初のテンポへ戻す(%s)" % d.rows("tempo"))
    ends = {}
    for kind in ("note", "rest"):
        for r in d.rows(kind):
            ends[r[0]] = max(ends.get(r[0], 0), r[1] + r[-1])
    check(set(ends.values()) == {384}, "2小節ちょうどまで休符で埋める(%s)" % ends)


def test_long_rest_and_range():
    print("--- 長い休符 / 音域の外 / --bars")
    q = 480
    seq = [(0, q, 60), (40 * 4 * q, q, 130), (41 * 4 * q, q, 5)]
    data = smf([track([tempo(0, 120)] + notes(0, seq))])
    _rc, err, text = convert(data)
    d = compile_ok(text, "長い休符")
    check("[r1]" in text, "長い休符は [r1]n で書く")
    check(d.notes(0) == [(0, 60, 48), (7680, 118, 48), (7872, 17, 48)], "位置と、オクターブをずらした高さ(%s)" % d.notes(0))
    check("オクターブをずらしました" in err, "ずらしたことを警告する")

    seq = [(i * q, q, 60 + i % 12) for i in range(32)]
    data = smf([track([tempo(0, 120), tempo(12 * q, 80)] + notes(0, seq))])
    _rc, _err, text = convert(data, "--bars", "3-4")
    d = compile_ok(text, "--bars")
    check(d.notes(0)[0] == (0, 68, 48) and len(d.notes(0)) == 8, "3〜4小節目だけ(%s)" % d.notes(0)[:2])
    check(d.tempo == 120 and d.rows("tempo") == [(1, 192, 80)], "範囲の中のテンポの変化だけ(%s)" % d.rows("tempo"))


def test_fit():
    print("--- 6KiBを超える曲の切り詰め")
    q = 480
    s16 = q // 4
    seqs = []
    for c in range(4):
        seqs += notes(c, [(i * s16, s16, 48 + c * 7 + (i * 5) % 11) for i in range(1600)], vel=40 + c * 20)
    data = smf([track([tempo(0, 160)] + seqs)])
    _rc, err, text = convert(data)
    d = compile_ok(text, "切り詰め")
    check(d.size <= 6144, "演奏データが6KiBに収まる(%d)" % d.size)
    check("小節目までに切りました" in err, "切ったことを警告する")
    for line in text.splitlines():
        if len(line.encode()) > 512:
            check(False, "1行512バイトまで")
            break
    _rc, _err, text = convert(data, "--no-fit")
    d = Dump(text)
    check(not d.ok and "長すぎます" in d.message, "--no-fit なら読み取りが断る(%s)" % d.message)


def test_velocity_and_names():
    print("--- 音量とShift_JISのトラック名")
    seq = [(0, 480, 60), (480, 480, 62)]
    evs = notes(0, seq[:1], vel=127) + notes(0, seq[1:], vel=64)
    data = smf([track([name("ゲームの曲", "cp932"), tempo(0, 120), (0, b"\xB0\x07\x7F")] + evs)])
    _rc, _err, text = convert(data)
    d = compile_ok(text, "音量")
    check("#title ゲームの曲" in text, "Shift_JISの名前を読める")
    check([v for _c, _t, v in d.rows("volume", 0)] == [8], "ベロシティ127→15(既定)、64→8(%s)" % d.rows("volume", 0))
    _rc, _err, text = convert(data, "--no-velocity")
    d = compile_ok(text, "--no-velocity")
    check(len(d.rows("volume", 0)) == 1, "--no-velocity なら v は1回だけ")


def test_errors():
    print("--- 読めないファイル")
    rc, err, _ = convert(b"hello", expect_fail=True)
    check(rc == 1 and "SMF" in err, "MIDIでなければ断る")
    rc, err, _ = convert(smf([track([tempo(0, 120)])]), expect_fail=True)
    check(rc == 1 and "音符がありません" in err, "音符が無ければ断る")


def test_sample_mml_lengths():
    print("--- 長さの書き方")
    for n in range(1, 800):
        s = midi2mml.length_text(n)
        total = 0
        for piece in s.split("^"):
            dots = len(piece) - len(piece.rstrip("."))
            base = 192 // int(piece.rstrip("."))
            add = base
            for _ in range(dots):
                add //= 2
                base += add
            total += base
        if total != n:
            check(False, "長さ%d → %s" % (n, s))
            return
    check(True, "1〜799ティックの書き方が全部正しい")
    check(midi2mml.length_text(72) == "4." and midi2mml.length_text(60) == "4^16", "4. と 4^16")


def main():
    if not DUMP or not os.path.exists(DUMP):
        print("usage: midi2mml_test.py <mml_dump>")
        return 2
    for t in (test_sample_mml_lengths, test_simple_melody, test_running_status_and_zero_velocity,
              test_triplets_and_grid, test_chords_and_assign, test_drums, test_tempo_changes, test_loop,
              test_long_rest_and_range, test_fit, test_velocity_and_names, test_errors):
        t()
    print("")
    print("midi2mml_test: %d/%d 件OK" % (checks - failures, checks))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
