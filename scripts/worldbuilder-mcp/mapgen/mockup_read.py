# Reads a mockup into plan signals: levels, cliff and ramp edges, beaches, building clusters (town or outpost, building families),
# forest clumps (trees by kind), kept objects. Writes plan_read.json and plan.png into the mockup folder for Claude to turn into the plan.
# Usage: mockup_read.py <mock dir> <plan.json>
#   mock dir: heights_new.npy (raw heights after any lift, same grid as the generated map) and mock.json (objects as read from the mockup);
#   plan.json gives border, shift (world offset of the generated map against the mockup), water height and the levels.
import json, math, sys, numpy as np
from scipy import ndimage
from PIL import Image, ImageDraw
D = sys.argv[1]
PLAN = json.load(open(sys.argv[2]))
RAW, CELL = 0.625, 10.0
B, SHIFT, WATER = PLAN["border"], float(PLAN["shift"]), PLAN["water"]["height"]
z = np.load(D + "/heights_new.npy").astype(np.float32) * RAW
m = json.load(open(D + "/mock.json"))
H, W = z.shape
gy, gx = np.gradient(z, CELL)
sl = ndimage.maximum_filter(np.hypot(gx, gy), 3)
sea = z < WATER
LEVELS = [l["z"] for l in PLAN["levels"] if isinstance(l["z"], (int, float))]
# Each land vertex gets the nearest level; peaks above the top level are mountains.
lvl = np.full(z.shape, -1, np.int8)
lvl[~sea] = np.argmin(np.abs(z[~sea][:, None] - np.array(LEVELS)[None]), axis=1)
mountain = z > PLAN.get("mountain_above", 140)
lvl[mountain] = 3
# Edges between neighbouring classes, with the steepest slope found there: cliff from 0.6, ramp below 0.35.
def edge(a, b):
    ea = ndimage.binary_dilation(lvl == a, iterations=2) & ndimage.binary_dilation(lvl == b, iterations=2)
    return ea
out = {"levels": LEVELS, "edges": {}}
img = np.zeros(z.shape + (3,), np.float32)
pal = {-1: (0.1, 0.25, 0.55), 0: (0.85, 0.8, 0.55), 1: (0.45, 0.65, 0.35), 2: (0.3, 0.5, 0.25), 3: (0.55, 0.5, 0.45)}
for k, c in pal.items():
    img[lvl == k] = c
cliffm = np.zeros(z.shape, bool); rampm = np.zeros(z.shape, bool)
for a, b in ((-1, 0), (-1, 1), (-1, 2), (0, 1), (1, 2), (0, 2), (1, 3), (2, 3), (0, 3)):
    e = edge(a, b) if a >= 0 else ndimage.binary_dilation(sea, iterations=2) & ndimage.binary_dilation(lvl == b, iterations=2)
    if not e.any():
        continue
    steep = e & (sl >= 0.6); gentle = e & (sl < 0.35)
    cliffm |= steep; rampm |= gentle
    out["edges"][f"{a}-{b}"] = {"cells": int(e.sum()), "steep_share": round(float(steep.sum() / e.sum()), 2), "gentle_share": round(float(gentle.sum() / e.sum()), 2)}
img[cliffm] = (0.85, 0.15, 0.1)
img[rampm & ~cliffm & (lvl >= 0)] = (1.0, 0.9, 0.2)
# Beaches: land of the low level or gentle shore within 3 cells of the sea.
shore = ndimage.binary_dilation(sea, iterations=3) & ~sea
beach = shore & (sl < 0.35)
out["beach_share_of_shore"] = round(float(beach.sum() / max(1, shore.sum())), 2)
# Objects, in the new coordinates.
objs = [dict(o, x=o["x"] + SHIFT, y=o["y"] + SHIFT) for o in m["objects"]["objects"]]
civ = [o for o in objs if o["kind"] == "object" and not o["template"].startswith(("Tree", "Tech", "Supply")) and o["owner"] != "team"]
pts = np.array([[o["x"], o["y"]] for o in civ])
lab = np.arange(len(civ))
for i in range(len(civ)):
    for j in range(len(civ)):
        if np.hypot(*(pts[i] - pts[j])) < 450:
            lab[lab == lab[j]] = lab[i]
