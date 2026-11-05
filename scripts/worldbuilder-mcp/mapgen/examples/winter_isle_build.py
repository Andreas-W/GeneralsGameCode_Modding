# WinterIsleGen after stage A (the plan from winter_isle_plan.py): Tech buildings and supplies, relief (relief.py), then the map itself in the
# second WorldBuilder, as northern_island_build.py does. Five players without symmetry, so the objects follow one rule per player, from
# its start: a supply dock 420 toward the coast on one side, an expansion (oil derrick, three supply piles) 560 toward the coast on the
# other side; one oil derrick shared by each pair of neighbours (plan "shared_derricks"); a hospital and a repair bay in the centre.
# Each object moves to the nearest flat open spot.
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
starts = {k["name"]: tuple(k["at"]) for k in plan["keep"] if k["kind"] == "waypoint"}

# (template, x, y, angle, footprint radius in cells, group offsets)
PILES = [(0, 0), (70, 45), (-60, 55)]
CX, CY = 2500.0, 2500.0


def toward(s, angle, d):
    return s[0] + d * math.cos(math.radians(angle)), s[1] + d * math.sin(math.radians(angle))


# Per player: (template, side, distance from the start, footprint radius, group). The side is the angle range from the player's own
# angle (outward) in which the spot is searched: the dock on one side, the expansion on the other. The spot is the flat one in that
# range whose distance is nearest the wanted one (the lobes' coasts are mostly cliff, so a fixed direction often ends on rock).
PER_PLAYER = [("SupplyDock", (-110, -15), 400, 7, None), ("TechOilDerrick", (20, 115), 560, 5, None)]
OBJECTS = []
for q in plan["shared_derricks"]:
    OBJECTS.append(("TechOilDerrick", *q["at"], 0, 5, None))
OBJECTS += [("TechHospital", CX - 60, CY + 40, 0, 8, None), ("TechRepairbay", CX + 220, CY - 120, 0, 8, None)]

gy, gx = np.gradient(z, CELL)
slope = np.hypot(gx, gy)
bad = ndimage.binary_dilation(imp, iterations=5) | (ndimage.maximum_filter(slope, 9) > 0.12) | (z < WATER + 6)


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


def ring(s, base, side, want, r):
    best = None
    for da in range(side[0], side[1] + 1, 5):
        for d in range(want - 150, want + 151, 10):
            px, py = toward(s, base + da, d)
            if ok(px, py, r):
                cost = abs(d - want) + 2 * abs(da - (side[0] + side[1]) / 2)
                if best is None or cost < best[0]:
                    best = (cost, px, py, d, da)
    if best is None:
        raise SystemExit(f"nothing flat at {want} from {s}")
    return best[1:]


objs, report = [], []
for p in plan["players"]:
    s = starts[f"Player_{p['n']}_Start"]
    for t, side, want, r, group in PER_PLAYER:
        px, py, d, da = ring(s, p["angle"], side, want, r)
        report.append((p["n"], t, round(px), round(py), d, da))
        objs.append({"kind": "object", "template": t, "x": round(px, 1), "y": round(py, 1), "angle_deg": p["angle"] + da, "owner": OWNER})
        if t == "TechOilDerrick":
            # The expansion's supply piles: between derrick and start, 110 short of the derrick, on flat ground nearby.
            qx, qy, _ = place(*toward((px, py), math.degrees(math.atan2(s[1] - py, s[0] - px)), 110), 9)
            for ox, oy in PILES:
                objs.append({"kind": "object", "template": "SupplyPileSmall", "x": round(qx + ox, 1), "y": round(qy + oy, 1), "angle_deg": 0,
                             "owner": OWNER})
for t, x, y, a, r, group in OBJECTS:
    px, py, d = place(x, y, r)
    report.append((t, round(px), round(py), d))
    for ox, oy in group or [(0, 0)]:
        objs.append({"kind": "object", "template": t, "x": round(px + ox, 1), "y": round(py + oy, 1), "angle_deg": a, "owner": OWNER})
print("moved", report)
for name, s in starts.items():
    print(name, "dock", min(round(math.dist(s, (o["x"], o["y"]))) for o in objs if o["template"] == "SupplyDock"),
          "derricks", sorted(round(math.dist(s, (o["x"], o["y"]))) for o in objs if o["template"] == "TechOilDerrick")[:4],
          "centre", round(math.dist(s, (CX, CY))))
json.dump(objs, open(os.path.join(work, "objects.json"), "w"), indent=0)

keep = [{"kind": "waypoint", "x": s[0], "y": s[1]} for s in starts.values()] + objs
json.dump(keep, open(os.path.join(work, "relief_objects.json"), "w"), indent=0)
subprocess.run([sys.executable, os.path.join(HERE, "relief.py"), plan_path, os.path.join(stage_a, "heights_world.npy"),
                os.path.join(work, "relief_objects.json"), os.path.join(work, "relief"), "--seed", str(plan.get("seed", 1))], check=True)
zr = np.load(os.path.join(work, "relief", "heights_world.npy")).astype(np.float64)
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
    wb.call("objects.place", template=o["template"], x=o["x"], y=o["y"], angle_deg=o["angle_deg"], owner=OWNER)
wb.call("map.set_info", time_of_day="AFTERNOON", weather="SNOWY")
wb.call("map.save", path=path, overwrite=True)
print("saved", path, wb.call("map.info")["counts"])
wb.close()
