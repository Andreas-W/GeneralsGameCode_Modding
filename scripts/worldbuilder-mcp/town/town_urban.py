# Block-based city from a style pack: street hierarchy, sidewalk rings around blocks, buildings along them, parking, concrete floor, grass islands.
# Usage: town_urban.py <pack> <seed> [jitter=<0..>] [theta=<degrees>] [grid_scale=<factor>] [theme=<name>] [lots=<kind:count,...>] [out=<folder>] [site=<file>]
#   theme picks the building mix from packs/building_themes.json (default: the pack's mix). lots: see town_lots.py.
#   jitter scales the pack's street node jitter (0 = perfect grid); theta turns the grid (0 = axis aligned). Both default to the pack.
#   grid_scale scales the pack's block size: below 1 for small or broken-up sites, where the pack's blocks leave no street to fit.
#   major=<road type> builds the major streets as that road type (e.g. the minor one) with its width; contain=1 keeps the major streets
#   inside the site instead of running them on past it, e.g. on an island whose edges are cliffs.
#   street_style=<pack> builds streets, sidewalks, ground, props, cars and trees as that pack does, on this pack's block layout (e.g. the
#   denser urban layout with Baghdad's old roads, narrow sidewalks and rocky ground for an Arabic city).
import json, math, os, sys, collections
import numpy as np
import scipy.ndimage as nd
from PIL import Image, ImageDraw
import town_lots
from town_site import Site
from town_paths import PACKS  # enters the work folder
pack = json.load(open(f"{PACKS}/{sys.argv[1]}.json"))
geo = json.load(open("gamedata/geo.json"))
SEED = int(sys.argv[2])
rng = np.random.default_rng(SEED)
OPT = dict(a.split("=", 1) for a in sys.argv[3:])
if "street_style" in OPT:
    look = json.load(open(f"{PACKS}/{OPT['street_style']}.json"))
    pack = dict(pack, streets=dict(pack["streets"], **{k: look["streets"][k] for k in ("major", "minor", "major_width", "minor_width")}),
                **{k: look[k] for k in ("sidewalk", "ground", "parking", "props_per_1000_street", "stoplights_per_major_junction",
                                        "cars_per_1000_street", "cars", "trees_per_1000_street", "trees")})
OUT = "plans/" + OPT.get("out", "urban_plan")
os.makedirs(OUT, exist_ok=True)
# --- the site ---
SITE = Site(OPT.get("site"), [(620, 480), (1720, 620), (1880, 1500), (1240, 1880), (560, 1500)])
POLY = SITE.poly
B, GN = SITE.B, SITE.GN
def inpoly(x, y, poly=POLY):
    c = False
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
            c = not c
    return c
ST = pack["streets"]
GS = float(OPT.get("grid_scale", 1.0))
ST = dict(ST, grid=[g * GS for g in ST["grid"]], jitter=[j * GS for j in ST["jitter"]])
MAJOR, MINOR = ST["major"], ST["minor"]
WIDTH = {MAJOR: ST["major_width"], MINOR: ST["minor_width"]}
# Street kinds stay apart (layout, sidewalks); TYPE says which road each is built as.
TYPE = {MAJOR: MAJOR, MINOR: MINOR}
if "major" in OPT:
    TYPE[MAJOR] = OPT["major"]
    if OPT["major"] == MINOR:
        WIDTH[MAJOR] = WIDTH[MINOR]
CONTAIN = OPT.get("contain", "0") not in ("0", "false", "False")
TH = math.radians(float(OPT.get("theta", ST["theta_deg"])))
JIT = float(OPT.get("jitter", 1.0))
cx, cy = np.mean([p[0] for p in POLY]), np.mean([p[1] for p in POLY])
def build_grid(angle, ou, ov):
    c_, s_ = math.cos(angle), math.sin(angle)
    nodes = {}
    for i in range(-8, 9):
        for j in range(-8, 9):
            u = ou + i * ST["grid"][0] + JIT * rng.uniform(-ST["jitter"][0], ST["jitter"][0])
            v = ov + j * ST["grid"][1] + JIT * rng.uniform(-ST["jitter"][1], ST["jitter"][1])
            x, y = cx + u * c_ - v * s_, cy + u * s_ + v * c_
            if inpoly(x, y):
                nodes[(i, j)] = (x, y)
    edges = set()
    for (i, j) in nodes:
        for d in ((1, 0), (0, 1)):
            k = (i + d[0], j + d[1])
            if k in nodes:
                edges.add(((i, j), k))
    return edges, nodes
