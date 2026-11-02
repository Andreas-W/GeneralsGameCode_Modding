# Example from the first slice (TaintedParadise, see ../README.md); its coordinates and polygons belong to that map.
# Stage D again, in the second WorldBuilder, from the map as it was before the towns:
# flatten the city's two levels, passability again, bridge ends onto the cliff edges, the two towns (no jitter), roads to the bridges.
# Usage: tainted_paradise_stage_d.py <work dir> <work map in the map's folder> [save path]   work dir holds stageA/impassable.png.   Without save path, the towns are dry runs.
import json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage
HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "town"), os.path.join(HERE, "cliffs")]
from wb_bridge import WorldBuilderBridge
import cliff_scan, town_tool

S, work = sys.argv[1], sys.argv[2]
save = sys.argv[3] if len(sys.argv) > 3 else None
RAW, B, WATER, TOP_RAW = 0.625, 20, 60.0, 306
wb = WorldBuilderBridge(port=47810, timeout=600)
info = wb.call("map.open", path=work, discard_changes=True)
hm = info["heightmap"]
assert abs(hm["height_map_scale"] - 1.2) < 0.01, hm
z = cliff_scan.heights(wb, hm["x_extent"], hm["y_extent"]) * RAW
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * 10.0, (yy - B) * 10.0

# 1. City: each level flat, the cliff between them and the ramps kept.
gy, gx = np.gradient(z, 10.0)
slope = ndimage.maximum_filter(np.hypot(gx, gy), 3)
city = (X >= 2600) & (X <= 3600) & (Y >= -50) & (Y <= 1100)
before = z.copy()
for level in (99.4, 125.6):
    snap = city & (np.abs(z - level) < 10) & (slope < 0.12)
    z = np.where(snap, level, z)
# Soften the seam between snapped and untouched ground over a few cells.
changed = np.abs(z - before) > 0.01
near = ndimage.binary_dilation(changed, iterations=2) & ~changed & city & (slope < 0.25)
z = np.where(near, (ndimage.uniform_filter(z, 3) + z) / 2, z)
print("city: vertices flattened", int(changed.sum()), "max change", round(float(np.abs(z - before).max()), 1), flush=True)
png = os.path.join(S, "rebuild_heights.png")
Image.fromarray(np.round(np.clip(z / RAW, 0, TOP_RAW) / TOP_RAW * 65535).astype(np.uint16)[::-1]).save(png)
wb.call("terrain.import_heightmap", path=png, fit="exact", range=[0, TOP_RAW])

# 2. Passability: steep ground, and the mountains of stage A.
imp0 = np.array(Image.open(os.path.join(S, "stageA", "impassable.png")))[::-1] > 0
gy, gx = np.gradient(z, 10.0)
sl = np.hypot(gx, gy)
cells = (np.maximum.reduce([sl[:-1, :-1], sl[1:, :-1], sl[:-1, 1:], sl[1:, 1:]]) >= 0.4) | (imp0 & (sl[:-1, :-1] < 0.4))
mask = os.path.join(S, "rebuild_impassable.png")
Image.fromarray((cells[::-1] * 255).astype(np.uint8)).save(mask)
wb.call("terrain.import_mask", path=mask, target="passability", fit="exact", invert=True, impassable=False)
wb.call("terrain.import_mask", path=mask, target="passability", fit="exact")

# 3. Bridges: each end onto the edge of the cliff on its side.
def height_at(x, y):
    i, j = x / 10 + B, y / 10 + B
    return float(ndimage.map_coordinates(z, [[j], [i]], order=1)[0])
listed = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
bp = [o for o in listed if o["kind"] == "bridge_point"]
bridges = []
for a, b in zip(bp[0::2], bp[1::2]):
    mx, my = (a["x"] + b["x"]) / 2, (a["y"] + b["y"]) / 2
    ends = []
    for e in (a, b):
        dx, dy = e["x"] - mx, e["y"] - my
        L = math.hypot(dx, dy)
        dx, dy = dx / L, dy / L
        prof = [(s, height_at(mx + dx * s, my + dy * s)) for s in np.arange(0, L + 300, 5)]
        top = max(h for s, h in prof if s >= L - 120)
        edge = next(s for s, h in prof if h >= top - 2)
        ends.append((round(mx + dx * (edge + 20), 1), round(my + dy * (edge + 20), 1), round(top, 1)))
    bridges.append({"template": a["template"], "old": [[a["x"], a["y"]], [b["x"], b["y"]]], "new": [list(ends[0][:2]), list(ends[1][:2])], "tops": [ends[0][2], ends[1][2]], "ids": [a["id"], b["id"]]})
