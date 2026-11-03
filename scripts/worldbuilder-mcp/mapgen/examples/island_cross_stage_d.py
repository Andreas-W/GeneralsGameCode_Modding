# Stage D of IslandCrossGen, in the second WorldBuilder: an Asian city over the plateau hill in the centre, on both levels.
# 1. Both city levels flat (main level 100, plateau 130), ramps kept; passability again from the slope.
# 2. The template's four Tech buildings go: hospital, refinery and repair bay come back as the town generator's lots, the construction
#    yard (no lot for it) is placed on the lower ring first and the town is built around it.
# 3. A road from every peninsula over its neck to the edge of the city, so the lower town connects to them.
# 4. One town over both levels (see there); streets up the four ramps where the town left them out.
# Usage: island_cross_stage_d.py <map path> <out dir> [--save]   Without --save the towns are dry runs (previews in the town work folder).
import json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "town"), os.path.join(HERE, "cliffs")]
from wb_bridge import WorldBuilderBridge
import cliff_scan, town_tool

RAW, CELL = 0.625, 10.0
C = (2245.0, 2245.0)
MAIN, TOP = 100.0, 130.0
CITY_R = 780.0              # flattening and the lower town reach no further from the centre
RAMPS = (45, 135, 225, 315)  # roughly, the plan's ramp directions
ROAD = "TwoLane"
CY_AT = (C[0] + 560, C[1] + 20)

path, out = sys.argv[1], sys.argv[2]
save = "--save" in sys.argv
os.makedirs(out, exist_ok=True)
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
info = wb.call("map.open", path=path, discard_changes=True)
hm = info["heightmap"]
B = hm["border"]
z = cliff_scan.heights(wb, hm["x_extent"], hm["y_extent"]) * RAW
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * CELL, (yy - B) * CELL
R = np.hypot(X - C[0], Y - C[1])

# 1. Flat levels. Only gentle ground near a level snaps to it, so cliffs, ramps and the beach slopes stay.
gy, gx = np.gradient(z, CELL)
slope = ndimage.maximum_filter(np.hypot(gx, gy), 3)
before = z.copy()
for level in (MAIN, TOP):
    z = np.where((R < CITY_R) & (np.abs(z - level) < 8) & (slope < 0.12), level, z)
changed = np.abs(z - before) > 0.01
seam = ndimage.binary_dilation(changed, iterations=2) & ~changed & (R < CITY_R + 40) & (slope < 0.25)
z = np.where(seam, (ndimage.uniform_filter(z, 3) + z) / 2, z)
print("flattened", int(changed.sum()), "vertices, max change", round(float(np.abs(z - before).max()), 1), flush=True)
png = os.path.join(out, "heights_d.png")
Image.fromarray(np.round(np.clip(z / RAW, 0, 255) / 255 * 65535).astype(np.uint16)[::-1]).save(png)
wb.call("terrain.import_heightmap", path=png, fit="exact", range=[0, 255])
gy, gx = np.gradient(z, CELL)
sl = np.hypot(gx, gy)
cells = np.maximum.reduce([sl[:-1, :-1], sl[1:, :-1], sl[:-1, 1:], sl[1:, 1:]]) >= 0.4
mask = os.path.join(out, "impassable_d.png")
Image.fromarray((cells[::-1] * 255).astype(np.uint8)).save(mask)
wb.call("terrain.import_mask", path=mask, target="passability", fit="exact", invert=True, impassable=False)
wb.call("terrain.import_mask", path=mask, target="passability", fit="exact")


def at(x, y, a):
    i, j = int(round(x / CELL + B)), int(round(y / CELL + B))
    return a[j, i]


def ray_outline(ok, r0, r1, inset, step=15):
    # For each direction, how far from the centre the ground stays ok (going out from r0), pulled in by inset.
    pts = []
    for a in range(0, 360, step):
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        r = r0
        while r < r1 and ok(C[0] + r * ca, C[1] + r * sa):
            r += 5
        r = max(r0, r - inset)
        pts.append([round(C[0] + r * ca), round(C[1] + r * sa)])
    return pts


# 2. Tech: the template's go, the construction yard comes back on the ring.
listed = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
old = [o["id"] for o in listed if o["kind"] == "object" and o["template"].startswith("Tech") and o["template"] != "TechOilDerrick"
       and math.hypot(o["x"] - C[0], o["y"] - C[1]) < 700]
wb.call("objects.delete", ids=old)
trees = [o["id"] for o in listed if o["kind"] == "object" and math.hypot(o["x"] - CY_AT[0], o["y"] - CY_AT[1]) < 160 and not o["template"].startswith("Tech")]
if trees:
    wb.call("objects.delete", ids=trees)
wb.call("objects.place", template="TechConstructionYard", x=CY_AT[0], y=CY_AT[1], angle_deg=180, owner="teamPlyrCivilian")
print("tech: removed", len(old), "placed TechConstructionYard at", CY_AT, flush=True)

