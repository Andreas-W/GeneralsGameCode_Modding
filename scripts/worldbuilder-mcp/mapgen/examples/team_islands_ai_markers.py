# Stage E of TeamIslandsGen: what the skirmish AI looks up by name, through the second WorldBuilder.
# - The user's trigger areas around the lagoons become no-shipyard zones (the engine refuses a shipyard whose centre lies in an area
#   named NO_SHIPYARD*, TerrainLogic::isInNoShipyardZone); empty areas go.
# - Land: ai.skirmish_setup (InnerPerimeter / OuterPerimeter, Center / Flank / Backdoor paths, CombatZone) for the six starts.
# - Ships (adapt-map-for-navy rules): Shipyard<N>_1..3 3 to 4 cells off the shore, closest to their own start, outside the no-shipyard
#   zones, at least 300 apart; NavalPerimeter<N>, an octagon over the water in front of them; Naval<N> along the sea lane from the
#   enemy's side and NavalFlank<N> around the other side, in deep water (18 cells and more from land) inside the playable area.
#   Worked out for players 1 (bottom left) and 2 (bottom centre) and mirrored like the lobes: 1 -> 3 (x), 6 (y), 4 (both); 2 -> 5 (y).
# Usage: team_islands_ai_markers.py <map path> [--save]
import json, math, os, sys
import numpy as np
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "cliffs")]
from wb_bridge import WorldBuilderBridge
import cliff_scan

map_path = sys.argv[1]
save = "--save" in sys.argv
CELL, WATER = 10.0, 60.0
C = np.array([2500.0, 3000.0])
MIRRORS = {1: [(3, -1, 1), (6, 1, -1), (4, -1, -1)], 2: [(5, 1, -1)]}
YARDS, YARD_D, YARD_GAP, YARD_REACH = 3, (30.0, 40.0), 300.0, 1300.0
DEEP = 18           # cells from land for naval path points
MARGIN = 60.0       # playable-area margin for every marker
PERIMETER = 500.0   # naval perimeter radius: 650 overlapped the land and the neighbours
FLANK, BACKDOOR = 35, 50
OPEN_REACH, OPEN_DIST = 25, 200.0   # cells, world units: open water this far from land within that reach of a shipyard
# Naval paths as offsets from the centre, from the open sea into the player's waters (the last point is set to the shipyards).
# Player 1 (bottom left): the enemy is above, so Naval1 comes down the west side; NavalFlank1 comes round the south from the east.
# Player 2 (bottom centre): Naval2 comes down the east side and round the south-east; NavalFlank2 the same on the west.
PATHS = {1: {"Naval": [(-2150, 600), (-2150, -300), (-2050, -1100)], "NavalFlank": [(1300, -2650), (0, -2700), (-1200, -2600), (-1950, -1900)]},
         2: {"Naval": [(2150, 300), (2050, -1300), (1300, -2400)], "NavalFlank": [(-2150, 300), (-2050, -1300), (-1300, -2400)]}}

wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
info = wb.call("map.open", path=map_path, discard_changes=True)
hm, world = info["heightmap"], info["world"]
B = hm["border"]
z = cliff_scan.heights(wb, hm["x_extent"], hm["y_extent"]) * 0.625
PX, PY = world["playable_max_x"], world["playable_max_y"]

# No-shipyard zones.
zones = []
for p in wb.call("polygons.list", include_points=True)["polygons"]:
    if p["kind"] != "area" or p["name"].startswith(("InnerPerimeter", "OuterPerimeter", "CombatZone", "NavalPerimeter")):
        continue
    pts = [q[:2] for q in p["points"]]
    if p["name"].upper().startswith("NO_SHIPYARD"):
        zones.append(pts)
        continue
    if save:
        wb.call("polygons.delete", id=p["id"])
    if len(pts) >= 3:
        zones.append(pts)
if save:
    for k, pts in enumerate(zones):
        wb.call("polygons.add", kind="area", name=f"NO_SHIPYARD_{k + 1}", points=pts)
print("no-shipyard zones", len(zones), flush=True)


