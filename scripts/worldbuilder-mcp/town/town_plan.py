# Fits a new town to an area from a style pack: street network, frontage prefabs along the streets, sidewalks, lamps, trees, ground.
import json, math, os, sys, collections
import numpy as np
import scipy.ndimage as nd
from PIL import Image, ImageDraw
import town_lots
from town_site import Site
from town_paths import PACKS  # enters the work folder
pack = json.load(open(f"{PACKS}/alpine_assault.json"))
geo = json.load(open("gamedata/geo.json"))
SEED = int(sys.argv[1]) if len(sys.argv) > 1 else 7
rng = np.random.default_rng(SEED)
# --- the site: an L shape, about three times the reference town, at another angle ---
OPT = dict(a.split("=", 1) for a in sys.argv[2:] if "=" in a)
OUT = "plans/" + OPT.get("out", "plan")
SITE = Site(OPT.get("site"), [(500, 500), (1500, 500), (1500, 1000), (1000, 1000), (1000, 1500), (500, 1500)])
POLY = SITE.poly
ENTRIES = [(1700, 760), (760, 1700)]
THETA = math.radians(20)
GRID = (225, 180)        # block size along and across: the reference's blocks
B, GN = SITE.B, SITE.GN
if SITE.live:
    # two roads out of town, along the grid
    ca, sa = SITE.center_of_poly()
    ENTRIES = [SITE.clamp((ca + d * math.cos(THETA) * SITE.exit_radius * 1.2, sa + d * math.sin(THETA) * SITE.exit_radius * 1.2)) for d in (1, -1)]
ROAD_HW = pack["streets"]["width"] / 2
def inpoly(x, y, poly=POLY):
    c = False
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            c = not c
    return c
# --- street network: a jittered grid at the site's angle, some streets dropped, one long diagonal through it for wedge blocks ---
cx, cy = np.mean([p[0] for p in POLY]), np.mean([p[1] for p in POLY])
def build_grid(angle, ou, ov):
    c, s = math.cos(angle), math.sin(angle)
    nodes = {}
    for i in range(-8, 9):
        for j in range(-8, 9):
            u, v = ou + i * GRID[0] + rng.uniform(-30, 30), ov + j * GRID[1] + rng.uniform(-24, 24)
            x, y = cx + u * c - v * s, cy + u * s + v * c
            if inpoly(x, y):
                nodes[(i, j)] = (x, y)
    edges = set()
    for (i, j) in nodes:
        for d in ((1, 0), (0, 1)):
            k = (i + d[0], j + d[1])
            if k in nodes:
                edges.add(((i, j), k))
    return edges, nodes
edges, nodes = SITE.best_grid(build_grid, rng, THETA, GRID, True)
if not edges:
    raise SystemExit("no street fits: this area is too small or too blocked for this grid")
def connected(es):
    adj = collections.defaultdict(set)
    for a, b in es:
        adj[a].add(b)
        adj[b].add(a)
    if not adj:
        return False
    st, seen = [next(iter(adj))], set()
    while st:
        n = st.pop()
        if n not in seen:
            seen.add(n)
            st.extend(adj[n] - seen)
    return len(seen) == len(nodes)
for e in sorted(edges, key=lambda _: rng.random()):
    if rng.random() < 0.20:
        rest = edges - {e}
        if connected(rest):
            edges = rest
# no triangles and no dead ends: each surplus connecting street goes, the rest of the town stays connected
diag = 0
def prune_triangles():
    adj = collections.defaultdict(set)
    for x, y in edges:
        adj[x].add(y)
        adj[y].add(x)
    for x, y in sorted(edges, key=lambda e: -math.dist(nodes[e[0]], nodes[e[1]])):
        if adj[x] & adj[y]:
            edges.discard((x, y))
            return True
    return False
while prune_triangles():
    pass
# entries join the nearest node
ENT = []
entry_nodes = set()
for ex, ey in ENTRIES:
    n = min(nodes, key=lambda k: math.dist(nodes[k], (ex, ey)))
    ex, ey = SITE.clip(nodes[n], (ex, ey))
    if math.dist(nodes[n], (ex, ey)) < 40:
        continue
    entry_nodes.add(n)
    ENT.append((nodes[n], (ex, ey)))
