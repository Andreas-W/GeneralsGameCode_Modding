# Stage A of TropicTrefoilGen (3 players, tropical, naval): the terrain from the user's template (Templates\3p_IslandTemplate.map), a trefoil
# island: three lobes around a hub plateau, a peak at each neck between two lobes, a mountain rim on each lobe's outer coast. Only the
# lower-left lobe (player 1) is planned out (texture hints, palms, huts, supply piles, ruins), so its 120-degree wedge is the pattern:
# - the island moves so that the starts' centre is the map's centre; the wedge around player 1 (heights, texture hints, objects) is turned
#   by 120 and 240 degrees onto the other two, blended over SEAM degrees at the seams (the neck peaks sit on the seams, so each peak is
#   made of the halves of two). Stage A gives every cliff its own noise, so the lobes differ in detail;
# - the wedge runs from 55 degrees before player 1's angle to 65 after it, so that objects at the necks (the refineries) are counted once;
# - heights are lifted and spread: the lobes stood 2 above the water (now 76), the hub 87 (now 100), the slopes between them follow;
#   ground above MOUNT on the template becomes mountain (the 175 marker for stage A's mountain frame); smaller rocks stay hills;
# - beaches where the template has light sand at the coast (SandMediumType6Light): the shore there drops to the beach level; elsewhere it
#   stays steep and becomes cliff; the sea floor falls from the coast to the floor within the shelf.
# Usage: tropic_trefoil_stage_a.py <plan.json> <template dir (heights_template.npy, mock.json, info.json, textures.json)> <work dir>
#   Writes <work>/mock (heights_new.npy, passability.png, mock.json, hints.npy + hints.json, objects_turned.json), runs terrain_from_mockup.py
#   into <work>/stageA.
import json, math, os, subprocess, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, src, work = sys.argv[1:4]
plan = json.load(open(plan_path))
CELL, RAW = 10.0, 0.625
WATER = plan["water"]["height"]
SEAM, MOUNT, BEFORE, AFTER = 8.0, 105.0, 55.0, 65.0
z = np.load(os.path.join(src, "heights_template.npy")).astype(np.float64) * RAW
info = json.load(open(os.path.join(src, "info.json")))
B = info["heightmap"]["border"]
ny, nx = z.shape
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * CELL, (yy - B) * CELL
objs = json.load(open(os.path.join(src, "mock.json")))["objects"]["objects"]
starts = {o["waypoint_name"]: (o["x"], o["y"]) for o in objs if o["kind"] == "waypoint"}
c_old = np.mean(list(starts.values()), axis=0)
centre = np.array([(nx - 1 - 2 * B) * CELL / 2, (ny - 1 - 2 * B) * CELL / 2])
shift = centre - c_old
p1 = np.array(starts["Player_1_Start"]) - c_old
a1 = math.degrees(math.atan2(p1[1], p1[0]))


def wedge_weight(ang):
    # 1 inside player 1's wedge, 0 outside, smooth over SEAM degrees at both seams; the three turned wedges add up to 1.
    dd = (ang - (a1 - BEFORE) + 180) % 360 - 180
    w = np.clip(np.minimum(dd, (BEFORE + AFTER) - dd) / SEAM + 0.5, 0, 1)
    return w * w * (3 - 2 * w)


def sample(field, turn, order=1, cval=0.0):
    # The template field seen from the new map turned by `turn` degrees: new point (x, y) about the new centre comes from the template
    # point turned back by `turn` about the starts' old centre.
    t = math.radians(-turn)
    dx, dy = X - centre[0], Y - centre[1]
    sx = c_old[0] + dx * math.cos(t) - dy * math.sin(t)
    sy = c_old[1] + dx * math.sin(t) + dy * math.cos(t)
    return ndimage.map_coordinates(field, [sy / CELL + B, sx / CELL + B], order=order, mode="constant", cval=cval)


ang_new = np.degrees(np.arctan2(Y - centre[1], X - centre[0]))
acc, wsum = np.zeros(z.shape), np.zeros(z.shape)
for turn in (0, 120, 240):
    w = wedge_weight(ang_new - turn)
    acc += w * sample(z, turn, cval=z.min())
    wsum += w
zt = acc / np.maximum(wsum, 1e-6)

