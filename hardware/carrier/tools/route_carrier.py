#!/usr/bin/env python3
"""キャリアボードの信号を配線する(格子の上の迷路探索。KiCad の Python だけで動く)。

gen_carrier.py が作った基板(配線なし)を読み、全ネットを両面で配線して GND のベタを埋め直す。
余裕を持たせるため、線は信号 0.4mm・電源 1.0mm、間隔 0.3mm、縁から 0.5mm、ビアは 0.8/0.4mm。

  python3 hardware/carrier/tools/route_carrier.py

**KiCad で手直しを始めたら回さないこと**(配線が全部引き直しになる)。

やり方:
- 0.5mm の格子の点を「表/裏」の2層に持つ。動きは上下左右と、同じ点での層の移り(ビア)だけ。
- 置いた銅(端子・線・ビア)ごとに、「この半分幅の線/ビアの中心を置けない点」を3種類の地図に書く
  (信号の線・電源の線・ビア)。地図の値はその銅のネット(他のネットだけを妨げる)。
- 複数の端子を持つネットは、できた木から一番近い未接続の端子へ順に伸ばす(A*、曲がりと層の移りに罰)。
- つながらなかったネットがあれば、それを先頭に回して最初から引き直す(数回)。
"""
import heapq
import math
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(OUT, "pico-os-carrier.kicad_pcb")
sys.path.insert(0, HERE)
from gen_carrier import BOARD_W, BOARD_H, NOTCH, PICO_CY  # noqa: E402

STEP = 0.5            # 格子の間隔(mm)
CLEAR = 0.3           # 銅どうしの間隔
EDGE = 0.5            # 縁からの間隔
W_SIG = 0.4           # 信号の線
W_PWR = 1.0           # 電源の線
VIA_D, VIA_DRILL = 0.8, 0.4
POWER = {"/GND", "/VSYS", "/VBUS", "/+3V3", "/LCD_VCC", "/PCM_VCC", "/GROVE_VCC"}
COST_STEP, COST_BEND, COST_VIA = 1.0, 2.0, 12.0
# 表は横、裏は縦を主に使う(向きに合わない動きは高くする)。2層の基板でよくあるやり方
COST_WRONG_DIR = 1.5
# アンテナの横(切り欠きの上下)は通れるが、なるべく避ける
ANT_AREA = (NOTCH[0] - 1.0, PICO_CY - 13.0, BOARD_W, BOARD_H)
COST_ANT = 6.0

NX = int(round(BOARD_W / STEP)) + 1
NY = int(round(BOARD_H / STEP)) + 1
LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)
# 地図の種類: 置こうとする物の「半分の大きさ」
KINDS = {"sig": W_SIG / 2, "pwr": W_PWR / 2, "via": VIA_D / 2}

mm = pcbnew.FromMM


def to_mm(v):
    return pcbnew.ToMM(v)


class Grid:
    def __init__(self):
        # maps[kind][layer] = 1次元の配列。0=空き、>0=そのネットだけ通れる、-1=誰も通れない
        self.maps = {k: [[0] * (NX * NY) for _ in LAYERS] for k in KINDS}

    def idx(self, ix, iy):
        return iy * NX + ix

    def mark(self, kind, li, ix, iy, net):
        m = self.maps[kind][li]
        i = self.idx(ix, iy)
        v = m[i]
        if v == 0:
            m[i] = net
        elif v != net:
            m[i] = -1

    def free(self, kind, li, ix, iy, net):
        v = self.maps[kind][li][self.idx(ix, iy)]
        return v == 0 or v == net


def dist_point_rect(px, py, x1, y1, x2, y2):
    dx = max(x1 - px, 0, px - x2)
    dy = max(y1 - py, 0, py - y2)
    return math.hypot(dx, dy)


