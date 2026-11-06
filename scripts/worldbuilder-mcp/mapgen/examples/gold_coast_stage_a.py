# Stage A of GoldCoastGen (2 players, naval): the terrain from the user's template (Templates\2PGoldCoast.map). From west to east: the sea,
# a wide beach (template 60 to 66), cliffs up to the main plateau (template about 112, with hills), a river in a shallow valley (its own
# water area at 105, bed near 60), a mountain ridge along the east edge. The starts are south and north on the plateau.
# - beach band -> the beach level (64), so stage A makes a soft beach shore; the climb from it to the plateau stays as the template has it
#   (steep: cliff; the gentle bulges near each base: slopes and ramps);
# - the river is a second water level, which stage A does not know: its channel is filled to the plateau for stage A and carved afterwards
#   (bed RIVER_BED below its surface at 105, banks eased over BANK), with the template's meanders;
# - ground above RIDGE east of the river is the mountain ridge (the 175 marker for stage A's mountain frame); the plateau's hills stay hills.
# Usage: gold_coast_stage_a.py <plan.json> <template dir (heights_template.npy, mock.json, info.json)> <work dir>
import json, os, subprocess, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan_path, src, work = sys.argv[1:4]
plan = json.load(open(plan_path))
CELL, RAW = 10.0, 0.625
WATER, RIVER = plan["water"]["height"], plan["river"]["height"]
RIVER_BED, BANK, RIDGE = 28.0, 70.0, 122.0
z = np.load(os.path.join(src, "heights_template.npy")).astype(np.float64) * RAW
info = json.load(open(os.path.join(src, "info.json")))
B = info["heightmap"]["border"]
ny, nx = z.shape
yy, xx = np.indices(z.shape)
X, Y = (xx - B) * CELL, (yy - B) * CELL
low, main = plan["levels"][0]["z"], plan["levels"][1]["z"]

zm = z.copy()
sea = z < WATER + 0.5
# The river channel: low ground east of the plateau's middle (the beach is in the west).
channel = (z < 100) & (X > 2100) & ~sea
channel = ndimage.binary_opening(channel, iterations=1)
lab, n = ndimage.label(channel)
sizes = ndimage.sum(channel, lab, range(1, n + 1))
channel = lab == (np.argmax(sizes) + 1)
zm = np.where(channel, main, zm)
beach = ~sea & (z < 70) & ~channel
zm = np.where(beach, low, zm)
ridge = (z > RIDGE) & (X > 2600)  # the whole high band east of the river (the white tops alone left gaps)
zm = np.where(ridge, 175.0, np.minimum(zm, 134.0))
out = os.path.join(work, "mock")
os.makedirs(out, exist_ok=True)
np.save(os.path.join(out, "heights_new.npy"), np.clip(np.round(zm / RAW), 0, 255 * plan["height_scale"]).astype(np.float32))
Image.fromarray(np.zeros((ny - 1, nx - 1), np.uint8)).save(os.path.join(out, "passability.png"))
objs = json.load(open(os.path.join(src, "mock.json")))["objects"]["objects"]
json.dump({"objects": {"objects": objs}}, open(os.path.join(out, "mock.json"), "w"), indent=1)
np.save(os.path.join(out, "river_channel.npy"), channel)
print(json.dumps({"channel_vertices": int(channel.sum()), "beach_vertices": int(beach.sum()), "ridge_vertices": int(ridge.sum())}))
subprocess.run([sys.executable, os.path.join(HERE, "terrain_from_mockup.py"), plan_path, os.path.join(out, "heights_new.npy"),
                os.path.join(out, "mock.json"), os.path.join(out, "passability.png"), os.path.join(work, "stageA")], check=True)

# The river, carved into stage A's plateau: the template's channel smoothed (its outline as a meandering band), the bed falling to
# RIVER_BED below the surface within 30 of the edge, the banks eased from the plateau down to just above the water over BANK.
st = os.path.join(work, "stageA")
za = np.load(os.path.join(st, "heights_world.npy")).astype(np.float64)
np.save(os.path.join(st, "heights_world_noriver.npy"), za)
band = ndimage.gaussian_filter(channel.astype(np.float64), 2.0) > 0.5
d_in = ndimage.distance_transform_edt(band) * CELL
d_out = ndimage.distance_transform_edt(~band) * CELL
rng = np.random.default_rng(plan.get("seed", 1) + 3)
wob = ndimage.gaussian_filter(rng.standard_normal(z.shape), 4)
wob = wob / (wob.std() + 1e-9)
bed = RIVER - 2 - (RIVER_BED - 2) * np.clip(d_in / 30, 0, 1) ** 0.8
tb = np.clip(d_out / (BANK * (1 + 0.25 * wob)), 0, 1)
tb = tb * tb * (3 - 2 * tb)
bank = RIVER + 1.5 + (za - RIVER - 1.5) * tb
za = np.where(band, bed, np.where(d_out < BANK * 1.6, np.minimum(za, bank), za))
np.save(os.path.join(st, "heights_world.npy"), za.astype(np.float32))
TOP = 255 * RAW * plan["height_scale"]
Image.fromarray(np.round(np.clip(za, 0, TOP) / TOP * 65535).astype(np.uint16)[::-1]).save(os.path.join(st, "heights.png"))
np.save(os.path.join(st, "river_band.npy"), band)
print(json.dumps({"river_vertices": int(band.sum()), "river_bed_min": round(float(za[band].min()), 1)}))
