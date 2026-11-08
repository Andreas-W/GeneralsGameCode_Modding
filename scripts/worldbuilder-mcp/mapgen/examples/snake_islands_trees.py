# SnakeIslandsGen, stage C: tropical plants by ground, in the jungle mix of IslandCrossGen (Eastern Everglades: palms, dogwood and oak
# in groves, bamboo, Bush08).
# - jungle groves where a noise field (some 140 across) is high, more of them on the plateaus and the middle islands than on the starts'
#   lowlands; trees at least SPACING apart;
# - bamboo stands at the foot of the plateau cliffs;
# - palms in loose groups on the beaches and the grassy sand behind them, and on the islets;
# - a thin scatter of single palms and bushes over open ground.
# Kept open: each start within START_CLEAR (groves thin out to START_FADE), the ramps, the drawbridge quays, the wooden bridge ends,
# steep ground and its edge, water.
# Writes stageC/trees.json and stageC/trees.png; with --place puts them into the map through the second WorldBuilder and saves.
# Usage: snake_islands_trees.py <plan.json> <work dir (mapgen_work)> [--place]
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
WATER, B, CELL = plan["water"]["height"], plan["border"], 10.0
LOW, HIGH = plan["levels"][0]["z"], plan["levels"][1]["z"]
START_CLEAR, START_FADE = 380.0, 560.0
SPACING = 20.0
GROVE = 0.45          # grove where the noise is above this (lowlands of the starts); lower elsewhere
JUNGLE = {"TreePalm04": 22, "TreePalm03": 20, "TreeDogwood1": 18, "TreePalm1short": 16, "TreeOak01": 10, "Bush08": 6}
BAMBOO = {"TreeBamboo01": 5, "TreeBamboo02": 3, "TreeBamboo03": 2}
PALMS = {"TreePalm1": 3, "TreePalm2": 3, "TreePalm1short": 2, "TreePalm2short": 2, "TreePalm04": 2}
SCATTER = {"TreePalm1": 3, "TreePalm2": 2, "Bush08": 3, "TreePalm03": 2}
rng = np.random.default_rng(plan.get("seed", 1) + 17)

z = np.load(os.path.join(work, "heights_world.npy")).astype(np.float64)
zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
cs = np.maximum.reduce([np.abs(z[1:, 1:] - z[:-1, :-1]), np.abs(z[1:, :-1] - z[:-1, 1:]), np.abs(z[1:, :-1] - z[:-1, :-1]),
                        np.abs(z[:-1, 1:] - z[:-1, :-1])]) / CELL
shape = zc.shape
cy, cx = np.indices(shape)
X, Y = (cx - B + 0.5) * CELL, (cy - B + 0.5) * CELL
layout = json.load(open(os.path.join(work, "mock", "layout.json")))
bridges = json.load(open(os.path.join(work, "bridges.json")))
report = json.load(open(os.path.join(work, "stageA", "report.json")))


def noise(size):
    n = ndimage.gaussian_filter(rng.standard_normal(shape), size)
    return n / (n.std() + 1e-9)


def pick(mix):
    names = list(mix)
    w = np.array([mix[n] for n in names], float)
    return names[rng.choice(len(names), p=w / w.sum())]


land = zc >= WATER + 1.5
# Steep dry ground only: the drop from a beach into the sea counted as cliff and put bamboo on the shore.
steep = (cs >= 0.35) & (zc >= WATER + 3)
d_steep = ndimage.distance_transform_edt(~steep) * CELL
d_sea = ndimage.distance_transform_edt(zc >= WATER) * CELL
ok = land & (d_steep > 25)
d_start = np.full(shape, 1e9)
for sx, sy in layout["starts"].values():
    d_start = np.minimum(d_start, np.hypot(X - sx, Y - sy))
start_w = np.clip((d_start - START_CLEAR) / (START_FADE - START_CLEAR), 0, 1)
for r in report["ramps"]:
    ok &= np.hypot(X - r["at"][0], Y - r["at"][1]) > 110
for b in bridges["drawbridges"]:
    a = math.radians(b["angle_deg"])
    t = np.abs((X - b["x"]) * math.cos(a) + (Y - b["y"]) * math.sin(a))
    s = np.abs(-(X - b["x"]) * math.sin(a) + (Y - b["y"]) * math.cos(a))
    ok &= ~((t < 300) & (s < 90))
