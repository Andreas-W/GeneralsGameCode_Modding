# TeamIslandsGen: the user arranged two player lobes by hand (bottom left and bottom centre: grass hut outposts on grass patches, Tech,
# supplies, the start); this copies that arrangement, mirrored, onto the other lobes. Bottom left goes to bottom right (mirror x), top
# left (mirror y) and top right (both); bottom centre to top centre (mirror y). Copied: every object but plants, the texture cells the
# user changed (against the painted masks), the heights the user changed (against the build), the plants inside those patches, and the
# start waypoints (Player_3 to Player_6). In the target lobes the generated Tech and supplies go, and plants where copies land. The rest
# of each lobe keeps its own textures (painted without mirroring).
# Usage: team_islands_mirror_lobes.py <scan dir (ref_scan of the edited map, with all_objects.json)> <map path> <work dir> [--save]
import collections, json, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, HERE)
scan, map_path, work = sys.argv[1:4]
CELL, B, WATER = 10.0, 30, 60.0
C = np.array([2500.0, 3000.0])
PLANTS = ("Tree", "Bush")
# Lobes as offsets from the centre: the centre (lagoons, necks, the Tech there) belongs to none.
def lobe(u, v):
    if abs(u) <= 650 and abs(v) > 950:
        return "S" if v < 0 else "N"
    if abs(u) > 650 and abs(v) > 250:
        return ("S" if v < 0 else "N") + ("W" if u < 0 else "E")
    return None
# Source lobe -> (target lobe, mirror x, mirror y); start numbers for the targets.
COPIES = {"SW": [("SE", -1, 1), ("NW", 1, -1), ("NE", -1, -1)], "S": [("N", 1, -1)]}
STARTS = {"SE": 3, "NE": 4, "N": 5, "NW": 6}

info = json.load(open(os.path.join(scan, "info.json")))
names = info["textures"]
tex = np.load(os.path.join(scan, "textures.npy"))
z = np.load(os.path.join(scan, "heights.npy")).astype(np.float64)
z0 = np.load(os.path.join(os.path.dirname(map_path), "mapgen_work", "build", "heights_world.npy")).astype(np.float64)
objs = json.load(open(os.path.join(scan, "all_objects.json")))
ny, nx = z.shape
cur = np.array(names, object)[tex]
expected = np.full(tex.shape, "", object)
for m in json.load(open(os.path.join(os.path.dirname(map_path), "mapgen_work", "stageB", "masks.json")))["masks"]:
    a = np.array(Image.open(m["path"]).convert("L"))[::-1] > 127
    expected[:a.shape[0], :a.shape[1]][a] = m["texture"]

# Per cell / vertex: offsets from the centre and the lobe.
cy, cx = np.indices(tex.shape)
CU, CV = (cx - B + 0.5) * CELL - C[0], (cy - B + 0.5) * CELL - C[1]
clobe = np.vectorize(lobe, otypes=[object])(CU, CV)
vy, vx = np.indices(z.shape)
VU, VV = (vx - B) * CELL - C[0], (vy - B) * CELL - C[1]
vlobe = np.vectorize(lobe, otypes=[object])(VU, VV)
zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
painted = (cur != expected) & (zc > WATER + 0.5)
lifted = np.abs(z - z0) > 0.3


def cell_of(u, v):
    return int(math.floor((u + C[0]) / CELL + B)), int(math.floor((v + C[1]) / CELL + B))


new_tex = {}            # (iy, ix) -> texture
new_z = {}              # (iy, ix) vertex -> world height
patch_cells = collections.defaultdict(set)  # target lobe -> cells (for clearing plants)
places, starts = [], []
for src, targets in COPIES.items():
    src_cells = painted & (clobe == src)
    src_patch = ndimage.binary_dilation(src_cells, iterations=3) & (clobe == src)
    src_verts = lifted & (vlobe == src)
    for tgt, mx, my in targets:
        for iy, ix in np.argwhere(src_cells):
            jx, jy = cell_of(mx * CU[iy, ix], my * CV[iy, ix])
            new_tex[(jy, jx)] = cur[iy, ix]
        for iy, ix in np.argwhere(src_patch):
            patch_cells[tgt].add(cell_of(mx * CU[iy, ix], my * CV[iy, ix])[::-1])
        for iy, ix in np.argwhere(src_verts):
            jx, jy = int(round((mx * VU[iy, ix] + C[0]) / CELL + B)), int(round((my * VV[iy, ix] + C[1]) / CELL + B))
            new_z[(jy, jx)] = z[iy, ix]
        for o in objs:
            u, v = o["x"] - C[0], o["y"] - C[1]
            if lobe(u, v) != src:
                continue
            plant = o["kind"] == "object" and o["template"].startswith(PLANTS)
            if plant:
                ix, iy = cell_of(u, v)
                if not src_patch[iy, ix]:
                    continue
            a = math.radians(o.get("angle_deg", 0))
            ang = math.degrees(math.atan2(my * math.sin(a), mx * math.cos(a))) % 360
            x, y = round(C[0] + mx * u, 1), round(C[1] + my * v, 1)
            if o["kind"] == "waypoint":
                if "Start" in (o.get("waypoint_name") or ""):
                    starts.append({"name": f"Player_{STARTS[tgt]}_Start", "x": x, "y": y})
                continue
            if o["kind"] != "object":
                continue
            places.append({"template": o["template"], "x": x, "y": y, "angle_deg": round(ang, 1), "owner": o.get("owner", "team"),
                           "lobe": tgt, "plant": plant})

