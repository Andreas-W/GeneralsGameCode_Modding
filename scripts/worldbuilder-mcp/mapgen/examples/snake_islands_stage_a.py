# Stage A of SnakeIslandsGen (4 players, naval, tropical): the terrain from the user's template (Templates\4PSnakeIslands.map). The
# template is five smooth brush blobs on a sea at 1.25 (water 60): a south and a north island, each with a lowland in the west (62.5)
# and a plateau in the east (125), a low west island in the middle, a high east island with a low foot, joined by three drawbridges
# (decks at the lowland, about 3 above the water) and two wooden bridges (plateau to plateau). TempBuildingSize markers are the starts.
# - irregular coasts: every island's outline is pushed in and out by noise at two scales (plan coast.warp: [sigma cells, amplitude
#   world units]), the plateau outlines too (plateau_warp); calm around bridge ends, islands never closer than coast.min_channel;
# - levels: lowlands (and the template's knobs and the east island's foot) 68, plateaus 120. Stage A makes lowland shores beaches and
#   plateau shores cliffs;
# - ramps: road hints across the plateau step, two on each big island, one on the east island from its drawbridge foot;
# - three islets in open sea, two sandy and one rock stack;
# - after stage A: each drawbridge gets a quay at both ends (flat at the deck height from 120 out along its axis, a short wall down to
#   the channel) and a channel at least 18 deep under it, so its leaves can open for ships.
# Writes mock/ (heights_new.npy, mock.json, passability.png, layout.json: starts, drawbridges, wooden bridges) and stageA/.
# Usage: snake_islands_stage_a.py <plan.json> <template dir (heights_template.npy, mock.json)> <work dir>
import json, math, os, subprocess, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, src, work = sys.argv[1:4]
plan = json.load(open(plan_path))
CELL, RAW = 10.0, 0.625
B = plan["border"]
WATER = plan["water"]["height"]
LOW, HIGH = plan["levels"][0]["z"], plan["levels"][1]["z"]
DRAW_HALF = 142.5  # IndustrialDrawBridge_New_Large is 285 long (town/work/gamedata/geo.json)
coast = plan["coast"]
rng = np.random.default_rng(plan.get("seed", 1))

z = np.load(os.path.join(src, "heights_template.npy")).astype(np.float64) * RAW
ny, nx = z.shape
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * CELL, (yy - B) * CELL
FLOOR = float(np.median(z[z < 30]))
objs = json.load(open(os.path.join(src, "mock.json")))["objects"]["objects"]