while True:
    deg = collections.Counter()
    for x, y in edges:
        deg[x] += 1
        deg[y] += 1
    dead = [k for k, v in deg.items() if v == 1 and k not in entry_nodes]
    if not dead:
        break
    edges = {e for e in edges if e[0] not in dead and e[1] not in dead}
# chains: streets run through two-way nodes as one road with tight curves; junctions and ends break them
adjg = collections.defaultdict(set)
for x, y in edges:
    adjg[nodes[x]].add(nodes[y])
    adjg[nodes[y]].add(nodes[x])
for a_, b_ in ENT:
    adjg[a_].add(b_)
    adjg[b_].add(a_)
def is_break(n):
    return len(adjg[n]) != 2
CHAINS, seen_e = [], set()
for n in list(adjg):
    if not is_break(n):
        continue
    for m in adjg[n]:
        if (n, m) in seen_e:
            continue
        chain = [n, m]
        seen_e.add((n, m))
        seen_e.add((m, n))
        while not is_break(chain[-1]):
            nxt = next(q for q in adjg[chain[-1]] if q != chain[-2])
            seen_e.add((chain[-1], nxt))
            seen_e.add((nxt, chain[-1]))
            chain.append(nxt)
        CHAINS.append(chain)
print("chains", len(CHAINS), "points", sum(len(c) for c in CHAINS), "junctions", sum(1 for n in adjg if len(adjg[n]) >= 3))
SEG = [(nodes[a], nodes[b]) for a, b in edges] + ENT
street_len = sum(math.dist(a, b) for a, b in SEG if inpoly(*a) and inpoly(*b))
degree = collections.Counter()
for a, b in edges:
    degree[a] += 1
    degree[b] += 1
print("nodes", len(nodes), "streets", len(SEG), "length", round(street_len), "diagonals", diag)
# --- occupancy ---
OS = 4.0
ON = int(GN * 10 / OS)
occ = np.zeros((ON, ON), bool)
SITE.stamp(occ, OS)
def oc(x, y):
    return int((x + B * 10) / OS), int((y + B * 10) / OS)
def rect(x, y, a, b, deg, m):
    r = math.radians(deg)
    cr, sr = math.cos(r), math.sin(r)
    R = math.hypot(a, b) + m + OS
    i0, j0 = oc(x - R, y - R)
    i1, j1 = oc(x + R, y + R)
    i0, j0 = max(i0, 0), max(j0, 0)
    ii, jj = np.meshgrid(np.arange(i0, min(i1 + 1, ON)), np.arange(j0, min(j1 + 1, ON)))
    px, py = ii * OS - B * 10 - x, jj * OS - B * 10 - y
    u, v = px * cr + py * sr, -px * sr + py * cr
    k = (np.abs(u) <= a + m) & (np.abs(v) <= b + m)
    return jj[k], ii[k]
def mark_seg(a, b, hw):
    L = math.dist(a, b)
    for t in np.linspace(0, 1, max(2, int(L / 3))):
        x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
        i, j = oc(x, y)
        r = int(hw / OS)
        occ[max(j - r, 0):j + r + 1, max(i - r, 0):i + r + 1] = True
for a, b in SEG:
    mark_seg(a, b, ROAD_HW + 2)
# --- prefabs ---
pre = []
for p in pack["prefabs"]:
    p = dict(p)
    # some reference houses turn their back to the nearest street because they face another one; here they face the street
    p["turn"] = 180 if p["facing"] < -0.5 else 0
    p["side_on"] = abs(p["facing"]) < 0.5
    pre.append(p)
