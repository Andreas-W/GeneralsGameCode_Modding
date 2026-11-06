# GoldCoastGen after stage A (gold_coast_stage_a.py): supply docks, relief (relief.py), then the map in the second WorldBuilder as
# northern_island_build.py does, with two water areas from the template (the sea at 60, the river at 105). The template's docks (one on
# the beach and one by the river per player) are kept for player 1; player 2's are player 1's offsets from its start, mirrored north-south
# (the template's were 152 and 159 farther). Each moves to the nearest flat open spot. The farms and the town come later.
# Usage: northern_island_build.py <plan.json> <work dir (mapgen_work)> [--save]
import json, math, os, subprocess, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
CELL, B, RAW = 10.0, plan["border"], 0.625
W, H = plan["playable_cells"]
WATER = plan["water"]["height"]
OWNER = "teamPlyrCivilian"
stage_a = os.path.join(work, "stageA")
z = np.load(os.path.join(stage_a, "heights_world.npy")).astype(np.float64)
ny, nx = z.shape
imp = np.zeros(z.shape, bool)
cells = np.array(Image.open(os.path.join(stage_a, "impassable.png")).convert("L"))[::-1] > 127
imp[:cells.shape[0], :cells.shape[1]] = cells
tpl = json.load(open(os.path.join(work, "template", "mock.json")))["objects"]["objects"]
starts = {o["waypoint_name"]: (o["x"], o["y"]) for o in tpl if o["kind"] == "waypoint"}

# (template, x, y, angle, footprint radius in cells, group offsets)
s1, s2 = starts["Player_1_Start"], starts["Player_2_Start"]
OBJECTS = []
for o in tpl:
    if o["kind"] == "object" and o["template"] == "SupplyDock" and math.dist((o["x"], o["y"]), s1) < math.dist((o["x"], o["y"]), s2):
        dx, dy = o["x"] - s1[0], o["y"] - s1[1]
        OBJECTS.append(("SupplyDock", o["x"], o["y"], o["angle_deg"], 7, None))
        OBJECTS.append(("SupplyDock", s2[0] + dx, s2[1] - dy, -o["angle_deg"], 7, None))

gy, gx = np.gradient(z, CELL)
slope = np.hypot(gx, gy)
river = np.load(os.path.join(stage_a, "river_band.npy"))
# The beach (64) counts as ground: the template's beach docks stand on it (below WATER + 6 they slid 340 to 440 up onto the plateau).
bad = ndimage.binary_dilation(imp | river, iterations=5) | (ndimage.maximum_filter(slope, 9) > 0.12) | (z < WATER + 2.5)


def ok(x, y, r):
    i, j = int(round(y / CELL + B)), int(round(x / CELL + B))
    return r <= j < nx - r and r <= i < ny - r and not bad[i - r:i + r + 1, j - r:j + r + 1].any()


def place(x, y, r):
    for d in range(0, 500, 10):
        for a in range(0, 360, 15 if d else 360):
            px, py = x + d * math.cos(math.radians(a)), y + d * math.sin(math.radians(a))
            if ok(px, py, r):
                return px, py, d
    raise SystemExit(f"no flat spot near {x}, {y}")


objs, report = [], []
for t, x, y, a, r, group in OBJECTS:
    px, py, d = place(x, y, r)
    report.append((t, round(px), round(py), d))
    for ox, oy in group or [(0, 0)]:
        objs.append({"kind": "object", "template": t, "x": round(px + ox, 1), "y": round(py + oy, 1), "angle_deg": a, "owner": OWNER})
print("moved", report)
for name, s in sorted(starts.items()):
    print(name, "docks", sorted(round(math.dist(s, (o["x"], o["y"]))) for o in objs if o["template"] == "SupplyDock")[:2])
json.dump(objs, open(os.path.join(work, "objects.json"), "w"), indent=0)

keep = [{"kind": "waypoint", "x": s[0], "y": s[1]} for s in starts.values()] + objs
json.dump(keep, open(os.path.join(work, "relief_objects.json"), "w"), indent=0)
subprocess.run([sys.executable, os.path.join(HERE, "relief.py"), plan_path, os.path.join(stage_a, "heights_world.npy"),
                os.path.join(work, "relief_objects.json"), os.path.join(work, "relief"), "--seed", str(plan.get("seed", 1))], check=True)
