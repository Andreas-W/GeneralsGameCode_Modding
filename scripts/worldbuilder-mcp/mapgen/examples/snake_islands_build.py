# SnakeIslandsGen after stage A (snake_islands_stage_a.py): relief (relief.py, gentle: the lowlands are 8 above the water), then the
# map in the second WorldBuilder: heights, passability, one water area over the whole map, starts, map.ini (ships, underwater light),
# the bridges. Drawbridges stay where the user put them, their height set so the deck meets the quays (object z is above the ground,
# here the seabed); wooden bridges go from cliff edge to cliff edge along the user's line (the first point where the plateau reaches its
# top, 20 in).
# Usage: snake_islands_build.py <plan.json> <work dir (mapgen_work)> [--save]
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
LOW, HIGH = plan["levels"][0]["z"], plan["levels"][1]["z"]
stage_a = os.path.join(work, "stageA")
z = np.load(os.path.join(stage_a, "heights_world.npy")).astype(np.float64)
ny, nx = z.shape
cells = np.array(Image.open(os.path.join(stage_a, "impassable.png")).convert("L"))[::-1] > 127
layout = json.load(open(os.path.join(work, "mock", "layout.json")))
starts = layout["starts"]

# Kept flat by the relief: starts and the drawbridge quays.
keep = [{"kind": "waypoint", "x": s[0], "y": s[1]} for s in starts.values()]
quays = []
for d in layout["drawbridges"]:
    a = math.radians(d["angle_deg"])
    for s in (-1, 1):
        quays.append((d["x"] + s * 200 * math.cos(a), d["y"] + s * 200 * math.sin(a)))
plan.setdefault("relief", {})["keep"] = [{"at": [round(x), round(y)], "flat": 110, "fade": 220} for x, y in quays]
json.dump(plan, open(os.path.join(work, "plan_relief.json"), "w"), indent=1)
json.dump(keep, open(os.path.join(work, "relief_objects.json"), "w"), indent=0)
subprocess.run([sys.executable, os.path.join(HERE, "relief.py"), os.path.join(work, "plan_relief.json"), os.path.join(stage_a, "heights_world.npy"),
                os.path.join(work, "relief_objects.json"), os.path.join(work, "relief"), "--seed", str(plan.get("seed", 1))], check=True)
zr = np.load(os.path.join(work, "relief", "heights_world.npy")).astype(np.float64)
# Lowland that stage A put 3 or more above the water stays there (beaches below that keep their profile).
zr = np.where((z >= WATER + 3) & (zr < WATER + 3), WATER + 3, zr)
step = RAW * plan["height_scale"]
zr = np.round(zr / step) * step
# No ground exactly at the water surface: on the gentle beaches whole rows of vertices sat at 60.0 and flickered against the water as
# dashed lines. One height step up on land, down in the sea.
zr = np.where(np.abs(zr - WATER) < step / 2, np.where(z >= WATER, WATER + step, WATER - step), zr)
gy, gx = np.gradient(zr, CELL)
sl = np.hypot(gx, gy)
cs = np.maximum.reduce([sl[:-1, :-1], sl[1:, :-1], sl[:-1, 1:], sl[1:, 1:]])
imp_cells = (cs >= 0.4) | cells
top = 255 * RAW * plan["height_scale"]
Image.fromarray(np.round(np.clip(zr, 0, top) / top * 65535).astype(np.uint16)[::-1]).save(os.path.join(work, "heights.png"))
Image.fromarray((imp_cells.astype(np.uint8) * 255)[::-1]).save(os.path.join(work, "impassable.png"))
np.save(os.path.join(work, "heights_world.npy"), zr.astype(np.float32))


def at(x, y):
    # Bilinear height at a world point.
    fx, fy = x / CELL + B, y / CELL + B
    j, i = int(fx), int(fy)
    tx, ty = fx - j, fy - i
    return float(zr[i, j] * (1 - tx) * (1 - ty) + zr[i, j + 1] * tx * (1 - ty) + zr[i + 1, j] * (1 - tx) * ty + zr[i + 1, j + 1] * tx * ty)


bridges = []
for d in layout["drawbridges"]:
    a = math.radians(d["angle_deg"])
    ground = at(d["x"], d["y"])
    ends = [round(at(d["x"] + s * 150 * math.cos(a), d["y"] + s * 150 * math.sin(a)), 1) for s in (-1, 1)]
    bridges.append(dict(d, z=round(d["deck"] - ground, 2), ground=round(ground, 1), end_heights=ends))
wooden = []
for wbr in layout["wooden"]:
    (ax, ay), (bx, by) = wbr["points"]
    L = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / L, (by - ay) / L
    mx, my = (ax + bx) / 2, (ay + by) / 2
    pts = []
    for s in (-1, 1):
        # From the middle outward: the first point where the ground reaches the plateau top, then 20 further in.
        t = 0.0
        while t < L and at(mx + s * t * ux, my + s * t * uy) < HIGH - 3:
            t += 5
        t += 20
        pts.append([round(mx + s * t * ux, 1), round(my + s * t * uy, 1)])
    wooden.append({"template": wbr["template"], "points": [pts[0], pts[1]], "length": round(math.dist(*pts)),
                   "end_heights": [round(at(*p), 1) for p in pts]})
print(json.dumps({"drawbridges": [{k: b[k] for k in ("x", "y", "z", "ground", "end_heights")} for b in bridges], "wooden": wooden}))
json.dump({"drawbridges": bridges, "wooden": wooden}, open(os.path.join(work, "bridges.json"), "w"), indent=1)

land = np.zeros(z.shape, bool)
land[:-1, :-1] = ~imp_cells
land &= zr >= WATER
for (wa, wb_) in [w["points"] for w in wooden]:
    for k in range(51):
        x, y = wa[0] + (wb_[0] - wa[0]) * k / 50, wa[1] + (wb_[1] - wa[1]) * k / 50
        land[int(round(y / CELL + B)) - 1:int(round(y / CELL + B)) + 2, int(round(x / CELL + B)) - 1:int(round(x / CELL + B)) + 2] = True
for b in bridges:
    a = math.radians(b["angle_deg"])
    for t in range(-160, 161, 5):
        x, y = b["x"] + t * math.cos(a), b["y"] + t * math.sin(a)
        land[int(round(y / CELL + B)) - 1:int(round(y / CELL + B)) + 2, int(round(x / CELL + B)) - 1:int(round(x / CELL + B)) + 2] = True
lab, n = ndimage.label(land)
piece = lambda x, y: int(lab[int(round(y / CELL + B)), int(round(x / CELL + B))])
print("pieces of the starts (bridges joined)", {k: piece(*s) for k, s in starts.items()})
if "--save" not in sys.argv:
    sys.exit(0)

sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge
path = plan["map"]
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
w = info["world"]
wb.call("polygons.add", kind="water", water_height=WATER, name="Sea",
        points=[[w["min_x"], w["min_y"]], [w["max_x"], w["min_y"]], [w["max_x"], w["max_y"]], [w["min_x"], w["max_y"]]])
for name, (x, y) in sorted(starts.items()):
    wb.call("waypoints.add", name=name, x=x, y=y)
for b in bridges:
    wb.call("objects.place", template=b["template"], x=b["x"], y=b["y"], z=b["z"], angle_deg=b["angle_deg"], owner="team")
for wbr in wooden:
    wb.call("roads.add", type=wbr["template"], points=wbr["points"])
wb.call("map.set_info", time_of_day="AFTERNOON", weather="NORMAL")
wb.call("map.save", path=path, overwrite=True)
print("saved", path, wb.call("map.info")["counts"])
wb.close()