front_p = [p for p in pre if not p["side_on"]]
side_p = [p for p in pre if p["side_on"]]
print("prefabs front-on", len(front_p), "side-on (used at corners)", len(side_p))
OBJ, ground = [], []
counts = collections.Counter()
SMALL_ONCE = {"AsianStreetSign", "StopLight", "SignalLights", "CrossingGuard"}
hedge_groups = 0
def place_prefab(p, ox, oy, tx, ty, nx, ny, used_props):
    global hedge_groups
    if pack["buildings"]["mix"].get(p["items"][0]["t"], 2) == 1 and counts[p["items"][0]["t"]] >= 1:
        return False
    if any(it["t"] == "Hedge1" for it in p["items"]):
        hedge_groups += 1
    tang = math.degrees(math.atan2(ty, tx))
    bld = p["items"][0]
    ba, bb = geo.get(bld["t"], [20, 20])
    u, v = bld["p"]
    # setback from the street edge as in the reference, never into the sidewalk
    v = max(v, ROAD_HW + 10 + (abs(math.sin(math.radians(bld["a"]))) * ba + abs(math.cos(math.radians(bld["a"]))) * bb) * 0.95)
    dv = v - bld["p"][1]
    x, y = ox + nx * v, oy + ny * v
    deg = bld["a"] + tang + p["turn"]
    jj, ii = rect(x, y, ba, bb, deg, 5)
    if occ[jj, ii].any():
        return False
    occ[jj, ii] = True
    OBJ.append({"template": bld["t"], "x": round(x), "y": round(y), "angle_deg": round(deg, 1), "owner": "teamPlyrCivilian"})
    for it in p["items"][1:]:
        pu, pv = it["p"][0] - u, it["p"][1] + dv
        px, py = ox + tx * pu + nx * pv, oy + ty * pu + ny * pv
        d2 = it["a"] + tang + (p["turn"] if it["role"] == "bib" else 0)
        if it["role"] == "bib":
            if math.hypot(it["p"][0] - u, it["p"][1] - bld["p"][1]) > 12:
                continue
            OBJ.append({"template": it["t"], "x": round(px), "y": round(py), "angle_deg": round(d2, 1), "owner": "teamPlyrCivilian"})
            continue
        if it["t"] in SMALL_ONCE or not inpoly(px, py):
            continue
        # one-off props stay rare: each type keeps its distance from the last one of its kind
        last = used_props.get(it["t"])
        if it["t"] == "Hedge1" and used_props.get("_hedge_owner") not in (None, id(p)) and hedge_groups >= 2:
            continue
        if last and it["t"] != "Hedge1" and any(math.dist((px, py), q) < 260 for q in last):
            continue
        a2, b2 = geo.get(it["t"], [5, 5])
        jj2, ii2 = rect(px, py, min(a2, 12), min(b2, 12), d2, 1)
        if occ[jj2, ii2].any():
            continue
        occ[jj2, ii2] = True
        used_props.setdefault(it["t"], []).append((px, py))
        OBJ.append({"template": it["t"], "x": round(px), "y": round(py), "angle_deg": round(d2, 1)})
    for gu, gv in p["ground"]:
        pu, pv = gu - u, gv + dv
        # the reference's cobble hugs the house: keep what lies within 34 units of this building
        rr = math.radians(bld["a"])
        lu, lv = (gu - u) * math.cos(rr) + (gv - bld["p"][1]) * math.sin(rr), -(gu - u) * math.sin(rr) + (gv - bld["p"][1]) * math.cos(rr)
        if max(abs(lu) - ba, abs(lv) - bb) > 34:
            continue
        ground.append((ox + tx * pu + nx * pv, oy + ty * pu + ny * pv))
    counts[bld["t"]] += 1
    return True
# special lots along the longest streets, before the houses take the frontage
lot_rects = []
LOTS = next((a[5:] for a in sys.argv[2:] if a.startswith("lots=")), "")
for kind, count in town_lots.parse_spec(LOTS):
    for _ in range(count):
        w, d, g, items = town_lots.layout(kind, rng)
        for k in list(rng.permutation(len(SEG))) * 6:
            a, b = SEG[k]
            L = math.dist(a, b)
            if L < w + 44 or not (inpoly(*a) and inpoly(*b)):
                continue
            dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
            if rng.random() < 0.5:
                a, b, dx, dy = b, a, -dx, -dy
            nx, ny = -dy, dx
            s = rng.uniform(w / 2 + 22, L - w / 2 - 22)
            ox, oy = a[0] + dx * s + nx * (ROAD_HW + 8), a[1] + dy * s + ny * (ROAD_HW + 8)
            deg = math.degrees(math.atan2(dy, dx))
            cx_, cy_ = ox + nx * d / 2, oy + ny * d / 2
            jj, ii = rect(cx_, cy_, w / 2, d / 2, deg, 2)
            if occ[jj, ii].any() or not inpoly(ox + nx * d, oy + ny * d):
                continue
            occ[jj, ii] = True
            OBJ.extend(town_lots.to_world(it, ox, oy, dx, dy) for it in items)
            lot_rects.append((cx_, cy_, w / 2, d / 2, deg, g))
            print("lot", kind)
            break
        else:
            print("no room for lot", kind)
