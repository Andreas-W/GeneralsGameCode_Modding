# Style measures of the towns of a map: street shape and irregularity, buildings against streets, ground, props.
# Usage: town_measure.py <name> <street types, comma separated> [min buildings]   (needs refs/<name>/dump.json, tex_*.png, info.json)
import glob, json, math, os, re, sys, collections
import numpy as np
from PIL import Image
from town_paths import ROADS_INI  # enters the work folder
name, types = sys.argv[1], set(sys.argv[2].split(","))
minb = int(sys.argv[3]) if len(sys.argv) > 3 else 5
info = json.load(open(f"refs/{name}/info.json"))
B = info["heightmap"]["border"]
O = json.load(open(f"refs/{name}/dump.json"))["objects"]
geo = json.load(open("gamedata/geo.json"))
objs = json.load(open("gamedata/objs.json"))
ROADS = {}
for line in open(ROADS_INI, encoding="latin-1"):
    p = line.split()
    if len(p) == 2 and p[0] == "Road":
        cur = p[1]
    if len(p) == 3 and p[0] == "RoadWidth":
        ROADS[cur] = float(p[2])
rp = [o for o in O if o["kind"] == "road_point"]
pairs = list(zip(rp[0::2], rp[1::2]))
segs = [(a["template"], a["x"], a["y"], b["x"], b["y"]) for a, b in pairs]
NOT_BLD = re.compile(r"^(Amb|Tree|Rock|Bush|Hedge|Car|Truck|Tractor|Bus|Tour|Big|Firetruck|Fork|Billboard|HighwaySign|DrainPipe|Supply|Tech|Arbor|Burnt|Boat|Dock|Train|RR|Road|Shipping|Street|Stop|Fuel|Gas|Water|Crane|Cargo|Soccer|Planter|Statue|Chain|Fountain|Broken|Sandbag|SandBag|Picket|Clothes|America|China[A-Z]|GLA|Egyptian|Palm|CINE)|Bib")
bld = [o for o in O if o["kind"] == "object" and not NOT_BLD.search(o["template"]) and max(geo.get(o["template"], [0, 0])) >= 12]
xs, ys = np.array([o["x"] for o in bld]), np.array([o["y"] for o in bld])
parent = list(range(len(bld)))
def find(a):
    while parent[a] != a:
        parent[a] = parent[parent[a]]
        a = parent[a]
    return a
for i in range(len(bld)):
    for j in range(i):
        if math.hypot(xs[i] - xs[j], ys[i] - ys[j]) < 170:
            parent[find(i)] = find(j)
groups = collections.defaultdict(list)
for i in range(len(bld)):
    groups[find(i)].append(i)
towns = [g for g in groups.values() if len(g) >= minb]
print(f"== {name}: towns {[len(g) for g in sorted(towns, key=len, reverse=True)]}, of {len(bld)} buildings")
boxes = [(xs[g].min() - 60, ys[g].min() - 60, xs[g].max() + 60, ys[g].max() + 60) for g in towns]
def inside(x, y, m=0):
    return any(b[0] - m <= x <= b[2] + m and b[1] - m <= y <= b[3] + m for b in boxes)
st = [s for s in segs if s[0] in types and inside((s[1] + s[3]) / 2, (s[2] + s[4]) / 2)]
L = lambda s: math.dist(s[1:3], s[3:5])
total = sum(map(L, st)) or 1
area = sum((b[2] - b[0]) * (b[3] - b[1]) for b in boxes)
tb = collections.Counter()
for s in st:
    tb[s[0]] += L(s)
town_b = [bld[i] for g in towns for i in g]
print(f"  street {total:.0f}, per 100x100 {total / area * 1e4:.1f}; types", {k: (round(v), ROADS.get(k)) for k, v in tb.most_common()}, "segment median", round(float(np.median(list(map(L, st))))))
# node graph: points shared within 6 units
key = lambda x, y: (round(x / 6), round(y / 6))
adj = collections.defaultdict(list)
for s in st:
    a, b = key(s[1], s[2]), key(s[3], s[4])
    adj[a].append((s[3] - s[1], s[4] - s[2]))
    adj[b].append((s[1] - s[3], s[2] - s[4]))
deg = collections.Counter(len(v) for v in adj.values())
junc = sum(v for k, v in deg.items() if k >= 3)
print(f"  ends {deg[1]} ({deg[1] / total * 1000:.1f}/1000), junctions {junc} ({junc / total * 1000:.1f}/1000), T {deg[3]}, X {deg[4]}")
# bends at two-way points
turns = []
for v in adj.values():
    if len(v) == 2:
        a, b = v
        c = (a[0] * b[0] + a[1] * b[1]) / ((math.hypot(*a) * math.hypot(*b)) or 1)
        turns.append(180 - math.degrees(math.acos(max(-1, min(1, c)))))