def inside(pts, x, y):
    c = False
    for (x1, y1), (x2, y2) in zip(pts, pts[1:] + pts[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            c = not c
    return c


# Sea: below the water and joined to the map edge (lagoons are not); distance from land in world units, per vertex.
lab, _ = ndimage.label(z < WATER)
edge = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
sea = np.isin(lab, list(edge))
dist = ndimage.distance_transform_edt(z < WATER) * CELL
# Open water within reach: a shipyard in a channel between two lobes would sit in a narrow lane.
open_water = ndimage.maximum_filter(np.where(sea, dist, 0), size=2 * OPEN_REACH + 1) >= OPEN_DIST
starts = {int(w["waypoint_name"].split("_")[1]): (w["x"], w["y"]) for w in wb.call("waypoints.list")["waypoints"]
          if (w.get("waypoint_name") or "").startswith("Player_")}
print("starts", starts, flush=True)
vy, vx = np.indices(z.shape)
VX, VY = (vx - B) * CELL, (vy - B) * CELL


def at(x, y):
    return int(round(y / CELL + B)), int(round(x / CELL + B))


def depth_cells(x, y):
    iy, ix = at(x, y)
    return dist[iy, ix] / CELL if sea[iy, ix] else -1


def yards_for(n, side=0):
    # side: 0 any; -1 / 1 only west / east of the centre line; 2 only on it (a player on the axis gets a pair and one off its tip).
    sx, sy = starts[n]
    ok = sea & open_water & (dist >= YARD_D[0]) & (dist <= YARD_D[1]) & (VX > MARGIN) & (VY > MARGIN) & (VX < PX - MARGIN) & (VY < PY - MARGIN)
    cand = [(math.hypot(x - sx, y - sy), x, y) for iy, ix in np.argwhere(ok) for x, y in [(VX[iy, ix], VY[iy, ix])]]
    cand.sort()
    out = []
    for d, x, y in cand:
        if d > YARD_REACH:
            break
        if any(inside(zn, x, y) for zn in zones):
            continue
        if (side in (-1, 1) and (x - C[0]) * side < 50) or (side == 2 and abs(x - C[0]) > 5):
            continue
        if any(math.hypot(x - ox, y - oy) <= d for m, (ox, oy) in starts.items() if m != n):
            continue
        if any(math.hypot(x - a, y - b) < YARD_GAP for a, b in out):
            continue
        out.append((float(x), float(y)))
        if len(out) == YARDS:
            break
    return out


def mirror(pts, mx, my):
    return [(float(C[0] + mx * (x - C[0])), float(C[1] + my * (y - C[1]))) for x, y in pts]


def octagon(cx, cy, r):
    return [[round(cx + r * math.cos(math.radians(22.5 + 45 * k)), 1), round(cy + r * math.sin(math.radians(22.5 + 45 * k)), 1)] for k in range(8)]


plan = {}
for n in (1, 2):
    # Player 2 stands on the mirror axis: one shipyard each side and one off the tip keeps its waters symmetric, so its ships
    # are not drawn round one side (with the nearest three, two lay west and the naval paths crossed the lobe).
    yards = yards_for(n) if n == 1 else yards_for(n, -1)[:1] + yards_for(n, 1)[:1] + yards_for(n, 2)[:1]
    cx, cy = np.mean(yards, axis=0)
    paths = {}
    for label, pts in PATHS[n].items():
        w = [(C[0] + u, C[1] + v) for u, v in pts]
        # The last point: open water in front of the shipyards, the nearest point to their centre at least 15 cells from land.
        deep = np.argwhere(sea & (dist >= 150))
        k = np.argmin(np.hypot(VX[deep[:, 0], deep[:, 1]] - cx, VY[deep[:, 0], deep[:, 1]] - cy))
        w.append((float(VX[tuple(deep[k])]), float(VY[tuple(deep[k])])))
        paths[label] = w
    # The naval perimeter over the water in front of the shipyards, between them and the last path point.
    lx, ly = paths["Naval"][-1]
    plan[n] = {"yards": yards, "centre": (float((cx + 2 * lx) / 3), float((cy + 2 * ly) / 3)), "paths": paths}
for src, targets in MIRRORS.items():
    for n, mx, my in targets:
        p = plan[src]
        plan[n] = {"yards": mirror(p["yards"], mx, my), "centre": mirror([p["centre"]], mx, my)[0],
                   "paths": {k: mirror(v, mx, my) for k, v in p["paths"].items()}}

for n in sorted(plan):
    p = plan[n]
    d = {k: [round(depth_cells(x, y), 1) for x, y in v] for k, v in p["paths"].items()}
    print(n, "yards", [(round(x), round(y), round(depth_cells(x, y), 1)) for x, y in p["yards"]], "path depth (cells)", d, flush=True)
    for k, v in p["paths"].items():
        for x, y in v:
            assert MARGIN <= x <= PX - MARGIN and MARGIN <= y <= PY - MARGIN, (n, k, x, y)
if not save:
    sys.exit(0)

# Narrower flanks than the default 70: wider ones crossed the bays between the lobes.
print(json.dumps(wb.call("ai.skirmish_setup", flank_angle=FLANK, backdoor_angle=BACKDOOR))[:300], flush=True)
for n in sorted(plan):
    p = plan[n]
    for k, (x, y) in enumerate(p["yards"]):
        wb.call("waypoints.add", x=round(x, 1), y=round(y, 1), name=f"Shipyard{n}_{k + 1}")
    wb.call("polygons.add", kind="area", name=f"NavalPerimeter{n}", points=octagon(*p["centre"], PERIMETER))
    for label, pts in p["paths"].items():
        prev = None
        for k, (x, y) in enumerate(pts):
            name = f"{label}{n}_{k + 1}"
            wb.call("waypoints.add", x=round(x, 1), y=round(y, 1), name=name, path_labels=[f"{label}{n}"], **({"link_from": prev} if prev else {}))
            prev = name
check = wb.call("ai.skirmish_check")
print(json.dumps({k: check.get(k) for k in ("problems", "naval_problems", "naval_ready", "ships_enabled")}), flush=True)
wb.call("map.save", path=map_path)
print("saved", map_path)
wb.close()