# Levels: lobe 62.5 -> 76, hub 87 -> 100; the slopes in between follow; peaks above MOUNT become mountain.
land = zt > WATER + 0.5
f_in = [WATER + 0.5, 63.0, 66.0, 84.0, 88.0, 95.0, MOUNT]
f_out = [72.0, 76.0, 78.0, 98.0, 100.0, 103.0, 118.0]
zn = np.interp(zt, f_in, f_out)
mountain = zt > MOUNT
lab, n = ndimage.label(mountain)
sizes = ndimage.sum(mountain, lab, range(1, n + 1))
big = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= 60])
# Grown by 2 cells: the neck peaks (some 330 cells) are under stage A's 400-cell minimum for a mountain and came out as flat-topped hills.
# More growth (3 cells, with a flank of 150) put the refineries at their feet onto the flank.
big = ndimage.binary_dilation(big, iterations=2) & land
# The rims (the large mountains on the lobes' outer coasts) become ridges after stage A; the hub's small hills become small mountains.
lab_b, nb = ndimage.label(big)
rims = np.isin(lab_b, [i + 1 for i, sz in enumerate(ndimage.sum(big, lab_b, range(1, nb + 1))) if sz >= 700])
r_new = np.hypot(X - centre[0], Y - centre[1])
hub_hills = (zt > 97) & (r_new < 350) & ~big
zn = np.where(big, 175.0, np.minimum(zn, 125.0))
# Texture hints: each template texture mask (cells) turned like the heights; a cell takes the texture with the largest weight.
tex = json.load(open(os.path.join(src, "textures.json")))
names = sorted(tex)
cy, cx = np.indices((ny - 1, nx - 1))
Xc, Yc = (cx - B + 0.5) * CELL, (cy - B + 0.5) * CELL
ang_c = np.degrees(np.arctan2(Yc - centre[1], Xc - centre[0]))
votes = np.zeros((len(names), ny - 1, nx - 1))
for k, name in enumerate(names):
    m = (np.array(Image.open(tex[name]).convert("L"))[::-1] > 127).astype(np.float64)
    for turn in (0, 120, 240):
        w = wedge_weight(ang_c - turn) > 0.5
        t = math.radians(-turn)
        dx, dy = Xc - centre[0], Yc - centre[1]
        sx = c_old[0] + dx * math.cos(t) - dy * math.sin(t)
        sy = c_old[1] + dx * math.sin(t) + dy * math.cos(t)
        votes[k] += w * ndimage.map_coordinates(m, [sy / CELL + B - 0.5, sx / CELL + B - 0.5], order=0, mode="constant")
hint = np.where(votes.max(0) > 0, votes.argmax(0), -1).astype(np.int16)

# Beaches: land within reach of the coast near light-sand hints drops to the beach level, then climbs to the lobe.
coast_in = ndimage.distance_transform_edt(land) * CELL
beach_hint = np.zeros(z.shape, bool)
beach_hint[:-1, :-1] = hint == names.index("SandMediumType6Light")
# Wide and gentle (the user found the first beaches too abrupt): reach 400 inland, low for 80, climbing over 320, eased along the coast.
beach_zone = ndimage.binary_dilation(beach_hint, iterations=14) & land & (coast_in < 400) & ~ndimage.binary_dilation(big, iterations=6)
low, main = plan["levels"][0]["z"], plan["levels"][1]["z"]
tb = np.clip((coast_in - 80) / 320, 0, 1)
tb = tb * tb * (3 - 2 * tb)
along = np.clip(ndimage.gaussian_filter(beach_zone.astype(np.float64), 6) * 1.6, 0, 1)
zn = np.where(land, zn * (1 - along) + np.minimum(zn, low + (main - low) * tb) * along, zn)
# Sea: the floor falls from the coast to the floor within the shelf.
sea_d = ndimage.distance_transform_edt(~land) * CELL
floor = plan["water"]["floor"]
zn = np.where(land, zn, floor + (WATER - 12 - floor) * np.clip(1 - sea_d / plan["shelf"], 0, 1))

out = os.path.join(work, "mock")
os.makedirs(out, exist_ok=True)
np.save(os.path.join(out, "heights_new.npy"), np.clip(np.round(zn / RAW), 0, 255 * plan["height_scale"]).astype(np.float32))
Image.fromarray(np.zeros((ny - 1, nx - 1), np.uint8)).save(os.path.join(out, "passability.png"))
np.save(os.path.join(out, "hints.npy"), hint)
json.dump({"names": names}, open(os.path.join(out, "hints.json"), "w"))

# Objects: the centre's own (within CENTRE of the old centre) kept as they are, moved with the island; the wedge's objects turned.
CENTRE = 300.0
turned = []


def rot(x, y, turn):
    t = math.radians(turn)
    dx, dy = x - c_old[0], y - c_old[1]
    return centre[0] + dx * math.cos(t) - dy * math.sin(t), centre[1] + dx * math.sin(t) + dy * math.cos(t)


for o in objs:
    rel = (o["x"] - c_old[0], o["y"] - c_old[1])
    if o["kind"] != "waypoint" and math.hypot(*rel) < CENTRE:
        turned.append(dict(o, x=o["x"] + shift[0], y=o["y"] + shift[1], copy=0))
        continue
    if o["kind"] == "waypoint" and o["waypoint_name"] != "Player_1_Start":
        continue
    d = (math.degrees(math.atan2(rel[1], rel[0])) - (a1 - BEFORE)) % 360
    if d > BEFORE + AFTER:
        continue
    for k, turn in enumerate((0, 120, 240)):
        x, y = rot(o["x"], o["y"], turn)
        q = dict(o, x=round(x, 1), y=round(y, 1), angle_deg=round(o["angle_deg"] + turn, 1), copy=k)
        if o["kind"] == "waypoint":
            # Player 1's start turned: 120 degrees lands on the template's player 3, 240 on player 2.
            nm = {0: "Player_1_Start", 120: "Player_3_Start", 240: "Player_2_Start"}[turn]
            q.update(waypoint_name=nm, unique_id=nm)
        turned.append(q)
