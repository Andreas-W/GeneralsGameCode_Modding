# Example for TaintedParadise (its coordinates belong to that map): the top-left island made again from a description. A rocky island,
# cliffs all round except a soft beach on the side facing the map centre; two levels, a lower terrace behind the beach and a rocky upper
# plateau to the north-west with a ramp between them; an oil refinery with supply piles on the terrace and grass hut outposts.
# Heights, passability and textures of the island and the sea around it are replaced; what lies further out stays. Runs in the second
# WorldBuilder (town/start_wb.py) on the saved map and saves it again.
# Usage: tainted_paradise_island.py <scan dir (ref_scan of the map)> <map path> <out dir> [--save] [--seed N]
#   Without --save it writes heights.png, impassable.png, preview.png and stops.
import json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "town"), os.path.join(HERE, "cliffs")]
from cliff_gen import noise, wall_noise, smooth_profile

args = [a for i, a in enumerate(sys.argv[1:]) if not a.startswith("--") and not (i and sys.argv[i] == "--seed")]
scan, map_path, out = args[:3]
seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 3
rng = np.random.default_rng(seed)
os.makedirs(out, exist_ok=True)

B, CELL, WATER = 20, 10.0, 60.0
RAW_LIMIT = 306                      # 255 * HeightMapScale 1.2
SNAP = 1.2 * 0.625                   # heights are multiples of the scale
C = np.array([760.0, 2620.0])        # island centre
AX, AY, TILT = 330.0, 410.0, 12.0    # half axes and tilt of the outline (degrees)
MAP_CENTRE = np.array([1795.0, 1795.0])
LOW, HIGH = 84.0, 124.0              # terrace and plateau
FOOT, BASE = 26.0, None              # foot of the coast cliffs under water; BASE: the deep seabed, read from the map
SPLIT = -40.0                        # the plateau's edge, along the beach direction from the centre
BEACH_IN, BEACH_OUT, SHELF = 170.0, 220.0, 30.0   # beach: land width, shallow water width, its depth
RAMP_AT, RAMP_W = 110.0, 120.0       # ramp onto the plateau: offset along the edge, width
# Spots kept flat for objects: refinery, supplies, the outposts (offset along the beach direction, along the edge, radius).
SPOTS = {"refinery": (70, -110, 80), "supply": (110, 70, 120), "huts_low": (230, 175, 90), "huts_high": (-200, 70, 100)}

z0 = np.load(os.path.join(scan, "heights.npy")).astype(np.float64)
ny, nx = z0.shape
yy, xx = np.indices(z0.shape, dtype=np.float64)
X, Y = (xx - B) * CELL, (yy - B) * CELL
db = (MAP_CENTRE - C) / np.linalg.norm(MAP_CENTRE - C)   # toward the map centre: the beach side
tb = np.array([-db[1], db[0]])
P = (X - C[0]) * db[0] + (Y - C[1]) * db[1]
T = (X - C[0]) * tb[0] + (Y - C[1]) * tb[1]
def at(p, t):
    return C + p * db + t * tb

# The old island: land joined to the centre.
lab, _ = ndimage.label(z0 > WATER)
old = lab == lab[int(C[1] / CELL + B), int(C[0] / CELL + B)]
ring = (ndimage.distance_transform_edt(~old) * CELL > 600) & (ndimage.distance_transform_edt(~old) * CELL < 800) & (z0 < WATER)
BASE = float(np.median(z0[ring]))

# Outline: a tilted ellipse with large wobbles, a little wider toward the beach.
a = math.radians(TILT)
U = (X - C[0]) * math.cos(a) + (Y - C[1]) * math.sin(a)
V = -(X - C[0]) * math.sin(a) + (Y - C[1]) * math.cos(a)
r = np.hypot(U / AX, V / AY) * (1 + 0.07 * noise(z0.shape, rng, [(18, 1.0), (8, 0.4)])) - 0.06 * np.clip(P / 300, 0, 1)
inside = ndimage.gaussian_filter((r < 1).astype(np.float32), 2.0) > 0.5
d = (ndimage.distance_transform_edt(inside) - ndimage.distance_transform_edt(~inside)) * CELL   # + inside, world units

