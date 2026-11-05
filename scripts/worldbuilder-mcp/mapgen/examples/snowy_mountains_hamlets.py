# Rural hamlets of SnowyMountainsGen: a farm (farmhouse, barn, shed) in a picket-fenced yard, two chalets, a cottage and a windmill along
# one dirt lane, on the south end of the west terrace; the east terrace gets the same turned by 180 degrees, so both players have the same
# garrisons. Each building goes to the nearest spot to its planned place where its footprint and its turned twin lie on flat, passable
# ground inside the site, clear of the lane, the expansion's supply piles and derrick, and the other buildings.
# Usage: snowy_mountains_hamlets.py <work dir (mapgen_work)>   Writes hamlets.json (objects for objects_place_many, lanes, pads) and hamlets.png.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

work = sys.argv[1]
CELL, B = 10.0, 40
C = np.array([1495.0, 1795.0])
z = np.load(os.path.join(work, "heights_world.npy")).astype(np.float64)
m = np.array(Image.open(os.path.join(work, "roads_passability.png")).convert("L"))[::-1] > 127
imp = np.zeros(z.shape, bool)
imp[:m.shape[0], :m.shape[1]] = m
gy, gx = np.gradient(z, CELL)
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * CELL, (yy - B) * CELL
SITE = [(950, 360), (770, 380), (600, 610), (760, 1080), (860, 1210), (980, 910)]


def inside(poly, px, py):
    res = np.zeros(px.shape, bool)
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        cross = (ay > py) != (by > py)
        at = (bx - ax) * (py - ay) / (by - ay + 1e-9) + ax
        res ^= cross & (px < at)
    return res


