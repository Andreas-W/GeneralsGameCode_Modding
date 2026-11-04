# Stage A of TeamIslandsGen (6 players, 3 vs 3, Tournament Island as reference): the terrain from the user's template
# (Templates\6pTournamentTeamIslands.map). The template's land outline gives the layout: six lobes, one per player, around a centre with
# four lagoons. It is made symmetric across both axes about the centre the template's starts and lagoons share, moved to the middle of a
# map 50 cells larger on every side, and smoothed. Shores are soft everywhere, as on Tournament Island: the land is a flat level a little
# above the water, sinking over a wide beach, then a shelf and a seabed at least 50 deep. Lagoons are shallow with soft shores.
# The starts come from the template's TempBuildingSize placeholders, made symmetric (N/S on the axis, four on the sides).
# Usage: team_islands_stage_a.py <template scan dir (ref_scan)> <out dir>
#   Writes heights_world.npy (vertices, row 0 south), heights.png (16 bit, 0..65535 = 0..255 raw), land.png, mock.json (starts,
#   lagoon centres), preview.png.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "cliffs")]
from cliff_gen import noise, smooth_profile

scan, out = sys.argv[1], sys.argv[2]
plan = json.load(open(sys.argv[3])) if len(sys.argv) > 3 else {}
os.makedirs(out, exist_ok=True)
CELL, B, WATER = 10.0, 30, 60.0
W, H = 500, 600                         # playable cells of the new map: the template's 400 x 500 and 50 more on every side
T0 = np.array([2100.0, 2424.0])         # the template's centre of symmetry (starts and lagoons)
M0 = np.array([W * CELL / 2, H * CELL / 2])
LEVEL = 72.0                            # land: 12 above the water (Tournament Island: 6, with water at 4)
BEACH_IN = 170.0                        # beach: from the level down to the water over this distance
# The seabed falls off steeply (user): the floor is reached 440 from the shore (it was 780: 260 and 520).
SHELF, SHELF_W = 24.0, 160.0            # then this deep over this distance
FLOOR, FLOOR_W = 2.0, 280.0             # then down to the floor (58 below the water) over this
LAGOON_DEPTH, LAGOON_W = 16.0, 120.0
MIN_PIECE = 600                         # cells: smaller land pieces and holes (marks in the template) go
# Channels (inner end, outer end, half width), offsets from the centre in the north-east quadrant: between the north lobe and the
# north-east one, toward the north-east lagoon.
CHANNELS = [((625, 1150), (630, 1850), 120)]
GROW = 50.0                             # the players' land, a little larger than the template's (user)
ABOVE_SIDES = 1700.0
TIP_FROM = 1250.0                       # north and south lobes: a clean rounded rectangle beyond this offset from the centre
CORNER, CORNER_IN, CORNER_SPREAD = 300.0, 90.0, 200.0   # the marked corners: outer and inner radius, area widened by
ROUND, ROUND_IN = 260.0, 70.0          # radius of the outer corners (rounded rectangles, as on the reference) and of inner ones

tz = np.load(os.path.join(scan, "heights.npy")).astype(np.float64)
tland = (tz > WATER).astype(np.float32)
ny, nx = H + 2 * B, W + 2 * B
yy, xx = np.indices((ny, nx), dtype=np.float64)
X, Y = (xx - B) * CELL, (yy - B) * CELL


def sample(a, px, py):
    # Bilinear sample of a template vertex grid at world points; outside it: 0 (sea).
    fx, fy = px / CELL + B, py / CELL + B
    return ndimage.map_coordinates(a, [fy, fx], order=1, mode="constant", cval=0.0)


# Template land around its centre, averaged over the four mirror images, onto the new map's centre.
dx, dy = X - M0[0], Y - M0[1]
m = np.mean([sample(tland, T0[0] + sx * dx, T0[1] + sy * dy) for sx in (1, -1) for sy in (1, -1)], axis=0)
land = ndimage.gaussian_filter(m, 6.0) > 0.5   # smooth enough to take the template's brush wobbles off the straight sides
# Lagoons: water not joined to the map edge. Small marks (land or holes) go.
lab, n = ndimage.label(~land)
edge = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
sizes = ndimage.sum(np.ones_like(m), lab, range(1, n + 1))
lagoon = np.zeros_like(land)
for i in range(1, n + 1):
    if i in edge:
        continue
    if sizes[i - 1] < MIN_PIECE:
        land |= lab == i
    else:
        lagoon |= lab == i