used, rows = {}, []
order = sorted(SEG, key=lambda e: -math.dist(*e))
for a, b in order:
    if not (inpoly(*a) or inpoly(*b)):
        continue
    L = math.dist(a, b)
    tx, ty = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    for side in (1, -1):
        nx, ny = -ty * side, tx * side
        s_, last, n = rng.uniform(32, 46), None, 0
        run = []
        while s_ < L - 30:
            atcorner = s_ < 60 or s_ > L - 70
            pool = side_p if atcorner and side_p and rng.random() < 0.6 else front_p
            p = pool[rng.integers(len(pool))]
            if p is last:
                p = pool[(pool.index(p) + 1) % len(pool)]
            w = p["width"]
            ox, oy = a[0] + tx * s_, a[1] + ty * s_
            # keep the frame of the prefab: +y away from the street
            if inpoly(ox + nx * 40, oy + ny * 40) and place_prefab(p, ox, oy, tx * side, ty * side, nx, ny, used):
                run.append(s_)
                last = p
                n += 1
                s_ += w + rng.uniform(8, 26)
                if rng.random() < 0.10:
                    s_ += rng.uniform(40, 80)
            else:
                s_ += 24
        rows.append((a, b, side, run))
print("houses", sum(counts.values()), counts.most_common())
# --- sidewalks: flush against the road (offset checked on screen), along whole streets, stopping short of junctions ---
SW_OFF = 17
houses_on = collections.Counter()
for a, b, side, run in rows:
    houses_on[(a, b, side)] = len(run)
def seg_houses(p, q, side):
    return houses_on.get((p, q, side), 0) + houses_on.get((q, p, -side), 0)
TIGHT_R = 0.5 * pack["streets"]["width"]   # the engine's tight curve: 0.5 road widths on the centre line, in steps of 30 degrees
MITER_DEG = 27                             # below this the engine draws no curve, on the street and on the sidewalk alike
def bend(ch, i):
    """Signed turn at point i in degrees (positive: left), with the unit directions before and after it."""
    p = ch[i]
    L1, L2 = math.dist(ch[i - 1], p), math.dist(p, ch[i + 1])
    d1 = ((p[0] - ch[i - 1][0]) / L1, (p[1] - ch[i - 1][1]) / L1)
    d2 = ((ch[i + 1][0] - p[0]) / L2, (ch[i + 1][1] - p[1]) / L2)
    return math.degrees(math.atan2(d1[0] * d2[1] - d1[1] * d2[0], d1[0] * d2[0] + d1[1] * d2[1])), d1, d2
def outer_bends(ch, side):
    return sum(1 for i in range(1, len(ch) - 1) if abs(bend(ch, i)[0]) >= MITER_DEG and bend(ch, i)[0] * side < 0)
def offset_chain(ch, side, off):
    """Sidewalk line beside a street. On the outside of a curve it follows the street's arc in short mitred pieces;
    on the inside the street pivots on its inner edge, so one corner fits."""
    L0 = math.dist(ch[0], ch[1])
    out = [(ch[0][0] - side * (ch[1][1] - ch[0][1]) / L0 * off, ch[0][1] + side * (ch[1][0] - ch[0][0]) / L0 * off)]
    for i in range(1, len(ch) - 1):
        p = ch[i]
        deg, d1, d2 = bend(ch, i)
        n1, n2 = (-d1[1], d1[0]), (-d2[1], d2[0])
        bx, by = n1[0] + n2[0], n1[1] + n2[1]
        bl = math.hypot(bx, by) or 1
        bx, by = bx / bl, by / bl
        half = math.radians(abs(deg)) / 2
        if abs(deg) < MITER_DEG or deg * side > 0:
            out.append((p[0] + side * bx * off / math.cos(half), p[1] + side * by * off / math.cos(half)))
            continue
        t = 1 if deg > 0 else -1
        cx_, cy_ = p[0] + t * bx * TIGHT_R / math.cos(half), p[1] + t * by * TIGHT_R / math.cos(half)
        a1 = math.atan2(-t * n1[1], -t * n1[0])
        steps = max(2, math.ceil(abs(deg) / 25))
        for k in range(steps + 1):
            a_ = a1 + math.radians(deg) * k / steps
            out.append((cx_ + (TIGHT_R + off) * math.cos(a_), cy_ + (TIGHT_R + off) * math.sin(a_)))
    L1 = math.dist(ch[-2], ch[-1])
    out.append((ch[-1][0] - side * (ch[-1][1] - ch[-2][1]) / L1 * off, ch[-1][1] + side * (ch[-1][0] - ch[-2][0]) / L1 * off))
    return out
