# Irregular town from a style pack: a main street, side streets that branch off in T junctions, bend and dead-end, buildings close along them.
# Usage: town_organic.py <pack> <preset> <seed> [irregularity=<0..1>] [theme=<name>] [lots=<kind:count,...>] [out=<folder>] [site=<file>]
import json, math, os, sys, collections
import numpy as np
import scipy.ndimage as nd
from PIL import Image, ImageDraw
from town_paths import PACKS  # enters the work folder
import town_lots
from town_site import Site
pack = json.load(open(f"{PACKS}/{sys.argv[1]}.json"))
PR = pack["presets"][sys.argv[2]]
SEED = int(sys.argv[3])
OPT = dict(a.split("=", 1) for a in sys.argv[4:])
rng = np.random.default_rng(SEED)
OUT = "plans/" + OPT.get("out", "organic_plan")
os.makedirs(OUT, exist_ok=True)
geo = json.load(open("gamedata/geo.json"))
R = float(OPT.get("irregularity", PR["irregularity"]))
MAJOR, MINOR = PR["major"], PR["minor"]
WIDTH = {MAJOR: PR["major_width"], MINOR: PR["minor_width"]}
is_walkway = lambda t: t.startswith("Sidewalk")
SITE = Site(OPT.get("site"), [(640, 560), (1500, 470), (1860, 1000), (1700, 1640), (1040, 1840), (560, 1360)])
POLY = SITE.poly
B, GN = SITE.B, SITE.GN
def inpoly(x, y, poly=POLY):
    c = False
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            c = not c
    return c
site_area = 0.5 * abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(POLY, POLY[1:] + POLY[:1])))
cx, cy = SITE.free_center() if SITE.live else (np.mean([p[0] for p in POLY]), np.mean([p[1] for p in POLY]))
# --- street graph ---
P = []
edges = {}
adj = collections.defaultdict(set)
def node(p):
    P.append((float(p[0]), float(p[1])))
    return len(P) - 1
def add_edge(a, b, t):
    edges[(min(a, b), max(a, b))] = t
    adj[a].add(b)
    adj[b].add(a)
def typeof(a, b):
    return edges[(min(a, b), max(a, b))]
def split(e, p):
    """A new node on street e."""
    a, b = e
    # at an end of the street there is a node already
    if math.dist(P[a], p) < 12:
        return a
    if math.dist(P[b], p) < 12:
        return b
    t = edges.pop(e)
    adj[a].discard(b)
    adj[b].discard(a)
    n = node(p)
    add_edge(a, n, t)
    add_edge(n, b, t)
    return n
def nearest_edge(p, skip=()):
    best = (1e9, None, None)
    for e in edges:
        if e[0] in skip or e[1] in skip:
            continue
        a, b = P[e[0]], P[e[1]]
        dx, dy = b[0] - a[0], b[1] - a[1]
        t = max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
        q = (a[0] + dx * t, a[1] + dy * t)
        d = math.dist(p, q)
        if d < best[0]:
            best = (d, e, q)
    return best