for br in bridges:
    wb.call("objects.delete", ids=br["ids"])
    wb.call("roads.add", type=br["template"], points=br["new"])
    print("bridge", [[round(v) for v in p] for p in br["old"]], "->", [[round(v) for v in p] for p in br["new"]], "tops", br["tops"], flush=True)

# 4. Towns. The city replaces the ring road inside its rectangle.
rp = [o for o in wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"] if o["kind"] == "road_point"]
inside = lambda o: 2600 <= o["x"] <= 3800 and -300 <= o["y"] <= 1000
drop = [i for a, b in zip(rp[0::2], rp[1::2]) if inside(a) and inside(b) for i in (a["id"], b["id"])]
if drop:
    wb.call("objects.delete", ids=drop)
ISLAND = [[1665, 1721], [1810, 1928], [1895, 1954], [2304, 1540], [2224, 1372], [2015, 1306], [1853, 1414]]
runs = [("city", dict(polygon=[[2830, 5], [3585, 5], [3585, 885], [2830, 885]], style="urban", theme="asian", grid_scale=0.75, jitter=0, clear_nature=True, seed=1)),
        ("island", dict(polygon=ISLAND, style="urban", theme="asian", grid_scale=0.6, jitter=0, major="TwoLane", contain=True, clear_nature=True, seed=1))]
for name, kw in runs:
    try:
        res = town_tool.generate(wb, dry_run=not save, **kw)
        print(name, json.dumps({k: res.get(k) for k in ("objects", "road_runs", "notes", "bounds", "preview")}), flush=True)
    except Exception as e:
        print(name, "FAILED", str(e).splitlines()[0], flush=True)

# 5. Roads to the bridges: each end joins the nearest road point on its side (the island town's streets, or the mainland roads).
if save:
    # The island side is the end nearer the island's middle: bridge ends lie on the cliff edge, outside the flat top's outline, which
    # first sent island ends to mainland roads across the water.
    CX, CY = sum(p[0] for p in ISLAND) / len(ISLAND), sum(p[1] for p in ISLAND) / len(ISLAND)
    near_island = lambda x, y: math.hypot(x - CX, y - CY) < 450
    island_ends = set()
    for br in bridges:
        (ax, ay), (bx, by) = br["new"]
        island_ends.add((ax, ay) if math.hypot(ax - CX, ay - CY) < math.hypot(bx - CX, by - CY) else (bx, by))
    def in_island(x, y):
        return (x, y) in island_ends or near_island(x, y)
    rp = [o for o in wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"] if o["kind"] == "road_point" and o["template"] not in ("Sidewalk",)]
    for br in bridges:
        for (ex, ey) in br["new"]:
            isl = in_island(ex, ey)
            cand = [o for o in rp if in_island(o["x"], o["y"]) == isl and math.hypot(o["x"] - ex, o["y"] - ey) > 15]
            if not cand:
                print("no road near bridge end", ex, ey, flush=True)
                continue
            o = min(cand, key=lambda o: math.hypot(o["x"] - ex, o["y"] - ey))
            d = math.hypot(o["x"] - ex, o["y"] - ey)
            if d > 450:
                print("nearest road too far from bridge end", ex, ey, round(d), flush=True)
                continue
            wb.call("roads.add", type="TwoLane" if isl else o["template"] if o["template"] in ("TwoLane", "DirtRoad4") else "TwoLane", points=[[ex, ey], [o["x"], o["y"]]])
            print("road", "island" if isl else "mainland", "bridge end", (round(ex), round(ey)), "->", o["template"], (round(o["x"]), round(o["y"])), round(d), flush=True)
    wb.call("terrain.reblend")
    wb.call("map.save", path=save)
    print("saved", save)
