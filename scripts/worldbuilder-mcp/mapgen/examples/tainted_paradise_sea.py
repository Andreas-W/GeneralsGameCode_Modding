# Example for TaintedParadise: the seabed painted in the four SandMediumType9blue shades of the adapt-map-for-navy skill, from white at
# the shore to dark offshore, with band edges wandering on noise. Where cliffs meet the water the rock stays: underwater cells with a
# cliff texture near land are left alone, and in front of a cliff shore the unpainted strip is wider. Bands go by distance from land,
# so a tight bay only gets the light shades its width reaches instead of a squeezed full gradient.
# Runs in the second WorldBuilder (town/start_wb.py) on the saved map and saves it.
# Usage: tainted_paradise_sea.py <map path> <out dir> [--save] [--seed N]
import json, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [HERE, os.path.join(HERE, "cliffs")]
from wb_bridge import WorldBuilderBridge
from cliff_gen import noise
import cliff_scan

map_path, out = [a for i, a in enumerate(sys.argv[1:]) if not a.startswith("--") and sys.argv[i] != "--seed"][:2]
seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 1234
rng = np.random.default_rng(seed)
os.makedirs(out, exist_ok=True)
WATER = 60.0
SKIP, SKIP_CLIFF, ROCK_KEEP = 2, 4, 8      # cells from land: unpainted strip, the same before a cliff shore, cliff texture kept within
BANDS = [(2, "SandMediumType9blue4"), (14, "SandMediumType9blue3"), (28, "SandMediumType9blue2"), (45, "SandMediumType9blue")]
BLUES = [t for _, t in BANDS]
OLD_SEABED = "SandLargeType2"

wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
# A WorldBuilder that has this path open already keeps its copy on map.open; a new map in between makes it read the file.
wb.call("map.new", width=32, height=32, discard_changes=True)
info = wb.call("map.open", path=map_path, discard_changes=True)
hm = info["heightmap"]
z = cliff_scan.heights(wb, hm["x_extent"], hm["y_extent"]) * 0.625
cz = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4
rock = np.zeros(cz.shape, bool)
seabed = np.zeros(cz.shape, bool)
for t in wb.call("terrain.list_textures")["textures"]:
    if t.get("used_in_map") and t["name"].startswith("Cliff"):
        p = os.path.join(out, "_cliff.png")
        wb.call("terrain.export_mask", kind="texture", texture=t["name"], path=p, include_border=True)
        m = np.array(Image.open(p).convert("L"))[::-1] > 127
        rock[:m.shape[0], :m.shape[1]] |= m
# The old seabed texture is no shore texture to keep: next to a beach its hard edge against the sand showed as dark steps.
p = os.path.join(out, "_seabed.png")
wb.call("terrain.export_mask", kind="texture", texture=OLD_SEABED, path=p, include_border=True)
m = np.array(Image.open(p).convert("L"))[::-1] > 127
seabed[:m.shape[0], :m.shape[1]] = m

# Sea: cells under the water level joined to the largest water body; land is the rest.
lab, n = ndimage.label(cz < WATER)
sizes = ndimage.sum(np.ones_like(cz), lab, range(1, n + 1))
sea = lab == (int(np.argmax(sizes)) + 1)
dist = ndimage.distance_transform_edt(sea)
gy, gx = np.gradient(z, 10.0)
slope = np.hypot(gx, gy)[:-1, :-1]
cliff_shore = ndimage.binary_dilation(~sea & (slope >= 0.4), iterations=3)
near_cliff = ndimage.distance_transform_edt(~cliff_shore) <= SKIP_CLIFF + 1
skip = (((dist < SKIP) | (near_cliff & (dist < SKIP_CLIFF))) & ~seabed) | (rock & (dist <= ROCK_KEEP))
dn = dist + noise(cz.shape, rng, [(28, 1.0), (10, 0.4)]) * 6.0
level = np.zeros(cz.shape, np.uint8)
for k, (lim, _) in enumerate(BANDS):
    level[sea & ~skip & (dn >= lim if k else True)] = k + 1
counts = {t: int((level == k + 1).sum()) for k, t in enumerate(BLUES)}
print("cells per shade", counts, "rock kept", int((sea & rock & skip).sum()), "of", int((sea & rock).sum()), "underwater rock cells")
prev = np.zeros(cz.shape + (3,), np.uint8)
prev[~sea] = [120, 100, 70]
for k, c in enumerate([(200, 225, 235), (120, 170, 210), (60, 110, 170), (20, 50, 110)]):
    prev[level == k + 1] = c
prev[sea & skip & rock] = [90, 90, 90]
Image.fromarray(prev[::-1]).save(os.path.join(out, "preview.png"))
if "--save" not in sys.argv:
    sys.exit(0)
for k, t in enumerate(BLUES):
    p = os.path.join(out, f"band{k + 1}.png")
    Image.fromarray(((level >= k + 1) * 255).astype(np.uint8)[::-1]).save(p)
    wb.call("terrain.import_mask", path=p, target="texture", texture=t, fit="exact")
wb.call("terrain.blend_all", textures=BLUES)
wb.call("map.save", path=map_path)
print("saved", map_path)
wb.close()