# Beach: the stretch of coast facing the map centre.
ang = np.arctan2(Y - C[1], X - C[0]) - math.atan2(db[1], db[0])
beach = smooth_profile((np.cos(ang) - math.cos(math.radians(48))) / (math.cos(math.radians(26)) - math.cos(math.radians(48))))

# Levels: the plateau behind a wandering edge, a cliff down to the terrace, and one ramp.
edge = SPLIT + 45 * noise(z0.shape, rng, [(25, 1.0), (10, 0.3)]) - P
wn = wall_noise(z0.shape, rng)
rise = HIGH - LOW
lev_cliff = LOW + rise * smooth_profile((edge + 9 * wn) / (1.5 * rise) + 0.5)
lev_ramp = LOW + rise * smooth_profile(edge / (rise / 0.2) + 0.5)
ramp = smooth_profile((RAMP_W / 2 + 40 - np.abs(T - RAMP_AT)) / 40) * (np.abs(edge) < rise / 0.2)
lev = lev_cliff * (1 - ramp) + lev_ramp * ramp

# Coast: cliffs falling to FOOT, then the seabed down to BASE; on the beach a soft slope into shallow water.
dn = d + 14 * wall_noise(z0.shape, rng)
w = 1.5 * (lev - FOOT)
wall = FOOT + (lev - FOOT) * smooth_profile(dn / w + 0.5)
skirt = FOOT - (FOOT - BASE) * smooth_profile((-dn - w / 2) / 320)
z_cliff = np.where(dn >= -w / 2, wall, skirt)
# The beach crosses the water line at a steady slope: flat at the water level, the water surface cut the ground in steps.
def ease(t):
    t = np.clip(t, 0, 1)
    return 0.5 * t * (2 - t) + 0.5 * smooth_profile(t)
z_beach = np.where(d >= 0, WATER + (lev - WATER) * ease(d / BEACH_IN),
                   np.where(-d < BEACH_OUT, WATER - SHELF * ease(-d / BEACH_OUT),
                            WATER - SHELF - (WATER - SHELF - BASE) * smooth_profile((-d - BEACH_OUT) / 300)))
z = beach * z_beach + (1 - beach) * z_cliff

# Relief: soft on the terrace, rough on the plateau, a few rock outcrops up there; calm near the coast and on the object spots.
inner = smooth_profile(d / 110)
plateau = smooth_profile((edge - 40) / 60)
flat = np.ones(z0.shape)
for p, t, rad in SPOTS.values():
    q = at(p, t)
    flat *= 1 - smooth_profile((rad + 40 - np.hypot(X - q[0], Y - q[1])) / 40)
z += inner * flat * (1.5 * noise(z0.shape, rng, [(14, 1.0), (6, 0.4)]) * (1 - plateau) + 5.0 * noise(z0.shape, rng, [(9, 1.0), (4, 0.6), (2, 0.3)]) * plateau)
rocks = []
for _ in range(400):
    if len(rocks) == 7:
        break
    p, t = rng.uniform(-420, -60), rng.uniform(-330, 330)
    q = at(p, t)
    iy, ix = int(round(q[1] / CELL + B)), int(round(q[0] / CELL + B))
    if d[iy, ix] < 90 or edge[iy, ix] < 70 or flat[iy, ix] < 0.99 or any(np.hypot(*(q - o[:2])) < 170 for o in rocks):
        continue
    rocks.append((q[0], q[1], rng.uniform(45, 85), rng.uniform(14, 28)))
for x, y, rad, h in rocks:
    rr = np.hypot((X - x) / rad, (Y - y) / (rad * rng.uniform(0.6, 1.0)))
    z += h * smooth_profile((1 - rr) / 0.6) * (1 + 0.25 * wn)

# Into the map: replace within 450 of the old and new island, fading out by 650; mainland ground further away is never touched.
both = old | (d > 0)
dist = ndimage.distance_transform_edt(~both) * CELL
wr = smooth_profile((650 - dist) / 200) * ((z0 < WATER - 1) | ndimage.binary_dilation(old, iterations=3))
z = wr * z + (1 - wr) * z0
# Shelf bumps just under the surface away from the shore showed as foam patches in the water.
dland = ndimage.distance_transform_edt(z <= WATER) * CELL
z = np.where((z < WATER) & (z > WATER - 3) & (dland > 30), WATER - 3, z)
z = np.clip(np.round(z / SNAP) * SNAP, 0, RAW_LIMIT * 0.625)
changed = wr > 0.001

