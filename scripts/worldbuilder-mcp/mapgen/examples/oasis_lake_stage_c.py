# Stage C of OasisLakeGen: vegetation by ground, as the style reference has it. On Golden Oasis plants stand on grass far more than on
# sand (28 to 70 per 1000 cells on its grasses, 0.4 on its main sand: the oases are palm groves on grass patches), so each cell of the
# painted map gets the reference's rate for its texture, and a plant the reference's mix on that texture. Textures the reference has no
# plants on get none; a texture it lacks gets the rate of its class (grass or not). Kept clear: bases, the island (stage D), the ramps
# onto the corner plateaus, cliffs and water.
# Usage: oasis_lake_stage_c.py <map name> <map path> <masks dir> <plan.json> <out dir> [--place] [--seed N]
import collections, json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [HERE, os.path.dirname(HERE)]
import symmetry

CELL = 10.0
REF = "Golden Oasis"
PLANTS = ("Tree", "Bush", "Vine", "Shrub", "Desert", "Reed")
START_CLEAR, RAMP_CLEAR, STEEP = 380.0, 140.0, 0.35
SPACING = 16.0
MIN_MIX = 5          # plants on a texture before its own mix counts; fewer: the reference's overall mix


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
    # A texture the reference lacks: the rate of its class, grass or not.
    grass = [rate[n] for n in rate if n.startswith("Grass")]
    rest = [rate[n] for n in rate if not n.startswith(("Grass", "Cliff"))]
    by_class = {"grass": float(np.median(grass)), "other": float(np.median(rest))}
    return rate, mix, overall, by_class


def pick(counter, rng):
    names = list(counter)
    w = np.array([counter[n] for n in names], float)
    return names[rng.choice(len(names), p=w / w.sum())]


def main():
    args = sys.argv[1:]
    seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
    name, map_path, masks_dir, plan_path, out = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:5]
    rng = np.random.default_rng(seed)
    plan = json.load(open(plan_path))
    L, B = plan["layout"], plan["border"]
    C = (L["playable_cells"] * CELL / 2,) * 2
    rate, mix, overall, by_class = reference_rates()
    f = np.load(os.path.join(HERE, "work", "maps", name, "features.npz"))
    shape = f["slope"].shape
    # The painted ground, from the masks (top row north).
    tex = np.full(shape, "", object)
    for m in json.load(open(os.path.join(masks_dir, "masks.json")))["masks"]:
        a = np.array(Image.open(m["path"]).convert("L"))[::-1] > 127
        tex[:a.shape[0], :a.shape[1]][a] = m["texture"]
    yy, xx = np.indices(shape, dtype=np.float32)
    X, Y = (xx - B + 0.5) * CELL, (yy - B + 0.5) * CELL
    ok = (f["wet"] < 0.5) & (f["slope"] < STEEP) & (np.hypot(X - C[0], Y - C[1]) > L["island_radius"] + 150)
    ok &= (X > 0) & (Y > 0) & (X < 2 * C[0]) & (Y < 2 * C[1])
    a = math.radians(L["start_angle"])
    for px, py, _ in symmetry.images(C[0] + L["start_radius"] * math.cos(a), C[1] + L["start_radius"] * math.sin(a), C):
        ok &= np.hypot(X - px, Y - py) > START_CLEAR
    for px, py, _ in symmetry.images(C[0] + L["ramp_u"], C[1] + L["corner"], C):
        ok &= np.hypot(X - px, Y - py) > RAMP_CLEAR
    p = np.zeros(shape, np.float32)
    for n in set(tex[ok].tolist()):
        r = rate.get(n, by_class["grass" if n.startswith("Grass") else "other"] if not n.startswith("Cliff") else 0.0)
        p[ok & (tex == n)] = r
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
    os.makedirs(out, exist_ok=True)
    json.dump({"reference": REF, "rate_per_1000_cells": {k: round(v * 1000, 2) for k, v in rate.items()}, "by_ground": dict(on.most_common()),
               "plants": plants}, open(os.path.join(out, "plants.json"), "w"), indent=0)
    print(len(plants), "plants; by ground:", dict(on.most_common()))
    print("by kind:", dict(collections.Counter(q["template"] for q in plants).most_common()))

    k = 2
    im = Image.open(os.path.join(masks_dir, "preview.png")).convert("RGB").resize((shape[1] * k, shape[0] * k), Image.NEAREST)
    dr = ImageDraw.Draw(im)
    for q in plants:
        px, py = (q["x"] / CELL + B) * k, (shape[0] - (q["y"] / CELL + B)) * k
        dr.ellipse([px - 2, py - 2, px + 2, py + 2], fill=(20, 90, 10))
    im.save(os.path.join(out, "preview.png"))

    if "--place" in args:
        from wb_bridge import WorldBuilderBridge
        wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=600)
        # A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
        wb.call("map.new", width=32, height=32, discard_changes=True)
        wb.call("map.open", path=map_path, discard_changes=True)
        for q in plants:
            wb.call("objects.place", template=q["template"], x=q["x"], y=q["y"], angle_deg=q["angle_deg"])
        wb.call("map.save", path=map_path)
        print("placed and saved", map_path)


if __name__ == "__main__":
    main()