# 3. Roads over the necks: from each peninsula's base edge to the edge of the city.
plan = json.load(open(os.path.join(os.path.dirname(os.path.dirname(out)), "mapgen_work", "stageC", "trees.json")))
for p in plan["peninsulas"]:
    px, py = p["at"]
    dx, dy = C[0] - px, C[1] - py
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    a = (px + ux * 0.3 * p["radius"], py + uy * 0.3 * p["radius"])
    b = (C[0] - ux * (CITY_R - 60), C[1] - uy * (CITY_R - 60))
    try:
        r = wb.call("roads.route", type=ROAD, to=[round(b[0]), round(b[1])], corners="auto", flatten=True, flatten_width=50,
                    flatten_feather=40, slope_weight=0.5, max_grade=10, avoid_cliffs=True, avoid_water=True, object_clearance=0,
                    straighten=1.25, snap=25, end_caps=True, **{"from": [round(a[0]), round(a[1])]})
        print("lane road", [round(v) for v in a], "->", [round(v) for v in b], {k: r.get(k) for k in ("length", "points")}, flush=True)
    except Exception as e:
        print("lane road FAILED", str(e).splitlines()[0], flush=True)

# 4. One town over both levels. The ring around the plateau is only some 250 across: block grids and the organic layout found no room in
#    it, as a separate town per level or per ring sector (1 to 6 buildings each). One grid over the whole main island puts blocks on both
#    levels, and its streets cross the plateau's edge only where it is gentle (the ramps and a few soft stretches).
low_ok = lambda x, y: (at(x, y, z) > MAIN - 6) and at(x, y, sl) < 0.3
city = ray_outline(low_ok, 520, CITY_R, 30, step=10)
json.dump({"city": city}, open(os.path.join(out, "town_polys.json"), "w"))
runs = [("city", dict(polygon=city, style="urban", theme="asian", grid_scale=0.6, jitter=0, major=ROAD, clear_nature=True,
                      lots="hospital:1,refinery:1,repair:1", seed=1))]
for name, kw in runs:
    try:
        res = town_tool.generate(wb, dry_run=not save, **kw)
        print(name, json.dumps({k: res.get(k) for k in ("objects", "road_runs", "notes", "bounds", "preview", "lots")}), flush=True)
    except Exception as e:
        print(name, "FAILED", str(e).splitlines()[0], flush=True)

if save:
    # Streets up the ramps: from the nearest lower road point below each ramp to the nearest upper one above it.
    rp = [o for o in wb.call("objects.list", include_waypoints_and_roads=True, limit=40000)["objects"]
          if o["kind"] == "road_point" and not o["template"].startswith("Sidewalk")]
    for a in RAMPS:
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        # The ramp: along the ray, where the ground climbs from the main level to the top.
        rs = [r for r in range(250, 600, 5) if MAIN + 3 < at(C[0] + r * ca, C[1] + r * sa, z) < TOP - 3]
        if not rs:
            print("ramp", a, "not found on the ray", flush=True)
            continue
        mid = (C[0] + (min(rs) + max(rs)) / 2 * ca, C[1] + (min(rs) + max(rs)) / 2 * sa)
        if any(math.hypot(o["x"] - mid[0], o["y"] - mid[1]) < 100 for o in rp):
            print("ramp", a, "has a street", flush=True)
            continue
        lo_p = (C[0] + (max(rs) + 40) * ca, C[1] + (max(rs) + 40) * sa)
        hi_p = (C[0] + (min(rs) - 40) * ca, C[1] + (min(rs) - 40) * sa)
        near = lambda p, level: min((o for o in rp if abs(at(o["x"], o["y"], z) - level) < 4), key=lambda o: math.hypot(o["x"] - p[0], o["y"] - p[1]), default=None)
        lo, hi = near(lo_p, MAIN), near(hi_p, TOP)
        if lo and hi and math.hypot(lo["x"] - lo_p[0], lo["y"] - lo_p[1]) < 250 and math.hypot(hi["x"] - hi_p[0], hi["y"] - hi_p[1]) < 250:
            try:
                wb.call("roads.route", type=ROAD, to=[hi["x"], hi["y"]], corners="auto", flatten=False, max_grade=0, avoid_cliffs=True,
                        object_clearance=0, straighten=1.25, snap=25, **{"from": [lo["x"], lo["y"]]})
                print("ramp street", a, (round(lo["x"]), round(lo["y"])), "->", (round(hi["x"]), round(hi["y"])), flush=True)
            except Exception as e:
                print("ramp street", a, "FAILED", str(e).splitlines()[0], flush=True)
        else:
            print("ramp", a, "no streets near both ends", flush=True)
    wb.call("terrain.reblend")
    wb.call("map.save", path=path)
    n = wb.call("map.info")["counts"]
    print("saved", path, n)
