# Stage A of OasisLakeGen, a map from a description only: 8 players placed as on Twilight Flame (two per side of a square map, 22.5
# degrees off each axis), a very large and deep lake in the middle with an island, a shallow beach close to every base, desert.
# Designed in one octant (east to north-east, holding one start), the same for every player. With plan symmetry "fold": true the terrain
# is folded onto the other seven (symmetry.py): an exact mirror across both axes and both diagonals. Without it the layout is the same
# for all, but cliffs and shores are each their own (the user preferred rough symmetry to an exact mirror).
# 1. An idealised mockup (heights, ramps as road segments across the corner plateaus' steps, kept objects) for terrain_from_mockup.py.
# 2. Its terrain, folded if asked, written as the stage A result.
# Usage: oasis_lake_stage_a.py <plan.json> <work dir>
import json, math, os, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)
import symmetry

RAW, CELL = 0.625, 10.0
plan = json.load(open(sys.argv[1]))
work = sys.argv[2]
L = plan["layout"]
B, N = plan["border"], L["playable_cells"]
n = N + 2 * B + 1
C = (N * CELL / 2, N * CELL / 2)
low, main, high = [l["z"] for l in plan["levels"]]
water = plan["water"]["height"]
rng = np.random.default_rng(plan.get("seed", 1))

yy, xx = np.indices((n, n), dtype=np.float32)
dx, dy = (xx - B) * CELL - C[0], (yy - B) * CELL - C[1]
# Folded coordinates: u >= v >= 0, theta 0 (east) to 45 (north-east) degrees.
u, v = np.maximum(np.abs(dx), np.abs(dy)), np.minimum(np.abs(dx), np.abs(dy))
r = np.hypot(u, v)
th = np.degrees(np.arctan2(v, u))


def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def noise(scale):
    a = rng.standard_normal((n, n)).astype(np.float32)
    from scipy import ndimage
    a = ndimage.gaussian_filter(a, scale / CELL)
    return a / (a.std() + 1e-9)


# The lake: a radius that pushes out into a bay toward each start, with a little wobble.
start_th = L["start_angle"]
# A cosine over the octant: round lobes toward the starts, not points (a narrow bump gave an eight-pointed star).
bay = 0.5 + 0.5 * np.cos(np.pi * np.clip((th - start_th) / L["bay_halfwidth_deg"], -1, 1))
R = L["lake_radius"] + (L["bay_radius"] - L["lake_radius"]) * bay + L["shore_wobble"] * noise(300)
lake = r < R
island = r < L["island_radius"] + L["shore_wobble"] * 0.5 * noise(200)
sea = lake & ~island
z = np.full((n, n), main, np.float32)
# Corner plateaus: the square's corners beyond `corner` in both folded coordinates, rounded.
corner = smooth((np.minimum(u, v) - L["corner"]) / 40 + 0.5)
z = z + (high - main) * corner
# A rocky ridge between the two bases of each side, on the axis.
ridge = smooth((L["ridge_halfwidth"] - v) / 30) * smooth((u - L["ridge_from"]) / 60)
z = np.maximum(z, main + (L["ridge_height"] - main) * ridge * (0.85 + 0.15 * noise(80)))
# Beaches: low ground on the lake shore in each bay, climbing to the main level; on the island at the diagonals. An island beach must
# be long along the shore (some 40 degrees) and climb quickly: a narrow one (18 degrees) on the small island became a pit ringed by cliffs.
coast_in = np.where(island, L["island_radius"] - r, r - R)
beach_where = np.where(island, smooth(1 - np.abs(th - 45) / L["island_beach_halfwidth_deg"]), smooth((bay - 0.35) / 0.4))
t = np.where(island, smooth((coast_in - L["island_beach_depth"]) / L["island_beach_climb"]), smooth((coast_in - L["beach_depth"]) / L["beach_climb"]))
z = np.where(~sea & (beach_where > 0), z * (1 - beach_where) + np.minimum(z, low + (main - low) * t) * beach_where, z)
# Gentle hills on the main level away from steps.
z = z + L["hills"] * noise(400) * ((np.abs(z - main) < 0.5) & (coast_in > 200))
# The lake: deep beyond a shelf.
sea_d = np.where(sea, np.minimum(r - L["island_radius"], R - r), 0)
z = np.where(sea, plan["water"]["floor"] + (water - 12 - plan["water"]["floor"]) * np.clip(1 - sea_d / L["shelf"], 0, 1), z)