zr = np.load(os.path.join(work, "relief", "heights_world.npy")).astype(np.float64)
# Land in reach of the river's water area stays above its surface (the water area is a quadrilateral over the river's whole valley).
near_river = ndimage.binary_dilation(river, iterations=25) & ~river
zr = np.where(near_river & (zr > WATER + 10), np.maximum(zr, plan["river"]["height"] + 1.5), zr)
step = RAW * plan["height_scale"]
zr = np.round(zr / step) * step
gy, gx = np.gradient(zr, CELL)
sl = np.hypot(gx, gy)
cs = np.maximum.reduce([sl[:-1, :-1], sl[1:, :-1], sl[:-1, 1:], sl[1:, 1:]])
imp_cells = (cs >= 0.4) | cells
top = 255 * RAW * plan["height_scale"]
Image.fromarray(np.round(np.clip(zr, 0, top) / top * 65535).astype(np.uint16)[::-1]).save(os.path.join(work, "heights.png"))
Image.fromarray((imp_cells.astype(np.uint8) * 255)[::-1]).save(os.path.join(work, "impassable.png"))
np.save(os.path.join(work, "heights_world.npy"), zr.astype(np.float32))
land = np.zeros(z.shape, bool)
land[:-1, :-1] = ~imp_cells
land &= zr >= WATER
lab, n = ndimage.label(land)
piece = lambda x, y: lab[int(y / CELL + B), int(x / CELL + B)]
print("pieces of starts and objects", {piece(*s) for s in starts.values()} | {piece(o["x"], o["y"] + 60) for o in objs})
if "--save" not in sys.argv:
    sys.exit(0)

sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge
path = plan["map"]
os.makedirs(os.path.dirname(path), exist_ok=True)
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
info = wb.call("map.new", width=W + 1, height=H + 1, border=B, initial_height=40, discard_changes=True)
assert info["heightmap"]["x_extent"] == nx and info["heightmap"]["y_extent"] == ny, info["heightmap"]
wb.call("map.save", path=path, overwrite=True)
with open(os.path.join(os.path.dirname(path), "map.ini"), "w") as f:
    f.write(f"MapData\n  HeightMapScale = {plan['height_scale']}\n  EnableShips = Yes\n"
            "  TerrainHeightAmbientLightColor1 = R:196 G:225 B:255\n  TerrainHeightAmbientLightColor2 = R:128 G:128 B:196\n"
            f"  TerrainHeightAmbientLightStart = {WATER - 2:g}\n  TerrainHeightAmbientLightHeight1 = {WATER - 17:g}\n"
            f"  TerrainHeightAmbientLightHeight2 = {WATER - 37:g}\nEnd\n")
wb.call("map.new", width=60, height=60, border=10, initial_height=10, discard_changes=True)
info = wb.call("map.open", path=path, discard_changes=True)
assert abs(info["heightmap"]["height_map_scale"] - plan["height_scale"]) < 1e-3, info["heightmap"]
wb.call("terrain.import_heightmap", path=os.path.join(work, "heights.png"), fit="exact", range=[0, 255 * plan["height_scale"]])
wb.call("terrain.import_mask", path=os.path.join(work, "impassable.png"), target="passability", fit="exact")
for p in wb.call("polygons.list").get("polygons", []):
    if p.get("kind") == "water":
        wb.call("polygons.delete", id=p["id"])
wmin, wmax, hmin, hmax = info["world"]["min_x"], info["world"]["max_x"], info["world"]["min_y"], info["world"]["max_y"]
for a in plan["water_areas"]:
    wb.call("polygons.add", kind="water", water_height=a["height"], points=a["points"], name=a["name"])
for name, (x, y) in sorted(starts.items()):
    wb.call("waypoints.add", name=name, x=x, y=y)
wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
for o in objs:
    wb.call("objects.place", template=o["template"], x=o["x"], y=o["y"], angle_deg=o["angle_deg"], owner=OWNER)
wb.call("map.set_info", time_of_day="AFTERNOON", weather="NORMAL")
wb.call("map.save", path=path, overwrite=True)
print("saved", path, wb.call("map.info")["counts"])
wb.close()