json.dump(turned, open(os.path.join(out, "objects_turned.json"), "w"), indent=0)
keep = [o for o in turned if o["kind"] == "waypoint" or o["template"].startswith(("Tech", "Supply"))]
json.dump({"objects": {"objects": keep}}, open(os.path.join(out, "mock.json"), "w"), indent=1)
print(json.dumps({"shift": [round(s) for s in shift], "player1_angle": round(a1, 1), "objects": len(turned),
                  "starts": {o["waypoint_name"]: [round(o["x"]), round(o["y"])] for o in turned if o["kind"] == "waypoint"},
                  "mountain_cells": int(big.sum()), "beach_vertices": int(beach_zone.sum())}))
subprocess.run([sys.executable, os.path.join(HERE, "terrain_from_mockup.py"), plan_path, os.path.join(out, "heights_new.npy"),
                os.path.join(out, "mock.json"), os.path.join(out, "passability.png"), os.path.join(work, "stageA")], check=True)

# After stage A. Its mountains are a frame with a flat crest (plateaus on the rims); the user wants ridges there: the crest follows the
# middle of each rim (where the distance to its edge is largest nearby) and the ground falls from it to both sides, with ridged relief.
st = os.path.join(work, "stageA")
za = np.load(os.path.join(st, "heights_world.npy")).astype(np.float64)
np.save(os.path.join(st, "heights_world_frame.npy"), za)
imp_c = np.array(Image.open(os.path.join(st, "impassable.png")).convert("L"))[::-1] > 127
rng = np.random.default_rng(plan.get("seed", 1) + 77)


def fbm(scales):
    o = np.zeros(z.shape)
    for sz, amp in scales:
        q = ndimage.gaussian_filter(rng.standard_normal(z.shape), sz)
        o += amp * q / (q.std() + 1e-9)
    return o


FOOT_Z = 86.0
R = (za > FOOT_Z) & ndimage.binary_dilation(rims, iterations=15)
d = ndimage.distance_transform_edt(R) * CELL
dl = np.maximum(ndimage.gaussian_filter(ndimage.maximum_filter(d, size=41), 5), 60.0)  # smoothed: the plain maximum left flat facets
t = ndimage.gaussian_filter(np.clip(d / dl, 0, 1), 1.0)
crest = 150.0 + 10.0 * fbm([(25, 1.0)])
ridged = 1 - np.abs(fbm([(6, 1.0), (2.5, 0.5)])) / 3.0
# t ** 0.75 gave a broad rounded top; above 1 the crest is a line and the flanks hollow, with gullies from the ridged noise.
zr_ = FOOT_Z + (crest - FOOT_Z) * t ** 1.3 * (0.72 + 0.28 * ridged)
za = np.where(R, zr_, za)
# The hub's small hills: steep little mountains (35 to 50 above the hub), impassable.
H = ndimage.binary_dilation(hub_hills, iterations=3)
lab_h, nh = ndimage.label(H)
dh = ndimage.distance_transform_edt(H) * CELL
peak = ndimage.maximum(dh, lab_h, range(1, nh + 1)) if nh else []
th = np.zeros(z.shape)
for k in range(nh):
    th = np.where(lab_h == k + 1, dh / max(peak[k], 1.0), th)
bump = (40.0 + 8.0 * fbm([(8, 1.0)])) * np.clip(th, 0, 1) ** 0.6 * (0.85 + 0.15 * ridged)
za = np.where(H, np.maximum(za, 100.0 + bump), za)
cells_R = np.zeros(imp_c.shape, bool)
blockv = R | H
cells_R |= blockv[:-1, :-1] | blockv[1:, 1:]
imp_c = imp_c | cells_R
np.save(os.path.join(st, "heights_world.npy"), za.astype(np.float32))
TOP = 255 * RAW * plan["height_scale"]
Image.fromarray(np.round(np.clip(za, 0, TOP) / TOP * 65535).astype(np.uint16)[::-1]).save(os.path.join(st, "heights.png"))
Image.fromarray((imp_c[::-1] * 255).astype(np.uint8)).save(os.path.join(st, "impassable.png"))
print(json.dumps({"ridge_vertices": int(R.sum()), "ridge_max": round(float(za[R].max()), 1), "hub_mountains": int(nh),
                  "hub_mountain_max": round(float(za[H].max()), 1) if nh else None}))
