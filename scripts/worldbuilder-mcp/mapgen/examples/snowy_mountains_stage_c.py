# Stage C of SnowyMountainsGen: trees, bushes and rocks by ground, as Bitter Winter has them (the method of oasis_lake_stage_c.py):
# each cell gets the reference's rate for its painted texture and a plant from the reference's mix on that texture. Mountain flanks get
# plants too, as on the reference (snowy spruces on the slopes), up to a slope of STEEP; cliff textures get none.
# Kept clear: bases, Tech and supplies, ramps.
# Usage: snowy_mountains_stage_c.py <map path> <work dir (mapgen_work)> <plan.json> [--place] [--seed N]
import collections, json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [HERE, os.path.dirname(HERE)]

CELL = 10.0
REF = "Bitter Winter"
# The map as scanned by paint_map.py (work/maps/<name>); other maps than the first set MAPGEN_NAME.
NAME = os.environ.get("MAPGEN_NAME", "SnowyMountainsGen")
PLANTS = ("Tree", "Bush", "Shrub", "Rock", "Boulder")
START_CLEAR, OBJECT_CLEAR, RAMP_CLEAR, STEEP = 380.0, 130.0, 140.0, 0.6
SPACING = 16.0
MIN_MIX = 5


def reference_rates():
    d = os.path.join(HERE, "work", "refs", REF)
    info = json.load(open(os.path.join(d, "info.json")))
    tex, B = np.load(os.path.join(d, "textures.npy")), info["border"]
    names = info["textures"]
    inner = tex[B:-B, B:-B]
    area = collections.Counter({names[k]: int(v) for k, v in zip(*np.unique(inner[inner >= 0], return_counts=True))})
    mix, overall = collections.defaultdict(collections.Counter), collections.Counter()
    for o in json.load(open(os.path.join(d, "objects.json"))):
        if o["kind"] == "object" and o["template"].startswith(PLANTS):
            t = tex[int(o["y"] / CELL + B), int(o["x"] / CELL + B)]
            if t >= 0:
                mix[names[t]][o["template"]] += 1
                overall[o["template"]] += 1
    rate = {n: sum(mix[n].values()) / a for n, a in area.items()}
    rest = [rate[n] for n in rate if not n.startswith("Cliff")]
    return rate, mix, overall, float(np.median(rest))


def pick(counter, rng):
    names = list(counter)
    w = np.array([counter[n] for n in names], float)
    return names[rng.choice(len(names), p=w / w.sum())]


def main():
    args = sys.argv[1:]
    seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
    map_path, work, plan_path = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:3]
    rng = np.random.default_rng(seed)
    plan = json.load(open(plan_path))
    B = plan["border"]
    rate, mix, overall, fallback = reference_rates()
    f = np.load(os.path.join(HERE, "work", "maps", NAME, "features.npz"))
    shape = f["slope"].shape
    masks_dir = os.path.join(work, "stageB")
    tex = np.full(shape, "", object)
    for m in json.load(open(os.path.join(masks_dir, "masks.json")))["masks"]:
        a = np.array(Image.open(m["path"]).convert("L"))[::-1] > 127
        tex[:a.shape[0], :a.shape[1]][a] = m["texture"]
    yy, xx = np.indices(shape, dtype=np.float32)
    X, Y = (xx - B + 0.5) * CELL, (yy - B + 0.5) * CELL
    W, H = plan["playable_cells"]
    ok = (f["slope"] < STEEP) & (X > -200) & (Y > -200) & (X < W * CELL + 200) & (Y < H * CELL + 200)
    # Nothing under water: the reference has no sea, so its rates know nothing of the seabed.
    zh = np.load(os.path.join(work, "heights_world.npy"))
    zc = (zh[:-1, :-1] + zh[1:, :-1] + zh[:-1, 1:] + zh[1:, 1:]) / 4
    wet = np.zeros(shape, bool)
    wet[:zc.shape[0], :zc.shape[1]] = zc < plan["water"]["height"] + 3
    ok &= ~wet
    mock = json.load(open(os.path.join(work, "mock", "mock.json")))["objects"]["objects"]
    for o in mock:
        if o["kind"] == "waypoint":
            ok &= np.hypot(X - o["x"], Y - o["y"]) > START_CLEAR
    for o in json.load(open(os.path.join(work, "objects.json"))):
        ok &= np.hypot(X - o["x"], Y - o["y"]) > OBJECT_CLEAR
    for r in json.load(open(os.path.join(work, "stageA", "report.json")))["ramps"]:
        ok &= np.hypot(X - r["at"][0], Y - r["at"][1]) > RAMP_CLEAR + r["radius"] * CELL
    p = np.zeros(shape, np.float32)
    for n in set(tex[ok].tolist()):
        p[ok & (tex == n)] = rate.get(n, fallback) if not n.startswith("Cliff") else 0.0
    cells = np.argwhere(rng.random(shape) < p)
    rng.shuffle(cells)
    plants, grid = [], {}
    for iy, ix in cells:
        x, y = float(X[iy, ix] + rng.uniform(-5, 5)), float(Y[iy, ix] + rng.uniform(-5, 5))
        gx, gy = int(x // SPACING), int(y // SPACING)
        if any(math.hypot(x - q[0], y - q[1]) < SPACING for dx in (-1, 0, 1) for dy in (-1, 0, 1) for q in grid.get((gx + dx, gy + dy), [])):
            continue
        grid.setdefault((gx, gy), []).append((x, y))
        n = tex[iy, ix]
        plants.append({"template": pick(mix[n] if sum(mix[n].values()) >= MIN_MIX else overall, rng), "x": round(x, 1), "y": round(y, 1),
                       "angle_deg": round(float(rng.uniform(0, 360)), 1), "ground": n})
    on = collections.Counter(q["ground"] for q in plants)
    out = os.path.join(work, "stageC")
    os.makedirs(out, exist_ok=True)
    json.dump({"reference": REF, "rate_per_1000_cells": {k: round(v * 1000, 2) for k, v in rate.items()}, "by_ground": dict(on.most_common()),
               "plants": plants}, open(os.path.join(out, "plants.json"), "w"), indent=0)
    print(len(plants), "plants; by ground:", dict(on.most_common()))
    print("by kind:", dict(collections.Counter(q["template"] for q in plants).most_common()))
    print("reference rate per 1000 cells:", {k: round(v * 1000, 1) for k, v in sorted(rate.items(), key=lambda kv: -kv[1])})

    if "--place" in args:
        from wb_bridge import WorldBuilderBridge
        wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
        wb.call("map.new", width=32, height=32, discard_changes=True)
        wb.call("map.open", path=map_path, discard_changes=True)
        for q in plants:
            wb.call("objects.place", template=q["template"], x=q["x"], y=q["y"], angle_deg=q["angle_deg"])
        wb.call("map.save", path=map_path)
        print("placed and saved", map_path)


if __name__ == "__main__":
    main()