walk, wlen = [], 0
target = pack["sidewalks"]["length_share_of_street"] * street_len
cands = []
OUTER_COST = 2   # one outside curve weighs as much as two houses
for k_, ch in enumerate(CHAINS):
    if not all(inpoly(*p) for p in ch):
        continue
    for side in (1, -1):
        h = sum(seg_houses(p, q, side) for p, q in zip(ch[:-1], ch[1:]))
        cands.append((h - OUTER_COST * outer_bends(ch, side), h, k_, ch, side))
# one side per street first, its better side; second sides only if the length is still short of the reference
ranked = sorted(cands, key=lambda c: (-c[0], c[2]))
first, second, seen_k = [], [], set()
for c_ in ranked:
    (second if c_[2] in seen_k else first).append(c_)
    seen_k.add(c_[2])
outer_total = 0
for _s, h, _, ch, side in first + second:
    if h < 2 or wlen > target:
        continue
    outer_total += outer_bends(ch, side)
    o = offset_chain(ch, side, SW_OFF)
    trim = 34
    def cut(a, b):
        L = math.dist(a, b)
        return (a[0] + (b[0] - a[0]) * trim / L, a[1] + (b[1] - a[1]) * trim / L)
    pts = [cut(o[0], o[1])] + o[1:-1] + [cut(o[-1], o[-2])]
    L = sum(math.dist(x, y) for x, y in zip(pts[:-1], pts[1:]))
    if L < 70:
        continue
    walk.append([[round(x, 1), round(y, 1)] for x, y in pts])
    wlen += L
print("outside curves on sidewalks", outer_total)
print("sidewalk", len(walk), round(wlen), "share", round(wlen / street_len, 2), "target", pack["sidewalks"]["length_share_of_street"])
# --- lamps at the reference's rate, on alternating sides at the road edge ---
rate = pack["lamp_rate_per_100"] * 0.7 / 100   # a little under the reference
spacing = 1 / rate
lamps = 0
lamp_pts = []
for a, b in SEG:
    if not (inpoly(*a) and inpoly(*b)):
        continue
    L = math.dist(a, b)
    tx, ty = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    side = 1 if rng.random() < 0.5 else -1
    s_ = rng.uniform(25, spacing * 0.8)
    while s_ < L - 20:
        x, y = a[0] + tx * s_ - ty * side * (ROAD_HW + 2), a[1] + ty * s_ + tx * side * (ROAD_HW + 2)
        i, j = oc(x + (-ty * side) * 6, y + tx * side * 6)
        if 0 <= i < ON and 0 <= j < ON and not occ[j, i] and all(math.dist((x, y), q) > spacing * 0.3 for q in lamp_pts):
            lamp_pts.append((x, y))
            OBJ.append({"template": "StreetLamp", "x": round(x), "y": round(y), "angle_deg": round(math.degrees(math.atan2(ty, tx)))})
            lamps += 1
        s_ += spacing * rng.uniform(0.5, 0.75)
        side = -side
print("lamps", lamps, "per 100", round(lamps / street_len * 100, 2), "reference", pack["lamp_rate_per_100"])
# --- trees in the gaps, near the road like the reference ---
trees = 0
want = int(pack["tree_rate_per_100_street"] * street_len / 100)
names = list(pack["trees"])
w_ = np.array([pack["trees"][n] for n in names], float)
w_ /= w_.sum()
pts = []
def try_tree(x, y):
    global trees
    if not inpoly(x, y) or any(math.dist((x, y), q) < 24 for q in pts):
        return False
    jj, ii = rect(x, y, 8, 8, 0, 2)
    if occ[jj, ii].any():
        return False
    occ[jj, ii] = True
    pts.append((x, y))
    OBJ.append({"template": str(rng.choice(names, p=w_)), "x": round(x), "y": round(y), "angle_deg": int(rng.integers(-180, 180))})
    trees += 1
    return True