def smoothstep(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def field(sigma):
    n = ndimage.gaussian_filter(rng.standard_normal(z.shape), sigma)
    return n / (n.std() + 1e-9)


def cell(x, y):
    return int(round(y / CELL + B)), int(round(x / CELL + B))


# Starts, counterclockwise from the south-west.
marks = [o for o in objs if o["template"] == "TempBuildingSize"]
cx0, cy0 = np.mean([o["x"] for o in marks]), np.mean([o["y"] for o in marks])
marks.sort(key=lambda o: (math.degrees(math.atan2(o["y"] - cy0, o["x"] - cx0)) - 225) % 360)
starts = {f"Player_{k + 1}_Start": (o["x"], o["y"]) for k, o in enumerate(marks)}
draws = [o for o in objs if o["template"].startswith("IndustrialDrawBridge")]
wood = [o for o in objs if o["kind"] == "bridge_point"]
wood = list(zip(wood[0::2], wood[1::2]))
ends = []
for o in draws:
    a = math.radians(o["angle_deg"])
    ends += [(o["x"] + s * DRAW_HALF * math.cos(a), o["y"] + s * DRAW_HALF * math.sin(a)) for s in (-1, 1)]
ends += [(p["x"], p["y"]) for pair in wood for p in pair]
calm = np.ones(z.shape)
for ex, ey in ends:
    calm *= smoothstep(np.hypot(X - ex, Y - ey) / coast["calm_bridges"])

# Islands: each outline warped on its own; never closer to another island than min_channel.
land0 = z > 30
lab0, n_isl = ndimage.label(land0)
_, (iy, ix) = ndimage.distance_transform_edt(~land0, return_indices=True)
owner = lab0[iy, ix]
warp = sum(amp * field(sig) for sig, amp in coast["warp"]) * calm
land = np.zeros(z.shape, bool)
for i in range(1, n_isl + 1):
    m = ndimage.gaussian_filter((lab0 == i).astype(float), 1.5) > 0.5
    sd = (ndimage.distance_transform_edt(m) - ndimage.distance_transform_edt(~m)) * CELL
    others = ndimage.distance_transform_edt(owner == i) * CELL  # distance to another island's side of the channel
    land |= ((sd + warp > 0) & (others >= coast["min_channel"] / 2)) | (lab0 == i)
land = ndimage.binary_fill_holes(ndimage.binary_opening(land, iterations=2))
lab, n = ndimage.label(land)
sizes = ndimage.sum(land, lab, range(1, n + 1))
land = np.isin(lab, [k + 1 for k, s in enumerate(sizes) if s >= 200])

# Plateaus: every land cell takes the level of the nearer core (template plateau above 95, lowland 30 to 66; the brush blur between
# them is neither), with the boundary warped. Thresholding the blur instead left the plateaus a thin lowland ring at the coast.
d_high = ndimage.distance_transform_edt(~(z > 95)) * CELL
# The blur also leaves a thin 30 to 66 band along the plateaus' coasts: only wide lowland counts.
d_low = ndimage.distance_transform_edt(~ndimage.binary_opening((z > 30) & (z < 66), iterations=6)) * CELL
pwarp = sum(amp * field(sig) for sig, amp in coast["plateau_warp"])
high = (d_low - d_high + pwarp * calm > 0) & land
for o in draws:
    a = math.radians(o["angle_deg"])
    for s in (-1, 1):
        high &= np.hypot(X - o["x"] - s * DRAW_HALF * math.cos(a), Y - o["y"] - s * DRAW_HALF * math.sin(a)) > 200
for name, (sx, sy) in starts.items():
    r = np.hypot(X - sx, Y - sy) < 260
    high = (high | r) & land if z[cell(sx, sy)] > 95 else high & ~r
high = ndimage.binary_opening(high, iterations=2)

# Islets in open sea between the islands: clear of land and bridges, away from the map edge (the farthest water was the corners).
sea_d = ndimage.distance_transform_edt(~land) * CELL
inside = (X > 700) & (X < plan["playable_max"] - 700) & (Y > 700) & (Y < plan["playable_max_y"] - 700)
for (ax, ay), (bx, by) in [((p["x"], p["y"]), (q["x"], q["y"])) for p, q in wood] + [(ends[2 * k], ends[2 * k + 1]) for k in range(len(draws))]:
    t = np.clip(((X - ax) * (bx - ax) + (Y - ay) * (by - ay)) / ((bx - ax) ** 2 + (by - ay) ** 2), 0, 1)
    inside &= np.hypot(X - ax - t * (bx - ax), Y - ay - t * (by - ay)) > 450
islets = []
for k, (r, level) in enumerate([(110, LOW), (90, LOW), (65, HIGH)]):
    score = np.where(inside & (sea_d > 330 + r), sea_d + 120 * field(30), -1)
    for jx, jy in islets:
        score = np.where(np.hypot(X - jx, Y - jy) < 1100, -1, score)
    i, j = np.unravel_index(np.argmax(score), score.shape)
    if score[i, j] < 0:
        break
    jx, jy = X[i, j], Y[i, j]
    islets.append((jx, jy))
    m = np.hypot(X - jx, Y - jy) - r + 0.35 * r * field(6) < 0
    land |= m
    if level == HIGH:
        high |= m
print(json.dumps({"islets": [[round(a), round(b)] for a, b in islets]}))

# Ramps as road hints across the plateau step (a pair of road points crossing it).
lowland = land & ~high
step = high & ndimage.binary_dilation(lowland, iterations=1)
d_sea = ndimage.distance_transform_edt(land) * CELL
sdh = ndimage.gaussian_filter((ndimage.distance_transform_edt(high) - ndimage.distance_transform_edt(~high)) * CELL, 3)
ghy, ghx = np.gradient(sdh)
lab, n = ndimage.label(land)
roads = []


def ramp_at(i, j):
    nrm = math.hypot(ghx[i, j], ghy[i, j]) + 1e-9
    ux, uy = ghx[i, j] / nrm, ghy[i, j] / nrm  # into the plateau
    x, y = X[i, j], Y[i, j]
    roads.extend([{"kind": "road_point", "template": "hint", "x": x - 110 * ux, "y": y - 110 * uy},
                  {"kind": "road_point", "template": "hint", "x": x + 110 * ux, "y": y + 110 * uy}])
    return [round(x), round(y)]


ramps = []
far_start = np.ones(z.shape, bool)
for sx, sy in starts.values():
    far_start &= np.hypot(X - sx, Y - sy) > 380
for k in range(1, n + 1):
    has_start = any(lab[cell(*s)] == k for s in starts.values())
    cand = np.argwhere(step & (lab == k) & (d_sea > (220 if has_start else 120)) & far_start)
    if len(cand) < 10 or not (high & (lab == k)).sum() > 3000:
        continue
    if has_start:
        # Two ramps, far apart along the step.
        pts = cand.astype(float)
        c = pts.mean(0)
        u = np.linalg.svd(pts - c, full_matrices=False)[2][0]
        proj = (pts - c) @ u
        for q in (0.15, 0.85):
            i, j = cand[np.argmin(abs(proj - np.quantile(proj, q)))]
            ramps.append(ramp_at(i, j))
    else:
        # The east island: from its drawbridge foot up onto the plateau.
        fx, fy = min(ends[:2 * len(draws)], key=lambda e: lab[cell(*e)] != k)
        dd = np.hypot(X[cand[:, 0], cand[:, 1]] - fx, Y[cand[:, 0], cand[:, 1]] - fy)
        i, j = cand[np.argmin(np.where(dd > 250, dd, 1e9))]
        ramps.append(ramp_at(i, j))
print(json.dumps({"ramps": ramps}))

zm = np.where(land, np.where(high, HIGH, LOW), np.minimum(z, FLOOR + 0.5))
out = os.path.join(work, "mock")
os.makedirs(out, exist_ok=True)
np.save(os.path.join(out, "heights_new.npy"), np.clip(np.round(zm / RAW), 0, 255 * plan["height_scale"]).astype(np.float32))
Image.fromarray(np.zeros((ny - 1, nx - 1), np.uint8)).save(os.path.join(out, "passability.png"))
mock = [{"kind": "waypoint", "template": "Waypoint", "waypoint_name": k, "x": x, "y": y} for k, (x, y) in starts.items()]
mock += [{"kind": "waypoint", "template": "Waypoint", "waypoint_name": f"BridgeEnd{k}", "x": x, "y": y} for k, (x, y) in enumerate(ends[:2 * len(draws)])]
mock += [{"kind": "bridge_point", "template": p["template"], "x": p["x"], "y": p["y"]} for pair in wood for p in pair]
mock += roads
json.dump({"objects": {"objects": mock}}, open(os.path.join(out, "mock.json"), "w"), indent=1)
layout = {"starts": starts, "drawbridges": [{"template": o["template"], "x": o["x"], "y": o["y"], "angle_deg": o["angle_deg"], "deck": LOW} for o in draws],
          "wooden": [{"template": p["template"], "points": [[p["x"], p["y"]], [q["x"], q["y"]]]} for p, q in wood], "islets": islets, "ramps": ramps}
json.dump(layout, open(os.path.join(out, "layout.json"), "w"), indent=1)
subprocess.run([sys.executable, os.path.join(HERE, "terrain_from_mockup.py"), plan_path, os.path.join(out, "heights_new.npy"),
                os.path.join(out, "mock.json"), os.path.join(out, "passability.png"), os.path.join(work, "stageA")], check=True)

# Drawbridge quays and channels.
st = os.path.join(work, "stageA")
za = np.load(os.path.join(st, "heights_world.npy")).astype(np.float64)
np.save(os.path.join(st, "heights_world_noquay.npy"), za)
for o in draws:
    a = math.radians(o["angle_deg"])
    t = (X - o["x"]) * math.cos(a) + (Y - o["y"]) * math.sin(a)
    s = -(X - o["x"]) * math.sin(a) + (Y - o["y"]) * math.cos(a)
    at = np.abs(t)
    ws = 1 - smoothstep((np.abs(s) - 45) / 60)
    wt = 1 - smoothstep((at - 170) / 110)
    target = np.where(at >= 120, LOW, WATER - 14 + (LOW - WATER + 14) * smoothstep((at - 95) / 25))
    quay = za + (target - za) * ws * wt
    za = np.where(at < 95, np.minimum(za, WATER - 18 + (za - WATER + 18) * (1 - ws)), quay)
# Lowland shores (plan "beaches"). Stage A's beach profile runs from the seabed to the lowland over beach_width with the waterline at
# 0.65 of it; with the lowland only 8 above the water, that left a strip some 50 wide above the water and a steep drop below it. Here
# lowland coasts are rebuilt from a smoothed outline: sand rising gently from the waterline to the lowland over in_land, a shallow shelf
# shelf_depth deep out to shelf_width, then the seabed. A few stretches (rocky) become low rocky shores instead: the ground rises a
# little toward the edge and drops into the water within two cells. Plateau shores and the drawbridge quays keep their profile.
bch = plan["beaches"]
land = za >= WATER
shore = land & ndimage.binary_dilation(~land)
low_shore = shore & (ndimage.maximum_filter(za, size=13) < (LOW + HIGH) / 2)
_, (siy, six) = ndimage.distance_transform_edt(~shore, return_indices=True)
not_quay = np.ones(z.shape, bool)
for o in draws:
    a = math.radians(o["angle_deg"])
    t = (X - o["x"]) * math.cos(a) + (Y - o["y"]) * math.sin(a)
    s = -(X - o["x"]) * math.sin(a) + (Y - o["y"]) * math.cos(a)
    not_quay &= ~((np.abs(t) < 330) & (np.abs(s) < 170))
for sx, sy in starts.values():
    not_quay &= np.hypot(X - sx, Y - sy) > 250
# Ramps keep their foot: lowering a ramp's lower end into the beach cut a lowland strip off below a plateau.
for r in json.load(open(os.path.join(st, "report.json")))["ramps"]:
    not_quay &= np.hypot(X - r["at"][0], Y - r["at"][1]) > 220
# Rocky stretches: where a noise along the coast is highest, the longest few pieces, clear of the quays.
rn = field(bch["rocky_noise"])
ilab, _ = ndimage.label(land)
isz = np.bincount(ilab.ravel())
rocky = low_shore & (rn > bch["rocky_above"]) & not_quay & (isz[ilab] >= 3000)  # not on the islets
rlab, rn_n = ndimage.label(ndimage.binary_dilation(rocky, iterations=2))
rsz = ndimage.sum(rocky, rlab, range(1, rn_n + 1))
rc = ndimage.center_of_mass(rocky, rlab, range(1, rn_n + 1))
keep_ids = []
# The longest pieces, spread out (the longest alone put two on one small island).
for k in np.argsort(rsz)[::-1]:
    if len(keep_ids) >= bch["rocky_count"] or rsz[k] < 12:
        break
    if all(math.dist(rc[k], rc[j - 1]) * CELL >= bch["rocky_spacing"] for j in keep_ids):
        keep_ids.append(k + 1)
rocky &= np.isin(rlab, keep_ids)
# Shore type by the nearest shore: sharp where the nearest shore switches from lowland to plateau, which drew wedges into the seabed;
# smoothed more out in the water.
typ = low_shore[siy, six].astype(float)
w_low = np.where(land, ndimage.gaussian_filter(typ, 4), ndimage.gaussian_filter(typ, 14)) * ndimage.gaussian_filter(not_quay.astype(float), 3)
w_rock = ndimage.gaussian_filter(rocky[siy, six].astype(float), 2.5)
d_coast = ndimage.distance_transform_edt(~shore) * CELL
zone = (d_coast < bch["shelf_width"] + 300) & ((za <= LOW + 2) | ~land)
m = ndimage.gaussian_filter(land.astype(float), bch["outline_sigma"]) > 0.5
sd = (ndimage.distance_transform_edt(m) - ndimage.distance_transform_edt(~m)) * CELL
sd_raw = (ndimage.distance_transform_edt(land) - ndimage.distance_transform_edt(~land)) * CELL
sw, sdp = bch["shelf_width"], bch["shelf_depth"]
zb = np.where(sd >= 0, WATER + (LOW - WATER) * smoothstep(sd / bch["in_land"]),
              WATER - sdp * smoothstep(-sd / sw) - (WATER - sdp - FLOOR) * smoothstep((-sd - sw) / 260))
# Rocky: a ragged crest rock_rise above the lowland some 25 in from the water, a steep face down to it, a back slope over 120. A crest
# 5 above the lowland looked like a flat dark strip.
rr = bch["rock_rise"] * (1 + 0.35 * field(3))
sdr = sd_raw + 12 * field(2)
zr = np.where(sdr >= 25, LOW + rr * smoothstep(1 - (sdr - 25) / 120),
              np.where(sdr >= 0, WATER - 4 + (LOW + rr - WATER + 4) * smoothstep(sdr / 25) ** 0.6,
                       WATER - 4 - (WATER - 4 - FLOOR) * smoothstep(-sdr / 240)))
wb_ = np.where(zone, w_low * (1 - w_rock), 0)
wr_ = np.where(zone, w_low * w_rock, 0)
za = za + (zb - za) * wb_ + (zr - za) * wr_
rocky_cells = ndimage.binary_dilation(rocky, iterations=10) & (za >= WATER) & (wr_ > 0.4)
np.save(os.path.join(work, "mock", "rocky.npy"), rocky_cells[:-1, :-1])
print(json.dumps({"rocky_stretches": len(keep_ids), "rocky_shore_cells": int(rocky.sum()), "low_shore_cells": int(low_shore.sum())}))
TOP = 255 * RAW * plan["height_scale"]
step_h = TOP / 255
za = np.round(np.clip(za, 0, TOP) / step_h) * step_h
gy, gx = np.gradient(za, CELL)
slv = np.hypot(gx, gy)
imp = np.maximum.reduce([slv[:-1, :-1], slv[1:, :-1], slv[:-1, 1:], slv[1:, 1:]]) >= 0.4
Image.fromarray((imp[::-1] * 255).astype(np.uint8)).save(os.path.join(st, "impassable.png"))
np.save(os.path.join(st, "heights_world.npy"), za.astype(np.float32))
Image.fromarray(np.round(za / TOP * 65535).astype(np.uint16)[::-1]).save(os.path.join(st, "heights.png"))
print(json.dumps({"land_cells": int(land.sum()), "plateau_cells": int(high.sum()), "starts": {k: [round(v[0]), round(v[1])] for k, v in starts.items()}}))