lab, n = ndimage.label(land)
sizes = ndimage.sum(np.ones_like(m), lab, range(1, n + 1))
land = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= MIN_PIECE])
# A little more land, into the sea (lagoons keep their size); then the outline rounded like Tournament Island's islands, rounded
# squares: an opening (erode, then grow back) by ROUND rounds every outer corner to that radius. It runs on land and lagoons together,
# so that the ground between a lagoon and a bay, narrower than twice the radius, stays.
solid = land | lagoon | (ndimage.distance_transform_edt(~land) * CELL <= GROW)
core = ndimage.distance_transform_edt(solid) * CELL > ROUND
solid = ndimage.distance_transform_edt(~core) * CELL <= ROUND
land = solid & ~lagoon
# The north and south lobes widen toward their tips in the template, which left small ears at the corners: past the lagoons they are
# a plain rounded rectangle as wide as the channels beside them leave it (the channel cut the middle narrower than the tip) and as long
# as before.
(cx_, _), _, half_ = CHANNELS[0]
hw = cx_ - half_
# The lobe itself past the lagoons, and everything above the side lobes (they end below ABOVE_SIDES), where the flares are.
tip = ((np.abs(dx) < hw + 40) & (np.abs(dy) > TIP_FROM)) | ((np.abs(dx) < 900) & (np.abs(dy) > ABOVE_SIDES))
col = int(M0[0] / CELL) + B
top = float(np.abs(dy[:, col][land[:, col]]).max())
qx, qy = np.maximum(np.abs(dx) - (hw - ROUND), 0), np.maximum(np.abs(dy) - (top - ROUND), 0)
rrect = (np.hypot(qx, qy) <= ROUND) & (np.abs(dx) <= hw) & (np.abs(dy) <= top)
land = np.where(tip, rrect, land)
# Channels between neighbouring lobes, from the sea toward a lagoon: averaging the mirror images closed the template's one-sided
# notches, and on Tournament Island each player's land is its own, joined to the others only around the lagoons in the middle.
# Given in the north-east quadrant (offsets from the centre), mirrored into the others; each ends short of its lagoon, leaving a neck.
for (ax_, ay_), (bx_, by_), half in CHANNELS:
    for sx in (1, -1):
        for sy in (1, -1):
            a_, b_ = np.array([sx * ax_, sy * ay_]) + M0, np.array([sx * bx_, sy * by_]) + M0
            ab = b_ - a_
            t = np.clip(((X - a_[0]) * ab[0] + (Y - a_[1]) * ab[1]) / (ab @ ab), 0, 1)
            land &= np.hypot(X - a_[0] - t * ab[0], Y - a_[1] - t * ab[1]) > half
# The corners where a channel meets the coast, softened (closing: grow, then erode back) without filling the channels.
grown = ndimage.distance_transform_edt(~(land | lagoon)) * CELL <= ROUND_IN
land = (ndimage.distance_transform_edt(grown) * CELL > ROUND_IN) & ~lagoon
# Corners the user marked with trigger areas (plan layout.round_areas): where a side lobe meets a channel mouth the channel, cut after
# the rounding, left a sharp corner. Inside the marked areas (mirrored into every quadrant, widened a little) the land is opened and
# closed again with CORNER, which rounds both the corner and the bend into the channel.
areas = plan.get("layout", {}).get("round_areas", {}).get("points", [])
if areas:
    from PIL import ImageDraw as _D
    img = Image.new("L", (nx, ny), 0)
    dr = _D.Draw(img)
    for pts in areas:
        for sx in (1, -1):
            for sy in (1, -1):
                dr.polygon([((M0[0] + sx * (x - M0[0])) / CELL + B, (M0[1] + sy * (y - M0[1])) / CELL + B) for x, y in pts], fill=255)
    region = ndimage.distance_transform_edt(~(np.array(img) > 0)) * CELL <= CORNER_SPREAD
    solid = land | lagoon
    opened = ndimage.distance_transform_edt(~(ndimage.distance_transform_edt(solid) * CELL > CORNER)) * CELL <= CORNER
    closed = ndimage.distance_transform_edt(ndimage.distance_transform_edt(~opened) * CELL <= CORNER_IN) * CELL > CORNER_IN
    land = np.where(region, closed & ~lagoon, land)