def crossing(a, b, skip=()):
    """First point where the segment a-b meets a street."""
    hit = None
    for e in edges:
        if e[0] in skip or e[1] in skip:
            continue
        c, d = P[e[0]], P[e[1]]
        den = (b[0] - a[0]) * (d[1] - c[1]) - (b[1] - a[1]) * (d[0] - c[0])
        if abs(den) < 1e-9:
            continue
        t = ((c[0] - a[0]) * (d[1] - c[1]) - (c[1] - a[1]) * (d[0] - c[0])) / den
        u = ((c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0])) / den
        if 0 < t < 1 and 0 <= u <= 1 and (hit is None or t < hit[0]):
            hit = (t, e, (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return hit
def street_length():
    return sum(math.dist(P[a], P[b]) for a, b in edges if inpoly(*P[a]) and inpoly(*P[b]))
def straight_next(a, b):
    """The street that continues a-b straight on past b, of the same type."""
    t = typeof(a, b)
    d0 = math.atan2(P[b][1] - P[a][1], P[b][0] - P[a][0])
    for c in adj[b]:
        if c != a and typeof(b, c) == t:
            d1 = math.atan2(P[c][1] - P[b][1], P[c][0] - P[b][0])
            if abs((d1 - d0 + math.pi) % (2 * math.pi) - math.pi) < math.radians(12):
                return c
    return None
# main street: through the middle, past the town at both ends, bending a little
entries = []
def main_street(angle):
    def run(a):
        x, y = cx, cy
        out = []
        while True:
            a += math.radians(rng.uniform(-22, 22)) * R
            x0, y0 = x, y
            x, y = x + math.cos(a) * 260, y + math.sin(a) * 260
            if not SITE.in_map(x, y) or not SITE.line_ok((x0, y0), (x, y)):
                return out
            out.append((x, y))
            if not inpoly(x, y) and math.dist((x, y), (cx, cy)) > SITE.exit_radius:
                return out
    return run(angle + math.pi)[::-1] + [(cx, cy)] + run(angle)
seq = main_street(rng.uniform(0, math.pi))
if len(seq) < 2:
    raise SystemExit("no room for a main street in this area")
ids = [node(p) for p in seq]
for a, b in zip(ids[:-1], ids[1:]):
    add_edge(a, b, MAJOR)
major_end_nodes = {ids[0], ids[-1]}
# side streets: start on a street at about a right angle, bend, stop at a dead end or in a T on another street
MIN_CLEAR = 95 + WIDTH[MINOR] / 2   # two rows of houses fit between neighbouring streets
total_want = PR["street_density"] * site_area / 1e4
attempts = 0
why = collections.Counter()
while street_length() < total_want and attempts < 3000:
    attempts += 1
    es = [e for e in edges if inpoly(*P[e[0]]) and inpoly(*P[e[1]])]
    ls = np.array([math.dist(P[a], P[b]) for a, b in es])
    if not len(es):
        break
    e = es[rng.choice(len(es), p=ls / ls.sum())]
    L = math.dist(P[e[0]], P[e[1]])
    if L < 90:
        continue
    t = rng.uniform(40 / L, 1 - 40 / L)
    a, b = P[e[0]], P[e[1]]
    p0 = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    side = 1 if rng.random() < 0.5 else -1
    heading = math.atan2(b[1] - a[1], b[0] - a[0]) + side * math.pi / 2 + math.radians(rng.uniform(-28, 28)) * R
    path = [p0]
    ending = None
    # the street it starts from does not count as too close while the new street leaves it
    host = set(e)
    for u, v in (e, e[::-1]):
        while True:
            w_ = straight_next(u, v)
            if w_ is None or w_ in host:
                break
            host.add(w_)
            u, v = v, w_
    turned = 0
    for k in range(int(rng.integers(2, 9))):
        if k and rng.random() < 0.2 + 0.7 * R:
            heading += math.radians(rng.uniform(32, 70)) * (1 if rng.random() < 0.5 else -1)
        else:
            heading += math.radians(rng.uniform(-8, 8)) * R
        L_ = rng.uniform(70, 150)
        q = (path[-1][0] + math.cos(heading) * L_, path[-1][1] + math.sin(heading) * L_)
        if not inpoly(*q) or not SITE.line_ok(path[-1], q):
            break
        hit = crossing(path[-1], q, skip=e if k == 0 else ())
        if hit:
            q = hit[2]
            ending = hit[1]
        # keep room for houses between this street and the others; a street end this close joins instead
        else:
            d, ee, qq = nearest_edge(q, skip=host if math.dist(q, p0) < MIN_CLEAR * 1.2 else e)
            if d < 45:
                q, ending = qq, ee
            elif d < MIN_CLEAR:
                # too close to run on, too far to join: the street ends here, as many do in the references
                break
        if math.dist(path[-1], q) < 50:
            ending = None
            break
        # the middle of the new piece must not run along a street
        m = ((path[-1][0] + q[0]) / 2, (path[-1][1] + q[1]) / 2)
        d, _, _ = nearest_edge(m, skip=host if math.dist(m, p0) < MIN_CLEAR * 1.2 else e)
        if d < MIN_CLEAR * 0.7:
            ending = None
            break
        if any(math.dist(m, ((u[0] + v[0]) / 2, (u[1] + v[1]) / 2)) < MIN_CLEAR for u, v in zip(path[:-1], path[1:])):
            break
        path.append(q)
        if ending:
            break
    if len(path) < 2 or sum(math.dist(u, v) for u, v in zip(path[:-1], path[1:])) < 240:
        # long winding streets, not many short stubs: the references have few junctions per length
        why["too short"] += 1
        continue
    # new nodes: start on its street, end possibly on another
    if ending is not None and ending == e:
        continue
    n0 = split(e, path[0])
    prev = n0
    for k, q in enumerate(path[1:]):
        if k == len(path) - 2 and ending is not None and ending in edges:
            n = split(ending, q)
        else:
            n = node(q)
        add_edge(prev, n, MINOR)
        prev = n
major_nodes = {n for e, t in edges.items() if t == MAJOR for n in e}
print("rejected", dict(why))
print("streets", len(edges), "length", round(street_length()), "want", round(total_want), "attempts", attempts)
deg = collections.Counter(len(v) for v in adj.values())
sl = street_length()
if sl <= 0:
    # Only the main street's stub fitted: nothing to build along, and the summary below would divide by zero.
    raise SystemExit("no street fits: this area is too small or too blocked for this layout")
bends = 0
for n, v in adj.items():
    if len(v) == 2:
        a, b = list(v)
        c = ((P[a][0] - P[n][0]) * (P[b][0] - P[n][0]) + (P[a][1] - P[n][1]) * (P[b][1] - P[n][1])) / (math.dist(P[a], P[n]) * math.dist(P[b], P[n]))
        bends += 180 - math.degrees(math.acos(max(-1, min(1, c)))) > 5
print(f"per 1000 street: dead ends {deg[1] / sl * 1000:.1f}, junctions {sum(v for k, v in deg.items() if k >= 3) / sl * 1000:.1f}, bends {bends / sl * 1000:.1f}  (measured {pack['measured']['dead_ends_per_1000']}, {pack['measured']['junctions_per_1000']}, {pack['measured']['bends_per_1000']})")
# --- road runs ---
def chains_of(kind):
    a2 = collections.defaultdict(set)
    for (a, b), t in edges.items():
        if t == kind:
            a2[a].add(b)
            a2[b].add(a)
    def through(n):
        return len(a2[n]) == 2 and (kind == MAJOR or len(adj[n]) == 2)
    out, seen = [], set()
    for n in a2:
        if through(n):
            continue
        for m in a2[n]:
            if (n, m) in seen:
                continue
            ch = [n, m]
            seen |= {(n, m), (m, n)}
            while through(ch[-1]):
                nx = next(q for q in a2[ch[-1]] if q != ch[-2])
                seen |= {(ch[-1], nx), (nx, ch[-1])}
                ch.append(nx)
            out.append(ch)
    return out
def beside_major(n, m):
    """A side street ends at the main street's edge; the main street keeps its connection."""
    nb = [q for q in adj[n] if typeof(n, q) == MAJOR]
    if not nb:
        return P[n]
    q = nb[0]
    Lm, L = math.dist(P[n], P[q]), math.dist(P[n], P[m])
    mx, my = (P[q][0] - P[n][0]) / Lm, (P[q][1] - P[n][1]) / Lm
    dx, dy = (P[m][0] - P[n][0]) / L, (P[m][1] - P[n][1]) / L
    d = WIDTH[MAJOR] / 2 / max(0.5, abs(mx * dy - my * dx))
    return (P[n][0] + dx * min(d, L * 0.8), P[n][1] + dy * min(d, L * 0.8))
roads = []
for ch in chains_of(MAJOR):
    roads.append({"type": MAJOR, "corners": "tight", "end_caps": False, "points": [[round(P[n][0], 1), round(P[n][1], 1)] for n in ch]})
for ch in chains_of(MINOR):
    pts = [P[n] for n in ch]
    pts[0] = beside_major(ch[0], ch[1])
    pts[-1] = beside_major(ch[-1], ch[-2])
    # paths and dirt tracks fade out at open ends; sidewalks used as paths carry no end caps in the references
    roads.append({"type": MINOR, "corners": "tight", "end_caps": not is_walkway(MINOR), "points": [[round(x, 1), round(y, 1)] for x, y in pts]})
SEG = [(P[a], P[b], t) for (a, b), t in edges.items()]
# --- occupancy ---
OS = 4.0
ON = int(GN * 10 / OS)
occ = np.zeros((ON, ON), bool)
SITE.stamp(occ, OS)
def oc(x, y):
    return int((x + B * 10) / OS), int((y + B * 10) / OS)
def rect(x, y, a, b, deg_, m):
    r = math.radians(deg_)
    cr, sr = math.cos(r), math.sin(r)
    Rr = math.hypot(a, b) + m + OS
    i0, j0 = oc(x - Rr, y - Rr)
    i1, j1 = oc(x + Rr, y + Rr)
    i0, j0 = max(i0, 0), max(j0, 0)
    ii, jj = np.meshgrid(np.arange(i0, min(i1 + 1, ON)), np.arange(j0, min(j1 + 1, ON)))
    px, py = ii * OS - B * 10 - x, jj * OS - B * 10 - y
    u, v = px * cr + py * sr, -px * sr + py * cr
    k = (np.abs(u) <= a + m) & (np.abs(v) <= b + m)
    return jj[k], ii[k]
def mark_seg(a, b, hw):
    for t in np.linspace(0, 1, max(2, int(math.dist(a, b) / 3))):
        i, j = oc(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        r = int(hw / OS) + 1
        yy, xx = np.ogrid[max(j - r, 0):j + r + 1, max(i - r, 0):i + r + 1]
        occ[max(j - r, 0):j + r + 1, max(i - r, 0):i + r + 1] |= (yy - j) ** 2 + (xx - i) ** 2 <= (hw / OS) ** 2
for a, b, t in SEG:
    mark_seg(a, b, WIDTH[t] / 2)
# --- special lots ---
OBJ = []
lot_rects = []
# frontage: straight stretches of street, not cut up by the junctions on them
frontage = []
seen_f = set()
for (a, b), t in edges.items():
    if (a, b) in seen_f:
        continue
    line = [a, b]
    seen_f |= {(a, b), (b, a)}
    while True:
        c = straight_next(line[-2], line[-1])
        if c is None or (line[-1], c) in seen_f or (c, line[-1]) in seen_f:
            break
        seen_f |= {(line[-1], c), (c, line[-1])}
        line.append(c)
    while True:
        c = straight_next(line[1], line[0])
        if c is None or (line[0], c) in seen_f or (c, line[0]) in seen_f:
            break
        seen_f |= {(line[0], c), (c, line[0])}
        line.insert(0, c)
    p, q = P[line[0]], P[line[-1]]
    if inpoly(*p) or inpoly(*q) or inpoly((p[0] + q[0]) / 2, (p[1] + q[1]) / 2):
        frontage.append((p, q, t))
        frontage.append((q, p, t))
print("frontage lines", len(frontage) // 2, "length median", round(float(np.median([math.dist(a, b) for a, b, _ in frontage]))))
for kind, count in town_lots.parse_spec(OPT.get("lots", "")):
    for _ in range(count):
        w, d, g, items = town_lots.layout(kind, rng)
        for k in list(rng.permutation(len(frontage))) * 4:
            a, b, t = frontage[k]
            L = math.dist(a, b)
            if L < w + 30:
                continue
            dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
            nx, ny = -dy, dx
            s = rng.uniform(w / 2 + 15, L - w / 2 - 15)
            ox, oy = a[0] + dx * s + nx * (WIDTH[t] / 2 + 8), a[1] + dy * s + ny * (WIDTH[t] / 2 + 8)
            deg_ = math.degrees(math.atan2(dy, dx))
            cx_, cy_ = ox + nx * d / 2, oy + ny * d / 2
            jj, ii = rect(cx_, cy_, w / 2, d / 2, deg_, 2)
            if occ[jj, ii].any() or not inpoly(ox + nx * d, oy + ny * d):
                continue
            occ[jj, ii] = True
            OBJ.extend(town_lots.to_world(it, ox, oy, dx, dy) for it in items)
            lot_rects.append((cx_, cy_, w / 2, d / 2, deg_, g))
            print("lot", kind)
            break
        else:
            print("no room for lot", kind)
# --- buildings close along the streets, in the street's direction ---
themes = json.load(open(f"{PACKS}/building_themes.json"))
MIX = themes[OPT.get("theme", "arabic")]
names = list(MIX)
wts = np.array([MIX[n] for n in names], float)
wts /= wts.sum()
want = int(pack["buildings_per_1000_street"] * sl / 1000)
buildings = []
OWNER = "teamPlyrCivilian"
def count_of(n):
    return sum(1 for b_ in buildings if b_[5] == n)
for a, b, t in [frontage[k] for k in rng.permutation(len(frontage))] * 5:
    if len(buildings) >= want:
        break
    L = math.dist(a, b)
    dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    nx, ny = -dy, dx
    tang = math.degrees(math.atan2(dy, dx))
    s = rng.uniform(26, 60)
    while s < L - 26 and len(buildings) < want:
        cap = np.array([count_of(n) < 1 + wts[k_] * want * 1.6 for k_, n in enumerate(names)])
        p_ = wts * cap
        if p_.sum() == 0:
            break
        name = str(rng.choice(names, p=p_ / p_.sum()))
        ga, gb = geo.get(name, [20, 20])
        if rng.random() < 0.62:
            along, depth = ga, gb
            deg_ = tang if rng.random() < 0.6 else tang + 180
        else:
            along, depth = gb, ga
            deg_ = tang + (90 if rng.random() < 0.5 else -90)
        gap = rng.uniform(*pack["gap_to_street"])
        off = WIDTH[t] / 2 + gap + depth
        x, y = a[0] + dx * (s + along) + nx * off, a[1] + dy * (s + along) + ny * off
        s_next = s + along * 2
        jj, ii = rect(x, y, ga, gb, deg_, 5)
        if s_next < L - 10 and inpoly(x, y) and not occ[jj, ii].any():
            occ[jj, ii] = True
            OBJ.append({"template": name, "x": round(x), "y": round(y), "angle_deg": round(deg_ % 360, 1), "owner": OWNER})
            buildings.append((x, y, ga, gb, deg_, name))
            s = s_next + rng.uniform(8, 40)
        else:
            s += 30
        if rng.random() < 0.35:
            s += rng.uniform(40, 140)
print("buildings", len(buildings), "want", want, "per 1000 street", round(len(buildings) / sl * 1000, 1), collections.Counter(b_[5] for b_ in buildings).most_common(8))
# --- ground ---
W = GN
XC, YC = np.meshgrid((np.arange(W) - B) * 10.0 + 5, (np.arange(W) - B) * 10.0 + 5)
d_st = np.full((W, W), 1e9)
for a, b, t in SEG:
    dx, dy = b[0] - a[0], b[1] - a[1]
    tt = np.clip(((XC - a[0]) * dx + (YC - a[1]) * dy) / (dx * dx + dy * dy), 0, 1)
    d_st = np.minimum(d_st, np.hypot(XC - a[0] - tt * dx, YC - a[1] - tt * dy) - WIDTH[t] / 2)
db = np.full((W, W), 1e9)
for x, y, ga, gb, deg_, _n in buildings:
    r = math.radians(deg_)
    u = (XC - x) * math.cos(r) + (YC - y) * math.sin(r)
    v = -(XC - x) * math.sin(r) + (YC - y) * math.cos(r)
    db = np.minimum(db, np.maximum(np.abs(u) - ga, np.abs(v) - gb))
def noise(k, sigma):
    n = nd.gaussian_filter(np.random.default_rng(SEED + k).standard_normal((W, W)), sigma)
    return n / n.std()
n_e, n_m, n_p = noise(21, 1.8), noise(22, 3.0), noise(23, 4.0)
# paved around the houses and joining them to the street, frayed at the edge
town = (db < 26 + n_e * 8) | ((d_st < 12 + n_e * 5) & (db < 70))
patch = (db > 22) & (db < 110) & (d_st > 14) & (n_p > 0.55)
town &= ~patch
lotdirt = np.zeros((W, W), bool)
for x, y, a_, b_, deg_, g in lot_rects:
    r = math.radians(deg_)
    u = (XC - x) * math.cos(r) + (YC - y) * math.sin(r)
    v = -(XC - x) * math.sin(r) + (YC - y) * math.cos(r)
    m_ = (np.abs(u) <= a_) & (np.abs(v) <= b_)
    if g == "dirt":
        lotdirt |= m_
    else:
        town |= m_
town &= ~lotdirt
patch &= ~lotdirt & ~town
MASKS = {}
acc = 0.0
floors = list(PR["floor"].items())
srt = np.sort(n_m[town]) if town.any() else np.zeros(1)
lo = srt[0]
for k, (name, share) in enumerate(floors):
    acc += share
    hi = srt[min(int(acc * len(srt)), len(srt) - 1)] if k < len(floors) - 1 else 1e9
    MASKS[name] = town & (n_m >= lo) & (n_m < hi if k < len(floors) - 1 else True)
    lo = hi
MASKS[PR["patch"]] = patch if PR["patch"] not in MASKS else MASKS[PR["patch"]] | patch
if lotdirt.any():
    MASKS["DirtMediumType12"] = lotdirt
for n_, m_ in MASKS.items():
    Image.fromarray((SITE.crop(m_) * 255).astype(np.uint8)).save(f"{OUT}/{n_}.png")
Image.fromarray(np.full((W - 1, W - 1), 255, np.uint8)).save(f"{OUT}/base.png")
print("cells", {k: int(v.sum()) for k, v in MASKS.items()})
# --- trees and bushes in small groups beside the houses, furniture near them ---
tn = list(PR["trees"])
tw = np.array([PR["trees"][n] for n in tn], float)
tw /= tw.sum()
want_t = int(PR["trees_rate"] * sl / 1000)
tpts = []
def tree(x, y):
    if not inpoly(x, y) or any(math.dist((x, y), q) < 24 for q in tpts):
        return False
    jj, ii = rect(x, y, 7, 7, 0, 1)
    if occ[jj, ii].any():
        return False
    occ[jj, ii] = True
    tpts.append((x, y))
    OBJ.append({"template": str(rng.choice(tn, p=tw)), "x": round(x), "y": round(y), "angle_deg": int(rng.integers(-180, 180))})
    return True
jj_, ii_ = np.nonzero((db > 6) & (db < 60) & (d_st > 4))
for k in rng.permutation(len(jj_)):
    if len(tpts) >= want_t:
        break
    x, y = XC[jj_[k], ii_[k]] + rng.uniform(-4, 4), YC[jj_[k], ii_[k]] + rng.uniform(-4, 4)
    if tree(x, y):
        for _ in range(int(rng.integers(0, 3))):
            tree(x + rng.uniform(-40, 40), y + rng.uniform(-40, 40))
placed = collections.Counter()
for name, rate in PR["props"].items():
    n = int(round(rate * sl / 1000))
    for k in rng.permutation(len(buildings)):
        if placed[name] >= n:
            break
        x, y, ga, gb, deg_, _n = buildings[k]
        r = math.radians(deg_)
        side = 1 if rng.random() < 0.5 else -1
        u, v = side * (ga + rng.uniform(8, 18)), rng.uniform(-gb, gb)
        qx, qy = x + u * math.cos(r) - v * math.sin(r), y + u * math.sin(r) + v * math.cos(r)
        ga2, gb2 = geo.get(name, [6, 6])
        jj, ii = rect(qx, qy, min(ga2, 12), min(gb2, 12), deg_, 1)
        if occ[jj, ii].any() or any(o["template"] == name and math.dist((o["x"], o["y"]), (qx, qy)) < 140 for o in OBJ):
            continue
        occ[jj, ii] = True
        OBJ.append({"template": name, "x": round(qx), "y": round(qy), "angle_deg": round(deg_ % 360, 1)})
        placed[name] += 1
if "lamp" in PR:
    lamp, rate = PR["lamp"]
    n = int(rate * sl / 1000)
    for k in rng.permutation(len(frontage)):
        if placed[lamp] >= n:
            break
        a, b, t = frontage[k]
        L = math.dist(a, b)
        if L < 60:
            continue
        dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
        s = rng.uniform(20, L - 20)
        off = WIDTH[t] / 2 + 6
        qx, qy = a[0] + dx * s - dy * off, a[1] + dy * s + dx * off
        jj, ii = rect(qx, qy, 3, 3, 0, 1)
        if occ[jj, ii].any() or any(o["template"] == lamp and math.dist((o["x"], o["y"]), (qx, qy)) < 120 for o in OBJ):
            continue
        occ[jj, ii] = True
        OBJ.append({"template": lamp, "x": round(qx), "y": round(qy), "angle_deg": round(math.degrees(math.atan2(dy, dx)) % 360, 1)})
        placed[lamp] += 1
print("trees", len(tpts), "want", want_t, "props", dict(placed), "objects", len(OBJ))
def exit_of(a, q):
    L = math.dist(a, q) or 1
    return {"point": [round(q[0], 1), round(q[1], 1)], "type": MAJOR, "width": WIDTH[MAJOR], "dir": [(q[0] - a[0]) / L, (q[1] - a[1]) / L]}
json.dump({"objects": OBJ, "roads": roads, "surround": PR["surround"], "masks": list(MASKS), "exits": [exit_of(seq[1], seq[0]), exit_of(seq[-2], seq[-1])]}, open(f"{OUT}/plan.json", "w"), separators=(",", ":"))
# preview
SC = 0.5
VIEW = max(SITE.xmax, SITE.ymax) + 10
im = Image.new("RGB", (int(VIEW * SC), int(VIEW * SC)), (120, 100, 70))
dr = ImageDraw.Draw(im)
PX = lambda x, y: (x * SC, (VIEW - y) * SC)
for m_, col in ((town, (170, 150, 120)), (patch, (90, 120, 70)), (lotdirt, (140, 100, 60))):
    for j, i in zip(*np.nonzero(m_)):
        a, b = PX((i - B) * 10, (j - B) * 10 + 10)
        dr.rectangle((a, b, a + 10 * SC, b + 10 * SC), fill=col)
for a, b, t in SEG:
    dr.line([PX(*a), PX(*b)], fill=(40, 40, 40), width=max(2, int(WIDTH[t] * SC)))
for x, y, ga, gb, deg_, _n in buildings:
    r = math.radians(deg_)
    q = [(x + u * math.cos(r) - v * math.sin(r), y + u * math.sin(r) + v * math.cos(r)) for u, v in ((-ga, -gb), (ga, -gb), (ga, gb), (-ga, gb))]
    dr.polygon([PX(*p) for p in q], fill=(200, 80, 70))
im.save(f"{OUT}/preview.png")
