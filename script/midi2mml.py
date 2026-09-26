#!/usr/bin/env python3
"""MIDI(SMF)を pico-os MML(MUSIC_FORMAT.md)へ変換する。標準ライブラリだけで動く。

pico-os の音源は4チャンネル・1チャンネル1音なので、MIDIの全部の音は鳴らせない。
このスクリプトは「それらしく鳴る下書き」を作るところまでで、仕上げは人がMMLを直す前提
(MUSIC_FORMAT.md「なぜMMLか」)。何をどこへ割り当てたかは出力の注釈に残す。

使い方:
    python3 script/midi2mml.py song.mid -o pc/sdcard/music/song.mml
    python3 script/midi2mml.py song.mid --list             # 声部の一覧(番号は --pick で使う)
    python3 script/midi2mml.py song.mid --pick A=1,B=3+4,C=2,D=drums
    python3 script/midi2mml.py song.mid --loop --bars 1-32

変換の流れ:
  1. 音符を読み、時間を MML のティック(4分音符 = 48)へ直して格子へ揃える(--grid。既定は自動)
  2. MIDIのトラック×チャンネル(=パート)ごとに、和音を「声部」(同時に1音の線)へ分ける。
     和音のいちばん上の音が1本目の声部に入る
  3. 声部を選んで A〜D へ割り当てる(--pick。既定は鳴っている時間の長い順に選び、
     いちばん低い声部を三角波の C、打楽器(MIDIの10ch)をノイズの D にする)
  4. チャンネルごとに1音へ間引く(同じ時刻なら高い音、後から来た音が前の音を切る)
  5. MMLを書く。演奏データが6KiBを超えるなら、収まる小節までで切る(--no-fit で切らない)

- テンポは最初の値を #tempo に、途中の変化は t で書く(MMLの t は全チャンネル共通)。
  空いているチャンネルがあればそこへテンポだけを書き、無ければ音符の切れ目にあるチャンネルへ書く
- 音量はベロシティ×CC7(ボリューム)×CC11(エクスプレッション)から v0〜15 へ(--no-velocity で一定)
- 音の長さは格子に揃えた長さそのもの。q8(切らない)にして、音と音の隙間は休符で書く
- 拍子は最初の拍子記号だけを見る(小節線 | と改行の位置、--bars と切り詰めの単位にだけ使う)
"""

import argparse
import math
import os
import sys

TICKS_PER_QUARTER = 48
TICKS_PER_WHOLE = TICKS_PER_QUARTER * 4
MAX_DATA_BYTES = 6144           # 演奏データの置き場(MUSIC_FORMAT.md「クライアント側の制約」)
MAX_LINE_BYTES = 512            # 1行の上限
LINE_SOFT_LIMIT = 300           # これを超えたら次の音から改行する
BARS_PER_LINE = 4
MAX_TOKEN_TICKS = 65535         # 音符/休符1つの長さの上限(演奏データがu16)

WAVES = ("pulse12", "pulse25", "pulse50", "pulse75", "triangle", "saw", "noise", "noise_short")
CHANNELS = "ABCD"
NOTE_NAMES = ("c", "c+", "d", "d+", "e", "f", "f+", "g", "g+", "a", "a+", "b")
DRUM_CHANNEL = 9                # MIDIの10ch(0始まりで9)

# GMの音色の系統(--list の表示と、ベースを見つけるのに使う)
GM_FAMILIES = ("ピアノ", "鍵盤打楽器", "オルガン", "ギター", "ベース", "弦", "合奏", "金管",
               "木管(リード)", "木管(笛)", "シンセリード", "シンセパッド", "シンセ効果", "民族楽器",
               "打楽器", "効果音")


def warn(msg):
    print("midi2mml: " + msg, file=sys.stderr)


# ---------------------------------------------------------------- SMFの読み取り

class Note:
    __slots__ = ("start", "end", "pitch", "vel", "track", "chan", "program", "qs", "qe")

    def __init__(self, start, end, pitch, vel, track, chan, program):
        self.start, self.end = start, end          # MIDIのティック
        self.pitch, self.vel = pitch, vel          # vel は CC7/CC11 を掛けた後(0〜127の実数)
        self.track, self.chan, self.program = track, chan, program
        self.qs = self.qe = 0                      # MMLのティック(格子に揃えた後)


class Song:
    def __init__(self):
        self.division = 480
        self.notes = []
        self.tempos = []            # (MIDIのティック, µs/4分音符)
        self.time_sig = (4, 4)
        self.track_names = {}
        self.first_name = None      # 最初に出てきたトラック名(曲名の候補)


def read_vlq(data, pos):
    value = 0
    for _ in range(4):
        if pos >= len(data):
            raise ValueError("可変長の数値の途中でファイルが終わっています")
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, pos
    raise ValueError("可変長の数値が長すぎます")