def segdist(px, py, pts):
    best = np.full(px.shape, 1e9)
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        L = (bx - ax) ** 2 + (by - ay) ** 2
        t = np.clip(((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / L, 0, 1)
        best = np.minimum(best, np.hypot(px - ax - t * (bx - ax), py - ay - t * (by - ay)))
    return best


LANE = [(910, 860), (850, 750), (790, 650), (730, 545)]
TRACK = [(1650, 790), (1400, 740), (1200, 790), (1070, 790), (910, 860), (780, 950)]
KEEP = [(610, 875), (740, 865), (670, 820), (680, 1250)]   # supply piles and the derrick

ok = inside(SITE, X, Y) & ~ndimage.binary_dilation(imp, iterations=3) & (ndimage.maximum_filter(np.hypot(gx, gy), 3) < 0.2)
ok &= segdist(X, Y, LANE) > 32
ok &= segdist(X, Y, TRACK) > 40
for kx, ky in KEEP:
    ok &= np.hypot(X - kx, Y - ky) > 75
# The east terrace: the same cells turned by 180 degrees about the centre must be free too (the vertex grid's centre is the map's).
free = ok.copy()
east_ok = ~ndimage.binary_dilation(imp, iterations=3) & (ndimage.maximum_filter(np.hypot(gx, gy), 3) < 0.2)
free &= east_ok[::-1, ::-1]
used = np.zeros(z.shape, bool)

lane_dir = math.degrees(math.atan2(LANE[-1][1] - LANE[0][1], LANE[-1][0] - LANE[0][0]))
# (template, planned x, y, half sizes (major, minor), angle)
PLAN = [
    ("FarmHouse", 880, 650, (25, 16), lane_dir),
    ("BarnCoop", 935, 560, (26, 34), lane_dir),
    ("BarnShed", 940, 720, (22, 26), lane_dir + 90),
    ("Chalet", 720, 720, (25, 55), lane_dir),
    ("Chalet2", 670, 610, (25, 55), lane_dir),
    ("GermanCotage01", 820, 470, (39, 37), lane_dir + 90),
    ("GermanWindMill01", 680, 480, (15, 29), 20),
]


def rect(cx, cy, hw, hh, ang, pad):
    a = math.radians(ang)
    u = (X - cx) * math.cos(a) + (Y - cy) * math.sin(a)
    v = -(X - cx) * math.sin(a) + (Y - cy) * math.cos(a)
    return (np.abs(u) <= hw + pad) & (np.abs(v) <= hh + pad)


placed = []
for t, px, py, (hw, hh), ang in PLAN:
    best = None
    for d in range(0, 260, 10):
        for k in range(max(1, int(2 * math.pi * d / 10))):
            a = 2 * math.pi * k / max(1, int(2 * math.pi * d / 10))
            cx, cy = px + d * math.cos(a), py + d * math.sin(a)
            r = rect(cx, cy, hw, hh, ang, 8)
            if r.any() and free[r].all() and not used[rect(cx, cy, hw, hh, ang, 18)].any():
                best = (cx, cy)
                break
        if best:
            break
    if not best:
        print("no room for", t)
        continue
    used |= rect(best[0], best[1], hw, hh, ang, 0)
    placed.append({"template": t, "x": round(best[0], 1), "y": round(best[1], 1), "angle_deg": round(ang % 360, 1), "half": [hw, hh],
                   "moved": round(math.hypot(best[0] - px, best[1] - py))})

# Picket fence around the farm buildings' yard, open toward the lane; pieces at the INI's FenceWidth, centred on their position.
FENCE_W = 32.75
farm = [p for p in placed if p["template"] in ("FarmHouse", "BarnCoop", "BarnShed")]
props = []
if farm:
    a = math.radians(lane_dir)
    ux, uy, vx, vy = math.cos(a), math.sin(a), -math.sin(a), math.cos(a)
    cx = np.mean([p["x"] for p in farm]); cy = np.mean([p["y"] for p in farm])
    us = [(p["x"] - cx) * ux + (p["y"] - cy) * uy for p in farm]
    vs = [(p["x"] - cx) * vx + (p["y"] - cy) * vy for p in farm]
    margin = 28
    u0, u1 = min(us) - margin, max(us) + margin
    v0, v1 = min(vs) - margin, max(vs) + margin
    # Which side faces the lane: the side whose middle is nearer to it.
    side_lane = min((v0, v1), key=lambda v: segdist(np.array([cx + v * vx]), np.array([cy + v * vy]), LANE)[0])
    far = v1 if side_lane == v0 else v0
    corners = [(u0, side_lane), (u0, far), (u1, far), (u1, side_lane)]
    pts = [(cx + u * ux + v * vx, cy + u * uy + v * vy) for u, v in corners]
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        L = math.hypot(bx - ax, by - ay)
        n = max(1, round(L / FENCE_W))
        dx, dy = (bx - ax) / L, (by - ay) / L
        deg = math.degrees(math.atan2(dy, dx))
        for k in range(n):
            f = (k + 0.5) * FENCE_W
            props.append({"template": "PicketFence01", "x": round(ax + dx * f, 1), "y": round(ay + dy * f, 1), "angle_deg": round(deg % 360, 1)})
    barn = next((p for p in farm if p["template"] == "BarnCoop"), farm[0])
    for k, (du, dv) in enumerate(((0, 50), (12, 56), (-10, 58))):
        props.append({"template": "Barrel", "x": round(barn["x"] + du * vx + dv * ux, 1), "y": round(barn["y"] + du * vy + dv * uy, 1), "angle_deg": 15.0 * k})
for p in placed:
    if p["template"].startswith("Chalet"):
        # Behind the chalet, beyond its short side.
        a = math.radians(p["angle_deg"])
        props.append({"template": "Outhouse01", "x": round(p["x"] - 75 * math.cos(a), 1), "y": round(p["y"] - 75 * math.sin(a), 1), "angle_deg": p["angle_deg"]})
# Props only where they and their turned twin stand on free ground and off the buildings.
def prop_ok(o):
    i, j = int(round(o["y"] / CELL + B)), int(round(o["x"] / CELL + B))
    return free[i, j] or (o["template"] == "PicketFence01" and ok[i, j] and east_ok[::-1, ::-1][i, j] and segdist(np.array([o["x"]]), np.array([o["y"]]), LANE)[0] > 32)
blocked = ndimage.binary_dilation(used, iterations=1)
props = [o for o in props if prop_ok(o) and not blocked[int(round(o["y"] / CELL + B)), int(round(o["x"] / CELL + B))]]


def turned(o):
    return dict(o, x=round(2 * C[0] - o["x"], 1), y=round(2 * C[1] - o["y"], 1), angle_deg=round((o["angle_deg"] + 180) % 360, 1))


west = [dict(p, owner="teamPlyrCivilian") for p in placed] + props
objs = west + [turned(o) for o in west]
lanes = [LANE, [(round(2 * C[0] - x, 1), round(2 * C[1] - y, 1)) for x, y in LANE]]
json.dump({"objects": [{k: o[k] for k in ("template", "x", "y", "angle_deg") if k in o} | ({"owner": o["owner"]} if "owner" in o else {}) for o in objs],
           "buildings": placed, "lanes": lanes}, open(os.path.join(work, "hamlets.json"), "w"), indent=1)
print("buildings", [(p["template"], p["x"], p["y"], p["moved"]) for p in placed])
print("props", len(props), "per side; objects in all", len(objs))

k = 3
x0, y0 = 450, 300
img = Image.new("RGB", (700 // 1 * k // 3 * 3, 1000 * k // 3 * 3 // 3 * 1), "white")
im = Image.fromarray((np.stack([free, ok, imp], -1) * 255).astype(np.uint8)[::-1]).resize((z.shape[1] * k, z.shape[0] * k), Image.NEAREST)
d = ImageDraw.Draw(im)
P = lambda x, y: ((x / CELL + B) * k, (z.shape[0] - 1 - (y / CELL + B)) * k)
for p in placed:
    a = math.radians(p["angle_deg"]); hw, hh = p["half"]
    cs = [(p["x"] + su * hw * math.cos(a) - sv * hh * math.sin(a), p["y"] + su * hw * math.sin(a) + sv * hh * math.cos(a)) for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    d.polygon([P(*c) for c in cs], outline=(255, 0, 0))
for o in props:
    x, y = P(o["x"], o["y"]); d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 0, 255))
d.line([P(*q) for q in LANE], fill=(255, 128, 0), width=3)
im.crop((int((B + 40) * k), int((z.shape[0] - 1 - (B + 140)) * k), int((B + 110) * k), int((z.shape[0] - 1 - (B + 20)) * k))).save(os.path.join(work, "hamlets.png"))
