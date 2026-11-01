# Measures a reference city: street hierarchy, how sidewalks relate to streets, how buildings meet streets, props.
# Usage: city_measure.py <name> <x0,y0,x1,y1> [more boxes]  (needs refs/<name>/objects.json, segs.json)
import json, math, os, re, sys, collections
import numpy as np
from town_paths import ROADS_INI  # enters the work folder
name = sys.argv[1]
boxes = [tuple(float(v) for v in a.split(",")) for a in sys.argv[2:]]
O = json.load(open(f"refs/{name}/objects.json"))
segs = [tuple(s) for s in json.load(open(f"refs/{name}/segs.json"))]
geo = json.load(open("gamedata/geo.json"))
ROADS = {}
for line in open(ROADS_INI, encoding="latin-1"):
    p = line.split()
    if len(p) == 2 and p[0] == "Road":
        cur = p[1]
    if len(p) == 3 and p[0] == "RoadWidth":
        ROADS[cur] = float(p[2])
def inside(x, y, m=0):
    return any(b[0] - m <= x <= b[2] + m and b[1] - m <= y <= b[3] + m for b in boxes)
mid = lambda s: ((s[1] + s[3]) / 2, (s[2] + s[4]) / 2)
isstreet = lambda t: "Lane" in t or t in ("Cobblestone",)
iswalk = lambda t: t.startswith("Sidewalk")
streets = [s for s in segs if isstreet(s[0]) and inside(*mid(s), 40)]
walks = [s for s in segs if iswalk(s[0]) and inside(*mid(s), 40)]
parking = [s for s in segs if s[0] == "ParkingLines" and inside(*mid(s), 40)]
L = lambda s: math.dist(s[1:3], s[3:5])
total = sum(map(L, streets))
area = sum((b[2] - b[0] + 80) * (b[3] - b[1] + 80) for b in boxes)
print(f"== {name}: street {total:.0f}, per 100x100 of area {total / area * 1e4:.1f}")
bytype = collections.Counter()
for s in streets:
    bytype[s[0]] += L(s)
print("street types", {k: (round(v), ROADS.get(k)) for k, v in bytype.most_common()})
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
# sidewalks against streets: distance from the street edge to the sidewalk centre
rows = collections.defaultdict(list)
for s in walks:
    d, st, *_ = nearest(*mid(s), streets)
    if st is None:
        continue
    rows[s[0]].append((d - ROADS.get(st[0], 35) / 2, L(s)))
for t, v in sorted(rows.items()):
    d = np.array([a for a, _ in v])
    w = np.array([b for _, b in v])
    hist = {f"{lo}..{hi}": int(w[(d >= lo) & (d < hi)].sum()) for lo, hi in ((-99, 0), (0, 6), (6, 12), (12, 25), (25, 60), (60, 999))}
    print(f"  {t} (width {ROADS.get(t)}) length {int(w.sum())} by distance from street edge {hist}")
# street coverage: left and right of each street, is there a sidewalk within 30 of the street edge?
cov = collections.Counter()
for s in streets:
    n = max(1, int(L(s) / 25))
    ax, ay, bx, by = s[1:]
    ln = L(s) or 1
    tx, ty = (bx - ax) / ln, (by - ay) / ln
    w_ = ROADS.get(s[0], 35) / 2
    for i in range(n):
        f = (i + 0.5) / n
        x, y = ax + (bx - ax) * f, ay + (by - ay) * f
        side = [nearest(x - ty * sg * (w_ + 12), y + tx * sg * (w_ + 12), walks)[0] < 22 for sg in (1, -1)]
        cov[sum(side)] += ln / n
tot = sum(cov.values()) or 1
print("  street length with sidewalks on 0/1/2 sides", {k: round(cov[k] / tot, 2) for k in (0, 1, 2)})
# junctions and corners
ends = collections.Counter()
for s in streets:
    ends[(round(s[1] / 6), round(s[2] / 6))] += 1
    ends[(round(s[3] / 6), round(s[4] / 6))] += 1
