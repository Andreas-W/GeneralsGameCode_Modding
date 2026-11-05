# NorthernIslandGen, stage C: dense woods on top of the per-ground scatter (snowy_mountains_stage_c.py with MAPGEN_NAME). Woods go on
# open land (not under water, beaches or the steepest ground) away from the starts, Tech and supplies, ramps and the centre's town site,
# spread out: at least 700 apart, roomy spots first. A wood is an irregular blob of snowy conifers in Bitter Winter's mix,
# about 21 apart; its forest floor (SnowType7) is painted only under the trees.
# Usage: northern_island_forests.py <plan.json> <work dir (mapgen_work)> [--place]   Placing uses the second WorldBuilder.
import json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
CELL, B, WATER = 10.0, plan["border"], plan["water"]["height"]
# Other maps set plan "woods": {"count": N, "town": [x, y, radius]} (the town site kept free); the defaults are NorthernIslandGen's.
WOODS = plan.get("woods", {})
COUNT, SPACING = WOODS.get("count", 6), 21.0
TOWN = (tuple(WOODS["town"][:2]), WOODS["town"][2]) if "town" in WOODS else ((1950, 1880), 480)
MIX = [("TreeSpruceSnow", 34), ("TreePine5snow2", 22), ("TreePine3snow", 20), ("TreeSpruce05Snow", 9), ("TreeFir07Snow", 6), ("Bush14Snow", 3)]
rng = np.random.default_rng(plan.get("seed", 1) + 40)
z = np.load(os.path.join(work, "heights_world.npy")).astype(np.float64)
zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
shape = zc.shape
yy, xx = np.indices(shape)
X, Y = (xx - B + 0.5) * CELL, (yy - B + 0.5) * CELL
gy, gx = np.gradient(zc, CELL)
ok = (zc > WATER + 8) & (np.hypot(gx, gy) < 0.45) & (X > 0) & (Y > 0) & (X < plan["playable_max"]) & (Y < plan["playable_max"])
for k in plan["keep"]:
    ok &= np.hypot(X - k["at"][0], Y - k["at"][1]) > 450
for o in json.load(open(os.path.join(work, "objects.json"))):
    ok &= np.hypot(X - o["x"], Y - o["y"]) > 180
for r in json.load(open(os.path.join(work, "stageA", "report.json")))["ramps"]:
    ok &= np.hypot(X - r["at"][0], Y - r["at"][1]) > 200
ok &= np.hypot(X - TOWN[0][0], Y - TOWN[0][1]) > TOWN[1]
room = ndimage.distance_transform_edt(ok) * CELL
centres = []
spread = np.full(shape, 1e9)
for _ in range(COUNT):
    score = np.where((room >= 140) & (spread >= 700), room + 0.3 * np.minimum(spread, 2000), -1)
    i = np.unravel_index(np.argmax(score), shape)
    if score[i] < 0:
        break
    cx, cy = X[i], Y[i]
    centres.append((cx, cy, min(200, room[i] + 30)))
    spread = np.minimum(spread, np.hypot(X - cx, Y - cy))
names = [n for n, w in MIX for _ in range(w)]
plants, floor = [], np.zeros(shape, bool)
for cx, cy, R in centres:
    turn = rng.uniform(0, 2 * math.pi)
    pts = []
    for _ in range(30000):
        r = R * math.sqrt(rng.random()); th = rng.uniform(0, 2 * math.pi)
        k = 1 + 0.18 * math.sin(3 * th + turn) + 0.1 * math.sin(5 * th + 2 * turn)
        x, y = cx + r * math.cos(th), cy + r * math.sin(th)
        if r > R * k or any(math.hypot(x - p[0], y - p[1]) < SPACING for p in pts):
            continue
        i, j = int((y / CELL) + B), int((x / CELL) + B)
        if not ok[i, j]:
            continue
        pts.append((x, y))
    for x, y in pts:
        plants.append({"template": names[rng.integers(len(names))], "x": round(x, 1), "y": round(y, 1), "angle_deg": round(float(rng.uniform(0, 360)), 1)})
    d = np.full(shape, 1e9)
    for x, y in pts:
        d = np.minimum(d, np.hypot(X - x, Y - y))
    floor |= d <= 26
floor = ndimage.binary_opening(ndimage.binary_closing(floor, iterations=2), iterations=1)
out = os.path.join(work, "stageC")
os.makedirs(out, exist_ok=True)
Image.fromarray((floor[::-1] * 255).astype(np.uint8)).save(os.path.join(out, "forest_floor.png"))
json.dump({"woods": [[round(a), round(b), round(r)] for a, b, r in centres], "plants": plants}, open(os.path.join(out, "woods.json"), "w"), indent=0)
print("woods", [[round(a), round(b), round(r)] for a, b, r in centres], "plants", len(plants), "floor cells", int(floor.sum()))
if "--place" in sys.argv:
    sys.path.insert(0, os.path.dirname(HERE))
    from wb_bridge import WorldBuilderBridge
    wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
    wb.call("map.new", width=32, height=32, discard_changes=True)
    wb.call("map.open", path=plan["map"], discard_changes=True)
    wb.call("terrain.import_mask", path=os.path.join(out, "forest_floor.png"), target="texture", texture="SnowType7", fit="exact")
    for q in plants:
        wb.call("objects.place", template=q["template"], x=q["x"], y=q["y"], angle_deg=q["angle_deg"])
    wb.call("terrain.reblend")
    wb.call("terrain.cliff_map", repair="fit")
    wb.call("map.save", path=plan["map"], overwrite=True)
    print("placed and saved")