def decode_text(raw):
    # 日本のMIDIはトラック名がShift_JISのことが多い
    for enc in ("utf-8", "cp932"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            pass
    return raw.decode("latin-1")


def read_smf(data):
    if data[:4] == b"RIFF" and data[8:12] == b"RMID":
        # RIFFで包まれたMIDI(.rmi)。中の data チャンクを取り出す
        pos = 12
        while pos + 8 <= len(data):
            size = int.from_bytes(data[pos + 4:pos + 8], "little")
            if data[pos:pos + 4] == b"data":
                data = data[pos + 8:pos + 8 + size]
                break
            pos += 8 + size + (size & 1)
    if data[:4] != b"MThd":
        raise ValueError("MIDIファイル(SMF)ではありません")
    hlen = int.from_bytes(data[4:8], "big")
    fmt = int.from_bytes(data[8:10], "big")
    ntrks = int.from_bytes(data[10:12], "big")
    division = int.from_bytes(data[12:14], "big")
    if fmt not in (0, 1):
        raise ValueError("SMFの形式%dには対応していません(0と1だけ)" % fmt)

    song = Song()
    if division & 0x8000:
        # SMPTEの時間。1秒 = fps×tpf ティックなので、テンポ120(4分音符0.5秒)とみなして直す
        fps = 256 - (division >> 8)
        tpf = division & 0xFF
        song.division = max(1, fps * tpf // 2)
        song.tempos.append((0, 500000))
    else:
        song.division = division or 480

    # (時刻, トラック, ファイル内の順番, イベント) を全トラックぶん集めて時刻順に処理する
    # (CC7等はチャンネルの状態で、別のトラックに書かれていることがあるため)
    events = []
    pos = 8 + hlen
    for track in range(ntrks):
        if pos + 8 > len(data):
            warn("トラックの数(%d)より先にファイルが終わっています" % ntrks)
            break
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError("トラック%dの見出しがありません" % (track + 1))
        tlen = int.from_bytes(data[pos + 4:pos + 8], "big")
        p, end = pos + 8, min(pos + 8 + tlen, len(data))
        pos += 8 + tlen
        t, status, seq = 0, 0, 0
        while p < end:
            delta, p = read_vlq(data, p)
            t += delta
            if p >= end:
                break
            b = data[p]
            if b == 0xFF:
                mtype = data[p + 1]
                mlen, p = read_vlq(data, p + 2)
                body = data[p:p + mlen]
                p += mlen
                if mtype == 0x2F:
                    break
                events.append((t, track, seq, ("meta", mtype, body)))
            elif b in (0xF0, 0xF7):
                slen, p = read_vlq(data, p + 1)
                p += slen
            else:
                if b & 0x80:
                    status = b
                    p += 1
                elif not status:
                    raise ValueError("トラック%dの先頭にステータスがありません" % (track + 1))
                kind = status & 0xF0
                n = 1 if kind in (0xC0, 0xD0) else 2
                args = data[p:p + n]
                p += n
                if len(args) < n:
                    break
                events.append((t, track, seq, ("ch", status, args)))
            seq += 1
        events.append((t, track, seq, ("end",)))

    events.sort(key=lambda e: (e[0], e[1], e[2]))

    cc7 = [127] * 16
    cc11 = [127] * 16
    program = [0] * 16
    on = {}         # (track, chan, pitch) -> [(開始, ベロシティ)]
    track_end = {}
    sig_seen = False

    def close(key, t):
        stack = on.get(key)
        if not stack:
            return
        start, vel, prog = stack.pop(0)
        if t > start:
            song.notes.append(Note(start, t, key[2], vel, key[0], key[1], prog))

    for t, track, _seq, ev in events:
        if ev[0] == "end":
            track_end[track] = t
        elif ev[0] == "meta":
            mtype, body = ev[1], ev[2]
            if mtype == 0x51 and len(body) >= 3:
                song.tempos.append((t, int.from_bytes(body[:3], "big")))
            elif mtype == 0x58 and len(body) >= 2 and not sig_seen:
                # 拍子は最初のものだけ使う(時刻順に処理しているので、最初に来たのがいちばん早い)
                if body[0] > 0 and body[1] <= 6:
                    song.time_sig = (body[0], 1 << body[1])
                sig_seen = True
            elif mtype == 0x03 and body:
                name = decode_text(body).strip()
                if name and track not in song.track_names:
                    song.track_names[track] = name
                    if song.first_name is None:
                        song.first_name = name
        else:
            status, args = ev[1], ev[2]
            kind, chan = status & 0xF0, status & 0x0F
            if kind == 0x90 and args[1] > 0:
                key = (track, chan, args[0])
                # 同じ音の鳴らし直しは、前の音をそこで切る
                if on.get(key):
                    close(key, t)
                vel = args[1] * cc7[chan] / 127.0 * cc11[chan] / 127.0
                on.setdefault(key, []).append((t, vel, program[chan]))
            elif kind == 0x80 or kind == 0x90:
                close((track, chan, args[0]), t)
            elif kind == 0xB0:
                if args[0] == 7:
                    cc7[chan] = args[1]
                elif args[0] == 11:
                    cc11[chan] = args[1]
            elif kind == 0xC0:
                program[chan] = args[0]

    # 離されずに終わった音はトラックの終わりで切る
    for key in list(on):
        while on.get(key):
            close(key, track_end.get(key[0], max((n.end for n in song.notes), default=0)))

    song.tempos.sort(key=lambda x: x[0])
    song.notes.sort(key=lambda n: (n.start, -n.pitch))
    return song


# ---------------------------------------------------------------- 時間を揃える

def to_mml_ticks(song, t):
    return t * TICKS_PER_QUARTER / song.division


def pick_grid(song):
    """音の出だしが(ほぼ)全部乗る、いちばん粗い格子を選ぶ。どれにも乗らなければ16分音符"""
    onsets = [to_mml_ticks(song, n.start) for n in song.notes]
    if not onsets:
        return 12
    for g in (12, 8, 6, 4, 3, 2, 1):
        hit = sum(1 for t in onsets if abs(t - round(t / g) * g) <= 0.25)
        if hit >= len(onsets) * 0.97:
            return g
    warn("音の出だしが格子に揃っていません(演奏を録ったMIDI?)。16分音符に揃えます(--grid で変えられます)")
    return 12


def quantize(song, grid):
    for n in song.notes:
        n.qs = int(round(to_mml_ticks(song, n.start) / grid)) * grid
        n.qe = int(round(to_mml_ticks(song, n.end) / grid)) * grid
        if n.qe <= n.qs:
            n.qe = n.qs + grid


def tempo_events(song, grid):
    """[(MMLのティック, bpm)]。1つ目は0ティック。範囲外は30〜300へ収める"""
    out = []
    clamped = False
    for t, us in song.tempos:
        bpm = int(round(60000000.0 / max(1, us)))
        if bpm < 30 or bpm > 300:
            clamped = True
            bpm = min(300, max(30, bpm))
        q = int(round(to_mml_ticks(song, t) / grid)) * grid
        if out and out[-1][0] == q:
            out[-1] = (q, bpm)
        elif not out or out[-1][1] != bpm:
            out.append((q, bpm))
    if not out or out[0][0] != 0:
        out.insert(0, (0, 120))
    if clamped:
        warn("テンポが30〜300の外にあったので、端へ寄せました")
    # 同じ値が続いたものは詰める
    merged = [out[0]]
    for t, bpm in out[1:]:
        if bpm != merged[-1][1]:
            merged.append((t, bpm))
    return merged


# ---------------------------------------------------------------- 声部へ分ける

class Line:
    """同時に1音の線。1つのパート(トラック×チャンネル)の和音を分けたうちの1本"""

    def __init__(self, track, chan, voice):
        self.track, self.chan, self.voice = track, chan, voice
        self.notes = []
        self.number = 0

    @property
    def is_drums(self):
        return self.chan == DRUM_CHANNEL

    def sounding(self):
        return sum(n.qe - n.qs for n in self.notes)

    def avg_pitch(self):
        total = self.sounding()
        if not total:
            return 0
        return sum(n.pitch * (n.qe - n.qs) for n in self.notes) / total

    def programs(self):
        return sorted(set(n.program for n in self.notes))


def split_lines(song, grid):
    parts = {}
    for n in song.notes:
        parts.setdefault((n.track, n.chan), []).append(n)

    lines = []
    tolerance = max(grid, 6)    # 前の音の終わりとの重なりがこれ以下なら、同じ声部で続ける(レガート)
    for (track, chan), notes in sorted(parts.items()):
        if chan == DRUM_CHANNEL:
            line = Line(track, chan, 0)
            line.notes = notes
            lines.append(line)
            continue
        notes = sorted(notes, key=lambda n: (n.qs, -n.pitch))
        voices = []
        seen = set()
        for n in notes:
            if (n.qs, n.pitch) in seen:
                continue
            seen.add((n.qs, n.pitch))
            for v in voices:
                last = v.notes[-1]
                if last.qe <= n.qs or (last.qs < n.qs and last.qe - n.qs <= tolerance):
                    last.qe = min(last.qe, n.qs) if last.qe > n.qs else last.qe
                    v.notes.append(n)
                    break
            else:
                v = Line(track, chan, len(voices))
                v.notes.append(n)
                voices.append(v)
        lines.extend(voices)
    return lines


def describe(song, line):
    name = song.track_names.get(line.track, "")
    head = "トラック%d ch%d" % (line.track + 1, line.chan + 1)
    if name:
        head += "「%s」" % name
    if line.is_drums:
        return head + " 打楽器"
    progs = line.programs()
    fam = "/".join(sorted(set(GM_FAMILIES[p // 8] for p in progs))) if progs else ""
    voice = "" if line.voice == 0 else " %d番目の声部" % (line.voice + 1)
    return head + voice + (" [" + fam + "]" if fam else "")


def number_lines(lines):
    """鳴っている時間の長い順に1から番号を振る(打楽器は drums で指すので除く)"""
    melodic = sorted((l for l in lines if not l.is_drums), key=lambda l: (-l.sounding(), l.track, l.chan, l.voice))
    for i, l in enumerate(melodic):
        l.number = i + 1
    return melodic


def pitch_name(p):
    return "%s%d" % (NOTE_NAMES[p % 12].replace("+", "#").upper(), p // 12 - 1)


def print_list(song, lines, grid, out=None):
    out = out or sys.stdout
    melodic = number_lines(lines)
    total = max((n.qe for n in song.notes), default=0) or 1
    print("格子: %s(%dティック)" % (grid_name(grid), grid), file=out)
    print("番号  音符  鳴っている割合  音域         パート", file=out)
    for l in melodic:
        lo = min(n.pitch for n in l.notes)
        hi = max(n.pitch for n in l.notes)
        print("%4d  %4d  %13d%%  %-5s〜%-5s  %s" % (
            l.number, len(l.notes), 100 * l.sounding() // total, pitch_name(lo), pitch_name(hi), describe(song, l)), file=out)
    drums = [l for l in lines if l.is_drums]
    if drums:
        count = sum(len(l.notes) for l in drums)
        print("drums %4d  (MIDIの10ch。ノイズで鳴らす)" % count, file=out)


def grid_name(g):
    d = TICKS_PER_WHOLE // g
    names = {12: "16分音符", 8: "3連16分音符", 6: "32分音符", 4: "3連32分音符", 3: "64分音符",
             2: "3連64分音符", 1: "192分音符", 16: "3連8分音符", 24: "8分音符", 48: "4分音符"}
    return names.get(g, "%d分音符" % d)


# ---------------------------------------------------------------- チャンネルへの割り当て

DEFAULT_WAVES = {"A": "pulse50", "B": "pulse25", "C": "triangle", "D": "pulse12"}


def auto_pick(lines, use_drums):
    """{チャンネル: [声部]} と 打楽器を載せるチャンネル(無ければNone)"""
    melodic = number_lines(lines)
    has_drums = use_drums and any(l.is_drums for l in lines)
    slots = 3 if has_drums else 4

    # 2本目以降の声部(和音の内声)は、別のパートの主旋律より後回しにする
    ranked = sorted(melodic, key=lambda l: -(l.sounding() * (1.0 if l.voice == 0 else 0.5)))
    chosen = ranked[:slots]

    # ベースが選ばれていなければ、目立つベースと入れ替える
    def is_bass(l):
        return l.avg_pitch() < 52 or any(32 <= p < 40 for p in l.programs())
    if chosen and len(chosen) == slots and not any(is_bass(l) for l in chosen):
        top = chosen[0].sounding()
        for l in ranked[slots:]:
            if is_bass(l) and l.sounding() >= top * 0.2:
                chosen[-1] = l
                break

    chosen.sort(key=lambda l: -l.avg_pitch())
    assign = {}
    free = [c for c in CHANNELS if not (has_drums and c == "D")]
    if len(chosen) >= 2 and chosen[-1].avg_pitch() < 60:
        assign["C"] = [chosen.pop()]
        free.remove("C")
    for l in chosen:
        assign[free.pop(0)] = [l]
    return assign, ("D" if has_drums else None)


def parse_pick(text, lines):
    melodic = number_lines(lines)
    by_number = {l.number: l for l in melodic}
    assign, drums_at = {}, None
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" not in item:
            raise ValueError("--pick は A=1,B=2+3,D=drums の形で書いてください(%s)" % item)
        ch, rhs = [s.strip() for s in item.split("=", 1)]
        ch = ch.upper()
        if ch not in CHANNELS:
            raise ValueError("チャンネルは A〜D です(%s)" % ch)
        if ch in assign or ch == drums_at:
            raise ValueError("チャンネル %s が2回出てきます" % ch)
        if rhs.lower() == "drums":
            if not any(l.is_drums for l in lines):
                raise ValueError("このMIDIには打楽器(10ch)がありません")
            drums_at = ch
            continue
        picked = []
        for num in rhs.split("+"):
            try:
                n = int(num)
            except ValueError:
                raise ValueError("声部の番号が読めません(%s)。番号は --list で見られます" % num)
            if n not in by_number:
                raise ValueError("声部%dはありません(1〜%d)。番号は --list で見られます" % (n, len(melodic)))
            picked.append(by_number[n])
        assign[ch] = picked
    return assign, drums_at


# ---------------------------------------------------------------- 1チャンネルぶんの出来事

class Ev:
    """チャンネルに書く出来事。kind は note / tempo"""
    __slots__ = ("kind", "t", "dur", "pitch", "state", "bpm")

    def __init__(self, kind, t, dur=0, pitch=0, state=None, bpm=0):
        self.kind, self.t, self.dur, self.pitch, self.state, self.bpm = kind, t, dur, pitch, state, bpm


def vel_to_volume(vel):
    return max(1, min(15, int(round(vel * 15 / 127.0))))


def fold_pitch(p, counter):
    """o0〜o8(ノート番号12〜119)へオクターブ単位で収める"""
    q = p
    while q < 12:
        q += 12
    while q > 119:
        q -= 12
    if q != p:
        counter[0] += 1
    return q


def melodic_events(line_list, wave, transpose, use_velocity, folded):
    notes = sorted((n for l in line_list for n in l.notes), key=lambda n: (n.qs, -n.pitch))
    mono = []
    for n in notes:
        if mono and mono[-1][0] == n.qs:
            continue                        # 同じ時刻なら高い音(先に並んでいる)
        if mono and mono[-1][1] > n.qs:
            mono[-1][1] = n.qs              # 後から来た音が前の音を切る
        mono.append([n.qs, n.qe, n.pitch, n.vel])
    if not use_velocity and mono:
        avg = sum(m[3] for m in mono) / len(mono)
        for m in mono:
            m[3] = avg
    return [Ev("note", s, e - s, fold_pitch(p + transpose, folded),
               (wave, vel_to_volume(v), 0)) for s, e, p, v in mono]


# 打楽器の鳴らし方: (優先度, 波形, ノート番号, 音量, 減衰)。ノイズは高いほど細かい「ザー」
DRUM_KICK = (6, "noise", 72, 14, -2)          # o5 c
DRUM_SNARE = (5, "noise", 96, 12, -2)         # o7 c
DRUM_TOM_LOW = (4, "noise", 79, 12, -2)
DRUM_TOM_MID = (4, "noise", 84, 12, -2)
DRUM_TOM_HIGH = (4, "noise", 89, 12, -2)
DRUM_CRASH = (3, "noise", 108, 9, -5)         # o8 c
DRUM_OPEN_HAT = (2, "noise_short", 108, 6, -3)
DRUM_RIDE = (2, "noise_short", 103, 6, -4)
DRUM_CLOSED_HAT = (1, "noise_short", 108, 6, -1)
DRUM_OTHER = (0, "noise_short", 101, 5, -1)

GM_DRUMS = {35: DRUM_KICK, 36: DRUM_KICK,
            37: DRUM_SNARE, 38: DRUM_SNARE, 39: DRUM_SNARE, 40: DRUM_SNARE,
            41: DRUM_TOM_LOW, 43: DRUM_TOM_LOW, 45: DRUM_TOM_MID, 47: DRUM_TOM_MID,
            48: DRUM_TOM_HIGH, 50: DRUM_TOM_HIGH,
            42: DRUM_CLOSED_HAT, 44: DRUM_CLOSED_HAT, 46: DRUM_OPEN_HAT,
            49: DRUM_CRASH, 52: DRUM_CRASH, 55: DRUM_CRASH, 57: DRUM_CRASH,
            51: DRUM_RIDE, 53: DRUM_RIDE, 59: DRUM_RIDE}


def drum_events(lines, grid):
    hits = {}
    for l in lines:
        if not l.is_drums:
            continue
        for n in l.notes:
            d = GM_DRUMS.get(n.pitch, DRUM_OTHER)
            if n.qs not in hits or d[0] > hits[n.qs][0]:
                hits[n.qs] = d
    times = sorted(hits)
    out = []
    for i, t in enumerate(times):
        _prio, wave, pitch, vol, env = hits[t]
        # 次の音まで伸ばす(減衰で自然に消える)。最後の1つは4分音符
        dur = (times[i + 1] - t) if i + 1 < len(times) else TICKS_PER_QUARTER
        out.append(Ev("note", t, max(grid, dur), pitch, (wave, vol, env)))
    return out


def covers(evs, t):
    """t がそのチャンネルの音符の途中(始まりでも終わりでもない)なら、その音符"""
    for e in evs:
        if e.kind == "note" and e.t < t < e.t + e.dur:
            return e
        if e.t > t:
            break
    return None


def place_tempos(chans, tempos, loop):
    """テンポの変化(2つ目以降)をどれかのチャンネルへ書き込む。{チャンネル: [Ev]} を書き換える"""
    changes = list(tempos[1:])
    if loop and changes:
        changes.insert(0, tempos[0])    # ループで頭へ戻ったら最初のテンポへ戻す
    if not changes:
        return
    free = [c for c in CHANNELS if c not in chans]
    if free:
        chans[free[0]] = [Ev("tempo", t, bpm=b) for t, b in changes]
        return
    split = 0
    for t, bpm in changes:
        host = None
        for c in sorted(chans):
            if covers(chans[c], t) is None:
                host = c
                break
        if host is None:
            host = sorted(chans)[0]
            e = covers(chans[host], t)
            # 音符をテンポの位置で2つに分ける(鳴らし直しになる)
            rest = Ev("note", t, e.t + e.dur - t, e.pitch, e.state)
            e.dur = t - e.t
            chans[host].append(rest)
            split += 1
        chans[host].append(Ev("tempo", t, bpm=bpm))
        chans[host].sort(key=lambda e: (e.t, 0 if e.kind == "tempo" else 1))
    if split:
        warn("テンポの変わり目が音符の途中だったので、%d個の音符を分けました" % split)


# ---------------------------------------------------------------- MMLを書く

def _length_pieces():
    pieces = {}
    for d in (1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 192):
        base = TICKS_PER_WHOLE // d
        t, add, s = base, base, str(d)
        if t not in pieces or len(s) < len(pieces[t]):
            pieces[t] = s
        # 付点は2つまで(6... のような書き方は正しくても読めない)
        while add % 2 == 0 and len(s) - len(str(d)) < 2:
            add //= 2
            t += add
            s += "."
            if t not in pieces or len(s) < len(pieces[t]):
                pieces[t] = s
    return sorted(pieces.items())


LENGTH_PIECES = _length_pieces()
_len_memo = {0: ""}


def length_text(n):
    """n ティックを「4」「4.」「2^16」のような長さの書き方にする(いちばん短いもの)"""
    if n in _len_memo:
        return _len_memo[n]
    # 小さい方から順に埋める(再帰の深さを避ける)
    start = max(k for k in _len_memo if k <= n)
    for m in range(start + 1, n + 1):
        best = None
        # 長い方から試し、同じ文字数なら長い音を先に書く(4^16 のように)
        for t, s in reversed(LENGTH_PIECES):
            if t > m:
                continue
            rest = _len_memo[m - t]
            cand = s if not rest else s + "^" + rest
            if best is None or len(cand) < len(best):
                best = cand
        _len_memo[m] = best
    return _len_memo[n]


def length_with_default(n, deflen):
    """音符の後ろに書く長さ。l の長さと同じなら省く"""
    full = length_text(n)
    if n == deflen:
        return ""
    if n > deflen:
        alt = "^" + length_text(n - deflen)
        if len(alt) < len(full):
            return alt
    return full


class Writer:
    """1チャンネルぶんのMMLを書きながら、演奏データのバイト数を数える(Music_Data.hpp)"""

    def __init__(self, ch, bar, deflen):
        self.ch, self.bar, self.deflen = ch, bar, deflen
        self.lines = []
        self.cur = []
        self.bytes = 1              # END
        self.pos = 0
        self.bars_in_line = 0

    def token(self, text, nbytes, at_note=False):
        if at_note and self.pos > 0 and self.pos % self.bar == 0:
            self.cur.append("|")
            self.bars_in_line += 1
            if self.bars_in_line >= BARS_PER_LINE:
                self.newline()
        if at_note and self.cur and len(" ".join(self.cur)) + len(text) > LINE_SOFT_LIMIT:
            self.newline()
        self.cur.append(text)
        self.bytes += nbytes

    def newline(self):
        while self.cur and self.cur[-1] == "|":
            self.cur.pop()
        if self.cur:
            self.lines.append(self.ch + "  " + " ".join(self.cur))
        self.cur = []
        self.bars_in_line = 0

    def rest(self, n):
        while n > 0:
            wholes = n // TICKS_PER_WHOLE
            if wholes >= 4:
                # 長い休符は [r1]n(6バイト)。r1^1^1… と書くより短く、行も長くならない
                w = min(wholes, 255)
                self.token("[r1]%d" % w, 6, at_note=True)
                step = w * TICKS_PER_WHOLE
            else:
                step = min(n, MAX_TOKEN_TICKS)
                self.token("r" + length_with_default(step, self.deflen), 3, at_note=True)
            self.pos += step
            n -= step


def build_channel(ch, evs, wave, bar, loop, end_tick, deflen):
    w = Writer(ch, bar, deflen)
    head = ["@" + wave, "q8"]
    w.bytes += 2 + 2
    if deflen != TICKS_PER_QUARTER:
        head.append("l" + length_text(deflen))
    octave = 4
    vol, env, cur_wave = 15, 0, wave
    if loop:
        head.append("L")
        w.bytes += 1
    w.cur = head
    for e in evs:
        if e.t > w.pos:
            w.rest(e.t - w.pos)
        if e.kind == "tempo":
            w.token("t%d" % e.bpm, 3)
            continue
        e_wave, e_vol, e_env = e.state
        pre = []
        if e_wave != cur_wave:
            pre.append("@" + e_wave)
            w.bytes += 2
            cur_wave = e_wave
        if e_vol != vol:
            pre.append("v%d" % e_vol)
            w.bytes += 2
            vol = e_vol
        if e_env != env:
            pre.append("E%d" % e_env)
            w.bytes += 2
            env = e_env
        o = e.pitch // 12 - 1
        if o == octave + 1:
            pre.append(">")
        elif o == octave - 1:
            pre.append("<")
        elif o != octave:
            pre.append("o%d" % o)
        octave = o
        dur = min(e.dur, MAX_TOKEN_TICKS)
        text = " ".join(pre + [NOTE_NAMES[e.pitch % 12] + length_with_default(dur, deflen)])
        w.token(text, 4, at_note=True)
        w.pos += dur
    if loop and end_tick > w.pos:
        w.rest(end_tick - w.pos)
    w.newline()
    return w


def common_length(evs):
    counts = {}
    for e in evs:
        if e.kind == "note":
            counts[e.dur] = counts.get(e.dur, 0) + 1
    if not counts:
        return TICKS_PER_QUARTER
    best = max(counts.items(), key=lambda kv: (kv[1], -len(length_text(kv[0]))))[0]
    # 1つの長さで書けるものだけ(l には ^ を書けない)
    return best if "^" not in length_text(best) else TICKS_PER_QUARTER


# ---------------------------------------------------------------- まとめ

def cut_range(chans, start, stop):
    """[start, stop) の範囲だけを残し、start を0へずらす"""
    out = {}
    for c, evs in chans.items():
        kept = []
        for e in evs:
            if e.kind == "tempo":
                if start <= e.t < stop:
                    kept.append(Ev("tempo", e.t - start, bpm=e.bpm))
                continue
            s, t = max(e.t, start), min(e.t + e.dur, stop)
            if t > s and e.t >= start:
                kept.append(Ev("note", s - start, t - s, e.pitch, e.state))
        out[c] = kept
    return out


def tempo_at(tempos, t):
    bpm = tempos[0][1]
    for tt, b in tempos:
        if tt <= t:
            bpm = b
    return bpm


def render(chans, waves, bar, loop, deflens):
    end_tick = 0
    for evs in chans.values():
        for e in evs:
            end_tick = max(end_tick, e.t + (e.dur if e.kind == "note" else 0))
    end_tick = -(-end_tick // bar) * bar if end_tick else bar
    writers = {}
    for c in CHANNELS:
        if c in chans:
            writers[c] = build_channel(c, chans[c], waves[c], bar, loop, end_tick, deflens[c])
    size = 16 + sum(w.bytes for w in writers.values())
    return writers, size, end_tick


def convert(song, args, out_err=None):
    out_err = out_err or sys.stderr
    grid = args.grid_ticks or pick_grid(song)
    quantize(song, grid)
    tempos = tempo_events(song, grid)
    lines = split_lines(song, grid)
    if not lines:
        raise ValueError("音符がありません")

    if args.pick:
        assign, drums_at = parse_pick(args.pick, lines)
    else:
        assign, drums_at = auto_pick(lines, not args.no_drums)

    waves = dict(DEFAULT_WAVES)
    for item in (args.wave or "").split(","):
        if item.strip():
            ch, name = [s.strip() for s in item.split("=", 1)]
            if ch.upper() not in CHANNELS or name not in WAVES:
                raise ValueError("--wave は A=saw のように書いてください(波形: %s)" % " ".join(WAVES))
            waves[ch.upper()] = name

    folded = [0]
    chans = {}
    notes_used = 0
    for c, picked in assign.items():
        chans[c] = melodic_events(picked, waves[c], args.transpose, not args.no_velocity, folded)
        notes_used += len(chans[c])
    if drums_at:
        chans[drums_at] = drum_events(lines, grid)
        waves[drums_at] = "noise"
    if folded[0]:
        warn("%d個の音が o0〜o8 の外だったので、オクターブをずらしました" % folded[0])

    bar = TICKS_PER_WHOLE * song.time_sig[0] // song.time_sig[1]

    if args.bars:
        a, _, b = args.bars.partition("-")
        first = max(1, int(a))
        last = int(b) if b else first
        if last < first:
            raise ValueError("--bars は 1-16 のように書いてください")
        start = (first - 1) * bar
        chans = cut_range(chans, start, last * bar)
        tempos = [(0, tempo_at(tempos, start))] + [(t - start, bpm) for t, bpm in tempos if start < t < last * bar]

    place_tempos(chans, tempos, args.loop)
    for evs in chans.values():
        evs.sort(key=lambda e: (e.t, 0 if e.kind == "tempo" else 1))
    deflens = {c: common_length(evs) for c, evs in chans.items()}

    writers, size, end_tick = render(chans, waves, bar, args.loop, deflens)
    if size > MAX_DATA_BYTES and not args.no_fit:
        # 収まる小節数を二分探索で探して切る
        lo, hi = 1, end_tick // bar
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            trial = cut_range(chans, 0, mid * bar)
            w2, s2, _ = render(trial, waves, bar, args.loop, deflens)
            if s2 <= MAX_DATA_BYTES:
                best = (mid, trial)
                lo = mid + 1
            else:
                hi = mid - 1
        if best is None:
            raise ValueError("1小節でも演奏データが%dバイトに収まりません" % MAX_DATA_BYTES)
        warn("演奏データが%dバイトで上限(%d)を超えるので、%d小節目までに切りました"
             "(--bars で範囲を選ぶ、--no-velocity で音量の変化を省く、等で延ばせます)" % (size, MAX_DATA_BYTES, best[0]))
        chans = best[1]
        writers, size, end_tick = render(chans, waves, bar, args.loop, deflens)

    # ---- 書き出し
    title = args.title or song.first_name or os.path.splitext(os.path.basename(args.input))[0]
    title = title.replace(";", " ").replace("\n", " ").replace("\r", " ").strip() or "untitled"
    out = []
    out.append("; %s から script/midi2mml.py で変換" % os.path.basename(args.input))
    out.append("; 格子: %s / 拍子: %d/%d / 演奏データ: 約%dバイト(上限%d)"
               % (grid_name(grid), song.time_sig[0], song.time_sig[1], size, MAX_DATA_BYTES))
    for c in CHANNELS:
        if c not in writers:
            continue
        if c == drums_at:
            out.append("; %s: 打楽器(MIDIの10ch)" % c)
        elif c in assign:
            out.append("; %s: %s" % (c, " + ".join("声部%d %s" % (l.number, describe(song, l)) for l in assign[c])))
        else:
            out.append("; %s: テンポの変化だけ" % c)
    out.append("#title %s" % title)
    if args.composer:
        out.append("#composer %s" % args.composer.replace(";", " "))
    out.append("#tempo %d" % tempos[0][1])
    for c in CHANNELS:
        if c in writers:
            out.append("")
            out.extend(writers[c].lines)

    for line in out:
        if len(line.encode("utf-8")) > MAX_LINE_BYTES:
            raise ValueError("1行が%dバイトを超えました(変換の不具合です)" % MAX_LINE_BYTES)

    total_notes = sum(1 for n in song.notes if n.chan != DRUM_CHANNEL)
    dropped = total_notes - notes_used
    print("midi2mml: %s → %sチャンネル、演奏データ約%dバイト" % (
        os.path.basename(args.input), "".join(sorted(writers)), size), file=out_err)
    if dropped > 0:
        print("midi2mml: 4音に収めるため、打楽器以外の音符%d個のうち%d個を使いませんでした(--list と --pick で選び直せます)"
              % (total_notes, dropped), file=out_err)
    return "\n".join(out) + "\n", size


def main(argv=None):
    ap = argparse.ArgumentParser(description="MIDI(SMF)を pico-os MML(MUSIC_FORMAT.md)へ変換する")
    ap.add_argument("input", help="MIDIファイル(.mid)")
    ap.add_argument("-o", "--output", help="書き出す .mml(省けば標準出力)")
    ap.add_argument("--list", action="store_true", help="声部の一覧を出して終わる(番号は --pick で使う)")
    ap.add_argument("--pick", help="割り当て。例: A=1,B=3+4,C=2,D=drums(+ でつなぐと1チャンネルへ重ねる)")
    ap.add_argument("--grid", type=int, default=0,
                    help="揃える格子(N分音符。16=16分、24=3連16分、32=32分)。既定は自動")
    ap.add_argument("--transpose", type=int, default=0, help="半音単位で移調する(打楽器以外)")
    ap.add_argument("--wave", help="波形を変える。例: A=pulse12,C=saw")
    ap.add_argument("--loop", action="store_true", help="全チャンネルの頭に L を付けて繰り返す")
    ap.add_argument("--bars", help="使う小節の範囲。例: 1-32、9-16")
    ap.add_argument("--no-velocity", action="store_true", help="音量を一定にする(演奏データが小さくなる)")
    ap.add_argument("--no-drums", action="store_true", help="打楽器(10ch)を使わない")
    ap.add_argument("--no-fit", action="store_true", help="演奏データが6KiBを超えても切り詰めない")
    ap.add_argument("--title", help="曲名(既定は最初のトラック名かファイル名)")
    ap.add_argument("--composer", help="作者")
    args = ap.parse_args(argv)

    args.grid_ticks = 0
    if args.grid:
        if args.grid <= 0 or TICKS_PER_WHOLE % args.grid:
            ap.error("--grid は 192 を割り切る数です(4 8 16 32 64、3連は 12 24 48 96)")
        args.grid_ticks = TICKS_PER_WHOLE // args.grid

    try:
        with open(args.input, "rb") as f:
            song = read_smf(f.read())
        if args.list:
            grid = args.grid_ticks or pick_grid(song)
            quantize(song, grid)
            print_list(song, split_lines(song, grid), grid)
            return 0
        text, _size = convert(song, args)
    except (ValueError, OSError) as e:
        warn(str(e))
        return 1

    if args.output:
        with open(args.output, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