def dist_point_seg(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    L = vx * vx + vy * vy
    t = 0 if L == 0 else max(0, min(1, ((px - ax) * vx + (py - ay) * vy) / L))
    return math.hypot(px - (ax + t * vx), py - (ay + t * vy))


def add_obstacle(grid, layers_i, net, dist_fn, bbox, half):
    """dist_fn(x,y) が (half + CLEAR + 置く物の半分) より近い点を、種類ごとに塞ぐ"""
    x1, y1, x2, y2 = bbox
    for kind, h in KINDS.items():
        r = half + CLEAR + h
        ix1 = max(0, int(math.floor((x1 - r) / STEP)))
        ix2 = min(NX - 1, int(math.ceil((x2 + r) / STEP)))
        iy1 = max(0, int(math.floor((y1 - r) / STEP)))
        iy2 = min(NY - 1, int(math.ceil((y2 + r) / STEP)))
        for iy in range(iy1, iy2 + 1):
            y = iy * STEP
            for ix in range(ix1, ix2 + 1):
                if dist_fn(ix * STEP, iy * STEP) < r - 1e-6:
                    for li in layers_i:
                        grid.mark(kind, li, ix, iy, net)


def board_outline():
    nx, ny1, ny2 = NOTCH
    return [(0, 0), (BOARD_W, 0), (BOARD_W, ny1), (nx, ny1), (nx, ny2), (BOARD_W, ny2),
            (BOARD_W, BOARD_H), (0, BOARD_H)]


def inside_board(x, y, margin):
    if x < margin or y < margin or x > BOARD_W - margin or y > BOARD_H - margin:
        return False
    nx, ny1, ny2 = NOTCH
    # 切り欠き(x >= nx, ny1 < y < ny2)から margin 以上離れていること
    if dist_point_rect(x, y, nx, ny1, BOARD_W + 10, ny2) < margin:
        return False
    return True


class Router:
    def __init__(self, board):
        self.board = board
        self.grid = Grid()
        self.netcode = {}
        self.pads = []      # (net名, pad, 層の番号の組, 形)
        self.tracks = []    # (net名, 層の番号, (x1,y1), (x2,y2), 幅)
        self.vias = []      # (net名, (x,y))
        self.owned = {}     # net名 -> その木の点の集合 {(li, ix, iy)}
        self.small_terms = {}  # 小さい端子の代わりの点 -> 端子の中心
        for f in board.GetFootprints():
            for p in f.Pads():
                self.pads.append(p)

    def code(self, name):
        if name not in self.netcode:
            self.netcode[name] = len(self.netcode) + 1
        return self.netcode[name]

    @staticmethod
    def pad_layers(p):
        return tuple(i for i, L in enumerate(LAYERS) if p.IsOnLayer(L))

    @staticmethod
    def pad_geom(p):
        bb = p.GetBoundingBox()
        x1, y1 = to_mm(bb.GetLeft()), to_mm(bb.GetTop())
        x2, y2 = to_mm(bb.GetRight()), to_mm(bb.GetBottom())
        cx, cy = to_mm(p.GetPosition().x), to_mm(p.GetPosition().y)
        circ = p.GetShape() == pcbnew.PAD_SHAPE_CIRCLE
        return cx, cy, x1, y1, x2, y2, circ

    def setup_static(self):
        g = self.grid
        # 基板の外と縁の近く
        for kind, h in KINDS.items():
            for iy in range(NY):
                for ix in range(NX):
                    if not inside_board(ix * STEP, iy * STEP, EDGE + h):
                        for li in range(len(LAYERS)):
                            g.maps[kind][li][g.idx(ix, iy)] = -1
        # 端子(穴だけの物も含む)
        for p in self.pads:
            net = p.GetNetname()
            code = self.code(net) if net else -1
            cx, cy, x1, y1, x2, y2, circ = self.pad_geom(p)
            if circ:
                r = (x2 - x1) / 2
                fn = (lambda px, py, cx=cx, cy=cy, r=r: max(0.0, math.hypot(px - cx, py - cy) - r))
            else:
                fn = (lambda px, py, a=(x1, y1, x2, y2): dist_point_rect(px, py, *a))
            layers_i = self.pad_layers(p) or (0, 1)
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                layers_i = (0, 1)
                code = -1
            add_obstacle(g, layers_i, code, fn, (x1, y1, x2, y2), 0.0)

    def terminals(self, p, net):
        """端子の銅の中にある格子の点(この点から線を出せばつながる)"""
        cx, cy, x1, y1, x2, y2, circ = self.pad_geom(p)
        out = []
        for li in self.pad_layers(p):
            for iy in range(int(math.floor(y1 / STEP)), int(math.ceil(y2 / STEP)) + 1):
                for ix in range(int(math.floor(x1 / STEP)), int(math.ceil(x2 / STEP)) + 1):
                    x, y = ix * STEP, iy * STEP
                    if circ:
                        ok = math.hypot(x - cx, y - cy) <= (x2 - x1) / 2 - 0.15
                    else:
                        ok = x1 + 0.15 <= x <= x2 - 0.15 and y1 + 0.15 <= y <= y2 - 0.15
                    if ok and 0 <= ix < NX and 0 <= iy < NY:
                        out.append((li, ix, iy))
        if not out:
            # 小さい端子: 一番近い点(短い線で端子の中心へつなぐ)
            ix, iy = int(round(cx / STEP)), int(round(cy / STEP))
            out = [(li, ix, iy) for li in self.pad_layers(p)]
            for n in out:
                self.small_terms[n] = (cx, cy)
        return out

    def kind_for(self, net, width):
        return "pwr" if width > W_SIG + 1e-6 else "sig"

    def search(self, net, code, sources, targets, kind):
        g = self.grid
        tset = set(targets)
        # 目標までの見積もり(一番近い目標へのマンハッタン距離)
        tx = [t[1] for t in targets]
        ty = [t[2] for t in targets]
        txmin, txmax, tymin, tymax = min(tx), max(tx), min(ty), max(ty)

        def h(ix, iy):
            dx = max(txmin - ix, 0, ix - txmax)
            dy = max(tymin - iy, 0, iy - tymax)
            return (dx + dy) * COST_STEP

        best = {}
        heap = []
        for s in sources:
            st = (s[0], s[1], s[2], -1)
            best[st] = 0.0
            heapq.heappush(heap, (h(s[1], s[2]), 0.0, st, None))
        parent = {}
        dirs = ((1, 0), (-1, 0), (0, 1), (0, -1))
        ax1, ay1, ax2, ay2 = ANT_AREA
        while heap:
            f, gcost, st, par = heapq.heappop(heap)
            if best.get(st, 1e18) < gcost - 1e-9:
                continue
            li, ix, iy, d = st
            if (li, ix, iy) in tset:
                # 道を戻す
                path = [(li, ix, iy)]
                cur = st
                while cur in parent:
                    cur = parent[cur]
                    path.append((cur[0], cur[1], cur[2]))
                return list(reversed(path))
            # 平面の動き
            for di, (dx, dy) in enumerate(dirs):
                nx_, ny_ = ix + dx, iy + dy
                if not (0 <= nx_ < NX and 0 <= ny_ < NY):
                    continue
                if not g.free(kind, li, nx_, ny_, code):
                    continue
                c = COST_STEP + (COST_BEND if d not in (-1, di) else 0.0)
                if (li == 0) != (dy == 0):
                    c += COST_WRONG_DIR
                if ax1 <= nx_ * STEP <= ax2 and ay1 <= ny_ * STEP <= ay2:
                    c += COST_ANT
                ns = (li, nx_, ny_, di)
                ng = gcost + c
                if ng < best.get(ns, 1e18) - 1e-9:
                    best[ns] = ng
                    parent[ns] = st
                    heapq.heappush(heap, (ng + h(nx_, ny_), ng, ns, st))
            # 層の移り(ビア)
            ol = 1 - li
            if g.free("via", 0, ix, iy, code) and g.free("via", 1, ix, iy, code) and \
                    g.free(kind, ol, ix, iy, code):
                ns = (ol, ix, iy, -1)
                ng = gcost + COST_VIA
                if ng < best.get(ns, 1e18) - 1e-9:
                    best[ns] = ng
                    parent[ns] = st
                    heapq.heappush(heap, (ng + h(ix, iy), ng, ns, st))
        return None

    def commit(self, net, code, path, width, kind):
        """見つけた道を線とビアにして、地図を塞ぐ"""
        g = self.grid
        half = width / 2
        own = self.owned.setdefault(net, set())
        # 層ごとの連続した区間を、まっすぐな線にまとめる
        segs = []
        nvias = len(self.vias)
        start = path[0]
        prev = path[0]
        prev_dir = None
        for node in path[1:]:
            if node[0] != prev[0]:
                if (start[1], start[2]) != (prev[1], prev[2]):
                    segs.append((prev[0], start, prev))
                self.vias.append((net, (prev[1] * STEP, prev[2] * STEP)))
                start = node
                prev_dir = None
            else:
                d = (node[1] - prev[1], node[2] - prev[2])
                if prev_dir is not None and d != prev_dir:
                    segs.append((prev[0], start, prev))
                    start = prev
                prev_dir = d
            prev = node
        if (start[1], start[2]) != (prev[1], prev[2]):
            segs.append((prev[0], start, prev))
        for (li, a, b) in segs:
            ax, ay, bx, by = a[1] * STEP, a[2] * STEP, b[1] * STEP, b[2] * STEP
            self.tracks.append((net, li, (ax, ay), (bx, by), width))
            add_obstacle(g, (li,), code, lambda px, py, s=(ax, ay, bx, by): dist_point_seg(px, py, *s),
                         (min(ax, bx), min(ay, by), max(ax, bx), max(ay, by)), half)
        for (vnet, (vx, vy)) in self.vias[nvias:]:
            if vnet == net:
                add_obstacle(g, (0, 1), code, lambda px, py, c=(vx, vy): math.hypot(px - c[0], py - c[1]),
                             (vx, vy, vx, vy), VIA_D / 2)
        for node in path:
            own.add(node)

    def add_stub(self, net, node, width):
        """小さい端子の代わりの点から、端子の中心へ短い線を足す"""
        if node in self.small_terms:
            cx, cy = self.small_terms[node]
            x, y = node[1] * STEP, node[2] * STEP
            if abs(x - cx) > 1e-6 or abs(y - cy) > 1e-6:
                self.tracks.append((net, node[0], (x, y), (cx, cy), min(width, W_SIG)))

    def route_all(self, order):
        failed = []
        by_net = {}
        for p in self.pads:
            n = p.GetNetname()
            if n and not n.startswith("unconnected-"):
                by_net.setdefault(n, []).append(p)
        for net in order:
            pads = by_net.get(net, [])
            if len(pads) < 2:
                continue
            code = self.code(net)
            # GND は両面のベタでもつながるので細い線で引く(引けなくてもベタに任せる)
            base_w = W_PWR if net in POWER and net != "/GND" else W_SIG
            # 端子の向こう側から順に: まず一番左上の端子を木にする
            pads = sorted(pads, key=lambda p: (to_mm(p.GetPosition().x), to_mm(p.GetPosition().y)))
            tree = set(self.terminals(pads[0], net))
            rest = pads[1:]
            ok = True
            while rest:
                # 一番近い端子を選ぶ
                def dist_to_tree(p):
                    cx, cy, *_ = self.pad_geom(p)
                    return min(abs(cx - t[1] * STEP) + abs(cy - t[2] * STEP) for t in tree)
                rest.sort(key=dist_to_tree)
                p = rest.pop(0)
                terms = self.terminals(p, net)

                def min_dim(q):
                    g_ = self.pad_geom(q)
                    return min(g_[4] - g_[2], g_[5] - g_[3])
                # 細い端子(Qwiic等)へは電源でも信号の太さで入る
                width = base_w if min(min_dim(p), min_dim(pads[0])) >= 1.0 else W_SIG
                kind = self.kind_for(net, width)
                srcs = [t for t in tree if self.grid.free(kind, t[0], t[1], t[2], code)] or list(tree)
                tg = [t for t in terms if self.grid.free(kind, t[0], t[1], t[2], code)] or terms
                path = self.search(net, code, srcs, tg, kind)
                if path is None and kind == "pwr":
                    kind, width = "sig", W_SIG
                    path = self.search(net, code, srcs, tg, kind)
                if path is None:
                    ok = False
                    print(f"  つながらない: {net} -> {p.GetParentFootprint().GetReference()}-{p.GetNumber()}")
                    continue
                self.commit(net, code, path, width, kind)
                self.add_stub(net, path[0], width)
                self.add_stub(net, path[-1], width)
                tree.update(path)
                tree.update(terms)
            if not ok and net != "/GND":
                failed.append(net)
        return failed


def apply(board, router):
    nets = board.GetNetInfo()
    for (net, li, a, b, w) in router.tracks:
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1])))
        t.SetEnd(pcbnew.VECTOR2I(mm(b[0]), mm(b[1])))
        t.SetWidth(mm(w))
        t.SetLayer(LAYERS[li])
        t.SetNet(nets.GetNetItem(net))
        board.Add(t)
    for (net, (x, y)) in router.vias:
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        v.SetWidth(mm(VIA_D))
        v.SetDrill(mm(VIA_DRILL))
        v.SetNet(nets.GetNetItem(net))
        board.Add(v)