for w in bridges["wooden"]:
    for px, py in w["points"]:
        ok &= np.hypot(X - px, Y - py) > 120

# Which island a cell is on: the starts' lowlands get fewer groves than the plateaus and the middle islands.
lab, n = ndimage.label(zc >= WATER)
start_isl = {int(lab[int(sy / CELL + B), int(sx / CELL + B)]) for sx, sy in layout["starts"].values()}
lowland = zc < (LOW + HIGH) / 2
thresh = np.where(np.isin(lab, list(start_isl)) & lowland, GROVE, 0.1)
g = noise(14) + 0.35 * noise(4)
grove = ok & (g > thresh + (1 - start_w) * 1.5) & (d_sea > 60)
bamboo = ok & lowland & (d_steep < 90) & (noise(5) > 0.6) & (start_w > 0.5)
beach = ok & (zc < WATER + 7) & (noise(6) > 0.2)
islet = np.zeros(shape, bool)
for jx, jy in layout["islets"]:
    islet |= ok & (np.hypot(X - jx, Y - jy) < 160)

trees = []
taken = []


def free(x, y, sp):
    return all((x - a) ** 2 + (y - b) ** 2 >= sp * sp for a, b in taken[-400:]) if taken else True


grid = {}


def free_grid(x, y, sp):
    gx, gy = int(x // 40), int(y // 40)
    for i in range(gx - 1, gx + 2):
        for j in range(gy - 1, gy + 2):
            for a, b in grid.get((i, j), ()):
                if (x - a) ** 2 + (y - b) ** 2 < sp * sp:
                    return False
    return True


def add(x, y, mix, sp):
    if not free_grid(x, y, sp):
        return
    grid.setdefault((int(x // 40), int(y // 40)), []).append((x, y))
    trees.append({"template": pick(mix), "x": round(float(x), 1), "y": round(float(y), 1), "angle_deg": round(float(rng.uniform(0, 360)), 1)})


def fill(mask, mix, sp, step, keep=1.0):
    pts = np.argwhere(mask[::max(1, int(step / CELL)), ::max(1, int(step / CELL))]) * max(1, int(step / CELL))
    rng.shuffle(pts)
    for i, j in pts:
        if rng.random() > keep:
            continue
        x, y = X[i, j] + rng.uniform(-step / 2, step / 2), Y[i, j] + rng.uniform(-step / 2, step / 2)
        ii, jj = int(y / CELL + B - 0.5), int(x / CELL + B - 0.5)
        if 0 <= ii < shape[0] and 0 <= jj < shape[1] and mask[ii, jj]:
            add(x, y, mix, sp)


fill(bamboo, BAMBOO, 14, 10, 0.5)
fill(grove, JUNGLE, SPACING, 10, 0.6)
fill(beach, PALMS, 45, 20, 0.35)
fill(islet, PALMS, 30, 20, 0.6)
fill(ok & ~grove & (start_w > 0.3), SCATTER, 60, 30, 0.05)
counts = {}
for t in trees:
    counts[t["template"]] = counts.get(t["template"], 0) + 1
print(len(trees), counts)
out = os.path.join(work, "stageC")
os.makedirs(out, exist_ok=True)
json.dump(trees, open(os.path.join(out, "trees.json"), "w"), indent=0)
im = Image.fromarray(np.where(land, 160, 40).astype(np.uint8)[::-1]).convert("RGB")
d = ImageDraw.Draw(im)
for t in trees:
    px, py = t["x"] / CELL + B, shape[0] - 1 - (t["y"] / CELL + B)
    d.point((px, py), fill=(0, 255, 0) if "Bamboo" not in t["template"] else (255, 255, 0))
im.save(os.path.join(out, "trees.png"))
if "--place" in sys.argv:
    sys.path.insert(0, os.path.dirname(HERE))
    from wb_bridge import WorldBuilderBridge
    wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
    wb.call("map.new", width=32, height=32, discard_changes=True)
    wb.call("map.open", path=plan["map"], discard_changes=True)
    for t in trees:
        wb.call("objects.place", **t)
    wb.call("map.save", path=plan["map"])
    print("placed and saved", plan["map"])
