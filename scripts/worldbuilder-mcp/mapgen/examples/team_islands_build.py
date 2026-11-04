# TeamIslandsGen after stage A: relief (relief.py, folded so that the heights stay symmetric across both axes), then the map itself in
# the second WorldBuilder: a new 500 x 600 map, heights, passability, one water area at 60, a civilian player, Tech buildings and
# supplies (symmetric, so top against bottom is even: N, NE, NW against S, SE, SW), map.ini for ships. No waypoints or AI markers yet.
# Per player, as on Tournament Island: a supply dock near the start, an expansion of two oil derricks and three supply piles in the far
# corner of its land. The centre, larger than Tournament Island's, holds a reinforcement pad, an oil derrick toward each team, a
# hospital and an oil refinery between the bays.
# Usage: team_islands_build.py <plan.json> <stage A dir> <work dir> [--save]   (the map path is in the plan)
import json, math, os, subprocess, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, stage_a, work = sys.argv[1:4]
plan = json.load(open(plan_path))
os.makedirs(work, exist_ok=True)
CELL, B, WATER = 10.0, plan["border"], plan["water"]["height"]
W, H = plan["playable_cells"]
M0 = np.array([W * CELL / 2, H * CELL / 2])
OWNER = "teamPlyrCivilian"

# Objects, as offsets from the centre in the north-east quadrant (x >= 0, y >= 0) with their angle there; mirrored into the others.
# On an axis they stand once. Angles mirror with the position.
QUAD = [
    ("TechReinforcementPad", 0, 0, 0),
    ("TechOilDerrick", 0, 430, 90),
    ("TechHospital", 380, 0, 180),
    ("SupplyDock", 0, 1110, 270),                    # north player
    ("TechOilDerrick", 280, 1900, 270), ("SupplyPileSmall", 0, 1930, 0), ("SupplyPileSmall", 75, 1880, 0), ("SupplyPileSmall", -75, 1880, 0),
    ("SupplyDock", 770, 640, 225),                   # north-east player
    ("TechOilDerrick", 1390, 1210, 225), ("TechOilDerrick", 1450, 1140, 225),
    ("SupplyPileSmall", 1320, 1130, 0), ("SupplyPileSmall", 1380, 1070, 0), ("SupplyPileSmall", 1290, 1060, 0),
]
# The west half of the centre holds an oil refinery where the east half holds the hospital: same distance from both teams.
SWAP_WEST = {"TechHospital": "TechOilRefinery"}


def objects():
    out, seen = [], set()
    for t, u, v, a in QUAD:
        for sx in (1, -1):
            for sy in (1, -1):
                x, y = M0[0] + sx * u, M0[1] + sy * v
                if (round(x), round(y)) in seen:
                    continue
                seen.add((round(x), round(y)))
                ang = math.degrees(math.atan2(sy * math.sin(math.radians(a)), sx * math.cos(math.radians(a))))
                out.append({"template": SWAP_WEST.get(t, t) if sx < 0 and u > 0 else t, "x": round(x, 1), "y": round(y, 1), "angle_deg": round(ang % 360, 1)})
    return out


objs = objects()
mock = json.load(open(os.path.join(stage_a, "mock.json")))
keep = mock["objects"] + [{"kind": "object", **o} for o in objs]
json.dump(keep, open(os.path.join(work, "relief_objects.json"), "w"), indent=0)
subprocess.run([sys.executable, os.path.join(HERE, "relief.py"), plan_path, os.path.join(stage_a, "heights_world.npy"),
                os.path.join(work, "relief_objects.json"), os.path.join(work, "relief"), "--seed", str(plan.get("seed", 1))], check=True)

# Fold the relief: the north-east quadrant mirrored onto the others, as stage A did.
z = np.load(os.path.join(work, "relief", "heights_world.npy")).astype(np.float64)
ny, nx = z.shape
c = (int(M0[0] / CELL) + B, int(M0[1] / CELL) + B)
j = np.minimum(np.abs(np.arange(ny) - c[1]) + c[1], ny - 1)
i = np.minimum(np.abs(np.arange(nx) - c[0]) + c[0], nx - 1)
z = z[np.ix_(j, i)]
z = np.round(z / 0.625) * 0.625
gy, gx = np.gradient(z, CELL)
slope = np.hypot(gx, gy)
cs = np.maximum.reduce([slope[:-1, :-1], slope[1:, :-1], slope[:-1, 1:], slope[1:, 1:]])
imp = cs >= 0.4
Image.fromarray((np.clip(z / 0.625 / 255, 0, 1) * 65535).astype(np.uint16)[::-1]).save(os.path.join(work, "heights.png"))
Image.fromarray((imp.astype(np.uint8) * 255)[::-1]).save(os.path.join(work, "impassable.png"))
np.save(os.path.join(work, "heights_world.npy"), z.astype(np.float32))
low = [o for o in objs if z[int(round(o["y"] / CELL + B)), int(round(o["x"] / CELL + B))] < WATER + 3]
print("objects", len(objs), "on low ground:", [(o["template"], o["x"], o["y"]) for o in low], "impassable cells", int(imp.sum()), flush=True)
json.dump(objs, open(os.path.join(work, "tech.json"), "w"), indent=0)
if "--save" not in sys.argv:
    sys.exit(0)

sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge
path = plan["map"]
os.makedirs(os.path.dirname(path), exist_ok=True)
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
info = wb.call("map.new", width=W, height=H, border=B, initial_height=0, discard_changes=True)
assert info["heightmap"]["x_extent"] == nx and info["heightmap"]["y_extent"] == ny, info["heightmap"]
wb.call("terrain.import_heightmap", path=os.path.join(work, "heights.png"), fit="exact", range=[0, 255])
wb.call("terrain.import_mask", path=os.path.join(work, "impassable.png"), target="passability", fit="exact")
for p in wb.call("polygons.list").get("polygons", []):
    if p.get("kind") == "water":
        wb.call("polygons.delete", id=p["id"])
wmin, wmax = info["world"]["min_x"], info["world"]["max_x"]
hmin, hmax = info["world"]["min_y"], info["world"]["max_y"]
wb.call("polygons.add", kind="water", water_height=WATER, points=[[wmin, hmin], [wmax, hmin], [wmax, hmax], [wmin, hmax]])
wb.call("sides.add_player", faction="FactionCivilian", name="PlyrCivilian", display_name="Civilian", is_human=False)
for o in objs:
    wb.call("objects.place", template=o["template"], x=o["x"], y=o["y"], angle_deg=o["angle_deg"], owner=OWNER)
wb.call("map.set_info", time_of_day=plan.get("time_of_day", "AFTERNOON"))
wb.call("map.save", path=path)
# Ships: EnableShips and the underwater light of the adapt-map-for-navy skill, relative to the water.
with open(os.path.join(os.path.dirname(path), "map.ini"), "w") as f:
    f.write("MapData\n  HeightMapScale = 1.0\n  EnableShips = Yes\n"
            "  TerrainHeightAmbientLightColor1 = R:196 G:225 B:255\n  TerrainHeightAmbientLightColor2 = R:128 G:128 B:196\n"
            f"  TerrainHeightAmbientLightStart = {WATER - 2:g}\n  TerrainHeightAmbientLightHeight1 = {WATER - 17:g}\n"
            f"  TerrainHeightAmbientLightHeight2 = {WATER - 37:g}\nEnd\n")
print("saved", path, wb.call("map.info")["counts"])
wb.close()