mock = os.path.join(work, "mock")
os.makedirs(mock, exist_ok=True)
np.save(os.path.join(mock, "heights_new.npy"), np.clip(np.round(z / RAW), 0, 255).astype(np.float32))
Image.fromarray(np.zeros((n - 1, n - 1), np.uint8)).save(os.path.join(mock, "passability.png"))
# Kept objects: the starts (calm ground) and the ramps onto the corner plateaus as roads across their steps, all eight of each.
a = math.radians(start_th)
sx, sy = C[0] + L["start_radius"] * math.cos(a), C[1] + L["start_radius"] * math.sin(a)
rx = C[0] + L["ramp_u"]
objs = [{"kind": "waypoint", "template": "*Waypoints/Waypoint", "x": px, "y": py, "unique_id": "start"} for px, py, _ in symmetry.images(sx, sy, C)]
for (ax, ay, _), (bx, by, _) in zip(symmetry.images(rx, C[1] + L["corner"] - 200, C), symmetry.images(rx, C[1] + L["corner"] + 150, C)):
    objs += [{"kind": "road_point", "template": "ramp", "x": ax, "y": ay}, {"kind": "road_point", "template": "ramp", "x": bx, "y": by}]
json.dump({"objects": {"objects": objs}}, open(os.path.join(mock, "mock.json"), "w"), indent=1)

# 2. Terrain, then folded.
out = os.path.join(work, "stageA")
subprocess.run([sys.executable, os.path.join(HERE, "terrain_from_mockup.py"), sys.argv[1], os.path.join(mock, "heights_new.npy"),
                os.path.join(mock, "mock.json"), os.path.join(mock, "passability.png"), out], check=True)
zw = np.load(os.path.join(out, "heights_world.npy"))
if plan["symmetry"].get("fold", True):
    zw = symmetry.fold_grid(zw)
np.save(os.path.join(out, "heights_world.npy"), zw)
top = 255 * RAW * plan.get("height_scale", 1.0)
Image.fromarray(np.round(zw / top * 65535).astype(np.uint16)[::-1]).save(os.path.join(out, "heights.png"))
gy, gx = np.gradient(zw, CELL)
sl = np.hypot(gx, gy)
cells = np.maximum.reduce([sl[:-1, :-1], sl[1:, :-1], sl[:-1, 1:], sl[1:, 1:]]) >= 0.4
Image.fromarray((cells[::-1] * 255).astype(np.uint8)).save(os.path.join(out, "impassable.png"))
# A preview of the folded terrain with the 8 starts.
t = np.clip((zw - water) / (high - water + 20), 0, 1)
rgb = np.stack([0.45 + 0.45 * t, 0.4 + 0.4 * t, 0.25 + 0.3 * t], -1)
rgb[zw < water] = np.array([0.05, 0.2, 0.5]) + 0.3 * (zw[zw < water] / water)[:, None]
rgb[np.pad(cells, ((0, 1), (0, 1)), mode="edge") & (zw >= water)] = (0.6, 0.15, 0.1)
im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1]).resize((n * 2, n * 2), Image.NEAREST)
from PIL import ImageDraw
d = ImageDraw.Draw(im)
for px, py, _ in symmetry.dedupe(symmetry.images(sx, sy, C)):
    X, Y = (px / CELL + B) * 2, (n - 1 - (py / CELL + B)) * 2
    d.ellipse([X - 8, Y - 8, X + 8, Y + 8], outline=(255, 255, 255), width=3)
im.save(os.path.join(out, "preview.png"))
print("starts", [(round(p[0]), round(p[1])) for p in symmetry.dedupe(symmetry.images(sx, sy, C))])
