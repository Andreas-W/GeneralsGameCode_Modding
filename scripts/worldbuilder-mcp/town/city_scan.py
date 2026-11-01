# A map's objects as a compact table: finds its city by building density, with road and sidewalk measures for that area.
# Usage: city_scan.py <name> <objects_list dump>  (writes refs/<name>/objects.json, buildings.png)
import json, math, os, re, sys, glob, collections
import numpy as np
import scipy.ndimage as nd
from PIL import Image, ImageDraw
import town_paths  # enters the work folder
name, src = sys.argv[1], sys.argv[2]
raw = json.loads(open(src, encoding="utf-8", errors="replace").read())
if isinstance(raw, list):
    raw = json.loads(raw[0]["text"])
elif "result" in raw:
    raw = json.loads(raw["result"])
print("total_matches", raw.get("total_matches"), "listed", len(raw["objects"]))
O = raw["objects"]
os.makedirs(f"refs/{name}", exist_ok=True)
json.dump(O, open(f"refs/{name}/objects.json", "w"))
# everything with a real footprint that is not nature, a prop or a vehicle counts as a building
geo = json.load(open("gamedata/geo.json"))
NOT_BLD = re.compile(r"^(Amb|Tree|Rock|Bush|Hedge|Car|Truck|Tractor|Bus|Tour|Big|Firetruck|Fork|Billboard|HighwaySign|DrainPipe|Supply|Tech|Arbor|Burnt|Boat|Dock|Train|RR|Road|Shipping|Street|Stop|Fuel|Gas|Water|Crane|Cargo|Soccer|Planter|Statue|Chain|Fountain)|Bib")
bld = [o for o in O if o["kind"] == "object" and not NOT_BLD.search(o["template"]) and max(geo.get(o["template"], [0, 0])) >= 14]
# density: buildings per cell of a 400 unit window
xs, ys = np.array([o["x"] for o in bld]), np.array([o["y"] for o in bld])
print("buildings", len(bld), "nearest neighbour median", round(float(np.median([np.partition(np.hypot(xs - a, ys - b), 1)[1] for a, b in zip(xs, ys)]))))
print(collections.Counter(o["template"] for o in bld).most_common(40))
# clusters: buildings closer than 170 join
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
rp = [o for o in O if o["kind"] == "road_point"]
segs = [(a["template"], a["x"], a["y"], b["x"], b["y"]) for a, b in zip(rp[0::2], rp[1::2])]
json.dump(segs, open(f"refs/{name}/segs.json", "w"))
print("road length by type", {k: round(v) for k, v in sorted(collections.Counter({t: sum(math.dist(s[1:3], s[3:5]) for s in segs if s[0] == t) for t in {s[0] for s in segs}}).items(), key=lambda kv: -kv[1])})
print("clusters of 5+ buildings:")
cl = []
for g in sorted(groups.values(), key=len, reverse=True):
    if len(g) < 5:
        break
    x0, x1, y0, y1 = xs[g].min(), xs[g].max(), ys[g].min(), ys[g].max()
    area = (x1 - x0 + 60) * (y1 - y0 + 60)
    inside = [s for s in segs if x0 - 40 <= (s[1] + s[3]) / 2 <= x1 + 40 and y0 - 40 <= (s[2] + s[4]) / 2 <= y1 + 40]
    L = collections.Counter()
    for s in inside:
        L[s[0]] += math.dist(s[1:3], s[3:5])
    cl.append({"box": [int(x0), int(y0), int(x1), int(y1)], "buildings": len(g)})
    print(f"  {len(g):3d} bld box {int(x0)},{int(y0)} - {int(x1)},{int(y1)}  per 100x100: {len(g) / area * 1e4:.2f}  roads {dict((k, round(v)) for k, v in L.most_common(6))}")
json.dump(cl, open(f"refs/{name}/clusters.json", "w"))
# plan picture of all roads and buildings
W = max(max(xs), max(s[1] for s in segs)) + 100
H = max(max(ys), max(s[2] for s in segs)) + 100
SC = 900 / max(W, H)
im = Image.new("RGB", (int(W * SC), int(H * SC)), (40, 50, 40))
dr = ImageDraw.Draw(im)
PX = lambda x, y: (x * SC, (H - y) * SC)
RC = {"Sidewalk": (240, 240, 240), "Sidewalk2": (240, 240, 240), "Sidewalk3": (240, 240, 240), "Sidewalk4": (240, 240, 240), "ParkingLines": (255, 255, 0), "FourLane": (90, 140, 255), "FourLaneDark": (90, 140, 255)}
for s in segs:
    dr.line([PX(s[1], s[2]), PX(s[3], s[4])], fill=RC.get(s[0], (150, 150, 150) if "Lane" in s[0] else (120, 90, 50)), width=1)
for o in bld:
    a, b = geo.get(o["template"], [10, 10])
    r = math.radians(o["angle_deg"])
    q = [(o["x"] + u * math.cos(r) - v * math.sin(r), o["y"] + u * math.sin(r) + v * math.cos(r)) for u, v in ((-a, -b), (a, -b), (a, b), (-a, b))]
    dr.polygon([PX(*p) for p in q], fill=(200, 80, 70))
im.save(f"refs/{name}/plan.png")
