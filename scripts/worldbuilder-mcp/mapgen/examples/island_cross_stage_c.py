# Stage C of IslandCrossGen: jungle. Tight groves in the mix of the jungle reference (Eastern Everglades: palms, dogwood and oak in
# groves some 14 apart) with bamboo stands and undergrowth, and palms in loose groups on the beaches. Kept open: each player's base core,
# a lane from every peninsula over its neck to the centre, the main island for the city (stage D), the oil derricks, cliffs and water.
# Positions come from the map's scan (work/maps/<name>/features.npz); the trees are placed through the second WorldBuilder (port 47810)
# and the map is saved there. Without --place it only writes the positions and a preview.
# Usage: island_cross_stage_c.py <map name> <map path> <out dir> [--place] [--seed N]
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [HERE, os.path.dirname(HERE)]
import painter

CELL = 10.0
CENTER = (2245.0, 2245.0)
CITY = 640.0          # main island kept open within this distance of the centre
CORE = 0.55           # base core: this share of a peninsula's radius; only strong groves stand in it
CORE_CLEAR = 0.4      # inner part of the core, always open
CORE_EDGE = 80.0      # the forest edge around a core fades in over this distance
LANE = 110.0          # half width of the lane from each peninsula to the centre
DERRICK = 110.0
STEEP = 0.35
SPACING = 17.0        # closest two trees in a grove
GRID = 20.0           # candidate spacing before thinning
GROVE = (14, -0.35)    # noise scale (cells) of groves and how much of the jungle zone stays a clearing (noise below this)
BEACH_SPACING = 40.0
BEACH_SHARE = 0.3
JUNGLE = {"TreePalm04": 22, "TreePalm03": 20, "TreeDogwood1": 18, "TreePalm1short": 16, "TreeOak01": 10, "Bush08": 6}
BAMBOO = {"TreeBamboo01": 5, "TreeBamboo02": 3, "TreeBamboo03": 2}
PALMS = {"TreePalm1": 3, "TreePalm2": 3, "TreePalm1short": 2, "TreePalm2short": 2, "TreePalm04": 2}


def pick(mix, rng):
    names = list(mix)
    w = np.array([mix[n] for n in names], float)
    return names[rng.choice(len(names), p=w / w.sum())]


