# TropicTrefoilGen after stage A (tropic_trefoil_stage_a.py): the objects of player 1's wedge turned onto the other two, relief (relief.py),
# then the map itself in the second WorldBuilder, as northern_island_build.py does. Structures (Tech, supplies, docks, towers) move to the
# nearest flat open spot within MOVE (a turned copy may land on a steeper patch than the original); decoration (palms, huts, ruins) stays
# where it was turned to (ruins on the mountain rims too, as the user placed them), unless it lands in the water.
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
turned = json.load(open(os.path.join(work, "mock", "objects_turned.json")))
starts = {o["waypoint_name"]: (o["x"], o["y"]) for o in turned if o["kind"] == "waypoint"}
NEAR, NEAR_COPY = 160, 80
MOVE = 400  # the refineries stood at the foot of the neck peaks, which stage A widened

# Footprint radius in cells of the structures that must stand on flat ground.
FOOT = {"SupplyDock": 7, "SupplyPileSmall": 3, "TechOilDerrick": 5, "TechOilRefinery": 6, "TechConstructionYard": 7, "GuardTower": 3}

gy, gx = np.gradient(z, CELL)
slope = np.hypot(gx, gy)
bad = ndimage.binary_dilation(imp, iterations=5) | (ndimage.maximum_filter(slope, 9) > 0.12) | (z < WATER + 6)


def ok(x, y, r):
    i, j = int(round(y / CELL + B)), int(round(x / CELL + B))
    return r <= j < nx - r and r <= i < ny - r and not bad[i - r:i + r + 1, j - r:j + r + 1].any()


PLACED = []  # (x, y, footprint radius in cells) of the structures placed so far


def free(px, py, r):
    return all(math.hypot(px - qx, py - qy) >= (r + qr) * CELL + 30 for qx, qy, qr in PLACED)


def place(x, y, r, reach=500, spaced=True):
    for d in range(0, reach, 10):
        for a in range(0, 360, 15 if d else 360):
            px, py = x + d * math.cos(math.radians(a)), y + d * math.sin(math.radians(a))
            if ok(px, py, r) and (not spaced or free(px, py, r)):
                return px, py, d
    raise SystemExit(f"no flat spot near {x}, {y}")


def on_slope_only(x, y, r):
    # Dry ground with nothing impassable under the footprint (plus 2 cells): a slope a pad can level, not a cliff or mountain.
    i, j = int(round(y / CELL + B)), int(round(x / CELL + B))
    k = r + 2
    return not imp[i - k:i + k + 1, j - k:j + k + 1].any() and z[i - k:i + k + 1, j - k:j + k + 1].min() > WATER + 4


PADS = []  # (x, y, radius) levelled after the relief: structures the user placed on the hub-to-lobe slopes (the neck refineries)


# The three copies of a structure move alike: copy 0 (player 1's wedge) takes the nearest flat spot, the others its offset turned by
# 120 and 240 degrees, then the nearest flat spot to that (copies moved one by one slid in different directions, 451 against 815).
CX, CY = (plan["playable_max"] / 2,) * 2
offset = {}


def turn(dx, dy, k):
    a = math.radians(120 * k)
    return dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a)