# What goes in the targets: everything but plants, and plants on the copied patches or under a copied object.
targets = {t for ts in COPIES.values() for t, _, _ in ts}
gone = []
for o in objs:
    if o["kind"] != "object":
        continue
    u, v = o["x"] - C[0], o["y"] - C[1]
    t = lobe(u, v)
    if t not in targets:
        continue
    plant = o["template"].startswith(PLANTS)
    ix, iy = cell_of(u, v)
    if not plant or (iy, ix) in patch_cells[t] or any(math.hypot(o["x"] - p["x"], o["y"] - p["y"]) < 40 for p in places if p["lobe"] == t and not p["plant"]):
        gone.append((o["template"], round(o["x"], 1), round(o["y"], 1)))
print("texture cells", len(new_tex), "height vertices", len(new_z), "objects to place", len(places),
      collections.Counter((p["lobe"], p["plant"]) for p in places), "to delete", len(gone), "starts", starts, flush=True)

os.makedirs(work, exist_ok=True)
by_tex = collections.defaultdict(list)
for k, t in new_tex.items():
    by_tex[t].append(k)
masks = []
for t, cells in by_tex.items():
    m = np.zeros(tex.shape, np.uint8)
    for iy, ix in cells:
        if 0 <= iy < tex.shape[0] and 0 <= ix < tex.shape[1]:
            m[iy, ix] = 255
    p = os.path.join(work, "mirror_" + t + ".png")
    Image.fromarray(m[::-1]).save(p)
    masks.append((t, p, len(cells)))
if "--save" not in sys.argv:
    sys.exit(0)

from wb_bridge import WorldBuilderBridge
wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=map_path, discard_changes=True)
# Object ids belong to one WorldBuilder session: the ones to delete are found again by template and position.
now = wb.call("objects.list", limit=20000)["objects"]
key = {(o["template"], round(o["x"], 1), round(o["y"], 1)): o["id"] for o in now}
ids = [key[g] for g in gone if g in key]
assert len(ids) == len(gone), (len(ids), len(gone))
if ids:
    wb.call("objects.delete", ids=ids)
if new_z:
    ks = np.array(list(new_z))
    y0, x0 = ks.min(0)
    y1, x1 = ks.max(0)
    rows = [[None] * (x1 - x0 + 1) for _ in range(y1 - y0 + 1)]
    for (iy, ix), h in new_z.items():
        rows[iy - y0][ix - x0] = int(round(h / 0.625))
    wb.call("terrain.set_heights", x0=int(x0), y0=int(y0), heights=rows)
for t, p, n in sorted(masks, key=lambda m: -m[2]):
    wb.call("terrain.import_mask", path=p, target="texture", texture=t, fit="exact")
for p in places:
    wb.call("objects.place", template=p["template"], x=p["x"], y=p["y"], angle_deg=p["angle_deg"], owner=p["owner"])
for s in starts:
    wb.call("waypoints.add", x=s["x"], y=s["y"], name=s["name"])
# Blends again where textures changed, lobe by lobe.
for t in targets:
    cells = [k for k in new_tex if lobe((k[1] - B + 0.5) * CELL - C[0], (k[0] - B + 0.5) * CELL - C[1]) == t]
    if cells:
        a = np.array(cells)
        wb.call("terrain.reblend", shape="rect", x0=float((a[:, 1].min() - B - 3) * CELL), y0=float((a[:, 0].min() - B - 3) * CELL),
                x1=float((a[:, 1].max() - B + 4) * CELL), y1=float((a[:, 0].max() - B + 4) * CELL))
wb.call("map.save", path=map_path)
print("saved", map_path, wb.call("map.info")["counts"])
wb.close()
