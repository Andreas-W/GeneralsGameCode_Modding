# What a reference map paints beside its roads: texture share by road type, inside/outside the town, and distance from the road edge.
import json, math, os, collections
import numpy as np
from PIL import Image
from town_paths import PACKS, ROADS_INI  # enters the work folder
B = 70
AREA = (980, 930, 1400, 1560)
NAMES = ["CobblestoneType1", "RocksType3", "GrassMediumType10", "GrassType1", "AsphaltType1", "GrassMediumType8", "GrassMediumType13Leaves", "GrassMediumType12Leaves", "GrassLargeType1Snow"]
tex = {n: (np.array(Image.open(f"refs/alpine/aa_{n}.png")) > 0)[::-1] for n in NAMES}
H, W = tex[NAMES[0]].shape
XC, YC = np.meshgrid((np.arange(W) - B) * 10.0 + 5, (np.arange(H) - B) * 10.0 + 5)
label = np.full((H, W), -1)
for k, n in enumerate(NAMES):
    label[tex[n]] = k
segs = json.load(open("refs/alpine/aa_segs.json"))
WIDTH = {"TwoLane": 35, "DirtRoad": None, "DirtRoad2": None, "GravelRoad": None}
for line in open(ROADS_INI, encoding="latin-1"):
    p = line.split()
    if len(p) == 2 and p[0] == "Road":
        cur = p[1]
    if len(p) == 3 and p[0] == "RoadWidth" and cur in WIDTH:
        WIDTH[cur] = float(p[2])
print("widths", WIDTH)
intown = (XC >= AREA[0]) & (XC <= AREA[2]) & (YC >= AREA[1]) & (YC <= AREA[3])
BANDS = [(0, 5), (5, 10), (10, 20), (20, 40)]
result = {}
for t in WIDTH:
    d = np.full((H, W), 1e9)
    L = 0
    for s in segs:
        if s[0] != t:
            continue
        ax, ay, bx, by = s[1:]
        L += math.dist((ax, ay), (bx, by))
        dx, dy = bx - ax, by - ay
        q = (dx * dx + dy * dy) or 1
        u = np.clip(((XC - ax) * dx + (YC - ay) * dy) / q, 0, 1)
        d = np.minimum(d, np.hypot(XC - ax - u * dx, YC - ay - u * dy))
    print(f"\n{t} length {L:.0f}")
    e = d - WIDTH[t] / 2
    for ctx, cm in (("town", intown), ("outside", ~intown)):
        for lo, hi in BANDS:
            m = cm & (e >= lo) & (e < hi)
            if m.sum() < 15:
                continue
            c = collections.Counter(label[m].tolist())
            tot = sum(c.values())
            row = {(NAMES[k] if k >= 0 else "cliff/other"): round(v / tot, 2) for k, v in c.most_common(5)}
            result[f"{t}/{ctx}/{lo}-{hi}"] = row
            print(f"  {ctx:8s} {lo:2d}-{hi:2d} cells {tot:5d}", row)
json.dump(result, open(f"{PACKS}/alpine_assault_road_ground.json", "w"), indent=1)