edges, nodes = SITE.best_grid(build_grid, rng, TH, ST["grid"], "theta" not in OPT)
if not edges:
    raise SystemExit("no street fits: this area is too small or too blocked for this grid")
# one major street each way through the middle
mi = min({k[0] for k in nodes}, key=abs)
mj = min({k[1] for k in nodes}, key=abs)
major = {e for e in edges if e[0][1] == e[1][1] == mj or e[0][0] == e[1][0] == mi}
def connected(es):
    adj = collections.defaultdict(set)
    for a, b in es:
        adj[a].add(b)
        adj[b].add(a)
    st, seen = [next(iter(adj))], set()
    while st:
        n = st.pop()
        if n not in seen:
            seen.add(n)
            st.extend(adj[n] - seen)
    return len(seen) == len(adj)
for e in sorted(edges, key=lambda _: rng.random()):
    if e not in major and rng.random() < 0.14:
        rest = edges - {e}
        deg = collections.Counter(n for x in rest for n in x)
        if connected(rest) and deg[e[0]] >= 2 and deg[e[1]] >= 2:
            edges = rest
while True:
    deg = collections.Counter(n for x in edges for n in x)
    dead = [k for k, v in deg.items() if v == 1 and not any(k in x for x in major)]
    if not dead:
        break
    edges = {e for e in edges if e[0] not in dead and e[1] not in dead}
used = {n for e in edges for n in e}
P = {n: nodes[n] for n in used}
etype = {e: (MAJOR if e in major else MINOR) for e in edges}
def typeof(a, b):
    return etype.get((a, b)) or etype.get((b, a))
adj = collections.defaultdict(set)
for a, b in edges:
    adj[a].add(b)
    adj[b].add(a)
# major streets run on past the town at both ends
entries = []
for axis, fixed in ((0, mj), (1, mi)):
    line = sorted((n for n in used if n[1 - axis] == fixed and any(typeof(n, m) == MAJOR for m in adj[n])), key=lambda n: n[axis])
    if len(line) < 2:
        continue
    for end, nxt in ((line[0], line[1]), (line[-1], line[-2])):
        ax, ay = P[end]
        dx, dy = ax - P[nxt][0], ay - P[nxt][1]
        L = math.hypot(dx, dy)
        q = (ax + dx / L * 340, ay + dy / L * 340)
        q = SITE.clip((ax, ay), SITE.clamp(q))
        if math.dist((ax, ay), q) > 40 and not CONTAIN:
            entries.append((end, q))
# --- road runs ---
def chains_of(kind):
    es = {e for e in edges if etype[e] == kind}
    a2 = collections.defaultdict(set)
    for a, b in es:
        a2[a].add(b)
        a2[b].add(a)
    out, seen = [], set()
    def through(n):
        return len(a2[n]) == 2 and (kind == MAJOR or len(adj[n]) == 2)
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
roads = []
major_chains = chains_of(MAJOR)
for ch in major_chains:
    pts = [P[n] for n in ch]
    for end, q in entries:
        if ch[0] == end:
            pts = [q] + pts
        elif ch[-1] == end:
            pts = pts + [q]
    roads.append({"type": TYPE[MAJOR], "corners": "tight", "end_caps": False, "points": [[round(x, 1), round(y, 1)] for x, y in pts]})
entry_nodes = {e for e, _ in entries}
def beside_major(n, m):
    """Where a minor street from m ends at node n: at the major street's edge, not on it. The major street keeps its connection."""
    major_nb = [q for q in adj[n] if typeof(n, q) == MAJOR]
    if not major_nb:
        return P[n]
    q = major_nb[0]
    Lm, L = math.dist(P[n], P[q]), math.dist(P[n], P[m])
    mx, my = (P[q][0] - P[n][0]) / Lm, (P[q][1] - P[n][1]) / Lm
    dx, dy = (P[m][0] - P[n][0]) / L, (P[m][1] - P[n][1]) / L
    d = WIDTH[MAJOR] / 2 / max(0.5, abs(mx * dy - my * dx))
    return (P[n][0] + dx * d, P[n][1] + dy * d)
for ch in chains_of(MINOR):
    pts = [P[n] for n in ch]
    pts[0] = beside_major(ch[0], ch[1])
    pts[-1] = beside_major(ch[-1], ch[-2])
    roads.append({"type": MINOR, "corners": "tight", "end_caps": True, "points": [[round(x, 1), round(y, 1)] for x, y in pts]})