lab, n = ndimage.label(land)
sizes = ndimage.sum(np.ones_like(m), lab, range(1, n + 1))
land = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= MIN_PIECE])

d =(ndimage.distance_transform_edt(land) - ndimage.distance_transform_edt(~land)) * CELL   # + on land
sea = ~land & ~lagoon
dsea = ndimage.distance_transform_edt(sea) * CELL           # distance into the sea from land or lagoon


def ease(t):
    # Half eased in and out, half rising from the start: no flat stretch at the water line (the water cut such ground in steps).
    t = np.clip(t, 0, 1)
    return 0.5 * t * (2 - t) + 0.5 * smooth_profile(t)


z = np.where(land, WATER + (LEVEL - WATER) * ease(d / BEACH_IN), 0.0)
z = np.where(sea, WATER - SHELF * ease(dsea / SHELF_W) - (WATER - SHELF - FLOOR) * smooth_profile((dsea - SHELF_W) / FLOOR_W), z)
z = np.where(lagoon, WATER - LAGOON_DEPTH * ease(-d / LAGOON_W), z)
# Exactly symmetric: the north-east quadrant mirrored onto the others (centre on a vertex).
c = (int(M0[0] / CELL) + B, int(M0[1] / CELL) + B)
def fold(a):
    j = np.minimum(np.abs(np.arange(ny) - c[1]) + c[1], ny - 1)
    i = np.minimum(np.abs(np.arange(nx) - c[0]) + c[0], nx - 1)
    return a[np.ix_(j, i)]
z = fold(z)
land, lagoon = fold(land), fold(lagoon)

# Starts: the placeholders, symmetric. Centre placeholder: the middle.
objs = json.load(open(os.path.join(scan, "all_objects.json")))
ph = [np.array([o["x"], o["y"]]) - T0 for o in objs if o["template"] == "TempBuildingSize"]
ph = [p for p in ph if np.hypot(*p) > 300]
axis = [p for p in ph if abs(p[0]) < 300]
side = [p for p in ph if abs(p[0]) >= 300]
ay = float(np.mean([abs(p[1]) for p in axis]))
sx, sy = float(np.mean([abs(p[0]) for p in side])), float(np.mean([abs(p[1]) for p in side]))
starts = [(0, ay), (sx, sy), (sx, -sy), (0, -ay), (-sx, -sy), (-sx, sy)]
starts = [[round(float(M0[0] + u), 1), round(float(M0[1] + v), 1)] for u, v in starts]
lab, n = ndimage.label(lagoon)
lagoons = [[round(float((cx - B) * CELL), 1), round(float((cy - B) * CELL), 1)] for cy, cx in ndimage.center_of_mass(lagoon, lab, range(1, n + 1))]

np.save(os.path.join(out, "heights_world.npy"), z.astype(np.float32))
Image.fromarray((np.clip(z / 0.625 / 255, 0, 1) * 65535).astype(np.uint16)[::-1]).save(os.path.join(out, "heights.png"))
Image.fromarray((land.astype(np.uint8) * 255)[::-1]).save(os.path.join(out, "land.png"))
mock = {"centre": list(M0), "starts": starts, "lagoons": lagoons, "level": LEVEL, "water": WATER,
        "objects": [{"kind": "waypoint", "template": "*Waypoints/Waypoint", "x": x, "y": y, "name": f"Player_{k + 1}_Start"} for k, (x, y) in enumerate(starts)]}
json.dump(mock, open(os.path.join(out, "mock.json"), "w"), indent=1)

g = np.clip((z - FLOOR) / (LEVEL + 10 - FLOOR), 0, 1)
rgb = np.stack([g * 255] * 3, -1).astype(np.uint8)
wet = z < WATER
rgb[wet] = (rgb[wet] * np.array([0.35, 0.55, 1.0])).astype(np.uint8)
im = Image.fromarray(rgb[::-1])
dr = ImageDraw.Draw(im)
for x, y in starts:
    px, py = x / CELL + B, ny - (y / CELL + B)
    dr.rectangle([px - 4, py - 4, px + 4, py + 4], outline=(255, 255, 0), width=2)
im.save(os.path.join(out, "preview.png"))
gy, gx = np.gradient(z, CELL)
print("starts", starts, "lagoons", lagoons, "land cells", int(land.sum()), "max land slope", round(float(np.hypot(gx, gy)[land].max()), 3),
      "sea floor min", round(float(z.min()), 1))
