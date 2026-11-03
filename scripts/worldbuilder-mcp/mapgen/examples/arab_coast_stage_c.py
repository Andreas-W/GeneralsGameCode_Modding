# Stage C of ArabCoastGen: patches of vegetation in a desert. Two kinds, as on the desert references: palm groves by the water (Golden
# Oasis, Wasteland Warlords: palms with vines and a willow) on the coastal terraces and beaches, and scrub (Dust Devil, Tournament Desert:
# bushes, desert shrubs, low dogwood) at the foot of the cliff border and on open ground. Clumps of some 5 to 25 plants about 20 apart,
# as there. Kept clear: the template's city and town areas (stage D), bases, roads, Tech and docks, cliffs, water and the high ground.
# Positions come from the map's scan (work/maps/<name>/features.npz, objects.json); placed through the second WorldBuilder (port 47810).
# Usage: arab_coast_stage_c.py <map name> <map path> <areas.json> <out dir> [--place] [--seed N]
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [HERE, os.path.dirname(HERE)]
import painter

CELL = 10.0
WATER = 60.0
MAIN_TOP = 100.0     # above this is the high ground behind the cliff border, out of play
STEEP = 0.35
AREA_MARGIN = 60.0
START_CLEAR = 320.0
OBJECT_CLEAR = 110.0
ROAD_CLEAR = 45.0
GROVE = {"TreePalm1short": 20, "TreePalm03": 15, "TreePalm04": 12, "TreePalm2": 11, "TreePalm1": 8, "TreePalm2short": 6, "VinesGreen": 10, "TreeWillow03": 4}
SCRUB = {"Bush11": 30, "Bush06": 25, "DesertShrub09": 12, "TreeDogwood3": 12, "TreeDogwood1": 10, "TreeDogwood8": 8, "Bush10": 6}
# (kind, number of patches, spacing between patches, patch radius range, plants range, spacing within)
PATCHES = [("grove", 12, 330, (90, 150), (10, 24), 20), ("scrub", 14, 300, (70, 120), (5, 12), 24)]


def pick(mix, rng):
    names = list(mix)
    w = np.array([mix[n] for n in names], float)
    return names[rng.choice(len(names), p=w / w.sum())]


