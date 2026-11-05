# SnowyMountainsGen after stage A: Tech buildings and supplies, relief (relief.py), then the map itself in the second WorldBuilder:
# a new 300 x 360 map at HeightMapScale 1.2 (saved, map.ini written, opened again, then heights imported), passability, starts,
# a civilian player with the objects, snowfall. Textures, plants and AI markers come later.
# Objects stand in pairs turned by 180 degrees about the centre, so both players get the same: a supply dock near the start, an
# expansion (oil derrick and three supply piles) on the terrace reached by the ramp near the start, an oil derrick on each side of the
# bowl, and a hospital in the middle of the bowl. Each is moved to the nearest flat open spot whose turned twin is flat and open too.
# Usage: snowy_mountains_build.py <plan.json> <work dir (mapgen_work)> [--save]
import json, math, os, subprocess, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
CELL, B, RAW = 10.0, plan["border"], 0.625
W, H = plan["playable_cells"]
C = np.array([W * CELL / 2, H * CELL / 2])
OWNER = "teamPlyrCivilian"
stage_a = os.path.join(work, "stageA")
z = np.load(os.path.join(stage_a, "heights_world.npy")).astype(np.float64)
ny, nx = z.shape
imp = np.zeros(z.shape, bool)
cells = np.array(Image.open(os.path.join(stage_a, "impassable.png")).convert("L"))[::-1] > 127
imp[:cells.shape[0], :cells.shape[1]] = cells
mock = json.load(open(os.path.join(work, "mock", "mock.json")))["objects"]["objects"]
starts = {o["unique_id"]: (o["x"], o["y"]) for o in mock if o["kind"] == "waypoint"}

# For the south player (Player_2_Start); the north player gets the same turned. (template, x, y, angle, footprint radius in cells)
SOUTH = [
    ("SupplyDock", 2250, 1050, 135, 7),
    ("TechOilDerrick", 600, 1250, 0, 5),
    ("SupplyPileSmall", 650, 820, 0, 9),       # a group of three, see PILES
    ("TechOilDerrick", 1150, 1900, 0, 5),
]
PILES = [(0, 0), (70, 45), (-60, 55)]
CENTRE = [("TechHospital", C[0], C[1], 0, 8)]

gy, gx = np.gradient(z, CELL)
slope = np.hypot(gx, gy)
yy, xx = np.indices(z.shape)
bad = ndimage.binary_dilation(imp, iterations=5) | (ndimage.maximum_filter(slope, 9) > 0.12)


def ok(x, y, r):
    i, j = int(round(y / CELL + B)), int(round(x / CELL + B))
    if not (r <= j < nx - r and r <= i < ny - r):
        return False
    return not bad[i - r:i + r + 1, j - r:j + r + 1].any()


def turned(x, y):
    return 2 * C[0] - x, 2 * C[1] - y


def place(x, y, r, pair=True):
    # The nearest spot (in rings of 10) where the object and its turned twin both fit.
    for d in range(0, 400, 10):
        for a in range(0, 360, 15 if d else 360):
            px, py = x + d * math.cos(math.radians(a)), y + d * math.sin(math.radians(a))
            if ok(px, py, r) and (not pair or ok(*turned(px, py), r)):
                return px, py, d
    raise SystemExit(f"no flat spot near {x}, {y}")


objs, report = [], []
for t, x, y, a, r in SOUTH:
    px, py, d = place(x, y, r)
    report.append((t, round(px), round(py), d))
    for sx, sy, ang in ((px, py, a), (*turned(px, py), a + 180)):
        offsets = PILES if t == "SupplyPileSmall" else [(0, 0)]
        sgn = 1 if (sx, sy) == (px, py) else -1
        for ox, oy in offsets:
            objs.append({"kind": "object", "template": t, "x": round(sx + sgn * ox, 1), "y": round(sy + sgn * oy, 1), "angle_deg": ang % 360, "owner": OWNER})