turns = np.array(turns) if turns else np.zeros(1)
print(f"  bends per 1000 street {np.sum(turns > 5) / total * 1000:.1f}, turn median {np.median(turns[turns > 5]) if (turns > 5).any() else 0:.0f}, share >60deg {np.mean(turns > 60):.2f}")
dirs = collections.Counter()
for s in st:
    dirs[round(math.degrees(math.atan2(s[4] - s[2], s[3] - s[1])) % 90 / 10) * 10 % 90] += L(s)
print("  direction mod 90 by 10 degrees", {k: round(v / total, 2) for k, v in dirs.most_common(4)})
# buildings
def nearest(x, y, sg):
    best = (1e9, None, 0, 0, 1, 0)
    for s in sg:
        ax, ay, bx, by = s[1:]
        dx, dy = bx - ax, by - ay
        q = dx * dx + dy * dy or 1
        t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / q))
        px, py = ax + dx * t, ay + dy * t
        d = math.hypot(x - px, y - py)
        if d < best[0]:
            ln = math.sqrt(q)
            best = (d, s, px, py, dx / ln, dy / ln)
    return best
gaps, face, al = [], [], []
for o in town_b:
    if not st:
        break
    a, b = geo.get(o["template"], [20, 20])
    r = math.radians(o["angle_deg"])
    d, s, px, py, tx, ty = nearest(o["x"], o["y"], st)
    nx, ny = (o["x"] - px) / (d or 1), (o["y"] - py) / (d or 1)
    half = abs(math.cos(r) * nx + math.sin(r) * ny) * a + abs(-math.sin(r) * nx + math.cos(r) * ny) * b
    gaps.append(d - ROADS.get(s[0], 30) / 2 - half)
    face.append(-(math.sin(r) * nx - math.cos(r) * ny))
    al.append(abs(math.cos(r) * tx + math.sin(r) * ty))
if gaps:
    g = np.array(gaps)
    al = np.array(al)
    misal = np.minimum(al, np.sqrt(np.maximum(1 - al ** 2, 0)))
    print(f"  buildings per 1000 street {len(town_b) / total * 1000:.1f}, per 100x100 {len(town_b) / area * 1e4:.2f}; street gap median {np.median(g):.0f} (10-90%: {np.percentile(g, 10):.0f}..{np.percentile(g, 90):.0f}); off-axis to street median {math.degrees(np.median(np.arcsin(misal))):.0f} deg; front {np.mean(np.array(face) > .5):.2f} back {np.mean(np.array(face) < -.5):.2f}")
near = [min(math.dist((a["x"], a["y"]), (b["x"], b["y"])) for b in town_b if b is not a) for a in town_b]
print("  nearest building median", round(float(np.median(near))), "mix", collections.Counter(o["template"] for o in town_b).most_common(12))
# props and trees in town
pr = collections.Counter(o["template"] for o in O if o["kind"] == "object" and inside(o["x"], o["y"]) and o not in town_b and not o["template"].startswith("Amb"))
print("  props per 1000 street:", {k: round(v / total * 1000, 1) for k, v in pr.most_common(26)})
# ground
tex = {os.path.basename(f)[4:-4]: (np.array(Image.open(f)) > 0)[::-1] for f in glob.glob(f"refs/{name}/tex_*.png")}
H, W = next(iter(tex.values())).shape
XC, YC = np.meshgrid((np.arange(W) - B) * 10.0 + 5, (np.arange(H) - B) * 10.0 + 5)
inb = np.zeros((H, W), bool)
for b in boxes:
    inb |= (XC >= b[0]) & (XC <= b[2]) & (YC >= b[1]) & (YC <= b[3])
names = list(tex)
label = np.full((H, W), -1)
for k, n in enumerate(names):
    label[tex[n]] = k
e = np.full((H, W), 1e9)
for s in st:
    ax, ay, bx, by = s[1:]
    dx, dy = bx - ax, by - ay
    t = np.clip(((XC - ax) * dx + (YC - ay) * dy) / ((dx * dx + dy * dy) or 1), 0, 1)
    e = np.minimum(e, np.hypot(XC - ax - t * dx, YC - ay - t * dy) - ROADS.get(s[0], 30) / 2)
db = np.full((H, W), 1e9)
for o in town_b:
    db = np.minimum(db, np.hypot(XC - o["x"], YC - o["y"]) - max(geo.get(o["template"], [20, 20])))
for label_, m in (("street edge 0-10", inb & (e >= 0) & (e < 10)), ("building 0-20", inb & (db < 20)), ("building 20-50", inb & (db >= 20) & (db < 50)), ("town rest", inb & (db >= 50) & (e >= 10))):
    c = collections.Counter(label[m].tolist())
    tot = sum(c.values()) or 1
    print(f"  ground {label_:17s} {tot:5d}", {(names[k] if k >= 0 else "cliff"): round(v / tot, 2) for k, v in c.most_common(5)})
