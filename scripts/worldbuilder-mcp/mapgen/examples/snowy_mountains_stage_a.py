# Stage A of SnowyMountainsGen (2 players, snowy mountains, Bitter Winter as reference): the terrain from the user's template
# (Templates\2PSnowyMountains.map). The template is a valley ringed by high ground, with terraces, a bowl in the middle and rock hills
# beside it; the starts face each other across the bowl. The user asked for rough symmetry, not a mirror, and more height and detail:
# - the template is moved so that the starts' midpoint is the centre of the map; its west half is kept and turned by 180 degrees into
#   the east half, blended across a band along the middle (averaging the whole template doubled the rock hills beside the bowl, which
#   are not where a turn would put them). Stage A gives every cliff its own noise, so the halves differ in detail;
# - heights are spread for more relief: bowl 42, valley 70, terraces 100, high ground 150 and more;
# - the template's terraces are gentle brush strokes, which stage A keeps as slopes: they are snapped to a hard step at 100, and get
#   two ramps each (as road hints crossing the step), the same ones turned for the other terrace;
# - low ground cut off from the starts by the high ground (the corners outside the ring) becomes mountain.
# Usage: snowy_mountains_stage_a.py <plan.json> <template dir (heights_template.npy, mock.json, info.json)> <out dir>
#   Writes heights_new.npy (raw, vertex grid, row 0 south), passability.png, mock.json into <out dir>/mock, then runs terrain_from_mockup.py
#   into <out dir>/stageA.
import json, os, subprocess, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, src, out = sys.argv[1:4]
plan = json.load(open(plan_path))
CELL, RAW = 10.0, 0.625
z = np.load(os.path.join(src, "heights_template.npy")).astype(np.float64) * RAW
info = json.load(open(os.path.join(src, "info.json")))
B = info["heightmap"]["border"]
ny, nx = z.shape
centre = np.array([(nx - 1 - 2 * B) * CELL / 2, (ny - 1 - 2 * B) * CELL / 2])
objs = json.load(open(os.path.join(src, "mock.json")))["objects"]["objects"]
starts = [o for o in objs if o.get("unique_id", "").startswith("Player_")]
mid = np.mean([[o["x"], o["y"]] for o in starts], axis=0)
shift = centre - mid
z = ndimage.shift(z, (shift[1] / CELL, shift[0] / CELL), order=1, mode="nearest")

# Turned by 180 degrees about the centre of the vertex grid, which is the map's centre: the west half kept, the east half its turn.
turned = z[::-1, ::-1]
SEAM = 25  # cells on each side of the middle
w = np.clip((np.arange(nx) - (nx - 1) / 2) / SEAM * 0.5 + 0.5, 0, 1)[None, :]
w = w * w * (3 - 2 * w)
HIGH = 110.0
blend = z * (1 - w) + turned * w
band = (w > 0) & (w < 1)
z = np.where(band & ((z > HIGH) | (turned > HIGH)), np.maximum(blend, np.where(w < 0.5, z, turned)), blend)

# More relief: template height to new height.
src_h = [0, 20, 40, 60, 75, 90, 110, 130, 145, 160]
new_h = [38, 40, 52, 70, 100, 110, 128, 150, 165, 175]
z = np.interp(z, src_h, new_h)

# Terraces: everything of the valley above 85 becomes a level at 100 with a smoothed outline.
TERRACE = 85.0
valley = z < plan["mountain_above"]
ter = ndimage.gaussian_filter((z > TERRACE).astype(np.float32), 2.0) > 0.5
# Only wide ground: the thin rim at the foot of the high ground is no terrace.
ter_lab, nt = ndimage.label(ndimage.binary_opening(ter & valley, iterations=6))
sizes = ndimage.sum(np.ones(z.shape), ter_lab, range(1, nt + 1))
terraces = [i + 1 for i, sz in enumerate(sizes) if sz >= 3000]
on = np.isin(ter_lab, terraces)
z = np.where(on, 100.0, np.where(valley & (z > 72), 70.0 + (z - 72) * 0.2, z))

# Low ground the starts cannot reach without crossing high ground becomes high ground.
lab, n = ndimage.label(z < plan["mountain_above"])
keep = {lab[int(round(o["y"] / CELL + B + shift[1] / CELL)), int(round(o["x"] / CELL + B + shift[0] / CELL))] for o in starts}
cut = (lab > 0) & ~np.isin(lab, list(keep))
z[cut] = 155.0
# Two ramps per terrace, worked out on the west terrace and turned for the east one: one toward the nearer start, one toward the bowl.
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * CELL, (yy - B) * CELL
edge = on & ~ndimage.binary_erosion(on)
moved_starts = [(o["x"] + shift[0], o["y"] + shift[1]) for o in starts]
hints = []
for t in terraces:
    m = edge & (ter_lab == t)
    if X[m].mean() > centre[0]:
        continue
    ex, ey = X[m], Y[m]
    for target in (min(moved_starts, key=lambda p: np.hypot(ex.mean() - p[0], ey.mean() - p[1])), tuple(centre)):
        i = int(np.argmin(np.hypot(ex - target[0], ey - target[1])))
        px, py = ex[i], ey[i]
        cx, cy = X[ter_lab == t].mean(), Y[ter_lab == t].mean()
        nxv, nyv = px - cx, py - cy
        nn = np.hypot(nxv, nyv)
        nxv, nyv = nxv / nn, nyv / nn
        for sgn in (1, -1):
            qx, qy = (centre[0] + sgn * (px - centre[0]), centre[1] + sgn * (py - centre[1]))
            dx, dy = sgn * nxv, sgn * nyv
            hints.append(((qx - 200 * dx, qy - 200 * dy), (qx + 200 * dx, qy + 200 * dy)))
print("terraces", len(terraces), "ramp hints", [(round(a[0]), round(a[1])) for a, b in hints])
print("shift", shift.round(1), "filled pockets", int(cut.sum()), "cells; height", round(z.min(), 1), round(z.max(), 1))

mock = os.path.join(out, "mock")
os.makedirs(mock, exist_ok=True)
np.save(os.path.join(mock, "heights_new.npy"), (z / RAW).astype(np.float32))
Image.fromarray(np.zeros((ny - 1, nx - 1), np.uint8)).save(os.path.join(mock, "passability.png"))
moved = [dict(o, x=round(float(o["x"] + shift[0]), 1), y=round(float(o["y"] + shift[1]), 1)) for o in objs]
# Road hints only make ramps in stage A; they are not built as roads.
for k, (a, b) in enumerate(hints):
    for p in (a, b):
        moved.append({"kind": "road_point", "template": "RampHint", "x": round(float(p[0]), 1), "y": round(float(p[1]), 1), "unique_id": f"ramp_hint {k}"})
json.dump({"objects": {"objects": moved}}, open(os.path.join(mock, "mock.json"), "w"), indent=1)
print("starts", [(o["unique_id"], o["x"], o["y"]) for o in moved])
subprocess.run([sys.executable, os.path.join(HERE, "terrain_from_mockup.py"), plan_path, os.path.join(mock, "heights_new.npy"),
                os.path.join(mock, "mock.json"), os.path.join(mock, "passability.png"), os.path.join(out, "stageA")], check=True)