SEG = [(P[a], P[b], etype[(a, b)]) for a, b in edges] + [(P[e], q, MAJOR) for e, q in entries]
street_len = sum(math.dist(a, b) for a, b, _ in SEG if inpoly(*a) and inpoly(*b))
print("nodes", len(P), "streets", len(edges), "length", round(street_len), "major share", round(sum(math.dist(a, b) for a, b, t in SEG if t == MAJOR and inpoly(*a) and inpoly(*b)) / street_len, 2))
site_area = 0.5 * abs(sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(POLY, POLY[1:] + POLY[:1])))
print("street per 100x100", round(street_len / site_area * 1e4, 1), "measured", pack["measured"]["street_per_100x100"])
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
    for t in np.linspace(0, 1, max(2, int(math.dist(a, b) / 3))):
        i, j = oc(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        # round, not square: a square stamp reaches far past the edge of a street at 45 degrees
        r = int(hw / OS) + 1
        yy, xx = np.ogrid[max(j - r, 0):j + r + 1, max(i - r, 0):i + r + 1]
        k = (yy - j) ** 2 + (xx - i) ** 2 <= (hw / OS) ** 2
        occ[max(j - r, 0):j + r + 1, max(i - r, 0):i + r + 1] |= k
for a, b, t in SEG:
    mark_seg(a, b, WIDTH[t] / 2)
# --- blocks: faces of the street graph, outer face last ---
def angle(a, b):
    return math.atan2(P[b][1] - P[a][1], P[b][0] - P[a][0])
next_he = {}
for n in adj:
    order = sorted(adj[n], key=lambda m: angle(n, m))
    for k, m in enumerate(order):
        # arriving from m at n, leave by the next street clockwise: the face stays on the left
        next_he[(m, n)] = (n, order[k - 1])
faces, seen = [], set()
for he in next_he:
    if he in seen:
        continue
    f = []
    h = he
    while h not in seen:
        seen.add(h)
        f.append(h[0])
        h = next_he[h]
    faces.append(f)
def signed_area(f):
    return 0.5 * sum(P[a][0] * P[b][1] - P[b][0] * P[a][1] for a, b in zip(f, f[1:] + f[:1]))
blocks = [f for f in faces if signed_area(f) > 0]
outer = [f for f in faces if signed_area(f) < 0]
if not blocks:
    raise SystemExit(f"only {len(edges)} streets fit and they enclose no block: this area is too small or too blocked for a block town")
print("blocks", len(blocks), "outer faces", len(outer), "median block area", round(float(np.median([signed_area(f) for f in blocks]))))
SW = pack["sidewalk"]
OFFS = {t: w * SW["offset_ratio"] for t, w in WIDTH.items()}   # 17 for the 35 wide street, checked on screen; the same share of the 60 wide one
TIGHT = {t: 0.5 * w for t, w in WIDTH.items()}
def ring(f):
    """Sidewalk line left of the face's streets. Where the street curves and the line is on its outside, it follows the arc in mitred pieces;
    everywhere else, crossings included, it has a plain angled corner because the street has no curve there either."""
    n = len(f)
    dirs, offs, types = [], [], []
    for k in range(n):
        a, b = f[k], f[(k + 1) % n]
        L = math.dist(P[a], P[b])
        dirs.append(((P[b][0] - P[a][0]) / L, (P[b][1] - P[a][1]) / L))
        t = typeof(a, b)
        types.append(t)
        offs.append(OFFS[t])
    pts, straight = [], []
    for k in range(n):
        v = f[k]
        d0, d1 = dirs[k - 1], dirs[k]
        o0, o1 = offs[k - 1], offs[k]
        n0, n1 = (-d0[1], d0[0]), (-d1[1], d1[0])
        a0 = (P[v][0] + n0[0] * o0, P[v][1] + n0[1] * o0)
        a1 = (P[v][0] + n1[0] * o1, P[v][1] + n1[1] * o1)
        cr = d0[0] * d1[1] - d0[1] * d1[0]
        deg = math.degrees(math.atan2(cr, d0[0] * d1[0] + d0[1] * d1[1]))
        if abs(cr) < 1e-6:
            c = a1
        else:
            t = ((a1[0] - a0[0]) * d1[1] - (a1[1] - a0[1]) * d1[0]) / cr
            c = (a0[0] + d0[0] * t, a0[1] + d0[1] * t)
        curve = len(adj[v]) == 2 and v not in entry_nodes and types[k - 1] == types[k]
        R = None
        if abs(deg) >= 27 and curve and deg < 0:
            R = TIGHT[types[k]] + offs[k]
        if R is None:
            pts.append(c)
            continue
        tl = R * math.tan(math.radians(abs(deg)) / 2)
        T0 = (c[0] - d0[0] * tl, c[1] - d0[1] * tl)
        sgn = 1 if deg > 0 else -1
        C = (T0[0] + n0[0] * R * sgn, T0[1] + n0[1] * R * sgn)
        a_ = math.atan2(T0[1] - C[1], T0[0] - C[0])
        st = max(2, math.ceil(abs(deg) / 25))
        for m in range(st + 1):
            b_ = a_ + math.radians(deg) * m / st
            pts.append((C[0] + R * math.cos(b_), C[1] + R * math.sin(b_)))
    return pts
walk = []
block_rings = {}
for idx, f in enumerate(blocks):
    pts = ring(f)
    block_rings[idx] = pts
    if SW["type"]:
        walk.append({"type": SW["type"], "corners": "angled", "end_caps": False, "points": [[round(x, 1), round(y, 1)] for x, y in pts + pts[:1]]})
# outer side: the same line, broken where a street leaves the town
outer_runs = 0
for f in outer:
    pts = ring(f)
    runs, cur = [], []
    def blocked(p):
        for e, q in entries:
            a, b = P[e], q
            dx, dy = b[0] - a[0], b[1] - a[1]
            t = max(0, min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
            if math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy) < WIDTH[MAJOR] / 2 + 34:
                return True
        return False
    start = next((k for k in range(len(pts)) if blocked(pts[k])), 0)
    for k in range(len(pts)):
        p = pts[(start + k) % len(pts)]
        if blocked(p):
            if len(cur) >= 2:
                runs.append(cur)
            cur = []
        else:
            cur.append(p)
    if len(cur) >= 2:
        runs.append(cur)
    for r in runs:
        L = sum(math.dist(a, b) for a, b in zip(r[:-1], r[1:]))
        if SW["type"] and L > 70 and rng.random() < SW["outer_keep"]:
            walk.append({"type": SW["type"], "corners": "angled", "end_caps": False, "points": [[round(x, 1), round(y, 1)] for x, y in r]})
            outer_runs += 1
print("sidewalk rings", len(blocks), "outer runs", outer_runs, "length", round(sum(math.dist(a, b) for w in walk for a, b in zip(w["points"][:-1], w["points"][1:]))))
# --- buildings along the ring edges ---
BD = pack["buildings"]
MIX = BD["mix"]
if "theme" in OPT:
    MIX = json.load(open(f"{PACKS}/building_themes.json"))[OPT["theme"]]
names = list(MIX)
wts = np.array([MIX[n] for n in names], float)
wts /= wts.sum()
want = int(BD["per_1000_street"] * street_len / 1000)
OBJ = []
owner = "teamPlyrCivilian"
sites = []
def edges_of(pts):
    out = []
    for a, b in zip(pts, pts[1:] + pts[:1]):
        L = math.dist(a, b)
        if L > 45:
            out.append((a, b, L))
    return out
def polymask_ok(pts, cx_, cy_, w, d, deg):
    """All corners of the rectangle inside the sidewalk ring."""
    r = math.radians(deg)
    for u, v in ((-w / 2, -d / 2), (w / 2, -d / 2), (w / 2, d / 2), (-w / 2, d / 2)):
        if not inpoly(cx_ + u * math.cos(r) - v * math.sin(r), cy_ + u * math.sin(r) + v * math.cos(r), pts):
            return False
    return True
buildings = []
def try_building(a, b, L, s, name):
    ga, gb = geo.get(name, [20, 20])
    dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    nx, ny = -dy, dx
    roll = rng.random()
    al = pack["measured"]["aligned_to_street"]["along"] / (pack["measured"]["aligned_to_street"]["along"] + pack["measured"]["aligned_to_street"]["square"])
    tang = math.degrees(math.atan2(dy, dx))
    if roll < al:
        along, depth = ga, gb
        deg = tang if rng.random() < 0.6 else tang + 180    # front or back to the street
    else:
        along, depth = gb, ga
        deg = tang + (90 if rng.random() < 0.5 else -90)
    gap = rng.uniform(*BD["gap_to_sidewalk"])
    if s + along * 2 > L:
        return None
    x = a[0] + dx * (s + along) + nx * (gap + depth)
    y = a[1] + dy * (s + along) + ny * (gap + depth)
    jj, ii = rect(x, y, ga, gb, deg, 4)
    if occ[jj, ii].any():
        return None
    occ[jj, ii] = True
    OBJ.append({"template": name, "x": round(x), "y": round(y), "angle_deg": round(deg % 360, 1), "owner": owner})
    buildings.append((x, y, ga, gb, deg))
    return along * 2
all_edges = [e for pts in block_rings.values() for e in edges_of(pts)]
# special lots first, on their own blocks while there are blocks left: they need the room, and buildings fill in around them
lot_rects = []
OWNER_EDGES = [(idx, e) for idx, pts in block_rings.items() for e in edges_of(pts)]
used_blocks = collections.Counter()
for kind, count in town_lots.parse_spec(OPT.get("lots", "")):
    for _ in range(count):
        w, d, g, items = town_lots.layout(kind, rng)
        cands = [OWNER_EDGES[k] for k in rng.permutation(len(OWNER_EDGES))]
        cands.sort(key=lambda c: used_blocks[c[0]])
        for idx, (a, b, L) in cands:
            if L < w + 12:
                continue
            dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
            nx, ny = -dy, dx
            s = rng.uniform(w / 2 + 6, L - w / 2 - 6)
            lg = SW.get("lot_gap", 5)
            ox, oy = a[0] + dx * s + nx * lg, a[1] + dy * s + ny * lg
            deg = math.degrees(math.atan2(dy, dx))
            cx_, cy_ = ox + nx * d / 2, oy + ny * d / 2
            jj, ii = rect(cx_, cy_, w / 2, d / 2, deg, 2)
            if occ[jj, ii].any() or not polymask_ok(block_rings[idx], cx_, cy_, w, d, deg):
                continue
            occ[jj, ii] = True
            OBJ.extend(town_lots.to_world(it, ox, oy, dx, dy) for it in items)
            lot_rects.append((cx_, cy_, w / 2, d / 2, deg, g))
            used_blocks[idx] += 1
            print("lot", kind, "block", idx)
            break
        else:
            print("no room for lot", kind)
# block by block in turn, so that every block gets its share
per_block = []
for pts in block_rings.values():
    es = edges_of(pts)
    per_block.append([es[k] for k in rng.permutation(len(es))])
order = []
for k in range(max(map(len, per_block))):
    for es in per_block:
        if k < len(es):
            order.append(es[k])
# a few rounds: lots and large buildings of a theme can leave a round short of the wanted count
for a, b, L in order * 4:
    if len(buildings) >= want:
        break
    s = rng.uniform(4, 30)
    while s < L - 40 and len(buildings) < want:
        for _ in range(int(rng.integers(1, 4))):
            # large buildings fail more often, so a cap per type keeps the small ones from taking over the theme
            cap = np.array([sum(1 for q in OBJ if q["template"] == n) < 1 + wts[k_] * want * 1.5 for k_, n in enumerate(names)])
            p_ = wts * cap
            w = try_building(a, b, L, s, str(rng.choice(names, p=p_ / p_.sum()))) if p_.sum() > 0 else None
            if w is None:
                break
            s += w + rng.uniform(6, 20)
        s += rng.uniform(70, 190)
print("buildings", len(buildings), "want", want, "per 1000 street", round(len(buildings) / street_len * 1000, 1), collections.Counter(o["template"] for o in OBJ).most_common())
# --- parking lots inside the blocks ---
PK = pack["parking"]
lots = []
plen = 0
want_park = PK["share_of_street"] * street_len
CARS = pack["cars"]
def place_car(x, y, deg, in_lot=False):
    jj, ii = rect(x, y, 6, 12, deg, 2)
    if not in_lot and occ[jj, ii].any():
        return False
    occ[jj, ii] = True
    OBJ.append({"template": str(rng.choice(CARS)), "x": round(x), "y": round(y), "angle_deg": round(deg % 360, 1)})
    return True
for a, b, L in [order[k] for k in rng.permutation(len(order))]:
    if plen >= want_park:
        break
    if L < PK["row_length"] + 30:
        continue
    dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
    nx, ny = -dy, dx
    deg = math.degrees(math.atan2(dy, dx))
    rows = int(rng.integers(1, 3))
    s = rng.uniform(10, L - PK["row_length"] - 10)
    d0 = rng.uniform(26, 60)
    mx, my = a[0] + dx * (s + PK["row_length"] / 2), a[1] + dy * (s + PK["row_length"] / 2)
    depth = (rows - 1) * PK["row_spacing"] + 24
    cxl, cyl = mx + nx * (d0 + depth / 2 - 12), my + ny * (d0 + depth / 2 - 12)
    jj, ii = rect(cxl, cyl, PK["row_length"] / 2 + 8, depth / 2 + 6, deg, 2)
    if occ[jj, ii].any():
        continue
    occ[jj, ii] = True
    lots.append((cxl, cyl, PK["row_length"] / 2 + 12, depth / 2 + 10, deg))
    for r_ in range(rows):
        off = d0 + r_ * PK["row_spacing"]
        p0 = (a[0] + dx * s + nx * off, a[1] + dy * s + ny * off)
        p1 = (p0[0] + dx * PK["row_length"], p0[1] + dy * PK["row_length"])
        roads.append({"type": PK["type"], "corners": "angled", "end_caps": False, "points": [[round(p0[0], 1), round(p0[1], 1)], [round(p1[0], 1), round(p1[1], 1)]]})
        plen += PK["row_length"]
print("parking lots", len(lots), "row length", plen, "want", round(want_park))
# --- street furniture ---
EDGE_FOR = {"StreetLamp03": 9, "BusStop": 8, "PhoneBooth": 14, "MailBox": 14}   # measured from the street edge, here from the sidewalk line
def prop_near_building(name, count, along_min=26):
    placed = 0
    for k in rng.permutation(len(all_edges)):
        if placed >= count:
            break
        a, b, L = all_edges[k]
        if L < along_min * 2:
            continue
        dx, dy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
        nx, ny = -dy, dx
        s = rng.uniform(along_min, L - along_min)
        off = EDGE_FOR.get(name, 8)
        x, y = a[0] + dx * s + nx * off, a[1] + dy * s + ny * off
        # next to a building, where people would use it
        if name not in ("StreetLamp03", "BusStop") and not any(math.hypot(x - q[0], y - q[1]) < max(q[2], q[3]) + 26 for q in buildings):
            continue
        if any(o["template"] == name and math.dist((o["x"], o["y"]), (x, y)) < 180 for o in OBJ):
            continue
        ga, gb = geo.get(name, [4, 4])
        deg = math.degrees(math.atan2(dy, dx))
        jj, ii = rect(x, y, min(ga, 8), min(gb, 8), deg, 1)
        if occ[jj, ii].any():
            continue
        occ[jj, ii] = True
        OBJ.append({"template": name, "x": round(x), "y": round(y), "angle_deg": round(deg % 360, 1)})
        placed += 1
    return placed
counts = {}
for name, rate in pack["props_per_1000_street"].items():
    if name in ("Dumpster", "TrashDumpster"):
        continue
    counts[name] = prop_near_building(name, int(round(rate * street_len / 1000)))
# bins stand behind the buildings
for name in ("Dumpster", "TrashDumpster"):
    n = int(round(pack["props_per_1000_street"].get(name, 0) * street_len / 1000))
    placed = 0
    for k in rng.permutation(len(buildings)):
        if placed >= n:
            break
        x, y, ga, gb, deg = buildings[k]
        r = math.radians(deg)
        bx, by = x - math.sin(r) * (gb + 12) * -1, y + math.cos(r) * (gb + 12)
        jj, ii = rect(bx, by, 10, 6, deg, 1)
        if occ[jj, ii].any():
            continue
        occ[jj, ii] = True
        OBJ.append({"template": name, "x": round(bx), "y": round(by), "angle_deg": round(deg % 360, 1)})
        placed += 1
    counts[name] = placed
# stop lights on opposite corners of the major junctions
lights = 0
for n in used:
    if len(adj[n]) < 3 or not any(typeof(n, m) == MAJOR for m in adj[n]):
        continue
    corners = []
    for f, pts in block_rings.items():
        if n in blocks[f]:
            p = min(pts, key=lambda q: math.dist(q, P[n]))
            corners.append(p)
    corners.sort(key=lambda q: math.atan2(q[1] - P[n][1], q[0] - P[n][0]))
    for p in corners[::2][:pack["stoplights_per_major_junction"]]:
        OBJ.append({"template": "StopLight", "x": round(p[0]), "y": round(p[1]), "angle_deg": round(math.degrees(math.atan2(P[n][1] - p[1], P[n][0] - p[0])) % 360, 1)})
        lights += 1
counts["StopLight"] = lights
# cars in the lots and in front of houses
ncars = int(pack["cars_per_1000_street"] * street_len / 1000)
cars = 0
for x, y, a, b, deg in lots:
    for _ in range(int(rng.integers(1, 4))):
        r = math.radians(deg)
        u, v = rng.uniform(-a + 10, a - 10), rng.uniform(-b + 8, b - 8)
        if place_car(x + u * math.cos(r) - v * math.sin(r), y + u * math.sin(r) + v * math.cos(r), deg + 90 + (180 if rng.random() < 0.5 else 0), True):
            cars += 1
for k in rng.permutation(len(buildings)):
    if cars >= ncars:
        break
    x, y, ga, gb, deg = buildings[k]
    r = math.radians(deg)
    side = 1 if rng.random() < 0.5 else -1
    u, v = side * (ga + 12), rng.uniform(-gb, gb)
    if place_car(x + u * math.cos(r) - v * math.sin(r), y + u * math.sin(r) + v * math.cos(r), deg + rng.uniform(-8, 8)):
        cars += 1
counts["cars"] = cars
print("furniture", counts)
# --- ground ---
W = GN
XC, YC = np.meshgrid((np.arange(W) - B) * 10.0 + 5, (np.arange(W) - B) * 10.0 + 5)
def polymask(pts):
    im = Image.new("L", (W, W), 0)
    ImageDraw.Draw(im).polygon([((x / 10 + B), (y / 10 + B)) for x, y in pts], fill=255)
    return np.array(im) > 0
inblock = np.zeros((W, W), bool)
for pts in block_rings.values():
    inblock |= polymask(pts)
d_st = np.full((W, W), 1e9)
for a, b, t in SEG:
    dx, dy = b[0] - a[0], b[1] - a[1]
    tt = np.clip(((XC - a[0]) * dx + (YC - a[1]) * dy) / (dx * dx + dy * dy), 0, 1)
    d_st = np.minimum(d_st, np.hypot(XC - a[0] - tt * dx, YC - a[1] - tt * dy) - WIDTH[t] / 2)
def noise(k, sigma):
    n = nd.gaussian_filter(np.random.default_rng(SEED + k).standard_normal((W, W)), sigma)
    return n / n.std()
n_e, n_i, n_c = noise(11, 1.6), noise(12, 2.2), noise(13, 3.5)
in_site = np.zeros((W, W), bool)
for j in range(W):
    for i in range(W):
        in_site[j, i] = inpoly(XC[j, i], YC[j, i])
# the floor reaches a little past the outer streets and frays out
floor = inblock | (d_st < 16 + n_e * 5)
dbuild = np.full((W, W), 1e9)
for x, y, ga, gb, deg in buildings:
    r = math.radians(deg)
    u = (XC - x) * math.cos(r) + (YC - y) * math.sin(r)
    v = -(XC - x) * math.sin(r) + (YC - y) * math.cos(r)
    dbuild = np.minimum(dbuild, np.maximum(np.abs(u) - ga, np.abs(v) - gb))
park = np.zeros((W, W), bool)
for x, y, a, b, deg in lots:
    r = math.radians(deg)
    u = (XC - x) * math.cos(r) + (YC - y) * math.sin(r)
    v = -(XC - x) * math.sin(r) + (YC - y) * math.cos(r)
    park |= (np.abs(u) <= a) & (np.abs(v) <= b)
lotfloor = np.zeros((W, W), bool)
lotdirt = np.zeros((W, W), bool)
for x, y, a, b, deg, g in lot_rects:
    r = math.radians(deg)
    u = (XC - x) * math.cos(r) + (YC - y) * math.sin(r)
    v = -(XC - x) * math.sin(r) + (YC - y) * math.cos(r)
    m_ = (np.abs(u) <= a) & (np.abs(v) <= b)
    if g == "dirt":
        lotdirt |= m_
    else:
        lotfloor |= m_
# grass islands: away from the street, the buildings and the lots, as much as the floor share leaves over
inner = inblock & (dbuild > 18) & ~nd.binary_dilation(park | lotfloor | lotdirt, iterations=1)
score = np.minimum(dbuild, 70) * 0.03 + np.minimum(d_st, 70) * 0.03 + n_i * 0.7
target = (1 - pack["ground"]["floor_share"]) * inblock.sum()
vals = score[inner]
cut = np.sort(vals)[::-1][min(int(target), len(vals) - 1)]
island = inner & (score > cut)
island = nd.binary_opening(island, iterations=1)
isl = pack["ground"]["islands"]
floor_m = floor & ~island & ~park & ~lotdirt
print("floor share of block cells", round(float((floor_m & inblock).sum() / inblock.sum()), 2), "islands", round(float((island & inblock).sum() / inblock.sum()), 2), "parking", round(float((park & inblock).sum() / inblock.sum()), 2))
MASKS = {pack["ground"]["floor"]: floor_m, isl[1]: island & (n_c > 0.3), pack["ground"]["parking"]: park}
if lotdirt.any():
    MASKS[pack["ground"]["lot_dirt"]] = lotdirt
for n_, m_ in MASKS.items():
    Image.fromarray((SITE.crop(m_) * 255).astype(np.uint8)).save(f"{OUT}/{n_}.png")
Image.fromarray(np.full((W - 1, W - 1), 255, np.uint8)).save(f"{OUT}/base.png")
# --- trees on the islands ---
tn = list(pack["trees"])
tw = np.array([pack["trees"][n] for n in tn], float)
tw /= tw.sum()
want_t = int(pack["trees_per_1000_street"] * street_len / 1000)
jj_, ii_ = np.nonzero(island)
trees = 0
tpts = []
for k in rng.permutation(len(jj_)):
    if trees >= want_t:
        break
    x, y = XC[jj_[k], ii_[k]] + rng.uniform(-4, 4), YC[jj_[k], ii_[k]] + rng.uniform(-4, 4)
    if any(math.dist((x, y), q) < 26 for q in tpts):
        continue
    jj, ii = rect(x, y, 8, 8, 0, 1)
    if occ[jj, ii].any():
        continue
    occ[jj, ii] = True
    tpts.append((x, y))
    OBJ.append({"template": str(rng.choice(tn, p=tw)), "x": round(x), "y": round(y), "angle_deg": int(rng.integers(-180, 180))})
    trees += 1
print("trees", trees, "want", want_t, "objects", len(OBJ))
allroads = roads + walk
def exit_of(a, q):
    L = math.dist(a, q) or 1
    return {"point": [round(q[0], 1), round(q[1], 1)], "type": TYPE[MAJOR], "width": WIDTH[MAJOR], "dir": [(q[0] - a[0]) / L, (q[1] - a[1]) / L]}
json.dump({"objects": OBJ, "roads": allroads, "masks": list(MASKS), "surround": pack["ground"]["surround"], "blend": list(MASKS), "exits": [exit_of(P[e], q) for e, q in entries]}, open(f"{OUT}/plan.json", "w"), separators=(",", ":"))
# preview
SC = 0.5
VIEW = max(SITE.xmax, SITE.ymax) + 10
im = Image.new("RGB", (int(VIEW * SC), int(VIEW * SC)), (60, 90, 50))
dr = ImageDraw.Draw(im)
PX = lambda x, y: (x * SC, (VIEW - y) * SC)
for m_, col in ((floor_m, (150, 150, 145)), (island, (80, 120, 60)), (park, (70, 70, 75))):
    for j, i in zip(*np.nonzero(m_)):
        a, b = PX((i - B) * 10, (j - B) * 10 + 10)
        dr.rectangle((a, b, a + 10 * SC, b + 10 * SC), fill=col)
for a, b, t in SEG:
    dr.line([PX(*a), PX(*b)], fill=(40, 40, 40), width=int(WIDTH[t] * SC))
for w in allroads:
    if w["type"] == SW["type"]:
        dr.line([PX(*p) for p in w["points"]], fill=(235, 235, 235), width=1)
    elif w["type"] == PK["type"]:
        dr.line([PX(*p) for p in w["points"]], fill=(240, 220, 0), width=2)
for x, y, ga, gb, deg in buildings:
    r = math.radians(deg)
    q = [(x + u * math.cos(r) - v * math.sin(r), y + u * math.sin(r) + v * math.cos(r)) for u, v in ((-ga, -gb), (ga, -gb), (ga, gb), (-ga, gb))]
    dr.polygon([PX(*p) for p in q], fill=(200, 80, 70))
im.save(f"{OUT}/preview.png")