def inside(poly, x, y):
    c = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def main():
    args = sys.argv[1:]
    seed = int(args[args.index("--seed") + 1]) if "--seed" in args else 1
    name, map_path, areas_path, out = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or not args[i - 1].startswith("--"))][:4]
    rng = np.random.default_rng(seed)
    d = os.path.join(HERE, "work", "maps", name)
    info = json.load(open(os.path.join(d, "info.json")))
    f = np.load(os.path.join(d, "features.npz"))
    objs = json.load(open(os.path.join(d, "objects.json")))
    B = info["border"]
    pmx, pmy = info["world"]["playable_max_x"], info["world"]["playable_max_y"]
    shape = f["z"].shape
    yy, xx = np.indices(shape, dtype=np.float32)
    X, Y = (xx - B + 0.5) * CELL, (yy - B + 0.5) * CELL
    ok = (f["wet"] < 0.5) & (f["slope"] < STEEP) & (f["z"] < MAIN_TOP) & (X > 20) & (Y > 20) & (X < pmx - 20) & (Y < pmy - 20)
    areas = [[(p[0], p[1]) for p in a["points"]] for a in json.load(open(areas_path))["polygons"] if a["kind"] == "area"]
    clear = np.zeros(shape, bool)
    for poly in areas:
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        box = (X > min(xs) - AREA_MARGIN) & (X < max(xs) + AREA_MARGIN) & (Y > min(ys) - AREA_MARGIN) & (Y < max(ys) + AREA_MARGIN)
        clear |= box
    for o in objs:
        if o["kind"] == "waypoint":
            clear |= np.hypot(X - o["x"], Y - o["y"]) < START_CLEAR
        elif o["kind"] == "object" and o["template"].startswith(("Tech", "Supply", "Industrial")):
            clear |= np.hypot(X - o["x"], Y - o["y"]) < OBJECT_CLEAR
    rp = [o for o in objs if o["kind"] in ("road_point", "bridge_point")]
    for a, b in zip(rp[0::2], rp[1::2]):
        ux, uy = b["x"] - a["x"], b["y"] - a["y"]
        t = np.clip(((X - a["x"]) * ux + (Y - a["y"]) * uy) / max(ux * ux + uy * uy, 1e-6), 0, 1)
        clear |= np.hypot(X - a["x"] - t * ux, Y - a["y"] - t * uy) < ROAD_CLEAR
    ok &= ~clear
    # Where each kind goes: groves by the water and low, scrub at the foot of a cliff, both a little anywhere open.
    near_water = np.clip(1 - f["to_water"] / 25.0, 0, 1) * (f["z"] < WATER + 25)
    cliff_foot = np.clip(1 - f["to_cliff"] / 10.0, 0, 1) * (f["cliff_side"] < 0)
    noise = painter.field(shape, 20, rng) * 0.15
    score = {"grove": near_water + noise, "scrub": 0.6 * cliff_foot + 0.25 + noise}

    plants, centres = [], []
    for kind, count, gap, (r0, r1), (n0, n1), within in PATCHES:
        s = np.where(ok, score[kind], -1)
        order = np.argsort(s.ravel())[::-1]
        chosen = 0
        for idx in order:
            if chosen >= count or s.flat[idx] <= 0.2:
                break
            cx, cy = float(X.flat[idx]), float(Y.flat[idx])
            if any(math.hypot(cx - px, cy - py) < gap for px, py, _ in centres):
                continue
            centres.append((cx, cy, kind))
            chosen += 1
            r, n = rng.uniform(r0, r1), int(rng.integers(n0, n1 + 1))
            # An irregular patch: points around the centre, denser inside, kept on open ground and apart from each other.
            placed = []
            for _ in range(n * 12):
                if len(placed) >= n:
                    break
                a, dist = rng.uniform(0, 2 * math.pi), r * math.sqrt(rng.uniform(0, 1)) * rng.uniform(0.6, 1.0)
                x, y = cx + dist * math.cos(a), cy + dist * math.sin(a)
                iy, ix = int(y / CELL + B), int(x / CELL + B)
                if not (0 <= iy < shape[0] and 0 <= ix < shape[1]) or not ok[iy, ix]:
                    continue
                if any(math.hypot(x - qx, y - qy) < within for qx, qy in placed):
                    continue
                placed.append((x, y))
                plants.append({"template": pick(GROVE if kind == "grove" else SCRUB, rng), "x": round(x, 1), "y": round(y, 1),
                               "angle_deg": round(float(rng.uniform(0, 360)), 1)})
    counts = {}
    for p in plants:
        counts[p["template"]] = counts.get(p["template"], 0) + 1
    os.makedirs(out, exist_ok=True)
    json.dump({"patches": [{"at": [round(x), round(y)], "kind": k} for x, y, k in centres], "counts": counts, "plants": plants},
              open(os.path.join(out, "plants.json"), "w"), indent=0)
    print(len(plants), "plants in", len(centres), "patches", json.dumps(dict(sorted(counts.items(), key=lambda kv: -kv[1]))))

    k = 3
    base = os.path.join(os.path.dirname(out), "stageB", "coast2", "preview.png")
    im = Image.open(base).convert("RGB").resize((shape[1] * k, shape[0] * k), Image.NEAREST)
    dr = ImageDraw.Draw(im)
    for poly in areas:
        dr.polygon([((x / CELL + B) * k, (shape[0] - (y / CELL + B)) * k) for x, y in poly], outline=(0, 160, 255))
    for p in plants:
        px, py = (p["x"] / CELL + B) * k, (shape[0] - (p["y"] / CELL + B)) * k
        col = (30, 110, 20) if p["template"] in GROVE else (120, 100, 40)
        dr.ellipse([px - 3, py - 3, px + 3, py + 3], fill=col)
    im.save(os.path.join(out, "preview.png"))

    if "--place" in args:
        from wb_bridge import WorldBuilderBridge
        wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=600)
        # A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
        wb.call("map.new", width=32, height=32, discard_changes=True)
        wb.call("map.open", path=map_path, discard_changes=True)
        for p in plants:
            wb.call("objects.place", **p)
        wb.call("map.save", path=map_path)
        print("placed and saved", map_path)


if __name__ == "__main__":
    main()
