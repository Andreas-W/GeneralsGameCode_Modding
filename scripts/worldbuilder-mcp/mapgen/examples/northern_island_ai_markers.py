# NorthernIslandGen, stage E: what the skirmish AI looks up by name, through the second WorldBuilder, by the adapt-map-for-navy rules.
# - Land: ai.skirmish_setup (perimeters, Center / Flank / Backdoor paths, CombatZone).
# - Ships: Shipyard<N>_1..3 3 to 4 cells off the shore, nearer their own start than the other, no land higher than 15 above the water
#   within 6 cells (no cliff), at least 300 apart, within 1300 of the start; NavalPerimeter<N>, an octagon of 500 over the water in front
#   of them; Naval<N> along one sea lane and NavalFlank<N> around the other side of the island, every point moved to the nearest water at
#   least DEEP cells from land and inside the playable area.
# Usage: northern_island_ai_markers.py <plan.json> [--save]
import json, math, os, sys
import numpy as np
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

plan = json.load(open(sys.argv[1]))
CELL, B, WATER, PM = 10.0, plan["border"], plan["water"]["height"], plan["playable_max"]
DEEP, MARGIN = 18, 80.0
z = np.load(os.path.join(os.path.dirname(plan["map"]), "mapgen_work", "heights_world.npy")).astype(np.float64)
sea = z < WATER
d = ndimage.distance_transform_edt(sea)
high = ndimage.maximum_filter(np.where(sea, -1e9, z), size=13) > WATER + 15
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * CELL, (yy - B) * CELL
inside = (X > MARGIN) & (Y > MARGIN) & (X < PM - MARGIN) & (Y < PM - MARGIN)
starts = {int(k["name"].split("_")[1]): tuple(k["at"]) for k in plan["keep"] if k["kind"] == "waypoint"}
# Sea lanes: (Naval, NavalFlank) for each player, from the open sea toward its waters; the shipyards' middle is added at the end.
LANES = {1: ([(1900, 250), (1000, 250), (350, 600)], [(1700, 3700), (700, 3650), (200, 2900), (180, 2100)]),
         2: ([(1900, 3720), (2900, 3700), (3620, 3150)], [(2100, 200), (3300, 250), (3680, 1300), (3700, 1900)])}


def nearest_deep(x, y):
    ok = sea & (d >= DEEP) & inside
    dist = np.where(ok, np.hypot(X - x, Y - y), 1e12)
    i = np.unravel_index(np.argmin(dist), z.shape)
    return float(X[i]), float(Y[i])


def yards(n):
    s, o = starts[n], starts[3 - n]
    ds, do = np.hypot(X - s[0], Y - s[1]), np.hypot(X - o[0], Y - o[1])
    cand = sea & (d >= 3) & (d <= 4) & ~high & inside & (ds < do) & (ds < 1300)
    order = np.argsort(ds[cand])
    cx, cy = X[cand][order], Y[cand][order]
    picked = []
    for x, y in zip(cx, cy):
        if all(math.hypot(x - a, y - b) >= 300 for a, b in picked):
            picked.append((float(x), float(y)))
        if len(picked) == 3:
            break
    return picked


def octagon(cx, cy, r):
    # Corners stay inside the playable area, 60 to spare: a perimeter off the map is never entered.
    def c(v):
        return round(min(max(v, 60.0), PM - 60.0), 1)
    return [[c(cx + r * math.cos(math.radians(22.5 + 45 * k))), c(cy + r * math.sin(math.radians(22.5 + 45 * k)))] for k in range(8)]


result = {}
for n in starts:
    ys = yards(n)
    mx, my = np.mean([p[0] for p in ys]), np.mean([p[1] for p in ys])
    # The perimeter's middle moves out to sea, so that it covers water in front of the shipyards rather than the base.
    sx, sy = starts[n]
    v = np.array([mx - sx, my - sy]); v = v / np.linalg.norm(v)
    centre = (mx + v[0] * 250, my + v[1] * 250)
    paths = {}
    for label, lane in zip(("Naval", "NavalFlank"), LANES[n]):
        pts = [nearest_deep(x, y) for x, y in lane] + [nearest_deep(mx + v[0] * 300, my + v[1] * 300)]
        clean = []
        for p in pts:
            if not clean or math.dist(p, clean[-1]) > 150:
                clean.append(p)
        paths[label] = clean
    result[n] = {"yards": ys, "perimeter": octagon(*centre, 500), "paths": paths}
    print(n, "yards", [(round(a), round(b), round(math.dist((a, b), starts[n]))) for a, b in ys], "perimeter centre", [round(c) for c in centre],
          {k: [(round(a), round(b)) for a, b in v_] for k, v_ in paths.items()})
if "--save" not in sys.argv:
    sys.exit(0)
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=plan["map"], discard_changes=True)
print(json.dumps(wb.call("ai.skirmish_setup", inner_radius=350, outer_radius=650))[:200])
for n, r in result.items():
    for k, (x, y) in enumerate(r["yards"]):
        wb.call("waypoints.add", x=round(x, 1), y=round(y, 1), name=f"Shipyard{n}_{k + 1}")
    wb.call("polygons.add", kind="area", name=f"NavalPerimeter{n}", points=r["perimeter"])
    for label, pts in r["paths"].items():
        prev = None
        for k, (x, y) in enumerate(pts):
            name = f"{label}{n}_{k + 1}"
            wb.call("waypoints.add", x=round(x, 1), y=round(y, 1), name=name, path_labels=[f"{label}{n}"], **({"link_from": prev} if prev else {}))
            prev = name
check = wb.call("ai.skirmish_check")
print(json.dumps({k: check[k] for k in ("problems", "ready", "ships_enabled", "naval_problems", "naval_ready")}))
wb.call("map.save", path=plan["map"], overwrite=True)
print("saved")
