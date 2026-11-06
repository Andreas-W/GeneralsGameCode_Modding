# NorthernIslandGen (WinterIsleGen, TropicTrefoilGen), stage B after paint_map.py / import_masks.py (land from Bitter Winter, which has no water): the sea and the beaches.
# The seabed gets the navy skill's blue bands by distance from land (SandMediumType9blue4 in the shallows to SandMediumType9blue in the
# deep), their borders wandering with noise; the first two cells from land keep what the painter gave them, so cliff shores stay rock.
# Beaches (low, gentle land at the water and the shallows beside it) get grey shingle (DirtSnowType1). Then reblend, cliff mapping (fit), save.
# Usage: northern_island_sea.py <plan.json> <work dir (mapgen_work)>   Uses the second WorldBuilder (town/start_wb.py).
import json, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(HERE))
from wb_bridge import WorldBuilderBridge

plan_path, work = sys.argv[1:3]
plan = json.load(open(plan_path))
WATER = plan["water"]["height"]
z = np.load(os.path.join(work, "heights_world.npy")).astype(np.float64)
zc = (z[:-1, :-1] + z[1:, :-1] + z[:-1, 1:] + z[1:, 1:]) / 4          # cell centres, row 0 south
sea = zc < WATER
d = ndimage.distance_transform_edt(sea)                                # cells from land
rng = np.random.default_rng(plan.get("seed", 1))
noise = np.zeros(zc.shape)
for size, amp in ((28, 6.0), (10, 2.5)):
    n = ndimage.gaussian_filter(rng.standard_normal(zc.shape), size)
    noise += amp * n / n.std()
dn = d + noise
gy, gx = np.gradient(zc, 10.0)
slope = np.hypot(gx, gy)
# Beach: gentle land below 74 within 12 cells of the water, and the shallows (first two cells) beside such land.
near_water = ndimage.distance_transform_edt(~sea) <= 12
beach_land = ~sea & (zc < WATER + 14) & near_water & (ndimage.maximum_filter(slope, 3) < 0.3)
beach = beach_land | (sea & (d <= 3) & ndimage.binary_dilation(beach_land, iterations=3))
beach = ndimage.binary_opening(beach, iterations=1)
layers = [("SandMediumType9blue4", sea & (dn >= 2)), ("SandMediumType9blue3", sea & (dn >= 14)),
          ("SandMediumType9blue2", sea & (dn >= 28)), ("SandMediumType9blue", sea & (dn >= 45)),
          # Grey shingle: SandMediumType9Snow looked orange on NorthernIslandGen and was swapped for this one afterwards. Plan
          # "beach_texture" sets another (TropicTrefoilGen: the user's light sand).
          (plan.get("beach_texture", "DirtSnowType1"), beach)]
out = os.path.join(work, "stageB_sea")
os.makedirs(out, exist_ok=True)
paths = []
for tex, m in layers:
    p = os.path.join(out, tex + ".png")
    Image.fromarray((m[::-1] * 255).astype(np.uint8)).save(p)
    paths.append((tex, p, int(m.sum())))
print("cells", [(t, c) for t, _, c in paths])

wb = WorldBuilderBridge(port=int(os.environ.get("TOWN_WB_PORT", 47810)), timeout=900)
wb.call("map.new", width=32, height=32, discard_changes=True)
wb.call("map.open", path=plan["map"], discard_changes=True)
for tex, p, _ in paths:
    wb.call("terrain.import_mask", path=p, target="texture", texture=tex, fit="exact")
print("blend", {k: v for k, v in wb.call("terrain.reblend").items() if k in ("three_way_tiles", "hard_edges")})
c = wb.call("terrain.cliff_map", repair="fit")
print("cliffs", {k: c[k] for k in ("cliff_cells", "outside_v", "outside_u")})
wb.call("map.save", path=plan["map"], overwrite=True)
print("saved")