for t, x, y, a, r in CENTRE:
    if not ok(x, y, r):
        print("centre not flat for", t, "- relief keeps it flat")
    objs.append({"kind": "object", "template": t, "x": round(x, 1), "y": round(y, 1), "angle_deg": a, "owner": OWNER})
print("moved", report)
json.dump(objs, open(os.path.join(work, "objects.json"), "w"), indent=0)

keep = [o for o in mock if o["kind"] == "waypoint"] + objs
json.dump(keep, open(os.path.join(work, "relief_objects.json"), "w"), indent=0)
subprocess.run([sys.executable, os.path.join(HERE, "relief.py"), plan_path, os.path.join(stage_a, "heights_world.npy"),
                os.path.join(work, "relief_objects.json"), os.path.join(work, "relief"), "--seed", str(plan.get("seed", 1))], check=True)
zr = np.load(os.path.join(work, "relief", "heights_world.npy")).astype(np.float64)
step = RAW * plan["height_scale"]
zr = np.round(zr / step) * step
gy, gx = np.gradient(zr, CELL)
sl = np.hypot(gx, gy)
cs = np.maximum.reduce([sl[:-1, :-1], sl[1:, :-1], sl[:-1, 1:], sl[1:, 1:]])
# Mountains stay impassable where their tops are flat enough to drive on.
imp_cells = (cs >= 0.4) | cells
top = 255 * RAW * plan["height_scale"]
Image.fromarray(np.round(zr / top * 65535).astype(np.uint16)[::-1]).save(os.path.join(work, "heights.png"))
Image.fromarray((imp_cells.astype(np.uint8) * 255)[::-1]).save(os.path.join(work, "impassable.png"))
np.save(os.path.join(work, "heights_world.npy"), zr.astype(np.float32))

# Everything that matters must stay reachable: starts and all objects in one passable piece.
lab, n = ndimage.label(~imp_cells)
def piece(x, y):
    return lab[int(y / CELL + B), int(x / CELL + B)]
pieces = {piece(*p) for p in starts.values()} | {piece(o["x"], o["y"] + 60) for o in objs}
print("reachable pieces", pieces, "spread around starts", [round(float(np.ptp(zr[int(y / CELL + B) - 20:int(y / CELL + B) + 21, int(x / CELL + B) - 20:int(x / CELL + B) + 21])), 1) for x, y in starts.values()])
if "--save" not in sys.argv:
    sys.exit(0)

sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge
path = plan["map"]
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
info = wb.call("map.new", width=W + 1, height=H + 1, border=B, initial_height=60, discard_changes=True)
assert info["heightmap"]["x_extent"] == nx and info["heightmap"]["y_extent"] == ny, info["heightmap"]
wb.call("map.save", path=path, overwrite=True)
with open(os.path.join(os.path.dirname(path), "map.ini"), "w") as f:
    f.write(f"MapData\n  HeightMapScale = {plan['height_scale']}\nEnd\n")
wb.call("map.new", width=60, height=60, border=10, initial_height=10, discard_changes=True)
info = wb.call("map.open", path=path, discard_changes=True)
assert abs(info["heightmap"]["height_map_scale"] - plan["height_scale"]) < 1e-3, info["heightmap"]
wb.call("terrain.import_heightmap", path=os.path.join(work, "heights.png"), fit="exact", range=[0, 255 * plan["height_scale"]])
wb.call("terrain.import_mask", path=os.path.join(work, "impassable.png"), target="passability", fit="exact")
for p in wb.call("polygons.list").get("polygons", []):
    if p.get("kind") == "water":
        wb.call("polygons.delete", id=p["id"])
for name, (x, y) in sorted(starts.items()):
    wb.call("waypoints.add", name=name, x=x, y=y)
wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
for o in objs:
    wb.call("objects.place", template=o["template"], x=o["x"], y=o["y"], angle_deg=o["angle_deg"], owner=OWNER)
wb.call("map.set_info", time_of_day="AFTERNOON", weather="SNOWY")
wb.call("map.save", path=path, overwrite=True)
print("saved", path, wb.call("map.info")["counts"])
wb.close()