junc = [(k[0] * 6, k[1] * 6) for k, v in ends.items() if v >= 3]
jd = sorted(min((math.dist(p, q) for q in junc if q != p), default=0) for p in junc)
print("  junctions", len(junc), "per 1000 street", round(len(junc) / total * 1000, 1), "nearest junction median", round(float(np.median(jd))) if jd else None)
dirs = collections.Counter()
for s in streets:
    dirs[round(math.degrees(math.atan2(s[4] - s[2], s[3] - s[1])) % 180 / 15) * 15] += L(s)
print("  direction shares", {k: round(v / total, 2) for k, v in dirs.most_common(5)})
# buildings
NOT_BLD = re.compile(r"^(Amb|Tree|Rock|Bush|Hedge|Car|Truck|Tractor|Bus|Tour|Big|Firetruck|Fork|Billboard|HighwaySign|DrainPipe|Supply|Tech|Arbor|Burnt|Boat|Dock|Train|RR|Road|Shipping|Street|Stop|Fuel|Gas|Water|Crane|Cargo|Soccer|Planter|Statue|Chain|Fountain)|Bib")
blds = [o for o in O if o["kind"] == "object" and inside(o["x"], o["y"]) and not NOT_BLD.search(o["template"]) and max(geo.get(o["template"], [0, 0])) >= 14]
gaps, gapw, facing, align, sizes = [], [], [], [], []
for o in blds:
    a, b = geo.get(o["template"], [20, 20])
    r = math.radians(o["angle_deg"])
    d, st, px, py, tx, ty = nearest(o["x"], o["y"], streets)
    nx, ny = (o["x"] - px) / (d or 1), (o["y"] - py) / (d or 1)
    half = abs(math.cos(r) * nx + math.sin(r) * ny) * a + abs(-math.sin(r) * nx + math.cos(r) * ny) * b
    gaps.append(d - ROADS.get(st[0], 35) / 2 - half)
    d2 = nearest(o["x"], o["y"], walks)[0] if walks else 1e9
    gapw.append(d2 - half)
    fx, fy = math.sin(r), -math.cos(r)
    facing.append(-(fx * nx + fy * ny))
    align.append(abs(math.cos(r) * tx + math.sin(r) * ty))
    sizes.append(2 * max(a, b))
g = np.array(gaps)
print(f"  buildings {len(blds)}, per 1000 street {len(blds) / total * 1000:.1f}, edge gap to street median {np.median(g):.0f} (10-90%: {np.percentile(g, 10):.0f}..{np.percentile(g, 90):.0f}), gap to sidewalk centre median {np.median(gapw):.0f}")
f = np.array(facing)
print(f"  front to street {np.mean(f > 0.5):.2f}, side on {np.mean(np.abs(f) <= 0.5):.2f}, back to street {np.mean(f < -0.5):.2f}; aligned to street {np.mean(np.array(align) > 0.9):.2f} or square {np.mean(np.array(align) < 0.1):.2f}")
print("  building size (longest side) 25/50/75%", [round(float(np.percentile(sizes, p))) for p in (25, 50, 75)])
near = sorted(min(math.dist((a["x"], a["y"]), (b["x"], b["y"])) for b in blds if b is not a) for a in blds)
print("  nearest building median", round(float(np.median(near))))
print("  mix", collections.Counter(o["template"] for o in blds).most_common(14))
# props
props = [o for o in O if o["kind"] == "object" and inside(o["x"], o["y"], 40) and not o["template"].startswith("Amb") and o not in blds]
by = collections.defaultdict(list)
for o in props:
    by[o["template"]].append(o)
print("  props per 1000 street (edge dist median, junction dist median):")
for t, lst in sorted(by.items(), key=lambda kv: -len(kv[1]))[:22]:
    e = np.median([nearest(o["x"], o["y"], streets)[0] - ROADS.get(nearest(o["x"], o["y"], streets)[1][0], 35) / 2 for o in lst])
    j = np.median([min((math.dist((o["x"], o["y"]), q) for q in junc), default=999) for o in lst])
    print(f"    {t:24s} {len(lst):3d} {len(lst) / total * 1000:5.1f}  edge {e:5.0f}  junction {j:5.0f}")
print("  parking length", round(sum(map(L, parking))))
