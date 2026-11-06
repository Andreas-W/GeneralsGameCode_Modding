# GoldCoastGen, stage D after the town: farms, roads and trees, through the second WorldBuilder.
# - Four farms beside the user's crop fields (template CropsType1 / grass squares), two in each half: a farmhouse, a barn and a shed
#   around a dirt yard, haystacks, a tractor, a picket fence along the yard's road side, and one more crop field each. Each farm goes to the
#   nearest spot to its planned place where the yard (YARD across) is flat and free of the town, the docks, the river and the cliffs, and
#   at least BASE_CLEAR from a start.
# - A country road (TwoLane, as the town's) from each base to the town, dirt tracks (DirtRoadTracks2) from the road or town to each farm.
# - Trees: windbreak rows of oaks and maples along the fields, clumps on the river banks, bushes and desert shrubs scattered thinly on
#   the plateau; none on roads, yards, fields or near structures.
# - Ground under them (DirtMediumType12 yards, CropsType1 fields) as masks; then reblend, cliff mapping (fit), save.
# Usage: gold_coast_farms.py <plan.json> <work dir (mapgen_work)>
import json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
CELL, B, WATER, RIVER = 10.0, plan["border"], plan["water"]["height"], plan["river"]["height"]
YARD, BASE_CLEAR = 170.0, 520.0
rng = np.random.default_rng(plan.get("seed", 1) + 31)
# (name, planned yard centre, direction of the extra field from the yard in degrees)
FARMS = [("north-west", (1450, 1900), 90), ("north-east", (2330, 1950), 90), ("south-west", (1450, 1050), 270), ("south-east", (2300, 1000), 270)]

wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
wb.call("map.new", width=32, height=32, discard_changes=True)
info = wb.call("map.open", path=plan["map"], discard_changes=True)
hm = info["heightmap"]
nx, ny = hm["x_extent"], hm["y_extent"]
rows = []
while len(rows) < ny:
    rows += wb.call("terrain.get_heights", x0=0, y0=len(rows), w=nx, h=min(ny - len(rows), 60000 // nx))["heights"]
z = np.array(rows, np.float64) * 0.625
zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
cs = np.maximum.reduce([np.abs(z[1:, 1:] - z[:-1, :-1]), np.abs(z[1:, :-1] - z[:-1, 1:])]) / CELL
cy, cx = np.indices(zc.shape)
X, Y = (cx - B + 0.5) * CELL, (cy - B + 0.5) * CELL
river_v = np.load(os.path.join(work, "stageA", "river_band.npy"))
river = river_v[:-1, :-1] | river_v[1:, 1:]
objs = wb.call("objects.list", include_waypoints_and_roads=True, limit=30000)["objects"]
starts = [(o["x"], o["y"]) for o in objs if o["kind"] == "waypoint" and o.get("waypoint_name", "").startswith("Player_")]
structures = [(o["x"], o["y"]) for o in objs if o["kind"] == "object" and not o["template"].startswith(("Tree", "Bush", "Shrub", "Desert"))]
road_pts = [(o["x"], o["y"]) for o in objs if o["kind"] == "road_point"]
# West of the river and inside the playable area (a farm first went across the river onto the strip off the map); gentle, not flat:
# the relief tilts most of the plateau a little, and slopes under 0.12 sent farms 665 to 959 away.
river_x = np.where(river.any(0))[0]
free = (zc > 108) & ~ndimage.binary_dilation(river, iterations=10) & (ndimage.maximum_filter(cs, 5) < 0.2)
free &= (X > 0) & (Y > 0) & (X < plan["playable_max"]) & (Y < plan["playable_max"]) & (X < (river_x.min() - B) * CELL)
free &= (X < 2790) & (Y < 2990)
for sx, sy in starts:
    free &= np.hypot(X - sx, Y - sy) > BASE_CLEAR
for px, py in structures + road_pts:
    free &= np.hypot(X - px, Y - py) > 120
room = ndimage.distance_transform_edt(free) * CELL
os.makedirs(os.path.join(work, "stageD"), exist_ok=True)
Image.fromarray((np.stack([free, room >= YARD / 2, zc > 108], -1)[::-1] * 255).astype(np.uint8)).save(os.path.join(work, "stageD", "free.png"))
if os.environ.get("FARMS_DRY"):
    sys.exit(0)


def spot(x, y, need):
    global room
    cand = room >= need
    d = np.where(cand, np.hypot(X - x, Y - y), 1e12)
    i = np.unravel_index(np.argmin(d), d.shape)
    return float(X[i]), float(Y[i]), float(d[i])


placed, yards, fields, tracks = [], [], [], []
town = [(o["x"], o["y"]) for o in objs if o["kind"] == "object" and o["template"].startswith(("American", "Convience", "GasStation"))]
tcx, tcy = np.mean(town, axis=0) if town else (1850.0, 1400.0)
for name, (px, py), fdir in FARMS:
    fx, fy, moved = spot(px, py, YARD / 2)
    # Taken: the yard and its field keep the next farm away.
    free &= np.hypot(X - fx, Y - fy) > 420
    room = ndimage.distance_transform_edt(free) * CELL
    # The yard faces the town: buildings on its far side, the fence on the near side.
    face = math.degrees(math.atan2(tcy - fy, tcx - fx))
    a = math.radians(face)
    ux, uy = math.cos(a), math.sin(a)
    vx, vy = -uy, ux
    kit = [("FarmHouse", -30, 45, face + 90), ("BarnCoop", -45, -45, face), ("BarnShed", 20, -60, face + 90),
           ("Haystack", 30, 30, rng.uniform(0, 360)), ("Haystack", 45, 10, rng.uniform(0, 360)), ("Haystack", 20, 50, rng.uniform(0, 360)),
           ("Tractor", 5, -5, face + rng.uniform(-30, 30))]
    for t, du, dv, ang in kit:
        placed.append({"template": t, "x": round(fx + du * ux + dv * vx, 1), "y": round(fy + du * uy + dv * vy, 1), "angle_deg": round(ang, 1)})
    for k in range(-3, 4):
        x0, y0 = fx + 80 * ux + k * 30 * vx, fy + 80 * uy + k * 30 * vy
        if k != 0:  # a gate in the middle
            placed.append({"template": "PicketFence01", "x": round(x0, 1), "y": round(y0, 1), "angle_deg": round(face + 90, 1)})
    yards.append((fx, fy))
    # The extra field: a rectangle 260 x 170 beside the yard, as far as the ground stays free.
    fa = math.radians(fdir)
    fcx, fcy = fx + 230 * math.cos(fa), fy + 230 * math.sin(fa)
    fields.append((fcx, fcy, 130, 85, fdir))
    tracks.append((fx + 90 * ux, fy + 90 * uy))
    print(name, "farm at", round(fx), round(fy), "moved", round(moved), "field at", round(fcx), round(fcy))

# Roads: each base to the town (the nearest town street point), and farm tracks to the nearest road point.
town_pts = road_pts[:]
roads = []
for sx, sy in starts:
    end = min(town_pts, key=lambda p: math.dist(p, (sx, sy)))
    L = math.dist(end, (sx, sy))
    begin = (sx + (end[0] - sx) / L * 200, sy + (end[1] - sy) / L * 200)
    try:
        r = wb.call("roads.route", type="TwoLane", **{"from": list(begin)}, to=list(end), corners="curved", end_caps=True, object_clearance=30)
        roads.append(r["points"]); print("country road", round(r["length"]))
    except Exception as e:
        print("no country road from", sx, sy, str(e)[:80])
all_road = town_pts + [tuple(p) for r in roads for p in r]
for tx, ty in tracks:
    end = min(all_road, key=lambda p: math.dist(p, (tx, ty)))
    try:
        r = wb.call("roads.route", type="DirtRoadTracks2", **{"from": [tx, ty]}, to=list(end), corners="curved", end_caps=False, object_clearance=30)
        roads.append(r["points"]); print("farm track", round(r["length"]))
    except Exception as e:
        print("no track from", round(tx), round(ty), str(e)[:80])

# Ground: yards and fields.
yard_m = np.zeros(zc.shape, bool)
for fx, fy in yards:
    wob = 1 + 0.12 * np.sin(np.arctan2(Y - fy, X - fx) * 3 + rng.uniform(0, 6))
    yard_m |= np.hypot(X - fx, Y - fy) <= 95 * wob
field_m = np.zeros(zc.shape, bool)
for fcx, fcy, hw, hh, ang in fields:
    a = math.radians(ang)
    u = (X - fcx) * math.cos(a) + (Y - fcy) * math.sin(a)
    v = -(X - fcx) * math.sin(a) + (Y - fcy) * math.cos(a)
    field_m |= (np.abs(u) <= hh) & (np.abs(v) <= hw)
field_m &= free | (room > 0) | ((zc > 108) & (cs < 0.15) & ~river)
out = os.path.join(work, "stageD")
os.makedirs(out, exist_ok=True)
for name, m, t in (("fields", field_m, "CropsType1"), ("yards", yard_m, "DirtMediumType12")):
    p = os.path.join(out, name + ".png")
    Image.fromarray((m[::-1] * 255).astype(np.uint8)).save(p)
    wb.call("terrain.import_mask", path=p, target="texture", texture=t, fit="exact")

# Trees.
def segd(px, py, pts):
    best = np.full(px.shape, 1e9)
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        L = max((bx - ax) ** 2 + (by - ay) ** 2, 1e-6)
        t = np.clip(((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / L, 0, 1)
        best = np.minimum(best, np.hypot(px - ax - t * (bx - ax), py - ay - t * (by - ay)))
    return best


clear = (zc > 108) & ~river & (cs < 0.3) & ~yard_m & ~ndimage.binary_dilation(field_m, iterations=1)
for r in roads:
    clear &= segd(X, Y, r) > 35
for px, py in structures + [(p["x"], p["y"]) for p in placed] + town:
    clear &= np.hypot(X - px, Y - py) > 70
for sx, sy in starts:
    clear &= np.hypot(X - sx, Y - sy) > 300
trees = []
BROAD = ["TreeOak01", "TreeOak02", "TreeOak05", "TreeOak06", "TreeMaple1", "TreeMaple2", "TreeMaple03", "TreeMaple05"]


def add(x, y, names):
    i, j = int(y / CELL + B), int(x / CELL + B)
    if 0 <= i < clear.shape[0] and 0 <= j < clear.shape[1] and clear[i, j] and all(math.hypot(x - a, y - b) > 18 for a, b, _ in trees[-60:]):
        trees.append((x, y, names[rng.integers(len(names))]))


# Windbreaks along each field's long sides, just outside it.
for fcx, fcy, hw, hh, ang in fields:
    a = math.radians(ang)
    for side in (-1, 1):
        for s in np.arange(-hw, hw + 1, 26):
            x = fcx + (hh + 22) * side * math.cos(a) - s * math.sin(a)
            y = fcy + (hh + 22) * side * math.sin(a) + s * math.cos(a)
            add(x + rng.uniform(-4, 4), y + rng.uniform(-4, 4), BROAD)
# Clumps on the river banks.
bank = (ndimage.distance_transform_edt(~river) * CELL < 90) & clear
cells = np.argwhere(bank & (rng.random(bank.shape) < 0.05))
for iy, ix in cells:
    add(X[iy, ix] + rng.uniform(-5, 5), Y[iy, ix] + rng.uniform(-5, 5), BROAD + ["TreeBirch02", "TreeBirch04", "Bush05", "Bush08"])
# Thin scatter on the plateau.
cells = np.argwhere(clear & (rng.random(clear.shape) < 0.0035))
for iy, ix in cells:
    add(X[iy, ix], Y[iy, ix], ["DesertShrub02", "DesertShrub05", "DesertShrub08", "Bush03", "Bush06", "TreeOak05", "TreeMaple05"])
for t in placed:
    wb.call("objects.place", template=t["template"], x=t["x"], y=t["y"], angle_deg=t["angle_deg"])
for x, y, t in trees:
    wb.call("objects.place", template=t, x=round(float(x), 1), y=round(float(y), 1), angle_deg=round(float(rng.uniform(0, 360)), 1))
wb.call("terrain.reblend")
c = wb.call("terrain.cliff_map", repair="fit")
wb.call("map.save", path=plan["map"], overwrite=True)
json.dump({"farms": yards, "fields": fields, "roads": roads, "objects": placed}, open(os.path.join(out, "farms.json"), "w"), indent=0)
print("farm objects", len(placed), "trees", len(trees), "roads", len(roads), "cliffs outside", c.get("outside_v"), "saved")