objs, report, dropped = [], [], []
ORDER = ["TechConstructionYard", "TechOilRefinery", "TechOilDerrick", "SupplyDock", "GuardTower", "SupplyPileSmall"]
for o in sorted(turned, key=lambda q: (q.get("copy", 0), ORDER.index(q["template"]) if q["template"] in ORDER else 99)):
    if o["kind"] == "waypoint":
        continue
    x, y, t = o["x"], o["y"], o["template"]
    if t in FOOT:
        r = FOOT[t]
        dx, dy = turn(*offset.get(o["unique_id"], (0.0, 0.0)), o["copy"])
        pile = t == "SupplyPileSmall"  # piles stand in a cluster: no spacing among them
        # The user's spot (turned for the other copies, with copy 0's move turned too): flat ground as it is; else flat ground nearby
        # (the user allows small moves: up to NEAR for copy 0, NEAR_COPY around the turned move for the others); else the nearest spot a
        # pad can level. Copies follow copy 0, so they stay alike (searching each copy freely moved them differently: derricks 562
        # against 468).
        px, py = x + dx, y + dy
        if not ok(px, py, r) or not (pile or free(px, py, r)):
            try:
                px, py, _ = place(px, py, r, reach=NEAR if o["copy"] == 0 else NEAR_COPY, spaced=not pile)
            except SystemExit:
                pass
        if not ok(px, py, r) or not (pile or free(px, py, r)):
            px, py = x + dx, y + dy
            for dd in range(0, MOVE, 10):
                hit = [(x + dx + dd * math.cos(math.radians(a)), y + dy + dd * math.sin(math.radians(a))) for a in range(0, 360, 15 if dd else 360)]
                hit = [(qx, qy) for qx, qy in hit if on_slope_only(qx, qy, r) and (pile or free(qx, qy, r))]
                if hit:
                    px, py = hit[0]
                    break
            if not ok(px, py, r):
                PADS.append((px, py, (r + 2) * CELL))
        d = round(math.hypot(px - x, py - y))
        if o["copy"] == 0:
            offset[o["unique_id"]] = (px - x, py - y)
        if not pile:
            PLACED.append((px, py, r))
        report.append((t, o["copy"], round(px), round(py), d))
    else:
        i_, j_ = int(round(y / CELL + B)), int(round(x / CELL + B))
        if z[i_, j_] < WATER + 1:
            dropped.append(t)
            continue
        px, py = x, y
    objs.append({"kind": "object", "template": t, "x": round(px, 1), "y": round(py, 1), "angle_deg": o["angle_deg"], "owner": o["owner"]})
# Decoration off the structures' footprints (they moved, and pads level the ground under them).
structs = [(o["x"], o["y"], FOOT[o["template"]]) for o in objs if o["template"] in FOOT]
before = len(objs)
objs = [o for o in objs if o["template"] in FOOT or all(math.hypot(o["x"] - sx, o["y"] - sy) > (sr + 2) * CELL for sx, sy, sr in structs)]
print("dropped decoration", len(dropped), "in the water,", before - len(objs), "on structures")
print("moved", [r for r in report if r[4] != 0], "pads", [(round(a), round(b)) for a, b, _ in PADS])
for name, s in sorted(starts.items()):
    print(name, "dock", min(round(math.dist(s, (o["x"], o["y"]))) for o in objs if o["template"] == "SupplyDock"),
          "derricks", sorted(round(math.dist(s, (o["x"], o["y"]))) for o in objs if o["template"] == "TechOilDerrick")[:3],
          "refineries", sorted(round(math.dist(s, (o["x"], o["y"]))) for o in objs if o["template"] == "TechOilRefinery")[:2])
json.dump(objs, open(os.path.join(work, "objects.json"), "w"), indent=0)

keep = [{"kind": "waypoint", "x": s[0], "y": s[1]} for s in starts.values()] + objs
json.dump(keep, open(os.path.join(work, "relief_objects.json"), "w"), indent=0)
subprocess.run([sys.executable, os.path.join(HERE, "relief.py"), plan_path, os.path.join(stage_a, "heights_world.npy"),
                os.path.join(work, "relief_objects.json"), os.path.join(work, "relief"), "--seed", str(plan.get("seed", 1))], check=True)
zr = np.load(os.path.join(work, "relief", "heights_world.npy")).astype(np.float64)
yy, xx = np.indices(zr.shape)
for px, py, rad in PADS:
    d = np.hypot((xx - B) * CELL - px, (yy - B) * CELL - py)
    level = float(np.median(zr[d <= rad]))
    w = np.clip((rad + 60 - d) / 60, 0, 1)
    w = w * w * (3 - 2 * w)
    zr = zr * (1 - w) + level * w
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
wb.call("polygons.add", kind="water", water_height=WATER, points=[[wmin, hmin], [wmax, hmin], [wmax, hmax], [wmin, hmax]])
for name, (x, y) in sorted(starts.items()):
    wb.call("waypoints.add", name=name, x=x, y=y)
wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
for o in objs:
    wb.call("objects.place", template=o["template"], x=o["x"], y=o["y"], angle_deg=o["angle_deg"], owner=o["owner"])
wb.call("map.set_info", time_of_day="AFTERNOON", weather="NORMAL")
wb.call("map.save", path=path, overwrite=True)
print("saved", path, wb.call("map.info")["counts"])
wb.close()