def thin(points, spacing):
    # Keeps points in order, dropping any closer than spacing to one kept before.
    kept, grid = [], {}
    for p in points:
        gx, gy = int(p[0] // spacing), int(p[1] // spacing)
        near = [q for dx in (-1, 0, 1) for dy in (-1, 0, 1) for q in grid.get((gx + dx, gy + dy), [])]
        if all(math.hypot(p[0] - q[0], p[1] - q[1]) >= spacing for q in near):
            kept.append(p)
            grid.setdefault((gx, gy), []).append(p)
    return kept


def main():
    args = sys.argv[1:]
    seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
    name, map_path, out = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:3]
    rng = np.random.default_rng(seed)
    d = os.path.join(HERE, "work", "maps", name)
    info = json.load(open(os.path.join(d, "info.json")))
    f = np.load(os.path.join(d, "features.npz"))
    B = info["border"]
    water = max(w for w in info["water"] if w is not None)
    objs = json.load(open(os.path.join(d, "objects.json")))
    shape = f["z"].shape
    yy, xx = np.indices(shape, dtype=np.float32)
    X, Y = (xx - B + 0.5) * CELL, (yy - B + 0.5) * CELL
    land = f["wet"] < 0.5
    r = np.hypot(X - CENTER[0], Y - CENTER[1])
    ang = np.degrees(np.arctan2(Y - CENTER[1], X - CENTER[0])) % 360

    # Peninsulas: the land beyond the main island in each diagonal sector; centre and radius from its area.
    open_ = np.zeros(shape, bool)
    edge = np.zeros(shape, np.float32)
    peninsulas = []
    for a in (225, 315, 45, 135):
        sec = land & (np.abs((ang - a + 180) % 360 - 180) < 45) & (r > 900)
        cx, cy = float(X[sec].mean()), float(Y[sec].mean())
        rad = math.sqrt(sec.sum() * CELL * CELL / math.pi)
        peninsulas.append({"at": [round(cx), round(cy)], "radius": round(rad)})
        dc = np.hypot(X - cx, Y - cy)
        core = CORE * rad
        open_ |= dc < CORE_CLEAR * rad
        edge = np.maximum(edge, np.clip(1 - (dc - core) / CORE_EDGE, 0, 1))
        # The lane over the neck: a band along the line to the centre.
        ux, uy = CENTER[0] - cx, CENTER[1] - cy
        t = np.clip(((X - cx) * ux + (Y - cy) * uy) / (ux * ux + uy * uy), 0, 1)
        open_ |= np.hypot(X - cx - t * ux, Y - cy - t * uy) < LANE
    open_ |= r < CITY
    for o in objs:
        if o["template"] == "TechOilDerrick":
            open_ |= np.hypot(X - o["x"], Y - o["y"]) < DERRICK
    ok = land & (f["slope"] < STEEP) & ~open_
    above = f["z"] - water
    beach = ok & (f["to_water"] <= 30) & (above < 18)
    jungle = ok & ~beach

    # Groves: noise above the clearing threshold, fuller towards the coast and the cliffs, thinner at a base's edge.
    grove = painter.field(shape, GROVE[0], rng)
    coastal = np.clip(1 - f["to_water"] / 25.0, 0, 1)
    p = np.clip((grove - GROVE[1]) * 1.4 + 0.35 * coastal, 0, 1)
    # In the core only the strongest groves, so a base has room but is no bare disc.
    p = np.where(edge >= 1, np.clip((grove - 1.2) * 2, 0, 1), p * (1 - 0.6 * edge))
    bamboo = painter.field(shape, 6, rng) > 1.1

    trees = []
    cand = [(x, y) for y in np.arange(-B * CELL, (shape[0] - B) * CELL, GRID) for x in np.arange(-B * CELL, (shape[1] - B) * CELL, GRID)]
    cand = [(x + rng.uniform(-GRID / 2, GRID / 2), y + rng.uniform(-GRID / 2, GRID / 2)) for x, y in cand]
    rng.shuffle(cand)
    cell = lambda x, y: (int((y / CELL) + B), int((x / CELL) + B))
    jp, bp = [], []
    for x, y in cand:
        iy, ix = cell(x, y)
        if not (0 <= iy < shape[0] and 0 <= ix < shape[1]):
            continue
        if jungle[iy, ix] and rng.random() < p[iy, ix]:
            jp.append((x, y))
        elif beach[iy, ix] and rng.random() < BEACH_SHARE:
            bp.append((x, y))
    for x, y in thin(jp, SPACING):
        iy, ix = cell(x, y)
        trees.append({"template": pick(BAMBOO if bamboo[iy, ix] else JUNGLE, rng), "x": round(x, 1), "y": round(y, 1)})
    for x, y in thin(bp, BEACH_SPACING):
        trees.append({"template": pick(PALMS, rng), "x": round(x, 1), "y": round(y, 1)})
    for t in trees:
        t["angle_deg"] = round(float(rng.uniform(0, 360)), 1)

    os.makedirs(out, exist_ok=True)
    counts = {}
    for t in trees:
        counts[t["template"]] = counts.get(t["template"], 0) + 1
    json.dump({"peninsulas": peninsulas, "counts": counts, "trees": trees}, open(os.path.join(out, "trees.json"), "w"), indent=0)
    print(len(trees), "plants", json.dumps(dict(sorted(counts.items(), key=lambda kv: -kv[1]))))
    print("peninsulas", peninsulas)

    # Preview over the stage B colours, if there.
    k = 2
    base = os.path.join(os.path.dirname(out), "stageB", "composite3", "preview.png")
    im = Image.open(base).convert("RGB").resize((shape[1] * k, shape[0] * k), Image.NEAREST) if os.path.exists(base) else Image.new("RGB", (shape[1] * k, shape[0] * k))
    dr = ImageDraw.Draw(im)
    for t in trees:
        px, py = (t["x"] / CELL + B) * k, (shape[0] - (t["y"] / CELL + B)) * k
        col = (20, 60, 10) if t["template"] in JUNGLE else (120, 160, 30) if t["template"] in BAMBOO else (230, 200, 40)
        dr.ellipse([px - 2, py - 2, px + 2, py + 2], fill=col)
    im.save(os.path.join(out, "preview.png"))

    if "--place" in args:
        from wb_bridge import WorldBuilderBridge
        wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=600)
        # A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
        wb.call("map.new", width=32, height=32, discard_changes=True)
        wb.call("map.open", path=map_path, discard_changes=True)
        for t in trees:
            wb.call("objects.place", **t)
        wb.call("map.save", path=map_path)
        print("placed and saved", map_path)


if __name__ == "__main__":
    main()