gy, gx = np.gradient(z, CELL)
slope_v = np.hypot(gx, gy)
cz = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
cs = np.maximum.reduce([slope_v[:-1, :-1], slope_v[1:, :-1], slope_v[:-1, 1:], slope_v[1:, 1:]])
cchanged = changed[:-1, :-1] | changed[1:, 1:]
imp = cchanged & (cs >= 0.4)
land = (z > WATER)[:-1, :-1]

def png16(a, path):
    Image.fromarray((np.clip(a / 0.625 / RAW_LIMIT, 0, 1) * 65535).astype(np.uint16)[::-1]).save(path)
def mask(m, path):
    Image.fromarray((m.astype(np.uint8) * 255)[::-1]).save(path)
png16(z, os.path.join(out, "heights.png"))
mask(imp, os.path.join(out, "impassable.png"))
mask(cchanged & ~imp, os.path.join(out, "passable.png"))
g = np.clip(z / 140, 0, 1)
rgb = np.stack([g * 255] * 3, -1).astype(np.uint8)
rgb[z < WATER] = (rgb[z < WATER] * np.array([0.4, 0.6, 1.0])).astype(np.uint8)
rgb[:-1, :-1][imp] = [200, 80, 40]
for p, t, rad in SPOTS.values():
    q = at(p, t)
    iy, ix = int(q[1] / CELL + B), int(q[0] / CELL + B)
    rgb[iy - 2:iy + 3, ix - 2:ix + 3] = [255, 255, 0]
crop = rgb[int(2000 / CELL + B):int(3300 / CELL + B), int(150 / CELL + B):int(1450 / CELL + B)][::-1]
Image.fromarray(crop).resize((crop.shape[1] * 4, crop.shape[0] * 4), Image.NEAREST).save(os.path.join(out, "preview.png"))
pi = np.argwhere(land & cchanged)
iland = land & cchanged
print("base seabed", round(BASE, 1), "island cells", int(iland.sum()), "impassable share on the island", round(float((imp & iland).sum() / iland.sum()), 2),
      "beach length (cells)", int((beach[:-1, :-1] > 0.5)[iland & ~ndimage.binary_erosion(iland)].sum()),
      "rocks", [(round(x), round(y)) for x, y, *_ in rocks])
for k, (p, t, rad) in SPOTS.items():
    q = at(p, t)
    iy, ix = int(round(q[1] / CELL + B)), int(round(q[0] / CELL + B))
    print(k, q.round(), "z", round(z[iy, ix], 1), "slope p90 within", rad,
          round(float(np.percentile(slope_v[np.hypot(X - q[0], Y - q[1]) < rad], 90)), 3))
json.dump({"spots": {k: list(at(p, t).round(1)) + [rad] for k, (p, t, rad) in SPOTS.items()}, "rocks": rocks, "base": BASE,
           "beach_dir": list(db), "low": LOW, "high": HIGH}, open(os.path.join(out, "island.json"), "w"), indent=1)

# Textures, per cell. Cliffs: large rock on the tall ones, medium elsewhere. Plateau: light sand with grass patches; terrace: grassy
# sand; beach: sand (a thin wet-sand strip at the water left hard stepped edges). On the plateau rough ground is rock too. Under water the old island's ground gives way to the seabed sand (painted blue later).
cp = plateau[:-1, :-1]
cbeach = beach[:-1, :-1]
cd = d[:-1, :-1]
patch = noise(cz.shape, rng, [(6, 1.0), (14, 0.6)])
tex = {}
tex["SandLargeType2"] = cchanged & (cz < WATER - 2)
tex["SandLargeType2Grassy"] = cchanged & (cz >= WATER - 2) & (cp < 0.5)
tex["SandLargeType3Light"] = cchanged & (cz >= WATER - 2) & (cp >= 0.5)
tex["GrassMediumType21"] = cchanged & (cz >= WATER + 4) & (patch > 0.6) & (cs < 0.3)
tex["SandType3"] = cchanged & (cbeach > 0.5) & (cd > -40) & (cd < BEACH_IN * 0.7)
steep = cchanged & ((cs >= 0.35) | ((cs >= 0.22) & (cp > 0.5) & (patch < -0.3)))
tall = ndimage.maximum_filter(cz, 9) - ndimage.minimum_filter(cz, 9) > 55
tex["CliffMediumType15b"] = steep & ~tall
tex["CliffLargeType3"] = steep & tall
for k, m in tex.items():
    mask(m, os.path.join(out, "tex_" + k + ".png"))