for _ in range(6000):
    if trees >= want:
        break
    a, b = SEG[rng.integers(len(SEG))]
    t = rng.uniform(0.05, 0.95)
    L = math.dist(a, b)
    nx, ny = -(b[1] - a[1]) / L, (b[0] - a[0]) / L
    off = (ROAD_HW + rng.uniform(18, 40)) * (1 if rng.random() < 0.5 else -1)
    x, y = a[0] + (b[0] - a[0]) * t + nx * off, a[1] + (b[1] - a[1]) * t + ny * off
    # small groups, with open stretches between them
    if try_tree(x, y):
        for _k in range(int(rng.integers(1, 4))):
            try_tree(x + rng.uniform(-42, 42), y + rng.uniform(-42, 42))
print("trees", trees, "want", want)
# --- ground ---
W = GN
XC, YC = np.meshgrid((np.arange(W) - B) * 10.0 + 5, (np.arange(W) - B) * 10.0 + 5)
gm = np.zeros((W, W), float)
for x, y in ground:
    i, j = int(x / 10 + B), int(y / 10 + B)
    if 0 <= i < W and 0 <= j < W:
        gm[j, i] = 1
cob = nd.gaussian_filter(gm, 0.7) > 0.3
lotdirt = np.zeros((W, W), bool)
for x, y, a_, b_, deg, g in lot_rects:
    r = math.radians(deg)
    u = (XC - x) * math.cos(r) + (YC - y) * math.sin(r)
    v = -(XC - x) * math.sin(r) + (YC - y) * math.cos(r)
    m_ = (np.abs(u) <= a_) & (np.abs(v) <= b_)
    if g == "dirt":
        lotdirt |= m_
    else:
        cob |= m_   # the reference's paved ground
cob &= ~lotdirt
# grass patches around the houses as in the reference, between 20 and 40 units
gr = nd.binary_dilation(cob, iterations=3) & ~cob & (nd.gaussian_filter(np.random.default_rng(SEED).standard_normal((W, W)), 2.5) > 0.0)
# soft outline along the roads, from the context: mostly under the road, so only the blend shows; concrete between the houses, dirt with gaps outside the town
dr_c = np.full((W, W), 1e9)
for a, b in SEG:
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = np.clip(((XC - a[0]) * dx + (YC - a[1]) * dy) / (dx * dx + dy * dy), 0, 1)
    dr_c = np.minimum(dr_c, np.hypot(XC - a[0] - t * dx, YC - a[1] - t * dy))
dh = np.full((W, W), 1e9)
for o in OBJ:
    if o.get("owner") == "teamPlyrCivilian" and "Bib" not in o["template"]:
        dh = np.minimum(dh, np.hypot(XC - o["x"], YC - o["y"]))
def noise(k, sigma):
    n = nd.gaussian_filter(np.random.default_rng(SEED + k).standard_normal((W, W)), sigma)
    return n / n.std()
n_o, n_m, n_t, n_g = noise(1, 1.6), noise(2, 3.0), noise(3, 4.0), noise(4, 2.0)
town = dh + n_t * 18 < 105
out_town = town & (dr_c < 15 + n_o * 2)
out_land = ~town & (dr_c < 13 + n_o * 2) & (n_g > -0.6)
if len(sys.argv) > 2 and sys.argv[2] == "no-outline":   # many maps have none: Alpine Assault paints plain grass and cobble up to its roads
    out_town = out_land = np.zeros((W, W), bool)
outline = out_town | out_land
OUTLINE = {"ConcreteType1": out_town & (n_m <= 0.7), "ConcreteType4": out_town & (n_m > 0.7),
           "DirtMediumType12": (out_land & (n_m <= 0.5)) | lotdirt, "DirtMediumType5": out_land & (n_m > 0.5)}   # what SwissMP paints along its country roads