def main():
    board = pcbnew.LoadBoard(PCB)
    for t in list(board.GetTracks()):
        board.Remove(t)
    nets = sorted({p.GetNetname() for f in board.GetFootprints() for p in f.Pads()
                   if p.GetNetname() and not p.GetNetname().startswith("unconnected-")})

    def span(n):
        ps = [p for f in board.GetFootprints() for p in f.Pads() if p.GetNetname() == n]
        xs = [to_mm(p.GetPosition().x) for p in ps]
        ys = [to_mm(p.GetPosition().y) for p in ps]
        return (max(xs) - min(xs)) + (max(ys) - min(ys))

    # 信号(短い順)→ 電源(GND以外。塞がっていれば信号の太さに落とす)→ GND(ベタもあるので最後)
    order = (sorted([n for n in nets if n not in POWER], key=span) +
             [n for n in nets if n in POWER and n != "/GND"] + ["/GND"])
    best = None
    for attempt in range(10):
        router = Router(board)
        router.setup_static()
        failed = router.route_all(order)
        print(f"{attempt + 1}回目: つながらないネット {len(failed)} {failed}")
        if best is None or len(failed) < len(best[1]):
            best = (router, failed)
        if not failed:
            break
        order = failed + [n for n in order if n not in failed]
    router, failed = best
    apply(board, router)
    pcbnew.SaveBoard(PCB, board)
    loaded = pcbnew.LoadBoard(PCB)
    pcbnew.ZONE_FILLER(loaded).Fill(loaded.Zones())
    pcbnew.SaveBoard(PCB, loaded)
    print(f"線 {len(router.tracks)} 本、ビア {len(router.vias)} 個")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