if "--save" not in sys.argv:
    sys.exit(0)

from wb_bridge import WorldBuilderBridge
import outpost_tool
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
info = wb.call("map.open", path=map_path, discard_changes=True)
assert abs(info["heightmap"]["height_map_scale"] - 1.2) < 0.01, info["heightmap"]
# Objects on the old and new island and its shores go; the rest stays.
objs = wb.call("objects.list", include_waypoints_and_roads=True, limit=20000)["objects"]
gone = []
for o in objs:
    ix, iy = int(round(o["x"] / CELL + B)), int(round(o["y"] / CELL + B))
    if o["kind"] == "object" and 0 <= ix < nx and 0 <= iy < ny and (dist[iy, ix] < 60):
        gone.append(o["id"])
if gone:
    wb.call("objects.delete", ids=gone)
print("removed", len(gone), "objects", flush=True)
wb.call("terrain.import_heightmap", path=os.path.join(out, "heights.png"), fit="exact", range=[0, RAW_LIMIT])
wb.call("terrain.import_mask", path=os.path.join(out, "passable.png"), target="passability", fit="exact", impassable=False)
wb.call("terrain.import_mask", path=os.path.join(out, "impassable.png"), target="passability", fit="exact")
for k in tex:
    wb.call("terrain.import_mask", path=os.path.join(out, "tex_" + k + ".png"), target="texture", texture=k, fit="exact")
print("terrain imported", flush=True)

OWNER = "teamPlyrCivilian"
q = at(*SPOTS["refinery"][:2])
wb.call("objects.place", template="TechOilRefinery", x=round(q[0], 1), y=round(q[1], 1), angle_deg=round(math.degrees(math.atan2(db[1], db[0])), 1), owner=OWNER)
placed = {"refinery": [round(q[0]), round(q[1])]}
for name, kit in (("supply", "beach_063"), ("huts_low", "winding_136"), ("huts_high", "beach_065")):
    q = at(*SPOTS[name][:2])
    try:
        res = outpost_tool.place(wb, x=round(q[0], 1), y=round(q[1], 1), kit=kit, seed=seed, clear_nature=True)
        placed[name] = [kit, len(res.get("placed", res.get("objects", [])))]
    except Exception as e:
        placed[name] = "FAILED " + str(e).splitlines()[0][:100]
print("placed", placed, flush=True)

# Palms on the terrace and the top of the beach, a few windblown ones on the plateau; none on cliffs or object spots.
listed = wb.call("objects.list", include_waypoints_and_roads=False, limit=20000)["objects"]
taken = [(o["x"], o["y"]) for o in listed if math.hypot(o["x"] - C[0], o["y"] - C[1]) < 700]
palms = ["TreePalm1", "TreePalm2", "TreePalm03", "TreePalm1short", "TreePalm2short"]
n = 0
for _ in range(3000):
    if n >= 34:
        break
    x, y = C[0] + rng.uniform(-450, 450), C[1] + rng.uniform(-500, 500)
    iy, ix = int(round(y / CELL + B)), int(round(x / CELL + B))
    if d[iy, ix] < 30 or z[iy, ix] < WATER + 2 or slope_v[iy, ix] > 0.18 or flat[iy, ix] < 0.95:
        continue
    if plateau[iy, ix] > 0.5 and rng.random() > 0.3:
        continue
    if any(math.hypot(x - a_, y - b_) < 45 for a_, b_ in taken):
        continue
    wb.call("objects.place", template=palms[rng.integers(len(palms))], x=round(x, 1), y=round(y, 1), angle_deg=round(float(rng.uniform(0, 360)), 1))
    taken.append((x, y))
    n += 1
print("palms", n, flush=True)
wb.call("terrain.reblend")
wb.call("terrain.cliff_map", repair="fit")
wb.call("map.save", path=map_path)
print("saved", map_path)
wb.close()