gr &= ~outline & ~lotdirt
os.makedirs(OUT, exist_ok=True)
for n_, m_ in OUTLINE.items():
    m_ = m_ & ~cob
    Image.fromarray((SITE.crop(m_) * 255).astype(np.uint8)).save(f"{OUT}/{n_}.png")
    print("outline", n_, int(m_.sum()))
Image.fromarray((SITE.crop(cob) * 255).astype(np.uint8)).save(f"{OUT}/CobblestoneType1.png")
Image.fromarray((SITE.crop(gr) * 255).astype(np.uint8)).save(f"{OUT}/GrassType1.png")
print("cobble cells", int(cob.sum()), "grass", int(gr.sum()))
tot = collections.Counter(o["template"] for o in OBJ)
print("objects", len(OBJ), tot.most_common(30))
roads = [{"type": "TwoLane", "corners": "tight", "end_caps": True, "points": [[round(x, 1), round(y, 1)] for x, y in c]} for c in CHAINS]
roads += [{"type": "Sidewalk", "corners": "angled", "end_caps": False, "points": w} for w in walk]
json.dump({"objects": OBJ, "roads": roads, "stats": {"houses": sum(counts.values()), "street": street_len},
           "surround": "GrassMediumType8", "masks": ["GrassType1"] + list(OUTLINE) + ["CobblestoneType1"],
           "exits": [{"point": [round(b_[0], 1), round(b_[1], 1)], "type": "TwoLane", "width": pack["streets"]["width"], "dir": [(b_[0] - a_[0]) / (math.dist(a_, b_) or 1), (b_[1] - a_[1]) / (math.dist(a_, b_) or 1)]} for a_, b_ in ENT]}, open(f"{OUT}/plan.json", "w"), separators=(",", ":"))
# --- preview at the same scale as the reference plan ---
SC = 0.62
X0, Y1 = 330, 1800
im = Image.new("RGB", (int(1500 * SC), int(1450 * SC)), (70, 100, 60))
dr = ImageDraw.Draw(im)
PX = lambda x, y: ((x - X0) * SC, (Y1 - y) * SC)
dr.polygon([PX(*p) for p in POLY], outline=(255, 255, 255))
for j, i in zip(*np.nonzero(cob)):
    a, b = PX((i - B) * 10, (j - B) * 10 + 10)
    dr.rectangle((a, b, a + 10 * SC, b + 10 * SC), fill=(175, 150, 125))
for a, b in SEG:
    dr.line([PX(*a), PX(*b)], fill=(30, 30, 30), width=int(35 * SC))
for w in walk:
    dr.line([PX(*p) for p in w], fill=(240, 240, 240), width=3)
for o in OBJ:
    t, x, y = o["template"], o["x"], o["y"]
    r = math.radians(o["angle_deg"])
    a, b_ = geo.get(t, [5, 5])
    if max(a, b_) >= 14 and not t.startswith(("Tree", "Hedge", "Car")) and "Bib" not in t:
        q = [(x + u * math.cos(r) - v * math.sin(r), y + u * math.sin(r) + v * math.cos(r)) for u, v in ((-a, -b_), (a, -b_), (a, b_), (-a, b_))]
        dr.polygon([PX(*p) for p in q], fill=(200, 80, 70))
        dr.line([PX(x, y), PX(x + (b_ + 10) * math.sin(r), y - (b_ + 10) * math.cos(r))], fill=(255, 255, 255), width=2)
    elif t.startswith("Tree"):
        c_ = PX(x, y)
        dr.ellipse((c_[0] - 3, c_[1] - 3, c_[0] + 3, c_[1] + 3), outline=(220, 170, 40))
    elif t == "StreetLamp":
        c_ = PX(x, y)
        dr.ellipse((c_[0] - 2, c_[1] - 2, c_[0] + 2, c_[1] + 2), fill=(255, 255, 0))
    elif "Bib" not in t:
        c_ = PX(x, y)
        dr.ellipse((c_[0] - 2, c_[1] - 2, c_[0] + 2, c_[1] + 2), fill=(255, 140, 0))
im.save(f"{OUT}/preview.png")

base = np.full((W - 1, W - 1), 255, np.uint8)
Image.fromarray(base).save(f"{OUT}/base.png")