clusters = []
for g in np.unique(lab):
    mem = [civ[i] for i in range(len(civ)) if lab[i] == g]
    fam = {}
    for o in mem:
        f = "asian" if o["template"].startswith("Asian") else "tropical huts" if "GrassHut" in o["template"] else "industrial" if "Quonset" in o["template"] else "military" if "Guard" in o["template"] else o["template"]
        fam[f] = fam.get(f, 0) + 1
    c = np.mean([[o["x"], o["y"]] for o in mem], axis=0)
    clusters.append({"kind": "town" if len(mem) >= 4 else "outpost", "at": [round(float(c[0])), round(float(c[1]))], "buildings": len(mem), "families": fam})
out["clusters"] = clusters
trees = [o for o in objs if o["kind"] == "object" and o["template"].startswith("Tree")]
tp = np.array([[o["x"], o["y"]] for o in trees])
tl = np.arange(len(trees))
for i in range(len(trees)):
    for j in range(len(trees)):
        if np.hypot(*(tp[i] - tp[j])) < 220:
            tl[tl == tl[j]] = tl[i]
forests = []
for g in np.unique(tl):
    mem = [trees[i] for i in range(len(trees)) if tl[i] == g]
    c = np.mean([[o["x"], o["y"]] for o in mem], axis=0)
    r = max(150.0, float(np.max([np.hypot(o["x"] - c[0], o["y"] - c[1]) for o in mem])) + 120)
    kinds = {}
    for o in mem:
        kinds[o["template"]] = kinds.get(o["template"], 0) + 1
    forests.append({"at": [round(float(c[0])), round(float(c[1]))], "radius": round(r), "trees": kinds})
out["forests"] = forests
out["keep"] = [{"object": o["template"], "at": [round(o["x"]), round(o["y"])]} for o in objs if o["kind"] in ("object", "waypoint") and (o["template"].startswith(("Tech", "Supply")) or o["kind"] == "waypoint")]
json.dump(out, open(D + "/plan_read.json", "w"), indent=1)
k = 3
im = Image.fromarray((img[::-1] * 255).astype(np.uint8)).resize((W * k, H * k), Image.NEAREST)
d = ImageDraw.Draw(im)
P = lambda x, y: ((x / CELL + B) * k, (H - 1 - (y / CELL + B)) * k)
roads = [o for o in objs if o["kind"] in ("road_point", "bridge_point")]
for a, c in zip(roads[0::2], roads[1::2]):
    d.line([P(a["x"], a["y"]), P(c["x"], c["y"])], fill=(40, 40, 40) if a["kind"] == "road_point" else (140, 80, 30), width=4)
for f in forests:
    x, y = P(*f["at"]); r = f["radius"] / CELL * k
    d.ellipse([x - r, y - r, x + r, y + r], outline=(0, 120, 0), width=3)
for i, c in enumerate(clusters):
    x, y = P(*c["at"])
    d.rectangle([x - 14, y - 14, x + 14, y + 14], outline=(255, 0, 200) if c["kind"] == "town" else (200, 0, 255), width=3)
    d.text((x + 16, y - 6), f"{c['kind']} {i + 1}", fill=(0, 0, 0))
for o in objs:
    if o["kind"] == "waypoint":
        x, y = P(o["x"], o["y"]); d.ellipse([x - 9, y - 9, x + 9, y + 9], outline=(255, 255, 255), width=3); d.text((x + 10, y - 6), o["unique_id"], fill=(255, 255, 255))
    elif o["template"].startswith(("Tech", "Supply")):
        x, y = P(o["x"], o["y"]); d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(255, 220, 0) if o["template"].startswith("Tech") else (0, 220, 255))
pm = PLAN["playable_max"]
d.rectangle([P(0, pm)[0], P(0, pm)[1], P(pm, 0)[0], P(pm, 0)[1]], outline=(255, 255, 255))
im.save(D + "/plan.png")
print(json.dumps({k: out[k] for k in ("edges", "beach_share_of_shore")}, indent=0))
for c in clusters: print(c)
for f in forests: print(f)
